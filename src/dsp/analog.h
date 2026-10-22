#pragma once
/* ANALOG (LAYER page, one byte of the patch, 0..127): the small imperfections of an analogue polysynth, all at control
 * rate (nothing per sample, so no voice-count cost). 0 changes nothing (the voice skips all of it: bit-identical).
 *
 *   voice character  each of the 8 voice slots has its own fixed offsets, as each voice card of a real poly is trimmed
 *                    a little differently: OSC1 / OSC2 pitch, each filter's cutoff and resonance, the envelopes' speed,
 *                    the level. The same at every boot (a hash of the slot), so a voice keeps its character.
 *   drift            each voice's OSC1, OSC2 and cutoff wander slowly (~0.25 Hz, noise through two one-poles, pulled
 *                    back to centre), plus one slower warm-up drift that moves every voice's pitch together.
 *   note scatter     each new note gets its own small pitch and envelope-speed offset.
 *
 * The knob: 0..40 the voice character alone (it grows from 0), drift fading in from 40, everything at full at 127. */
#include <stdint.h>

enum { ANT_PITCH1, ANT_PITCH2, ANT_CUT1, ANT_CUT2, ANT_RES, ANT_RES2, ANT_TIME, ANT_LEVEL, ANT_COUNT };

/* what one control tick of a voice adds, at the patch's ANALOG amount */
typedef struct {
    float pitch1, pitch2;  /* OSC1 / OSC2 frequency ratio, ~1 (a few cents; rarely past 10 at 127) */
    int32_t cut1_q8;       /* FILTER1 / FILTER2 cutoff offsets, Q8 knob steps */
    int32_t cut2_q8;
    int res1, res2;        /* resonance offsets, knob steps */
    int32_t time_k_q16;    /* envelope speed, 65536 = as set (above: faster) */
    int32_t level_q15;     /* the voice's level, 32768 = as set */
} AnalogTick;

typedef struct {
    float tol[ANT_COUNT];  /* the slot's fixed offsets, -1..1: ANT_* */
    float drift[6];        /* three slow random walks (OSC1, OSC2, cutoff), two one-poles each */
    float note_pitch;      /* this note's scatter, -1..1, drawn at note start */
    float note_time;
    float base[4];         /* the fixed parts (character + note) of OSC1 / OSC2 cents and the two cutoffs, for amount */
    float damt;            /* the drift's share at amount */
    uint32_t rng;
    uint8_t amount;        /* the amount out and base hold; 0 = work them out on the next tick */
    uint8_t phase;         /* the walks step every 4th tick */
    AnalogTick out;
} AnalogVoice;

void analog_voice_init(AnalogVoice *a, unsigned slot);   /* once at boot: the slot's character, its own noise seed */
void analog_note_on(AnalogVoice *a);                     /* a new note (not a legato one): its scatter */
void analog_begin_tick(void);                            /* once per control tick: the shared warm-up drift */
/* one control tick of a voice at amount 1..127: the offsets (held in a; only the drift moves from tick to tick, the
 * rest is worked out again when the amount or the note changes) */
const AnalogTick *analog_voice_tick(AnalogVoice *a, uint8_t amount);
