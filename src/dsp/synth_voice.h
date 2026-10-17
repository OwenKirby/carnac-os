
/* firmware/src/synth_voice.h */
#pragma once
#include <stdint.h>
#include "PresetStructures.h" // NLAYER and the patch fields the voice reads
#include "voice_allocator.h" // SynthVoiceNode and StereoFrame
#include "midi_io.h"         // MidiInterfaceState, the MIDI source
#include "seq_data.h"        // SequencerStep and SEQEV_*, the MAQ sequencer source

/* Control rate. Modulation, envelopes, LFOs, glide, pitch and the filter coefficients run once per control tick of
 * 2^SYNTH_CTRL_SHIFT samples; the oscillators, filters and amp run every sample from what the tick set, with the amp
 * level and the filter coefficients ramped across the tick. 16 samples at 44.1 kHz is 2.76 kHz.
 *
 * 2026-10-15: 16 (was 8). On the FM1 the control tick was 42 % of one core at 8 voices (the stage profile, docs/
 * fm1-measure-checklist.md); half as many ticks halves it. Against 8 on the render scenes: loudness within 0.05 dB and
 * spectral brightness within ~1 % (medians over 23 ms windows), the largest differences at note starts, where the first
 * tick may now land up to 16 samples (0.36 ms) later. */
#ifndef SYNTH_CTRL_SHIFT
#define SYNTH_CTRL_SHIFT 4u
#endif
#define SYNTH_CTRL_PERIOD (1u << SYNTH_CTRL_SHIFT)

/* Everything one voice needs for one control tick. Each source is normalised into these fields before the
 * voice runs, so the voice never reads MIDI, the front panel or the sequencer directly. Adapters below
 * own the fields for their source. When two sources write one field, the caller decides which wins:
 * call voice_input_from_midi first, then layer the other adapters on top. */
typedef struct {
    int note;        /* 0..127, the pitch this voice plays */
    int velocity;    /* 0..127 */
    int gate;        /* 1 while the key or step gate is held */
    int tie;         /* 1 = legato: continue the held note without retriggering the envelopes */
    int pitch_bend;  /* -64..+63, spring-centred wheel (spec 5.1) */
    int mod_wheel;   /* 0..127 */
    int aftertouch;  /* 0..127 */
    int custom_cc1;  /* 0..127 */
    int custom_cc2;  /* 0..127 */
    int transpose;   /* semitones from a MAQ TRANSPOSE lane, -64..+63 */
    int pan;         /* 0..127, 64 = centre; the pan mod source is added on top */
    int glide_cents; /* portamento: the voice's pitch offset from its target note, cents (set by the runtime) */
    int detune_cents;/* UNISON_STACK: this voice's detune from the note, cents (set by the runtime) */
    uint32_t frame;  /* the audio frame of this control tick, which the clock-synced LFOs and retriggers ask the
                      * clock about (set by the runtime; 0 from the adapters) */
} VoiceInput;

/* MIDI: a note for this voice, plus the controllers from the shared MIDI state. */
void voice_input_from_midi(VoiceInput *in, const MidiInterfaceState *midi, int note, int velocity, int gate);

/* MVAVE FM1 front panel, Home page (spec 5.1). Knobs are stored 0..127. Knob 2 is bipolar about 64.
 * Knob 4 (portamento time) belongs to the glide stage, not the voice. */
void voice_input_from_home_knobs(VoiceInput *in, int knob1_velocity, int knob2_pitch_bend, int knob3_mod_wheel);

/* Firmware MAQ sequencer: applies one step of one lane. event_type is that lane's SEQEV_*.
 * note_slot picks one of the step's 4 pitch slots for SEQEV_NOTE. The lane's target_mask decides which
 * voice gets the event, so the caller applies it only to the voices that lane targets.
 * Returns 1 when the step starts or continues a note (open the voice; in->tie says whether to retrigger),
 * 0 otherwise. Gate, timing, ratchet, probability, program change and layer are sequencer-engine jobs. */
int voice_input_from_seq_step(VoiceInput *in, uint8_t event_type, const SequencerStep *step, int note_slot);

/* Once per control tick, before the voices' ticks: advances each layer's LFO1 and refreshes its OSCMIXER. */
void synth_voice_begin_tick(uint32_t frame);

/* One control tick of one voice, from the patch of voice->layer: advances its envelopes and LFO2/3, resolves the
 * mod sources its architecture (voice->arch, fixed at note start) uses and works out what voice_render_sample plays
 * for the next 2^SYNTH_CTRL_SHIFT samples. A choked voice (voice->choke) fades out over its next ticks. */
void voice_control_tick(SynthVoiceNode *voice, const VoiceInput *in);

/* The mix bus a voice renders into: one frame is MIX_N floats in Q15 units (32768 = full scale), the dry left and
 * right and the two effect sends, the voice's layer routing weight and the mix gain already applied. */
enum { MIX_L, MIX_R, MIX_DELAY, MIX_REVERB, MIX_N };

/* Per-voice mix gain (32768 = unity): 1.5. A single bright voice at full velocity peaks near -5 dBFS; a dense chord
 * goes over and the master limiter (synth_runtime.c) takes it back to its ceiling. The effect sends are taken before
 * this gain, so at a send of 127 the wet path runs about 3.5 dB under the dry voice. */
#define MIX_HEADROOM_Q15 49152

/* Renders one voice for n samples (at most SYNTH_CTRL_PERIOD, all inside one control tick) from its last control
 * tick, through the block render of its architecture (one per VARCH_*, so no branching on unused paths), and adds them
 * to n frames of the mix bus from mix (MIX_N floats a frame): the dry signal and the FX sends, routed. A silent voice
 * adds nothing. */
void voice_render_block(SynthVoiceNode *voice, float *mix, int n);

/* STRIKE: copies the attack decode table into RAM (synth_init, before any voice renders). */
void voice_attack_init(void);

/* Portamento: steps the voice's glide offset toward its target by one control tick. */
void voice_glide_advance(SynthVoiceNode *voice);

/* Advances the LFO2 and LFO3 playheads of one voice by one control tick (spec 4.4: per voice) and stores their
 * outputs. In REL mode (delay above 100) the playhead waits while in->gate is held. voice_control_tick calls this
 * first, before any early return. synth_runtime skips silent voices, so their LFO2/3 playheads hold until the voice
 * sounds again. */
void update_poly_lfo_engines(SynthVoiceNode *voice, const VoiceInput *in);

/* Advances each layer's LFO1 playhead and fade envelope by one control tick. Called by synth_voice_begin_tick. */
void update_global_lfo1(uint32_t frame);

/* LFO1 playhead of each layer (LFO1 runs once per layer, not per voice). */
extern LfoRuntimeState global_lfo1_state[NLAYER];

/* The mod slot the screen shows (0..NMODSLOT-1), -1 for none: each control pass also runs it, last, so its output
 * (voice->mod_fn[slot].out_q15, or synth_mod_layer_out with no voice) is live even when nothing reads it. Set by the
 * UI; one extra slot a pass. */
extern int8_t synth_mod_watch;

/* A slot's output from the layer's voice-less pass (LFO1, no voice sources), Q15 */
int32_t synth_mod_layer_out(int layer, int slot);
