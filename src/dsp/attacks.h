#pragma once
/* The firmware's attack samples: none since 2026-11-05 (ATK_COUNT 0; they were one-shot waves, codes 32.., and a PCM
 * wave's atk_fw). Kept: the shape, for the patch format's ATTACK byte (STRIKE, retired) and the mu-law decode table. */
#include <stdint.h>

#define ATK_COUNT 0u
#define ATK_UNPITCHED 255u

typedef struct {
    uint32_t offset;                 /* first byte in attack_data */
    uint16_t length;                 /* bytes (samples) */
    uint32_t rate_q16;               /* the sound's samples per engine sample at its own pitch, Q16 */
    uint8_t root;                    /* MIDI note it sounds at, or ATK_UNPITCHED */
} AttackInfo;

extern const float attack_mulaw[256];          /* mu-law byte -> sample (the PCM store's bytes too) */
