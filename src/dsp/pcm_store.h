/* PCM waves in the store flash (2026-10-21): uploaded over USB-MIDI into flash the firmware leaves free, read in place.
 *
 * The app slot is nearly full (wavetables + attacks), so the PCM waves live in the FM1's data area instead, in "areas"
 * the preset store does not use (src/hardware/pcm_store_fm1.c lists them). Each area holds one image of its own, so
 * uploading one never touches another. Since 2026-10-27 a third area, PCM_AREA_FW, is linked into the firmware itself
 * (src/dsp/pcm_area_fw.c, the app flash Hollow Reeds left): never uploaded, it attaches itself with the first area
 * attached. An image:
 *
 *   +0      PcmHdr      32 B, written last: an upload cut off leaves the area empty, never half a wave
 *   +32     PcmEntry    x count, 36 B each
 *   +off    the samples the sample entries name, 8-bit mu-law (the attacks' codec: mu 255, int8 -127..127)
 *
 * Two kinds of entry (version 2):
 *   a SAMPLE: PCM_LOOP (the whole sample repeats; one guard byte after it, a copy of its first, so a read across the
 *     wrap needs no test) or PCM_ONESHOT (an attack; played once). It plays at `rate` Hz with `root` as its note.
 *   a WAVE: what the oscillator's wave list shows (src/dsp/pcm_osc.h). Loop A, an optional loop B that SCAN
 *     crossfades to (read at b_rate, from b_off into its loop: the same sample as A makes a denser copy of it), and an
 *     optional attack on top at each note: a stored ONESHOT (atk_ref) or one of the firmware's attacks (atk_fw).
 * A sample reference is (area << 6) | index in that area, PCM_REF_NONE for none.
 *
 * The header's CRC-32 covers everything after it, up to len. pcm_store_check range-checks every entry, and every
 * reference into its own area, before the area is attached; a reference into the other area is checked when used
 * (pcm_sample), so a missing area only silences the waves that need it.
 * The tools: src/dsp/gen_pcm_waves.py builds the images, src/hardware/pcm_store_upload.py uploads them. */
#ifndef PCM_STORE_H
#define PCM_STORE_H
#include <stdint.h>

#define PCM_MAGIC 0x57505243u               /* "CRPW" */
#define PCM_VERSION 2u
#define PCM_AREAS 3u
#define PCM_FLASH_AREAS 2u                  /* areas 0, 1: in the store flash, uploaded */
#define PCM_AREA_FW 2u                      /* in the firmware (pcm_area_fw.c) */
#define PCM_NAME_LEN 12u                    /* NUL-padded; a full 12 has no NUL */
#define PCM_MAX_ENTRIES 63u                 /* in one area (a reference's index is 6 bits, 63 is NONE's) */
#define PCM_HDR_SIZE 32u
#define PCM_ENTRY_SIZE 36u
#define PCM_REF_NONE 0xFFu
#define PCM_REF(area, i) ((uint8_t)(((area) << 6) | (i)))

enum { PCM_LOOP = 1, PCM_ONESHOT = 2, PCM_WAVE = 3 };   /* PcmEntry.kind */

typedef struct {
    uint32_t magic;                         /* PCM_MAGIC */
    uint16_t version, count;                /* PCM_VERSION; the entries */
    uint32_t len;                           /* the whole image, header included */
    uint32_t crc;                           /* CRC-32 of bytes 32..len */
    uint32_t rsv[3];                        /* 0 */
    uint32_t hcrc;                          /* CRC-32 of the 28 bytes before it */
} PcmHdr;

typedef struct {
    char name[PCM_NAME_LEN];
    uint8_t kind;                           /* PCM_LOOP / PCM_ONESHOT / PCM_WAVE */
    uint8_t root;                           /* a sample: the MIDI note it sounds at `rate` */
    uint8_t rsv0, rsv1;                     /* 0 */
    union {
        struct {                            /* PCM_LOOP, PCM_ONESHOT */
            uint32_t rate;                  /* Hz, 4000..48000 */
            uint32_t off;                   /* its first byte, from the image start */
            uint32_t len;                   /* samples (a LOOP's guard byte follows them) */
            uint32_t rsv[2];                /* 0 */
        } s;
        struct {                            /* PCM_WAVE */
            uint8_t loop_a, loop_b;         /* LOOP references; loop_b PCM_REF_NONE: A alone */
            uint8_t atk_ref, atk_fw;        /* the attack: a ONESHOT reference, or a firmware attack (attacks.h), or
                                             * both PCM_REF_NONE; never both set */
            uint8_t atk_level, rsv;         /* the attack's level, 0..127 (127: as stored) */
            uint16_t b_rate_q12;            /* B's speed against A's own, 4096 = its rate (1024..16384) */
            uint16_t b_off_q16;             /* where B starts in its loop when A starts at 0, a fraction */
            uint16_t rsv2;                  /* 0 */
            uint32_t rsv3[2];               /* 0 */
        } w;
    } u;
} PcmEntry;

_Static_assert(sizeof(PcmHdr) == PCM_HDR_SIZE && sizeof(PcmEntry) == PCM_ENTRY_SIZE, "the stored layout");

/* the image at img, in an area of size bytes, as area a: its entry count (0..PCM_MAX_ENTRIES), or < 0: -1 no image
 * (erased or other data), -2 header, -3 entry, -4 data CRC */
int pcm_store_check(uint32_t a, const uint8_t *img, uint32_t size);
/* check the image and, if it holds, use its entries for area a; else the area is empty. Returns as pcm_store_check. */
int pcm_store_attach(uint32_t a, const uint8_t *img, uint32_t size);
void pcm_store_detach(uint32_t a);          /* before the area is erased or written */
uint32_t pcm_area_count(uint32_t a);        /* the entries area a holds now */
uint32_t pcm_area_used(uint32_t a);         /* its image's bytes (0: empty) */
/* every entry in order, area 0's, then 1's, then 2's; entry i, and its samples in *data (NULL for a WAVE) */
uint32_t pcm_entry_count(void);
const PcmEntry *pcm_entry(uint32_t i, const uint8_t **data);
/* the sample a reference names, if that area holds it and it is of `kind`; its samples in *data; else NULL */
const PcmEntry *pcm_sample(uint8_t ref, uint8_t kind, const uint8_t **data);
/* the WAVE entries in order (area 0's, then 1's, then 2's): the PCM part of the oscillator's wave list */
uint32_t pcm_wave_count(void);
const PcmEntry *pcm_wave(uint32_t i);

#endif
