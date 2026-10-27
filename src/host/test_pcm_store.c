/* Host test of the PCM wave store (2026-10-21: src/dsp/pcm_store.c, src/hardware/pcm_store_fm1.c) on a simulated 1 MiB
 * NOR (erase: 0xFF; program: clears bits only), with X0X's storage.c hooks stood in: fresh or foreign flash reads empty;
 * an upload through the editor commands attaches and survives a reboot; the waves read back byte for byte; a cut-off
 * upload, a damaged byte, writes out of order or past the area, and images with a bad entry leave the area empty; an
 * upload never writes outside its area; the WAVE entries list in order and their references find their samples (format
 * version 2); then the .syx pcm_store_upload.py makes from gen_pcm_waves.py's area 1 image replays. Area 2, wavetable
 * bank 0's image (2026-10-26, Foundations): it uploads, attaches and survives a reboot; anything else there leaves bank 0
 * on the sine; the other banks and the PCM areas never move.
 *   python src/dsp/gen_pcm_waves.py --images-only
 *   python src/hardware/pcm_store_upload.py syx src/dsp/pcm_area1.bin demo.syx --area 1
 *   gcc -O1 -Wall -Wextra -Isrc -Isrc/dsp -Isrc/hardware src/host/test_pcm_store.c src/dsp/pcm_store.c \
 *       src/dsp/wavetable_attach.c src/dsp/wavetables.c src/dsp/wavetables_image.c \
 *       src/host/foundations_image_host.c -o test_pcm_store \
 *       && ./test_pcm_store demo.syx src/dsp/pcm_area1.bin
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PresetStructures.h"
#include "pcm_store.h"
#include "wavetables.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* ---- the flash and X0X's storage.c hooks */
static uint8_t nor[0x100000] __attribute__((aligned(4096)));
static int progs_left = -1;                          /* >= 0: the power is cut after that many more programs */
static int cut;
static int in_store(uint32_t off, uint32_t n) { return off >= 0x97000u && off + n <= 0xE0000u; }
static int st_erase(uint32_t off)
{
    if ((off & 0xFFFu) || !in_store(off, 0x1000u)) return -8;
    if (cut) return -1;
    memset(nor + off, 0xFF, 0x1000u);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!in_store(off, n)) return -8;
    if (cut || progs_left == 0) { cut = 1; return -1; }
    if (progs_left > 0) progs_left--;
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}

