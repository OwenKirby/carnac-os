/* PCM waves in the store flash on the FM1 (2026-10-21, src/dsp/pcm_store.h): a unity-build fragment, like
 * preset_store_fm1.c. synth_main_fm1.c includes it after the preset store and the editor helpers (ed_b, ed_str);
 * X0X's storage.c gives st_erase / st_prog; syn_lock / syn_unlock and flash_ok are the glue's.
 *
 * The areas: flash in X0X's data area (0x97000..0xDFFFF, writable, read as plaintext through XIP) that nothing of
 * Carnac's uses:
 *   0  0x97000..0x9FFFF  36 KiB  X0X's sound / pattern / song objects (Carnac never reads them; a unit that ran X0X
 *                                has its old objects there, which read as no image)
 *   1  0xCE000..0xDBFFF  56 KiB  the preset store's tail: its chunks 23 and 24 (free since 2026-10-17, PS_NCHUNK 23
 *                                now) and the 40 KiB after them, up to X0X's panel calibration (OBJ_PANEL, 0xDC000)
 *   2  0xBC000..0xC1FFF  24 KiB  not PCM: wavetable bank 0's own image (wavetables.h WT_BANK_*, Foundations, since
 *                                2026-10-26), in the preset store's free chunks 14..16; the same commands upload it
 * Left alone: OBJ_PANEL 0xDC000 (the panel calibration), 0xDE000 (X0X's motion object: free, 8 KiB, kept spare),
 * 0xE0000.. (the update loader's staging).
 *
 * The editor's commands (SysEx F0 7D 46 4C cmd args F7, as the old wavetable upload; src/hardware/pcm_store_upload.py):
 *   100 PCM_BEGIN [area]                 the area is detached and its first sector erased: it reads empty from now on
 *                                        -> area, rc
 *   101 PCM_WRITE [off x3, data packed]  image bytes from offset 32, in order; each sector is erased as the writes
 *                                        reach it -> off x3, rc
 *   102 PCM_END   [header packed]        the 32-byte header, programmed last; the image is checked (pcm_store_check)
 *                                        and attached, or its magic is zeroed (the area reads empty) -> rc, waves
 *   103 PCM_INFO  [area, first]          -> areas, area, entries, size KiB x2, used bytes x3, then up to 16 names
 *                                        from entry `first` of the area (samples and waves, pcm_store.h)
 *   104 PCM_ID    [area]                 -> area, held (0/1), the held image's header CRC (hcrc, its bytes 28..31:
 *                                        it covers the data CRC and length) x5: an installer writes only the areas
 *                                        whose image differs (2026-10-26)
 * rc: 0 ok, 1 arguments, 2 flash, 3 order (no BEGIN, or not the next offset), 4 too big for the area, 5 refused on
 * the check (the reply then carries pcm_store_check's reason + 8 as the wave count).
 * The erases stop the audio for tens of ms each (st_erase silences it first): an upload is a studio job. */
#include "pcm_store.h"

#ifndef PCM_XIP
#define PCM_XIP(off) ((const uint8_t *)FM1_XIP(off))
#endif
#define PCM_SECTOR 4096u
enum { ED_PCM_BEGIN = 100, ED_PCM_WRITE, ED_PCM_END, ED_PCM_INFO, ED_PCM_ID };
#define PCM_BANK_AREA PCM_FLASH_AREAS               /* the area that holds bank 0's image */
#define PCM_ALL_AREAS (PCM_FLASH_AREAS + 1u)
static const struct { uint32_t base, size; } PCM_AREA[PCM_ALL_AREAS] = {
    {0x97000u, 0x9000u},
    {0xCE000u, 0xE000u},
    {0xBC000u, 0x6000u},
};
#ifdef PS_NCHUNK                                    /* (the firmware: the preset store comes first; not in the host test) */
PS_STATIC_ASSERT(0x97000u + 0x9000u <= PS_BASE, "area 0 ends below the preset store");
PS_STATIC_ASSERT(PS_BASE + PS_NCHUNK * 2u * PS_SECTOR <= 0xCE000u, "area 1 starts after the preset store's chunks");
PS_STATIC_ASSERT(0xBC000u == PS_BASE + 14u * 2u * PS_SECTOR && 0xBC000u + 0x6000u == PS_BASE + 17u * 2u * PS_SECTOR &&
                 PS_FIRST_PERF + PS_N_PERF <= 14u && PS_FIRST_PATCH >= 17u, "area 2 is the store's free chunks 14..16");
#endif
_Static_assert(WT_BANK_IMAGE_LEN <= 0x6000u, "bank 0's image fits area 2");
_Static_assert(0xCE000u + 0xE000u <= 0xDC000u, "area 1 ends below X0X's panel calibration (OBJ_PANEL)");

