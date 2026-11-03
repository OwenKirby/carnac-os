#ifndef WAVETABLES_H
#define WAVETABLES_H

#include <stdint.h>

/* The banks (gen_wavetables.py; wavetables.c checks the counts): bank 0, Foundations, is an image of its own in the
 * store flash (WT_BANK_*, uploaded); banks 1..16 are the image linked into the firmware (since 2026-10-26: they were
 * 0..15). */
#define NUM_BANKS 15
#define WT_BANK_FIRST_IMAGE 1           /* the linked image's first bank */
#define NUM_IMAGE_BANKS 14              /* = len(IMAGE_BANKS) in gen_wavetables.py */
#define NUM_MORPH_STEPS 8
#define WAVE_SAMPLES 512                /* level 0: one full cycle of a frame */

/* Band-limited mip levels (gen_wavetables.py). Each level is one cycle of the frame, keeping only the harmonics
 * that stay below Nyquist over its pitch range (wt_level_for_freq). Levels 0..5 are 512 >> L samples with harmonics
 * up to their own Nyquist; level 6 is 16 samples with harmonics up to 4, so its interpolation stays clean. */
#define WT_LEVELS 7
#define WT_FRAME_SAMPLES 1024           /* 512 + 256 + 128 + 64 + 32 + 16 + 16: every level of one frame */

/* samples per cycle at each level, and the shift that turns a 32-bit phase into that level's sample index */
static const uint16_t WT_LEVEL_SAMPLES[WT_LEVELS] = {512, 256, 128, 64, 32, 16, 16};
static const uint8_t WT_LEVEL_SHIFT[WT_LEVELS] = {23, 24, 25, 26, 27, 28, 28};

/* ---- The wavetable store (spec 6.5). The bank data (NUM_BANKS x 16 KiB) is not in the app: on the FM1 it is an
 * image in Felucca's store flash, uploaded over USB-MIDI and read in place through XIP (src/hardware/wt_store_fm1.c);
 * host and browser builds link the same image as wavetables_image.c. Image layout, little-endian:
 *   +0                 wt_store_hdr_t (32 B; until 2026-10-26 the data started a 4 KiB sector later)
 *   +WT_STORE_DATA_OFF NUM_BANKS x NUM_MORPH_STEPS x WT_FRAME_SAMPLES int16 Q15, the levels one after another */
#define WT_STORE_BASE 0xA0000u          /* flash offset: Felucca's user sample slots USR1..USR3 */
#define WT_STORE_SIZE 0x3C000u          /* .. 0xDBFFF (240 KiB) */
#define WT_STORE_DATA_OFF 0x20u
#define WT_STORE_MAGIC 0x42545746u      /* "FWTB" */
#define WT_STORE_VERSION 1u
#define WT_STORE_DATA_LEN ((uint32_t)NUM_IMAGE_BANKS * NUM_MORPH_STEPS * WT_FRAME_SAMPLES * 2u)
#define WT_STORE_IMAGE_LEN (WT_STORE_DATA_OFF + WT_STORE_DATA_LEN)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t banks, frames;              /* NUM_IMAGE_BANKS, NUM_MORPH_STEPS */
    uint16_t frame_samples;             /* WT_FRAME_SAMPLES */
    uint8_t levels, rsv;                /* WT_LEVELS, 0 */
    uint32_t data_off, data_len;        /* WT_STORE_DATA_OFF, WT_STORE_DATA_LEN */
    uint32_t crc;                       /* CRC-32 (zlib) of the data */
    uint32_t set_id;                    /* wavetable_set_id: which bank list the image holds */
    uint32_t hcrc;                      /* CRC-32 of the 28 bytes above */
} wt_store_hdr_t;
_Static_assert(sizeof(wt_store_hdr_t) == 32, "wavetable store header layout");
/* Since 2026-10-16 the image is linked into the app, not uploaded to WT_STORE_BASE (the presets live there now), so
 * it is bounded by the app slot (build_fm1 checks it), not by WT_STORE_SIZE. 0x48000 = 18 banks: a guard, not a plan. */
#define WT_IMAGE_MAX 0x48000u
_Static_assert(WT_STORE_IMAGE_LEN <= WT_IMAGE_MAX, "the wavetable banks outgrow WT_IMAGE_MAX");

/* The image's last WT_SPILL_FRAMES frames (bank 14, Dust, frames 3..8) are not linked into the FM1's app (2026-11-03:
 * the app flash was full; build_fm1 defines WT_IMAGE_SPILLED): they sit in the store flash's wavetable areas with
 * bank 0, below. Host and browser builds link the whole image. */
#define WT_SPILL_FRAMES 6u
#ifdef WT_IMAGE_SPILLED
#define WT_LINKED_FRAMES ((uint32_t)NUM_IMAGE_BANKS * NUM_MORPH_STEPS - WT_SPILL_FRAMES)
#else
#define WT_LINKED_FRAMES ((uint32_t)NUM_IMAGE_BANKS * NUM_MORPH_STEPS)
#endif
#define WT_IMAGE_LINKED_LEN (WT_STORE_DATA_OFF + WT_LINKED_FRAMES * WT_FRAME_SAMPLES * 2u)

