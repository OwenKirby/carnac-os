
#ifndef LFO_H
#define LFO_H

#include <stdint.h>
#include "PresetStructures.h"

// Runtime state of one LFO playhead. LFO1 has one per layer, LFO2 and LFO3 one per voice.
// last_phase: the phase the random shapes last saw, so they redraw once when the cycle wraps.
typedef struct { uint32_t phase_accumulator, delay_counter, fade_counter; int32_t envelope_amp_q15; uint32_t rand_seed; int16_t current_random, next_random; uint32_t last_phase; } LfoRuntimeState;

// spec 4.4: a delay index above this is REL mode (the LFO runs only after the key is released)
#define LFO_REL_DELAY 100u

// --- LFO CORE COMPILATION PROTOTYPES ---
int16_t compute_morphed_lfo_fixed(LfoRuntimeState *state, int raw_shape);
// The same shape read phase_offset (2^32 = one cycle) ahead of the playhead, without moving it or redrawing the random
// values: BINAURAL reads the right ear's LFOs this way. Depth and fade are the caller's (envelope_amp_q15).
int16_t lfo_peek_fixed(const LfoRuntimeState *state, int raw_shape, uint32_t phase_offset);
// speed_mod_q15: the resolved speed modulation source (bipolar Q15), scaled here by the speed amount
uint32_t calculate_lfo_step_increment_fixed(const LfoParams *cfg, int32_t speed_mod_q15);
void run_poly_lfo_envelope_fixed(const LfoParams *cfg, LfoRuntimeState *state, uint8_t shift);
// One control tick (2^shift samples) of the playhead: advances the phase and runs the delay and fade. A clock-synced LFO (spec 4.4 MIDI Clock
// Sync) takes its phase from midi_clock_phase() instead of stepping, so its cycles land on the clock grid.
// speed_mod_q15 is the resolved speed modulation source. In REL mode, while key_held, the playhead sits at
// its phase start and its output is 0. It starts on the release (spec 4.4). frame: the audio frame of this tick,
// the one the clock is asked about (midi_clock_phase_at).
void lfo_advance(const LfoParams *cfg, LfoRuntimeState *state, int32_t speed_mod_q15, int key_held, uint8_t shift,
                 uint32_t frame);
// KEY RESET: a note-on puts the playhead at its PHASE start (and restarts delay and fade) when the LFO asks for it
void lfo_key_reset(const LfoParams *cfg, LfoRuntimeState *state);

#endif
