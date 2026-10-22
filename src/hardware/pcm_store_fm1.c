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
 * Left alone: OBJ_PANEL 0xDC000 (the panel calibration), 0xDE000 (X0X's motion object: free, 8 KiB, kept spare),
 * 0xE0000.. (the update loader's staging) and the preset store's free chunks 1 and 14..16 (its room to grow).
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
 * rc: 0 ok, 1 arguments, 2 flash, 3 order (no BEGIN, or not the next offset), 4 too big for the area, 5 refused on
 * the check (the reply then carries pcm_store_check's reason + 8 as the wave count).
 * The erases stop the audio for tens of ms each (st_erase silences it first): an upload is a studio job. */
#include "pcm_store.h"

#ifndef PCM_XIP
#define PCM_XIP(off) ((const uint8_t *)FM1_XIP(off))
#endif
#define PCM_SECTOR 4096u
enum { ED_PCM_BEGIN = 100, ED_PCM_WRITE, ED_PCM_END, ED_PCM_INFO };
static const struct { uint32_t base, size; } PCM_AREA[PCM_AREAS] = {
    {0x97000u, 0x9000u},
    {0xCE000u, 0xE000u},
};
#ifdef PS_NCHUNK                                    /* (the firmware: the preset store comes first; not in the host test) */
PS_STATIC_ASSERT(0x97000u + 0x9000u <= PS_BASE, "area 0 ends below the preset store");
PS_STATIC_ASSERT(PS_BASE + PS_NCHUNK * 2u * PS_SECTOR <= 0xCE000u, "area 1 starts after the preset store's chunks");
#endif
_Static_assert(0xCE000u + 0xE000u <= 0xDC000u, "area 1 ends below X0X's panel calibration (OBJ_PANEL)");

static struct {
    uint8_t on, area;                               /* an upload since PCM_BEGIN, into area */
    uint32_t next;                                  /* the offset the next PCM_WRITE must start at */
    uint32_t erased;                                /* the area's bytes erased so far (whole sectors) */
} pcm_up;
static uint8_t pcm_buf[256] __attribute__((aligned(4)));   /* a write's bytes, in RAM for the flash driver */

static void pcm_boot(void)                          /* after ps_load: attach whatever the areas hold */
{
    uint32_t a;
    for (a = 0; a < PCM_AREAS; a++)
        if (flash_ok)
            pcm_store_attach(a, PCM_XIP(PCM_AREA[a].base), PCM_AREA[a].size);
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
    if (area >= PCM_AREAS)
        return 1;
    pcm_up.on = 0;
    syn_lock();
    pcm_store_detach(area);                         /* nothing may read the area while it changes */
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
    n = pcm_store_check(area, PCM_XIP(base), PCM_AREA[area].size);
    if (n < 0) {
        st_prog(base, &zero, 4u);                   /* the magic -> 0: the area reads empty */
        *waves = (uint32_t)(8 - n);
        return 5;
    }
    syn_lock();
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
        area = na ? a[0] : PCM_AREAS;
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
    case ED_PCM_INFO:
        area = na ? a[0] : 0u;
        first = na > 1u ? a[1] : 0u;
        ed_b(PCM_AREAS);
        ed_b(area);
        if (area >= PCM_AREAS) {
            ed_b(0);
            return 1;
        }
        n = pcm_area_count(area);
        used = pcm_area_used(area);
        ed_b(n);
        ed_b(PCM_AREA[area].size / 1024u);
        ed_b((PCM_AREA[area].size / 1024u) >> 7);
        ed_b(used);
        ed_b(used >> 7);
        ed_b(used >> 14);
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
