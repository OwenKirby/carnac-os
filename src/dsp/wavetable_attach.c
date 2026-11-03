/* The wavetable banks, from the wavetable image (wavetables.h, spec 6.5).
 *
 * Every build starts attached to the image linked in as wavetables_image.c: on the FM1 since 2026-10-16 it sits in the
 * firmware and is read in place through XIP (it was uploaded over USB-MIDI to the store flash at WT_STORE_BASE, which
 * now holds the saved presets). wavetable_attach checks an image's header and CRC; without a valid one every bank
 * plays wavetable_sine, like a sampler with its sample gone: the synth still sounds, in tune.
 * Since 2026-10-26 the image holds banks 1..14 (16 until Menhir and Hollow Reeds retired, 2026-10-27); bank 0 (Foundations) and,
 * since 2026-11-03, the image's last 6 frames on the FM1 (WT_IMAGE_SPILLED) are in the store flash's wavetable areas
 * (wavetable_area_attach), and play the sine until one is attached. */
#include <stddef.h>
#include "wavetables.h"
#include "dsp_cold.h"

extern const uint8_t wavetable_image_host[WT_IMAGE_LINKED_LEN];  /* wavetables_image.c (the FM1: spilled frames out) */
#define WT_START(b, f) ((const int16_t *)(const void *)(wavetable_image_host + WT_STORE_DATA_OFF) + \
                        ((b) * NUM_MORPH_STEPS + (f)) * WT_FRAME_SAMPLES)
#define WT_START_BANK(b) WT_START(b, 0), WT_START(b, 1), WT_START(b, 2), WT_START(b, 3), \
                         WT_START(b, 4), WT_START(b, 5), WT_START(b, 6), WT_START(b, 7)
#define WT_SINE_BANK wavetable_sine, wavetable_sine, wavetable_sine, wavetable_sine,                      wavetable_sine, wavetable_sine, wavetable_sine, wavetable_sine
_Static_assert(NUM_MORPH_STEPS == 8 && NUM_BANKS == 15 && WT_BANK_FIRST_IMAGE == 1 && NUM_IMAGE_BANKS == 14,
               "WT_START_BANK / the initialiser below list every frame");
#define WT_IMAGE_FRAMES (wavetable_frames + WT_BANK_FIRST_IMAGE * NUM_MORPH_STEPS)

const int16_t *wavetable_frames[NUM_BANKS * NUM_MORPH_STEPS] = {
    WT_SINE_BANK,                                          /* bank 0 until its image is attached */
    WT_START_BANK(0), WT_START_BANK(1), WT_START_BANK(2), WT_START_BANK(3), WT_START_BANK(4),
    WT_START_BANK(5), WT_START_BANK(6), WT_START_BANK(7), WT_START_BANK(8), WT_START_BANK(9),
    WT_START_BANK(10), WT_START_BANK(11), WT_START_BANK(12),
#ifdef WT_IMAGE_SPILLED                                    /* the last bank's frames 3..8: an area's, the sine until then */
    WT_START(13, 0), WT_START(13, 1), wavetable_sine, wavetable_sine, wavetable_sine, wavetable_sine, wavetable_sine,
    wavetable_sine,
#else
    WT_START_BANK(13),
#endif
};
_Static_assert(WT_SPILL_FRAMES == 6u, "the initialiser above leaves out the last 6 frames");
static int wt_ok = 1;
static uint32_t wt_area_len[WT_AREAS];                     /* each area's attached image (0: none) */
static uint8_t wt_area_idx[WT_AREAS][WT_AREA_MAX_FRAMES], wt_area_n[WT_AREAS];   /* the frames it holds */

