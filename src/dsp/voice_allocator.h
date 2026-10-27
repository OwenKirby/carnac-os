
#ifndef VOICE_ALLOCATOR_H
#define VOICE_ALLOCATOR_H

#include <stdint.h>
#include "PresetStructures.h"
#include "lfo.h"             // LfoRuntimeState for the per-voice LFO2 / LFO3 playheads
#include "envelopes.h"
#include "oscillators.h"
#include "oscmixer.h"
#include "filter.h"
#include "amplifier.h"
#include "analog.h"

#define POLYPHONY_VOICES 8   /* the physical pool; how many a layer may use is layer_voice_limit (see pool_range) */
PS_STATIC_ASSERT(POLYPHONY_VOICES == VOICE_POOL, "the allocator's pool is PresetStructures.h's VOICE_POOL");

/* Who struck a voice's note (2026-10-26), lowest rank first: a note-off releases only its own source's notes (the ARP
 * and the SEQ on one pitch no longer cut each other), and a full layer steals from the lowest rank it may (the ARP
 * never takes what the SEQ holds, neither what the keys hold). The keys are the panel and MIDI in. */
enum { VSRC_ARP, VSRC_SEQ, VSRC_KEYS };

// FIXED: Explicit structural array mapping to store your per-voice layer properties
typedef struct {
    LfoRuntimeState engines[2]; // Index 0 = LFO2 playhead, Index 1 = LFO3 playhead
} PolyLfoVoiceArray;

/* State of one mod slot (spec 4.5) that has memory from one sample to the next: sample & hold, lag and the
 * latch of smpleBonA. tick is the control pass it last ran in, so a slot read twice in one pass runs once. */
typedef struct {
    int32_t out_q15;      /* the slot's output for frame tick */
    int32_t held_q15;     /* lag: the slewed value; smpleBonA: the latched value of B */
    uint32_t tick;
    uint8_t prev_side;    /* smpleBonA: which side of the midpoint A was on last tick */
    uint8_t init;         /* held_q15 / prev_side have a value */
    uint8_t busy;         /* set while the slot is evaluating, so a loop through its sources reads 0 */
} ModFnState;

/* aligned (so padded) to a 64-byte cache line (2026-10-16): the two cores render alternate voices (synth_runtime.c
 * CORE1_VOICES), so no line holds two cores' voices (a precaution, as X0X's mailbox: fx.c FX_LINE) */
