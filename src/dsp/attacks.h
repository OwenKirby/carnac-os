#pragma once
/* The STRIKE architecture's attack samples (gen_attacks.py writes attacks.c): short one-shot sounds a voice plays at
 * the start of each note over its wavetable, as an LA synth puts a PCM attack in front of a synthesised partial. Each is
 * 8-bit mu-law at its own rate; a pitched one has a root note and follows the oscillator's pitch, an unpitched one plays
 * at its own rate. New sounds go at the end: a patch stores the sound's number. */
#include <stdint.h>

#define ATK_COUNT 20u
#define ATK_UNPITCHED 255u

typedef struct {
    uint32_t offset;                 /* first byte in attack_data */
    uint16_t length;                 /* bytes (samples) */
    uint32_t rate_q16;               /* the sound's samples per engine sample at its own pitch, Q16 */
    uint8_t root;                    /* MIDI note it sounds at, or ATK_UNPITCHED */
} AttackInfo;

extern const char *const attack_names[ATK_COUNT];
extern const AttackInfo attack_info[ATK_COUNT];
extern const float attack_mulaw[256];
extern const uint8_t attack_data[];
