
#ifndef ENVELOPES_H
#define ENVELOPES_H

#include <stdint.h>
#include "PresetStructures.h"

typedef enum { STATE_OFF, STATE_ATTACK, STATE_DECAY, STATE_SUSTAIN, STATE_RELEASE } EnvState;

typedef struct {
    EnvState state;
    int32_t level_q15;        // Fixed Q15 envelope gain level output
    int32_t level_q30;        // the same level in Q30, the working precision (level_q15 = level_q30 >> 15)
    int is_key_down;
    uint32_t retrigger_timer;
    uint32_t retrig_phase;    // clock-synced retrigger: the grid phase of the previous sample, to see a cycle start
} ADSR;

void adsr_init(ADSR *env);
void adsr_note_off(ADSR *env);
void adsr_note_on_legato(ADSR *env, const EnvParams *p, int is_any_other_voice_held);

// Advances the envelope 2^shift samples (one control tick; shift 0 = one sample).
// time_mod: velocity and key-position time modulation, in envelope steps (spec 4.4).
// release_boost_q15: 0 = no repeat, 32767 = the notes were struck back to back (AUTO DECAY acts on it).
// Returns the unipolar Q15 level at the end of the tick; polarity inversion is the caller's job.
// frame: the audio frame of this tick, for the clock-synced retrigger (midi_clock_phase_at).
int32_t adsr_advance(ADSR *env, const EnvParams *p, int time_mod, uint16_t release_boost_q15, uint8_t shift,
                     uint32_t frame);

#endif