static struct {
    uint8_t on, area;                               /* an upload since PCM_BEGIN, into area */
    uint32_t next;                                  /* the offset the next PCM_WRITE must start at */
    uint32_t erased;                                /* the area's bytes erased so far (whole sectors) */
} pcm_up;
static uint8_t pcm_buf[256] __attribute__((aligned(4)));   /* a write's bytes, in RAM for the flash driver */

/* The header CRCs (hcrc) of the images this build ships beside it (src/dsp/pcm_area0.bin, pcm_area1.bin,
 * foundations_bank.bin), from build_fm1.py; 0: not known (host builds), that area is only checked for an image */
#ifndef PCM_WANT_HCRC0
#define PCM_WANT_HCRC0 0u
#define PCM_WANT_HCRC1 0u
#define PCM_WANT_HCRC2 0u
#endif
static const uint32_t pcm_want_hcrc[PCM_ALL_AREAS] = {PCM_WANT_HCRC0, PCM_WANT_HCRC1, PCM_WANT_HCRC2};

enum { PCM_WAVES_OK, PCM_WAVES_MISSING, PCM_WAVES_STALE };
static uint8_t pcm_waves_state;                     /* at power-on: a firmware installed without its waves (the
                                                     * .fwsc alone, by another installer) says so on the splash */

static int pcm_held(uint32_t area, uint32_t *hcrc)  /* area's image is attached: 1 and its hcrc, else 0 */
{
    const uint8_t *h = PCM_XIP(PCM_AREA[area].base) + 28;   /* hcrc: last in both headers */
    if (!(area == PCM_BANK_AREA ? wavetable_bank_used() : pcm_area_used(area))) {
        *hcrc = 0;
        return 0;
    }
    *hcrc = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
    return 1;
}

static void pcm_boot(void)                          /* after ps_load: attach whatever the areas hold */
{
    uint32_t a, w;
    if (!flash_ok)
        return;
    for (a = 0; a < PCM_FLASH_AREAS; a++)
        pcm_store_attach(a, PCM_XIP(PCM_AREA[a].base), PCM_AREA[a].size);
    wavetable_bank_attach(PCM_XIP(PCM_AREA[PCM_BANK_AREA].base), PCM_AREA[PCM_BANK_AREA].size);
    for (a = 0; a < PCM_ALL_AREAS; a++) {
        if (!pcm_held(a, &w))
            pcm_waves_state = PCM_WAVES_MISSING;
        else if (pcm_want_hcrc[a] && w != pcm_want_hcrc[a] && pcm_waves_state == PCM_WAVES_OK)
            pcm_waves_state = PCM_WAVES_STALE;
    }
}

/* area a's image: its entries (or banks) when it holds, else < 0 (pcm_store_check's reasons) */
static int pcm_area_check(uint32_t a)
{
    const uint8_t *img = PCM_XIP(PCM_AREA[a].base);
    return a == PCM_BANK_AREA ? wavetable_bank_check(img, PCM_AREA[a].size) : pcm_store_check(a, img, PCM_AREA[a].size);
}

/* 7-bit packed bytes (groups of up to 7, each after their top bits) -> out; their count, or -1 when over max */
static int pcm_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0, i, j;
    for (i = 0; i < na; i += 8u) {
        uint32_t top = a[i], k = na - i - 1u > 7u ? 7u : na - i - 1u;
        for (j = 0; j < k; j++) {
            if (n >= max)
                return -1;
            out[n++] = (uint8_t)(a[i + 1u + j] | (((top >> j) & 1u) << 7));
        }
    }
    return (int)n;
}

static uint32_t pcm_u21(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14; }

static uint32_t pcm_begin(uint32_t area)
{
    if (area >= PCM_ALL_AREAS)
        return 1;
    pcm_up.on = 0;
    syn_lock();
    if (area == PCM_BANK_AREA)                      /* nothing may read the area while it changes */
        wavetable_bank_detach();
    else
        pcm_store_detach(area);
    syn_unlock();
    if (!flash_ok || st_erase(PCM_AREA[area].base))
        return 2;
    pcm_up.on = 1;
    pcm_up.area = (uint8_t)area;
    pcm_up.next = PCM_HDR_SIZE;
    pcm_up.erased = PCM_SECTOR;
    return 0;
}