/* ---- the glue's pieces */
static uint8_t flash_ok = 1;
static void syn_lock(void) {}
static void syn_unlock(void) {}
static uint8_t reply[600];
static uint32_t reply_n;
static void ed_b(uint32_t v) { if (reply_n < sizeof reply) reply[reply_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_str(const char *s, uint32_t max)
{
    for (uint32_t i = 0; s && s[i] && i < max; i++) ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
#define PCM_XIP(off) ((const uint8_t *)nor + (off))

#include "pcm_store_fm1.c"

/* ---- an image, as pcm_store_upload.py makes it */
static uint8_t img[0xE000] __attribute__((aligned(4)));
static uint32_t img_len;
static PcmEntry *ent(uint32_t i) { return (PcmEntry *)(void *)(img + PCM_HDR_SIZE + i * PCM_ENTRY_SIZE); }
static void seal(uint32_t count, uint32_t len)
{
    PcmHdr *h = (PcmHdr *)(void *)img;
    memset(h, 0, sizeof *h);
    h->magic = PCM_MAGIC;
    h->version = PCM_VERSION;
    h->count = (uint16_t)count;
    h->len = len;
    h->crc = wavetable_crc32(0, img + PCM_HDR_SIZE, len - PCM_HDR_SIZE);
    h->hcrc = wavetable_crc32(0, h, PCM_HDR_SIZE - 4u);
    img_len = len;
}
/* n entries for area `area`: a ONESHOT (1200 samples at 16 kHz), a LOOP (3000 at 22050 Hz, its guard byte after), then
 * a WAVE on that loop (twice, B faster) with that attack, over again */
static void make(uint32_t n, uint32_t area)
{
    uint32_t off = PCM_HDR_SIZE + n * PCM_ENTRY_SIZE, i, k;
    memset(img, 0, sizeof img);
    for (i = 0; i < n; i++) {
        PcmEntry *e = ent(i);
        snprintf(e->name, sizeof e->name, "ENTRY %u", (unsigned)(i % 100u));
        e->kind = (uint8_t)(i % 3u == 0u ? PCM_ONESHOT : i % 3u == 1u ? PCM_LOOP : PCM_WAVE);
        if (e->kind == PCM_WAVE) {
            e->u.w.loop_a = e->u.w.loop_b = PCM_REF(area, i - 1u);
            e->u.w.atk_ref = PCM_REF(area, i - 2u);
            e->u.w.atk_fw = PCM_REF_NONE;
            e->u.w.atk_level = 100;
            e->u.w.b_rate_q12 = 4342;
            e->u.w.b_off_q16 = 24000;
            continue;
        }
        e->u.s.off = off;
        e->root = e->kind == PCM_LOOP ? 57 : 60;
        e->u.s.rate = e->kind == PCM_LOOP ? 22050 : 16000;
        e->u.s.len = e->kind == PCM_LOOP ? 3000 : 1200;
        for (k = 0; k < e->u.s.len; k++) img[off + k] = (uint8_t)(k * 7u + i);
        off += e->u.s.len;
        if (e->kind == PCM_LOOP)
            img[off++] = img[e->u.s.off];          /* the guard: the loop's first byte again */
    }
    seal(n, off);
}

/* ---- the editor commands */
static uint32_t pack7(const uint8_t *b, uint32_t n, uint8_t *out)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 7u) {
        uint32_t k = n - i > 7u ? 7u : n - i, top = 0;
        for (uint32_t j = 0; j < k; j++) top |= (uint32_t)(b[i + j] >> 7) << j;
        out[o++] = (uint8_t)top;
        for (uint32_t j = 0; j < k; j++) out[o++] = b[i + j] & 0x7Fu;
    }
    return o;
}
static uint8_t args[400];
static int cmd(uint32_t c, const uint8_t *a, uint32_t na)   /* -> rc */
{
    reply_n = 0;
    if (!ed_pcm_handle(c, a, na)) return -1;
    return reply[c == ED_PCM_WRITE ? 3 : c == ED_PCM_BEGIN ? 1 : 0];
}
static int begin(uint32_t area) { args[0] = (uint8_t)area; return cmd(ED_PCM_BEGIN, args, 1); }
static int write_at(uint32_t off, uint32_t n)
{
    args[0] = off & 0x7Fu; args[1] = (off >> 7) & 0x7Fu; args[2] = (off >> 14) & 0x7Fu;
    return cmd(ED_PCM_WRITE, args, 3u + pack7(img + off, n, args + 3));
}
static int end(void) { return cmd(ED_PCM_END, args, pack7(img, PCM_HDR_SIZE, args)); }
/* the whole upload of img into area: the first failing rc, or 0 */
static int upload(uint32_t area)
{
    int rc = begin(area);
    for (uint32_t off = PCM_HDR_SIZE; !rc && off < img_len; off += 256u)
        rc = write_at(off, img_len - off > 256u ? 256u : img_len - off);
    return rc ? rc : end();
}
static const uint8_t *l2d(const PcmEntry *e, uint32_t a)
{
    return (const uint8_t *)(const void *)(nor + (a ? 0xCE000u : 0x97000u)) + e->u.s.off;
}
static void reboot(void)
{
    for (uint32_t a = 0; a < PCM_AREAS; a++) pcm_store_detach(a);
    wavetable_bank_detach();
    pcm_boot();
}

/* ---- area 2: bank 0's image */
extern const uint8_t wavetable_image_host[WT_STORE_IMAGE_LEN];
extern const uint8_t foundations_image_host[];
extern const uint32_t foundations_image_host_len;
static int bank0_sine(void)
{
    for (int f = 0; f < NUM_MORPH_STEPS; f++)
        if (wavetable_frame(0, f, 0) != wavetable_sine) return 0;
    return 1;
}
static int bank0_in_flash(void)
{
    for (int f = 0; f < NUM_MORPH_STEPS; f++)
        if (wavetable_frame(0, f, 0) != (const int16_t *)(const void *)(nor + 0xBC000u + WT_BANK_HDR_SIZE) + f * WT_FRAME_SAMPLES)
            return 0;
    return 1;
}
static int image_banks_linked(void)
{
    for (int b = WT_BANK_FIRST_IMAGE; b < NUM_BANKS; b++)
        if (wavetable_frame(b, 7, 0) != (const int16_t *)(const void *)(wavetable_image_host + WT_STORE_DATA_OFF) +
                                          ((b - WT_BANK_FIRST_IMAGE) * NUM_MORPH_STEPS + 7) * WT_FRAME_SAMPLES)
            return 0;
    return 1;
}
/* PCM_ID: area a -> held (or -1 when the command is not handled); id_hcrc: the hcrc of that reply */
static int id_of(uint32_t a)
{
    args[0] = (uint8_t)a;
    reply_n = 0;
    if (!ed_pcm_handle(ED_PCM_ID, args, 1) || reply[0] != a) return -1;
    return reply[1];
}
static uint32_t id_hcrc(void)
{
    uint32_t w = 0;
    for (uint32_t i = 0; i < 5u; i++) w |= (uint32_t)reply[2 + i] << (7u * i);
    return w;
}
static void bank_tests(void)
{
    uint32_t i;
    memset(nor, 0xFF, sizeof nor);
    reboot();
    CHECK(bank0_sine() && !wavetable_bank_used() && image_banks_linked(), "erased area 2: bank 0 plays the sine");
    for (i = 0; i < 0x6000u; i++) nor[0xBC000u + i] = (uint8_t)(i * 13u + 1u);   /* old preset chunks */
    reboot();
    CHECK(bank0_sine(), "foreign data in area 2: bank 0 plays the sine");

    memcpy(img, foundations_image_host, foundations_image_host_len);
    img_len = foundations_image_host_len;
    CHECK(img_len == WT_BANK_IMAGE_LEN, "the host image is one bank (%u B)", (unsigned)img_len);
    for (int at = 0; at <= 64; at += at < 3 ? 1 : at < 58 ? 11 : 6) {  /* cut off up to the header (program 65) */
        cut = 0; progs_left = at;
        upload(2);
        cut = 0; progs_left = -1;
        reboot();
        CHECK(bank0_sine(), "bank upload cut after %d programs: bank 0 plays the sine", at);
    }
    CHECK(upload(2) == 0 && reply[1] == 1u, "Foundations uploads to area 2 (rc 0, 1 bank)");
    CHECK(bank0_in_flash() && wavetable_bank_used() == WT_BANK_IMAGE_LEN && image_banks_linked(),
          "bank 0 reads area 2; banks 1..16 stay on the linked image");
    CHECK(!memcmp(nor + 0xBC000u, foundations_image_host, foundations_image_host_len), "area 2 holds the image");
    reboot();
    CHECK(bank0_in_flash(), "bank 0 survives a reboot");
    reply_n = 0;
    args[0] = 2; args[1] = 0;
    CHECK(ed_pcm_handle(ED_PCM_INFO, args, 2) && reply[0] == 3u && reply[1] == 2u && reply[2] == 1u &&
          !strcmp((const char *)reply + 8, "Foundations"), "PCM_INFO area 2: 3 areas, 1 bank, its name");

    CHECK(id_of(2) == 1 && id_hcrc() == ((const wt_bank_hdr_t *)(const void *)foundations_image_host)->hcrc,
          "PCM_ID area 2: held, the image's hcrc");
    CHECK(id_of(3) == 0 && reply_n == 7u, "PCM_ID area 3: not held");

    nor[0xBC000u + WT_BANK_IMAGE_LEN - 1u] ^= 0x01u;
    reboot();
    CHECK(bank0_sine(), "a flipped data bit in area 2: bank 0 plays the sine (CRC)");
    CHECK(id_of(2) == 0 && id_hcrc() == 0, "PCM_ID area 2 after the refused image: not held");

    make(4, 1);
    CHECK(upload(2) == 5 && reply[1] == 8u + 1u && bank0_sine(), "a PCM image in area 2: refused, the sine");
    memcpy(img, foundations_image_host, foundations_image_host_len);
    img_len = foundations_image_host_len;
    ((wt_bank_hdr_t *)(void *)img)->bank_id ^= 1u;
    ((wt_bank_hdr_t *)(void *)img)->hcrc = wavetable_crc32(0, img, WT_BANK_HDR_SIZE - 4u);
    CHECK(upload(2) == 5 && reply[1] == 8u + 3u && bank0_sine(), "an image of another bank: refused (reason 3)");
    memcpy(img, foundations_image_host, foundations_image_host_len);
    CHECK(upload(2) == 0 && bank0_in_flash(), "Foundations again");

    make(4, 1);
    CHECK(upload(1) == 0 && pcm_area_count(1) == 4 && bank0_in_flash(), "a PCM upload to area 1 leaves bank 0");
    CHECK(id_of(1) == 1 && id_hcrc() == ((const PcmHdr *)(const void *)img)->hcrc, "PCM_ID area 1: the PCM image's hcrc");
    reboot();
    CHECK(pcm_area_count(1) == 4 && bank0_in_flash() && image_banks_linked(), "both after a reboot");
    CHECK(begin(3) == 1, "no area 3: rc 1");
}

/* every entry of img is entry first + i, with the same entry and samples (a LOOP's guard byte too) */
/* the entries of the uploaded areas: the firmware's own area (PCM_AREA_FW) is always there, listed after them */
static uint32_t flash_entries(void) { return pcm_entry_count() - pcm_area_count(PCM_AREA_FW); }

static void same_waves(uint32_t first, uint32_t n, const char *what)
{
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *d;
        const PcmEntry *e = pcm_entry(first + i, &d);
        CHECK(e && !memcmp(e, ent(i), sizeof *e), "%s: entry %u", what, (unsigned)i);
        if (e && e->kind != PCM_WAVE)
            CHECK(d && !memcmp(d, img + e->u.s.off, e->u.s.len + (e->kind == PCM_LOOP)), "%s: entry %u's samples",
                  what, (unsigned)i);
        if (e && e->kind == PCM_WAVE)
            CHECK(!d, "%s: entry %u, a WAVE, has no samples", what, (unsigned)i);
    }
}

