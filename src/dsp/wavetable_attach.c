/* The wavetable banks, from the wavetable image (wavetables.h, spec 6.5).
 *
 * Every build starts attached to the image linked in as wavetables_image.c: on the FM1 since 2026-10-16 it sits in the
 * firmware and is read in place through XIP (it was uploaded over USB-MIDI to the store flash at WT_STORE_BASE, which
 * now holds the saved presets). wavetable_attach checks an image's header and CRC; without a valid one every bank
 * plays wavetable_sine, like a sampler with its sample gone: the synth still sounds, in tune.
 * Since 2026-10-26 the image holds banks 1..14 (16 until Menhir and Hollow Reeds retired, 2026-10-27); bank 0 (Foundations) is an
 * image of its own that the FM1 reads from the
 * store flash it was uploaded to (wavetable_bank_attach), and that plays the sine until one is attached. */
#include <stddef.h>
#include "wavetables.h"
#include "dsp_cold.h"

extern const uint8_t wavetable_image_host[WT_STORE_IMAGE_LEN];   /* wavetables_image.c */
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
    WT_START_BANK(10), WT_START_BANK(11), WT_START_BANK(12), WT_START_BANK(13),
};
static int wt_ok = 1;
static uint32_t wt_bank_len;                               /* bank 0's attached image (0: none) */

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
    for (int i = 0; i < NUM_IMAGE_BANKS * NUM_MORPH_STEPS; i++)
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
    if (wavetable_crc32(0, image + WT_STORE_DATA_OFF, WT_STORE_DATA_LEN) != h->crc)
        return -4;                                         /* damaged or half-written data */
    const int16_t *data = (const int16_t *)(const void *)(image + WT_STORE_DATA_OFF);
    for (int i = 0; i < NUM_IMAGE_BANKS * NUM_MORPH_STEPS; i++)
        WT_IMAGE_FRAMES[i] = data + i * WT_FRAME_SAMPLES;
    wt_ok = 1;
    return 0;
}

SYNTH_COLD int wavetable_bank_check(const uint8_t *img, uint32_t size)
{
    const wt_bank_hdr_t *h = (const wt_bank_hdr_t *)(const void *)img;
    if (!img || ((uintptr_t)img & 3u) || size < WT_BANK_IMAGE_LEN || h->magic != WT_BANK_MAGIC)
        return -1;
    if (h->version != WT_BANK_VERSION || h->hcrc != wavetable_crc32(0, h, sizeof *h - 4u) || h->banks != 1u ||
        h->len != WT_BANK_IMAGE_LEN || h->frames != NUM_MORPH_STEPS || h->frame_samples != WT_FRAME_SAMPLES)
        return -2;
    if (h->bank_id != wavetable_store_bank_id)
        return -3;
    if (wavetable_crc32(0, img + WT_BANK_HDR_SIZE, WT_BANK_DATA_LEN) != h->crc)
        return -4;
    return 1;
}

void wavetable_bank_detach(void)
{
    wt_bank_len = 0;
    for (int f = 0; f < NUM_MORPH_STEPS; f++)
        wavetable_frames[f] = wavetable_sine;
}

int wavetable_bank_attach(const uint8_t *img, uint32_t size)
{
    int n = wavetable_bank_check(img, size);
    wavetable_bank_detach();
    if (n < 0)
        return n;
    const int16_t *data = (const int16_t *)(const void *)(img + WT_BANK_HDR_SIZE);
    for (int f = 0; f < NUM_MORPH_STEPS; f++)            /* one aligned pointer each: the audio sees old or new */
        wavetable_frames[f] = data + f * WT_FRAME_SAMPLES;
    wt_bank_len = WT_BANK_IMAGE_LEN;
    return n;
}

uint32_t wavetable_bank_used(void) { return wt_bank_len; }