typedef struct __attribute__((aligned(64))) {
    WavetableOsc osc1;
    WavetableOsc osc2;
    ADSR amp_env;        // ENV1, hardwired to the amp
    ADSR mod_env;        // ENV2
    ADSR aux_env;        // ENV3
    DualFilterMatrix synth_filters;
    StereoAmp master_amp;
    int active_note;
    int is_gate_open;
    uint32_t age_counter;
    uint8_t velocity;
    uint8_t layer;                // NLAYER index whose patch this voice plays
    uint16_t release_boost_q15;   // AUTO DECAY input: how closely this note followed the previous one on its layer
    uint32_t sub_phase;   // Sub-oscillator 32-bit cycle phase, per voice so each voice's sub runs at its own pitch

    // FIXED: Directly binds to the explicit structure layout container type
    PolyLfoVoiceArray poly_lfos;

    ModFnState mod_fn[NMODSLOT];  // memory of the MODSLOT functions that keep state (spec 4.5)
    uint32_t mod_pass;            // this voice's control pass count, the key of mod_fn's cache (per voice: two cores)
    int32_t glide_q16;            // portamento: pitch offset from the target note, Q16 cents, decays to 0
    int32_t glide_step_q16;       // portamento: how far glide_q16 moves toward 0 each sample (applied a tick at a time)
    int detune_cents;             // UNISON_STACK: this voice's offset from the note
    int32_t unison_gain_q15;      // UNISON_STACK: 1 / sqrt(stack size), so a stack is no louder than one voice
    int unison_pan;               // UNISON_STACK: this voice's pan offset from the layer's pan, 0..127 units (0 = centre)
    int32_t route_q15;            // layer routing weight from the keyboard and velocity split (32767 = full)

    /* set by the control tick (synth_voice.c voice_control_tick), read every sample by voice_render_sample */
    OscMixVoice mix;              // the oscillator mixer's settings
    int32_t lfo_q15[2];           // LFO2 / LFO3 outputs, depth and fade applied
    int32_t env_vel_q15[3];       // ENV1..3's level for this note's velocity (VEL > LEVEL), 32768 = unity, each tick
    int32_t lfo_gain_q15[3];      // LFO1..3's depth for this voice (DEPTH MOD SRC / AMT), 32768 = unity, each tick
    /* The amp, in float (2026-10-15: the FM1's FPU beats its integer multiplies, synth_voice.c finish_mono). vol is the
     * amp level (envelope, amp mod, velocity, 0..1), ramped by vol_step each sample; vol_b is STACK's second amp (ENV2).
     * mix_a / mix_b take a path's amped (and driven) sample straight into the mix bus, MIX_* order: everything that
     * holds still over a tick folded into one gain each (pan law, headroom gain, the layer's routing weight, the mix
     * gain, the FX sends). Path B is STACK's B / BINAURAL's right ear. */
    float vol, vol_step, vol_b, vol_b_step;
    float mix_a[4], mix_b[4];
    uint8_t noise_at;             // the block's first frame in the render pass: where it reads the shared noise
    /* STRIKE's attack (dsp/attacks.h): restarted by each new note (synth_runtime.c voice_play), played once at atk_step
     * (Q16 bytes of the sound per sample, set each tick from OSC1's pitch), mixed after the amp envelope at atk_vol
     * (ramped by atk_vol_step: velocity, level, amp mod, choke) through mix_a. atk_p NULL: nothing playing. */
    const uint8_t *atk_p;
    uint32_t atk_pos, atk_step, atk_end;  // Q16; atk_end: (length - 1) << 16, where the read stops
    uint8_t atk_sample;           // the sound the note started (its pitch info for the tick)
    float atk_vol, atk_vol_step;  // the part that skips the filter (and ENV1)
    float atk_pre, atk_pre_step;  // the part added into FILTER1's input (ATK > FILTER), before ENV1
    float atk_fade;               // ATK DECAY's fade, 1 at the note's start
    uint32_t atk_f1;              // OSC1's frequency atk_step was worked out for, and TUNE | TRACK << 8 (atk_tt):
    uint16_t atk_tt;              //   the step is worked out again only when one of them moves
    uint32_t atk_base;            // that step before BEND
    float atk_glide;              // BEND (+ BEND MOD): how much of it is left, 1 at the note's start, gliding to 0
    int32_t choke_q15;            // choke fade: 32767 = untouched, ramps to 0 over CHOKE_TICKS ticks, then silence
    uint8_t choke;                // 1 = fading out: over its layer's voice limit, or the layer's architecture changed
    uint8_t arch;                 // VARCH_* the note was started with: picks the control tick and render function
    uint8_t drive;                // AMP DRIVE (0: bypassed); drv its tick's constants, drv_s each path's state (A, B)
    AmpDrive drv;
    float drv_s[2][2];
    uint8_t ctrl_left;            // samples until this voice's next control tick; 0 = tick before the next sample
    uint8_t snap;                 // 1 = a new note: the next tick sets the filter coefficients without a ramp
    uint8_t silent;               // 1 = the last tick found the voice finished: it renders silence
    uint8_t src;                  // VSRC_*: who struck the note
    AnalogVoice analog;          // ANALOG: this slot's character, drift and note scatter (dsp/analog.h)
} SynthVoiceNode;

/* a voice nobody hears and nobody holds: free to take (STACK's ENV2 path keeps a voice sounding after ENV1 ends) */
static inline int voice_is_free(const SynthVoiceNode *v) { return !v->is_gate_open && v->silent; }

extern SynthVoiceNode poly_bank[POLYPHONY_VOICES];

// --- ALLOCATOR WRAPPERS ---
// The voices a layer may use (spec 5.3, PresetStructures.h layer_voice_limit): layer 1 from voice 0, layer 2 from
// the second half of the pool (earlier when layer 2 has more than half), count = the limit its architecture, the
// performance mode and the two voice modes allow (0 = none).
void pool_range(uint8_t layer, int *first, int *count);
/* Overload shedding (the FM1 glue sets it; 0 everywhere else, so the host and the browser are unchanged): each layer
 * may use this many voices fewer than its limit, never fewer than one. A voice the lower limit leaves out is choked
 * (a few ms fade) at the next layer tick, as for any limit change. */
extern volatile uint8_t voice_shed;
/* a voice for a note of source src (VSRC_*), -1 when there is none: the layer has no voices, or every one is held by
 * a higher rank */
int allocate_voice_slot(uint8_t layer, uint8_t src);
int check_if_any_other_voice_held(int current_voice_index, uint8_t layer);
void voice_allocator_init(void);

/* Clears one mod slot's memory on this voice (a note-on that retriggers the voice starts the slot fresh). */
void voice_mod_fn_reset(SynthVoiceNode *v, int slot);

#endif

