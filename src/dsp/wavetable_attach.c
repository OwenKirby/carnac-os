/* The wavetable banks, from the wavetable image (wavetables.h, spec 6.5).
 *
 * Every build starts attached to the image linked in as wavetables_image.c: on the FM1 since 2026-10-16 it sits in the
 * firmware and is read in place through XIP (it was uploaded over USB-MIDI to the store flash at WT_STORE_BASE, which
 * now holds the saved presets). wavetable_attach checks an image's header and CRC; without a valid one every bank
 * plays wavetable_sine, like a sampler with its sample gone: the synth still sounds, in tune. */
#include <stddef.h>
#include "wavetables.h"

extern const uint8_t wavetable_image_host[WT_STORE_IMAGE_LEN];   /* wavetables_image.c */
#define WT_START(b, f) ((const int16_t *)(const void *)(wavetable_image_host + WT_STORE_DATA_OFF) + \
                        ((b) * NUM_MORPH_STEPS + (f)) * WT_FRAME_SAMPLES)
#define WT_START_BANK(b) WT_START(b, 0), WT_START(b, 1), WT_START(b, 2), WT_START(b, 3), \
                         WT_START(b, 4), WT_START(b, 5), WT_START(b, 6), WT_START(b, 7)
_Static_assert(NUM_MORPH_STEPS == 8 && NUM_BANKS == 16, "WT_START_BANK / the initialiser below list every frame");

const int16_t *wavetable_frames[NUM_BANKS * NUM_MORPH_STEPS] = {
    WT_START_BANK(0), WT_START_BANK(1), WT_START_BANK(2), WT_START_BANK(3), WT_START_BANK(4),
    WT_START_BANK(5), WT_START_BANK(6), WT_START_BANK(7), WT_START_BANK(8), WT_START_BANK(9),
    WT_START_BANK(10), WT_START_BANK(11), WT_START_BANK(12), WT_START_BANK(13), WT_START_BANK(14),
    WT_START_BANK(15),
};
static int wt_ok = 1;

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
    for (int i = 0; i < NUM_BANKS * NUM_MORPH_STEPS; i++)
        wavetable_frames[i] = wavetable_sine;
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
    if (h->banks != NUM_BANKS || h->frames != NUM_MORPH_STEPS || h->frame_samples != WT_FRAME_SAMPLES ||
        h->levels != WT_LEVELS || h->data_off != WT_STORE_DATA_OFF || h->data_len != WT_STORE_DATA_LEN ||
        h->set_id != wavetable_set_id)
        return -3;                                         /* an image of another bank list: upload the matching one */
    if (wavetable_crc32(0, image + WT_STORE_DATA_OFF, WT_STORE_DATA_LEN) != h->crc)
        return -4;                                         /* damaged or half-written data */
    const int16_t *data = (const int16_t *)(const void *)(image + WT_STORE_DATA_OFF);
    for (int i = 0; i < NUM_BANKS * NUM_MORPH_STEPS; i++)
        wavetable_frames[i] = data + i * WT_FRAME_SAMPLES;
    wt_ok = 1;
    return 0;
}