static uint32_t pcm_write(uint32_t off, const uint8_t *a, uint32_t na)
{
    int n = pcm_unpack7(a, na, pcm_buf, sizeof pcm_buf);
    uint32_t base, size;
    if (n <= 0)
        return 1;
    if (!pcm_up.on || off != pcm_up.next)
        return 3;
    base = PCM_AREA[pcm_up.area].base;
    size = PCM_AREA[pcm_up.area].size;
    if ((uint32_t)n > size - off)
        return 4;
    while (off + (uint32_t)n > pcm_up.erased) {
        if (st_erase(base + pcm_up.erased)) {
            pcm_up.on = 0;
            return 2;
        }
        pcm_up.erased += PCM_SECTOR;
    }
    if (st_prog(base + off, pcm_buf, (uint32_t)n)) {
        pcm_up.on = 0;
        return 2;
    }
    pcm_up.next = off + (uint32_t)n;
    return 0;
}

/* -> rc; *waves: the area's waves, or the check's reason + 8 when refused */
static uint32_t pcm_end(const uint8_t *a, uint32_t na, uint32_t *waves)
{
    static const uint32_t zero = 0;
    const PcmHdr *h = (const PcmHdr *)(const void *)pcm_buf;
    uint32_t area = pcm_up.area, base = PCM_AREA[area].base;
    int n;
    *waves = 0;
    if (!pcm_up.on)
        return 3;
    pcm_up.on = 0;
    if (pcm_unpack7(a, na, pcm_buf, sizeof pcm_buf) != (int)PCM_HDR_SIZE)
        return 1;
    if (h->len != pcm_up.next)
        return 3;                                   /* not every byte the header covers was written */
    if (st_prog(base, pcm_buf, PCM_HDR_SIZE))
        return 2;
    n = pcm_area_check(area);
    if (n < 0) {
        st_prog(base, &zero, 4u);                   /* the magic -> 0: the area reads empty */
        *waves = (uint32_t)(8 - n);
        return 5;
    }
    syn_lock();
    if (area == PCM_BANK_AREA)
        wavetable_bank_attach(PCM_XIP(base), PCM_AREA[area].size);
    else
        pcm_store_attach(area, PCM_XIP(base), PCM_AREA[area].size);
    syn_unlock();
    *waves = (uint32_t)n;
    return 0;
}

/* the editor's PCM commands: handled (reply built) -> 1 */
static int ed_pcm_handle(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t rc, w, i, area, first, n, used;
    switch (cmd) {
    case ED_PCM_BEGIN:
        area = na ? a[0] : PCM_FLASH_AREAS;
        rc = pcm_begin(area);
        ed_b(area);
        ed_b(rc);
        return 1;
    case ED_PCM_WRITE:
        w = na >= 3u ? pcm_u21(a) : 0u;
        rc = na > 3u ? pcm_write(w, a + 3, na - 3u) : 1u;
        ed_b(w);
        ed_b(w >> 7);
        ed_b(w >> 14);
        ed_b(rc);
        return 1;
    case ED_PCM_END:
        rc = pcm_end(a, na, &w);
        ed_b(rc);
        ed_b(w);
        return 1;
    case ED_PCM_ID:
        area = na ? a[0] : PCM_ALL_AREAS;
        ed_b(area);
        w = 0;
        used = area < PCM_ALL_AREAS ? (uint32_t)pcm_held(area, &w) : 0u;
        ed_b(used ? 1u : 0u);
        for (i = 0; i < 5u; i++)
            ed_b(w >> (7u * i));
        return 1;
    case ED_PCM_INFO:
        area = na ? a[0] : 0u;
        first = na > 1u ? a[1] : 0u;
        ed_b(PCM_ALL_AREAS);
        ed_b(area);
        if (area >= PCM_ALL_AREAS) {
            ed_b(0);
            return 1;
        }
        used = area == PCM_BANK_AREA ? wavetable_bank_used() : pcm_area_used(area);
        n = area == PCM_BANK_AREA ? (used ? 1u : 0u) : pcm_area_count(area);
        ed_b(n);
        ed_b(PCM_AREA[area].size / 1024u);
        ed_b((PCM_AREA[area].size / 1024u) >> 7);
        ed_b(used);
        ed_b(used >> 7);
        ed_b(used >> 14);
        if (area == PCM_BANK_AREA) {
            if (n && !first)
                ed_str(wavetable_bank_names[0], PCM_NAME_LEN);
            return 1;
        }
        for (w = 0, i = 0; i < area; i++)
            w += pcm_area_count(i);                 /* the area's first entry in the whole list */
        for (i = first; i < n && i < first + 16u; i++) {
            const PcmEntry *e = pcm_entry(w + i, NULL);
            char s[PCM_NAME_LEN + 1];
            uint32_t k;
            for (k = 0; k < PCM_NAME_LEN; k++)
                s[k] = e->name[k];
            s[PCM_NAME_LEN] = 0;
            ed_str(s, PCM_NAME_LEN);
        }
        return 1;
    default:
        return 0;
    }
}