uint32_t wavetable_crc32(uint32_t crc, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = ~crc;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

void wavetable_detach(void)
{
    wt_ok = 0;
    for (uint32_t i = 0; i < WT_LINKED_FRAMES; i++)
        WT_IMAGE_FRAMES[i] = wavetable_sine;
}

int wavetable_attached(void) { return wt_ok; }

int wavetable_attach(const uint8_t *image)
{
    const wt_store_hdr_t *h = (const wt_store_hdr_t *)(const void *)image;
    wavetable_detach();
    if (!image || ((uintptr_t)image & 3u))
        return -1;
    if (h->magic != WT_STORE_MAGIC || h->version != WT_STORE_VERSION ||
        h->hcrc != wavetable_crc32(0, h, sizeof *h - 4u))
        return -2;                                         /* no image (erased, or sample data from before) */
    if (h->banks != NUM_IMAGE_BANKS || h->frames != NUM_MORPH_STEPS || h->frame_samples != WT_FRAME_SAMPLES ||
        h->levels != WT_LEVELS || h->data_off != WT_STORE_DATA_OFF || h->data_len != WT_STORE_DATA_LEN ||
        h->set_id != wavetable_set_id)
        return -3;                                         /* an image of another bank list: upload the matching one */
#ifndef WT_IMAGE_SPILLED                                   /* (spilled: the CRC covers frames not linked; the
                                                            * firmware package's own checks cover the rest) */
    if (wavetable_crc32(0, image + WT_STORE_DATA_OFF, WT_STORE_DATA_LEN) != h->crc)
        return -4;                                         /* damaged or half-written data */
#endif
    const int16_t *data = (const int16_t *)(const void *)(image + WT_STORE_DATA_OFF);
    for (uint32_t i = 0; i < WT_LINKED_FRAMES; i++)
        WT_IMAGE_FRAMES[i] = data + i * WT_FRAME_SAMPLES;
    wt_ok = 1;
    return 0;
}

SYNTH_COLD int wavetable_area_check(const uint8_t *img, uint32_t size)
{
    const wt_area_hdr_t *h = (const wt_area_hdr_t *)(const void *)img;
    if (!img || ((uintptr_t)img & 3u) || size < WT_AREA_HDR_SIZE || h->magic != WT_AREA_MAGIC)
        return -1;
    if (h->version != WT_AREA_VERSION || h->hcrc != wavetable_crc32(0, h, sizeof *h - 4u) || !h->count ||
        h->count > WT_AREA_MAX_FRAMES || h->len != WT_AREA_HDR_SIZE + h->count * WT_FRAME_SAMPLES * 2u ||
        h->len > size)
        return -2;
    for (uint32_t i = 0; i < h->count; i++)
        if (h->idx[i] >= NUM_BANKS * NUM_MORPH_STEPS)
            return -2;
    if (h->tag != wavetable_area_tag())
        return -3;
    if (wavetable_crc32(0, img + WT_AREA_HDR_SIZE, h->len - WT_AREA_HDR_SIZE) != h->crc)
        return -4;
    return h->count;
}

/* a frame no area holds: bank 0's the sine, the image's its linked frame (or the sine, spilled or no image) */
static const int16_t *wt_frame_home(uint32_t f)
{
    uint32_t i = f - WT_BANK_FIRST_IMAGE * NUM_MORPH_STEPS;
    if (f < WT_BANK_FIRST_IMAGE * NUM_MORPH_STEPS || i >= WT_LINKED_FRAMES || !wt_ok)
        return wavetable_sine;
    return (const int16_t *)(const void *)(wavetable_image_host + WT_STORE_DATA_OFF) + i * WT_FRAME_SAMPLES;
}

SYNTH_COLD void wavetable_area_detach(uint32_t slot)
{
    if (slot >= WT_AREAS)
        return;
    for (uint32_t i = 0; i < wt_area_n[slot]; i++)
        wavetable_frames[wt_area_idx[slot][i]] = wt_frame_home(wt_area_idx[slot][i]);
    wt_area_n[slot] = 0;
    wt_area_len[slot] = 0;
}

SYNTH_COLD int wavetable_area_attach(uint32_t slot, const uint8_t *img, uint32_t size)
{
    const wt_area_hdr_t *h = (const wt_area_hdr_t *)(const void *)img;
    int n = slot < WT_AREAS ? wavetable_area_check(img, size) : -1;
    wavetable_area_detach(slot);
    if (n < 0)
        return n;
    const int16_t *data = (const int16_t *)(const void *)(img + WT_AREA_HDR_SIZE);
    for (int i = 0; i < n; i++) {                          /* one aligned pointer each: the audio sees old or new */
        wt_area_idx[slot][i] = h->idx[i];
        wavetable_frames[h->idx[i]] = data + i * WT_FRAME_SAMPLES;
    }
    wt_area_n[slot] = (uint8_t)n;
    wt_area_len[slot] = h->len;
    return n;
}

uint32_t wavetable_area_used(uint32_t slot) { return slot < WT_AREAS ? wt_area_len[slot] : 0u; }