static int replay(const char *syx, const char *bin)
{
    FILE *f = fopen(syx, "rb");
    static uint8_t s[1 << 18];
    size_t n = f ? fread(s, 1, sizeof s, f) : 0;
    int frames = 0, bad = 0;
    if (f) fclose(f);
    CHECK(n > 0, "%s: no SysEx", syx);
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && s[j] != 0xF7) j++;
        if (j >= n || s[i] != 0xF0 || s[i + 1] != 0x7D || s[i + 2] != 0x46 || s[i + 3] != 0x4C) { bad++; break; }
        int rc = cmd(s[i + 4], s + i + 5, (uint32_t)(j - i - 5));
        if (rc) { bad++; printf("FAIL: replay frame %d (cmd %u): rc %d\n", frames, s[i + 4], rc); }
        frames++;
        i = j + 1;
    }
    CHECK(!bad, "%s replays", syx);
    f = fopen(bin, "rb");
    img_len = f ? (uint32_t)fread(img, 1, sizeof img, f) : 0u;
    if (f) fclose(f);
    CHECK(img_len > PCM_HDR_SIZE, "%s: the image", bin);
    return frames;
}

int main(int argc, char **argv)
{
    static uint8_t ref[sizeof nor];
    uint32_t a, i;
    int rc;

    /* fresh flash, then X0X's objects in area 0 and the old preset chunks in area 1: no image anywhere */
    memset(nor, 0xFF, sizeof nor);
    reboot();
    CHECK(flash_entries() == 0 && pcm_wave_count() == 0, "erased flash: no entries");
    CHECK(pcm_area_count(PCM_AREA_FW) > 0 && pcm_entry(0, NULL) && !pcm_wave(0),
          "erased flash: the firmware's area still attached (%u entries), no waves", (unsigned)pcm_area_count(PCM_AREA_FW));
    for (i = 0; i < 0x9000u; i++) nor[0x97000u + i] = (uint8_t)(i * 31u);
    memcpy(nor + 0xCE000u, "CRNP", 4);
    reboot();
    CHECK(flash_entries() == 0, "foreign data: no entries");

    /* an upload to each area; nothing outside the area changes; the waves come back after a reboot */
    for (i = 0; i < sizeof nor; i++) nor[i] = (uint8_t)(i * 13u + 5u);
    memcpy(ref, nor, sizeof nor);
    make(5, 1);
    rc = upload(1);
    CHECK(rc == 0 && reply[1] == 5u, "area 1 upload: rc %d, %u waves", rc, (unsigned)reply[1]);
    CHECK(!memcmp(nor, ref, 0xCE000u) && !memcmp(nor + 0xDC000u, ref + 0xDC000u, sizeof nor - 0xDC000u),
          "area 1 upload writes inside 0xCE000..0xDBFFF only");
    CHECK(!memcmp(nor + 0xCE000u, img, img_len), "area 1 holds the image");
    same_waves(0, 5, "area 1, attached");
    reboot();
    CHECK(flash_entries() == 5, "area 1 after a reboot: %u entries", (unsigned)flash_entries());
    same_waves(0, 5, "area 1, after a reboot");
    memcpy(ref, nor, sizeof nor);
    make(3, 0);
    rc = upload(0);
    CHECK(rc == 0 && reply[1] == 3u, "area 0 upload: rc %d", rc);
    CHECK(!memcmp(nor, ref, 0x97000u) && !memcmp(nor + 0xA0000u, ref + 0xA0000u, sizeof nor - 0xA0000u),
          "area 0 upload writes inside 0x97000..0x9FFFF only");
    reboot();
    CHECK(flash_entries() == 8 && pcm_area_count(0) == 3 && pcm_area_count(1) == 5, "both areas: 3 + 5 entries");
    same_waves(0, 3, "area 0 first in the list");

    /* the wave list: area 0's WAVE (entry 2), then area 1's (entry 5); a reference finds its sample, of its kind only */
    CHECK(pcm_wave_count() == 2 && pcm_wave(0) == pcm_entry(2, NULL) && pcm_wave(1) == pcm_entry(5, NULL) &&
          !pcm_wave(2), "the wave list: one WAVE from each area, in order");
    {
        const uint8_t *d;
        const PcmEntry *w = pcm_wave(1), *l = pcm_sample(w->u.w.loop_a, PCM_LOOP, &d);
        CHECK(l == pcm_entry(4, NULL) && d == l2d(l, 1), "area 1's wave: its loop A");
        CHECK(pcm_sample(w->u.w.atk_ref, PCM_ONESHOT, &d) == pcm_entry(3, NULL), "area 1's wave: its attack");
        CHECK(!pcm_sample(w->u.w.atk_ref, PCM_LOOP, &d) && !d, "a reference of the wrong kind: none");
        CHECK(!pcm_sample(PCM_REF(1, 9), PCM_LOOP, NULL) && !pcm_sample(PCM_REF(PCM_AREA_FW, 62), PCM_LOOP, NULL) &&
              !pcm_sample(PCM_REF(PCM_AREAS, 0), PCM_LOOP, NULL),
              "a reference past the entries or the areas: none");
    }

    /* PCM_INFO: the names */
    args[0] = 1; args[1] = 0;
    reply_n = 0;
    ed_pcm_handle(ED_PCM_INFO, args, 2);
    CHECK(reply[0] == PCM_ALL_AREAS && reply[1] == 1 && reply[2] == 5 && reply[3] == 56 && !strcmp((char *)reply + 8, "ENTRY 0"),
          "PCM_INFO area 1: 5 entries, 56 KiB, ENTRY 0 first");

    /* the biggest image an area holds, and one byte too many */
    memset(img, 0, sizeof img);
    ent(0)->kind = PCM_ONESHOT; ent(0)->u.s.rate = 8000; ent(0)->u.s.off = PCM_HDR_SIZE + PCM_ENTRY_SIZE;
    ent(0)->u.s.len = 0x9000u - ent(0)->u.s.off;
    strcpy(ent(0)->name, "FULL");
    seal(1, 0x9000u);
    CHECK(upload(0) == 0 && pcm_area_count(0) == 1, "a full 36 KiB image fits area 0");
    ent(0)->u.s.len++;
    seal(1, 0x9001u);
    CHECK(upload(0) == 4 && pcm_area_count(0) == 0, "36 KiB + 1 is refused (rc 4), area 0 empty");

    /* an upload cut off before each of its first programs, then every 7th, up to the header: the area reads empty, before and after a reboot */
    make(4, 1);
    for (int at = 0; at < (int)((img_len - PCM_HDR_SIZE + 255u) / 256u) + 1; at += at < 3 ? 1 : 7) {
        cut = 0; progs_left = at;
        upload(1);
        cut = 0; progs_left = -1;
        reboot();
        CHECK(pcm_area_count(1) == 0, "upload cut after %d programs: area 1 empty", at);
    }
    CHECK(upload(1) == 0 && pcm_area_count(1) == 4, "a whole upload after the cuts");

    /* one damaged byte in the samples: refused at boot */
    nor[0xCE000u + img_len - 1u] ^= 0x01u;
    reboot();
    CHECK(pcm_area_count(1) == 0, "a flipped data bit: area 1 empty (CRC)");

    /* out of order, no BEGIN, a short image, past the area */
    CHECK(begin(1) == 0 && write_at(PCM_HDR_SIZE + 256u, 64) == 3, "a write that skips bytes: rc 3");
    CHECK(begin(1) == 0 && write_at(PCM_HDR_SIZE, 64) == 0 && end() == 3, "an END before every byte: rc 3");
    CHECK(end() == 3, "an END with no upload: rc 3");
    CHECK(begin(3) == 1, "no area 3: rc 1");
    reboot();
    CHECK(pcm_area_count(1) == 0, "after the refused uploads: area 1 empty");

    /* images with a bad entry: refused at END (rc 5) and the area reads empty */
    static const char *const bad[] = {"samples past the image", "a loop without its guard byte", "rate 0 on a sample",
                                      "samples over the entries", "a wave on a one-shot", "two attacks on a wave",
                                      "an unknown kind", "a wave with no name", "B at 20x"};
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        make(3, 1);
        switch (i) {
        case 0: ent(0)->u.s.len += 4000; break;
        case 1: ent(1)->u.s.len += 1; break;
        case 2: ent(1)->u.s.rate = 0; break;
        case 3: ent(0)->u.s.off = PCM_HDR_SIZE; break;
        case 4: ent(2)->u.w.loop_a = PCM_REF(1, 0); break;
        case 5: ent(2)->u.w.atk_fw = 3; break;
        case 6: ent(0)->kind = 7; break;
        case 7: memset(ent(2)->name, 0, PCM_NAME_LEN); break;
        case 8: ent(2)->u.w.b_rate_q12 = (uint16_t)(20 * 4096 > 65535 ? 65535 : 20 * 4096); break;
        }
        seal(3, img_len);
        rc = upload(1);
        CHECK(rc == 5 && reply[1] == 8u + 3u, "%s: rc %d (5), reason %u (entry)", bad[i], rc, (unsigned)reply[1]);
        reboot();
        CHECK(pcm_area_count(1) == 0, "%s: area 1 empty after a reboot", bad[i]);
    }

    bank_tests();

    /* the tool's own upload */
    if (argc > 2) {
        int frames = replay(argv[1], argv[2]);
        a = pcm_area_count(1);
        CHECK(a == ((const PcmHdr *)(const void *)img)->count && a > 0, "the tool's image: %u entries", (unsigned)a);
        same_waves(pcm_area_count(0), a, "the tool's image");
        printf("replayed %d frames of %s\n", frames, argv[1]);
    }

    printf(failures ? "%d FAILED\n" : "PCM store: uploads, reboots, cuts and bad images all hold\n", failures);
    return failures;
}