/* ---- The wavetable areas (2026-11-03; before, area 2 held bank 0's image alone, "CRWB"): frames in the store flash,
 * uploaded over USB-MIDI like the PCM waves (src/hardware/pcm_store_fm1.c areas 2 and 3, pcm_store_upload.py,
 * src/dsp/gen_wt_areas.py) and read in place. Area 2 holds bank 0 (Foundations) and three spilled frames, area 3 the
 * other three. A frame no attached area holds plays wavetable_sine (or, when linked, the linked image's). Layout,
 * little-endian:
 *   +0                  wt_area_hdr_t (32 B, written last: magic and len where a PcmHdr has them)
 *   +WT_AREA_HDR_SIZE   count x WT_FRAME_SAMPLES int16 Q15, the levels one after another; frame i is idx[i] */
#define WT_AREA_MAGIC 0x46575243u       /* "CRWF" */
#define WT_AREA_VERSION 1u
#define WT_AREA_HDR_SIZE 32u
#define WT_AREA_MAX_FRAMES 12u
#define WT_AREAS 2u                     /* the store's wavetable areas (pcm_store_fm1.c areas 2 and 3) */
typedef struct {
    uint32_t magic;                     /* WT_AREA_MAGIC */
    uint8_t version, tag, count, rsv;   /* WT_AREA_VERSION; wavetable_area_tag(); the frames; 0 */
    uint32_t len;                       /* header included */
    uint32_t crc;                       /* CRC-32 (zlib) of the data */
    uint8_t idx[WT_AREA_MAX_FRAMES];    /* each frame's place in wavetable_frames (bank x 8 + frame); unused 0xFF */
    uint32_t hcrc;                      /* CRC-32 of the 28 bytes above */
} wt_area_hdr_t;
_Static_assert(sizeof(wt_area_hdr_t) == WT_AREA_HDR_SIZE, "wavetable area header layout");
/* the images' tag: which bank set they were cut from (the image's set id and bank 0's, folded to a byte) */
#define wavetable_area_tag() ((uint8_t)((wavetable_set_id ^ wavetable_store_bank_id) & 0xFFu))

/* Generated by gen_wavetables.py (wavetables.c): the tables the app keeps. Do not edit. */
extern const uint16_t wavetable_level_start[WT_LEVELS];   /* where each level begins inside a frame */
extern const char *const wavetable_bank_names[NUM_BANKS];
extern const uint32_t wavetable_set_id;                   /* CRC-32 of the image's bank names, one per line */
extern const uint32_t wavetable_store_bank_id;            /* CRC-32 of bank 0's name */
/* A sine and the sub-oscillator's SQUARE, band-limited, each laid out as one frame (wavetable_level_start applies).
 * The sine is also what every bank plays while no valid image is attached. */
extern const int16_t wavetable_sine[WT_FRAME_SAMPLES];
extern const int16_t wavetable_sub_square[WT_FRAME_SAMPLES];

/* wavetable_attach.c. Every frame's address, bank-major: into the attached image, or all wavetable_sine. Each entry
 * is one aligned pointer, so the audio interrupt never sees a torn one; detach before the store is erased. */
extern const int16_t *wavetable_frames[NUM_BANKS * NUM_MORPH_STEPS];

/* Check an image (header, sizes, set id, data CRC) and point banks 1..16 at it: 0, or < 0 and those banks on the
 * sine. The image must stay readable (flash through XIP, or a const array) while attached. */
int wavetable_attach(const uint8_t *image);
void wavetable_detach(void);            /* banks 1..16 play the sine */
int wavetable_attached(void);           /* a valid image is in use: 1 */
/* A wavetable area's image at img in flash of size bytes: its frames (> 0), or < 0: -1 no image (erased or other
 * data), -2 header, -3 another bank set, -4 data CRC. attach (slot 0 = area 2, 1 = area 3) also points the frames
 * it holds at it; detach puts them back (the sine, or the linked image's) before the area is erased or written. */
int wavetable_area_check(const uint8_t *img, uint32_t size);
int wavetable_area_attach(uint32_t slot, const uint8_t *img, uint32_t size);
void wavetable_area_detach(uint32_t slot);
uint32_t wavetable_area_used(uint32_t slot);   /* the attached image's bytes (0: none) */
uint32_t wavetable_crc32(uint32_t crc, const void *p, uint32_t n);   /* zlib CRC-32, crc 0 to start */

/* One frame at one level: WT_LEVEL_SAMPLES[level] samples of one cycle. */
static inline const int16_t *wavetable_frame(int bank, int morph, int level)
{
    return wavetable_frames[bank * NUM_MORPH_STEPS + morph] + wavetable_level_start[level];
}

#endif
