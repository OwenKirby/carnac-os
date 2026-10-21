
#include "synth_prof.h"
#include <stddef.h>
#include "dsp_clamp.h"
#include <stdint.h>
#include "synth_voice.h"
#include "voice_allocator.h"
#include "envelopes.h"
#include "oscillators.h"
#include "oscmixer.h"
#include "filter.h"
#include "amplifier.h"
#include "lfo.h"
#include "midi_io.h"
#include "wavetables.h"
#include "preset.h"
#include "attacks.h"
#include "ctrl_tables.h"

LfoRuntimeState global_lfo1_state[NLAYER];
MidiInterfaceState master_midi;
int8_t synth_mod_watch = -1;

/* Shared per control tick, one set per layer: the OSCMIXER knobs and the LFO1 value every voice of that layer sees. */
static OscillatorMixer osc_mixer[NLAYER];
static int32_t lfo1_out_q15[NLAYER];

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* a stored 0..127 value with 64 = 0 (spec "(bip)"), as -64..+63 */
static int bip(uint8_t v) { return (int)v - (int)BIPOLAR_CENTER; }

/* Pitch change in cents, applied to a Hz frequency in Q16: f * 2^(c/1200), exact at whole octaves, the
 * semitones from a table and the cents between them interpolated (under 1 cent error), integer only.
 * Good for any range, down to near 0 Hz and up to the 20 kHz ceiling. */
#define PITCH_MAX_Q16 (20000u << 16)
static uint32_t apply_cents_q16(uint32_t f_q16, int32_t cents) {
    int32_t oct = cents >= 0 ? cents / 1200 : -((1199 - cents) / 1200);   /* floor */
    int32_t rem = cents - oct * 1200;                                       /* 0..1199 */
    uint32_t ratio_q16 = ctrl_cents_ratio_q16[rem];   /* the semitones of 2^(i/12) interpolated (gen_ctrl_tables.py) */
    uint64_t f = ((uint64_t)f_q16 * ratio_q16) >> 16;
    if (oct >= 0) {
        if (oct > 16) return PITCH_MAX_Q16;
        f <<= oct;
    } else {
        if (oct < -32) return 0;
        f >>= -oct;
    }
    return f > PITCH_MAX_Q16 ? PITCH_MAX_Q16 : (uint32_t)f;
}

/* OSC pitch mod in cents for a Q15 source. The depth is squared so the low end stays fine for vibrato:
 * 10 -> +-30 cents, 20 -> +-1.2 semitones, 64 -> +-1 octave, 127 -> +-4 octaves at a full source. */
static int32_t pitch_mod_cents(int32_t src_q15, uint8_t amt) {
    int32_t depth_cents = amt < 128u ? ctrl_pitch_mod_depth[amt] : ((int32_t)amt * amt * 4800) / (127 * 127);
    return (src_q15 * depth_cents) >> 15;
}

/* ------------------------------------------------------------------ mod sources --- */

/* What a source can read: the layer's patch and, for a voice, that voice and its input. The LFO1 pass has
 * no voice, so the voice-dependent sources read as 0 there. Every source resolves to Q15: unipolar sources
 * 0..32767, bipolar ones -32768..32767. */
typedef struct {
    const EngineState *eng;
    int layer;
    SynthVoiceNode *voice;       /* NULL for the LFO1 pass */
    const VoiceInput *in;        /* NULL for the LFO1 pass */
    uint32_t lfo_offset;         /* BINAURAL right ear: LFO1..3 read this far ahead (2^32 = a cycle); 0 = the voice itself */
} SourceCtx;

static int32_t resolve_source_any(const SourceCtx *c, int src, int depth);
/* Most mod sources of a patch are NONE: those read 0 without the call (the FM1 pays a call and a jump table each,
 * ~10 times a voice tick). */
static inline int32_t resolve_source(const SourceCtx *c, int src, int depth) {
    return src == MSRC_NONE ? 0 : resolve_source_any(c, src, depth);
}

/* Pass counters for the stateful mod functions: each control pass bumps its own (a voice's tick: voice->mod_pass; the
 * layer-wide LFO1 pass: layer_mod_pass), and a slot runs once per pass and then answers from its cache. Per voice, not
 * one global: on the FM1 two cores run voice ticks at once (docs/dual-core-plan.md), and a shared counter bumped by
 * the other core mid-pass would make a slot run twice, advancing sample & hold and lag twice. */
static uint32_t layer_mod_pass;
/* The LFO1 pass has no voice, so the slots it reads keep their memory per layer. */
static ModFnState layer_mod_fn[NLAYER][NMODSLOT];

/* Sources that swing both sides of zero. Sample & hold and lag treat these as signals around 0; the unipolar
 * ones are treated as signals around half scale. */
static int src_is_bipolar(int src) {
    return src == MSRC_KEYBOARD || src == MSRC_LFO1 || src == MSRC_LFO2 || src == MSRC_LFO3 ||
           src == MSRC_MODSLOT1 || src == MSRC_MODSLOT2 || src == MSRC_MODSLOT3;
}

/* MODSLOT n (spec 4.5): function of two sources, scaled by the slot amount */
static int32_t tune_cents_now;                      /* GLOBAL > MASTER TUNE, read once a tick */

static int32_t slot_output(const SourceCtx *c, int slot, int depth) {
    /* Feedback guard (spec 4.5): a chain of slots cannot run deeper than the slot count. */
    if (depth >= (int)NMODSLOT) return 0;

    ModFnState *st = c->voice ? &c->voice->mod_fn[slot] : &layer_mod_fn[c->layer][slot];
    uint32_t pass = c->voice ? c->voice->mod_pass : layer_mod_pass;
    if (st->tick == pass && c->lfo_offset == 0) return st->out_q15;
    /* A loop back to this slot through its own sources (direct self-modulation): reads 0, spec 4.5. */
    if (st->busy) return 0;

    const ModSlotParams *m = &c->eng->mod[slot];
    /* BINAURAL right ear: the stateless functions run again on the phase-shifted LFOs. Sample & hold and lag keep
     * memory, and a second run in one tick would advance it twice, so they answer with the left ear's value (the
     * left ear always reads a source before the right ear reads the same one). */
    if (c->lfo_offset != 0 && (m->func == MFN_SAMPLE_B_ON_A || m->func == MFN_LAG))
        return st->tick == pass ? st->out_q15 : 0;
    st->busy = 1;
    int32_t a = resolve_source(c, m->src_a, depth + 1);
    int32_t b = resolve_source(c, m->src_b, depth + 1);
    int32_t out;
    switch (m->func) {
    case MFN_MUL:      /* a missing source is the product's 1 (2026-10-17): one source passes through, none gives 0 */
        if (m->src_a == MSRC_NONE) out = m->src_b == MSRC_NONE ? 0 : b;
        else if (m->src_b == MSRC_NONE) out = a;
        else out = (a * b) >> 15;
        break;
    case MFN_ADD:      out = a + b; break;
    case MFN_SUB:      out = a - b; break;
    case MFN_MAX:      out = a > b ? a : b; break;
    case MFN_MIN:      out = a < b ? a : b; break;
    case MFN_ABS_DIFF: out = a > b ? a - b : b - a; break;
    case MFN_SAMPLE_B_ON_A: {
        /* B is latched each time A crosses its midpoint; the output holds the latched value between crossings. */
        int side = a >= (src_is_bipolar(m->src_a) ? 0 : 16384);
        if (!st->init) {
            st->held_q15 = b;
            st->prev_side = (uint8_t)side;
            st->init = 1;
        } else if (side != st->prev_side) {
            st->held_q15 = b;
            st->prev_side = (uint8_t)side;
        }
        out = st->held_q15;
        break;
    }
    case MFN_QUANTIZE_B_TO_A: {
        /* B (unipolar) picks the bit depth: 0 keeps all 16 bits, full scale keeps only the sign bit. */
        int32_t bu = b > 0 ? b : 0;
        int shift = (int)((bu * 15) >> 15);             /* 0..14 bits dropped */
        out = (a >> shift) * (1 << shift);
        break;
    }
    case MFN_LAG: {
        /* A slews toward its value. B (unipolar) sets the step per sample: 4096 at B = 0, 1 at full scale, taken a
         * control tick at a time. */
        int32_t bu = b > 0 ? b : 0;
        int32_t step = 4096 >> (bu >> 11);
        if (step < 1) step = 1;
        step <<= SYNTH_CTRL_SHIFT;
        if (!st->init) {
            st->held_q15 = a;
            st->init = 1;
        }
        int32_t d = a - st->held_q15;
        if (d > step) d = step;
        else if (d < -step) d = -step;
        st->held_q15 += d;
        out = st->held_q15;
        break;
    }
    case MFN_DIODE:    out = (a - b) > 0 ? a - b : 0; break;   /* half-wave rectifier, spec 4.5 */
    default:           out = a; break;                          /* MSRC_NONE function slots pass A */
    }
    out = (out * bip(m->amount)) >> 6;          /* amount -64..+63 */
    out = clamp32(out, -32768, 32767);
    st->busy = 0;
    if (c->lfo_offset == 0) {                   /* the right ear's reruns never replace the voice's own value */
        st->tick = pass;
        st->out_q15 = out;
    }
    return out;
}

/* An LFO read lfo_offset ahead of its playhead, with the playhead's depth and fade (BINAURAL right ear). */
static int32_t lfo_offset_q15(const LfoRuntimeState *state, uint8_t shape, uint32_t lfo_offset) {
    return ((int32_t)lfo_peek_fixed(state, shape, lfo_offset) * state->envelope_amp_q15) >> 15;
}

/* The one resolver for the mod_source_list (spec 4.x). */
static int32_t resolve_source_any(const SourceCtx *c, int src, int depth) {
    SynthVoiceNode *v = c->voice;
    const VoiceInput *in = c->in;

    switch (src) {
    case MSRC_VELOCITY:   return in ? in->velocity * 258 : 0;
    case MSRC_KEYBOARD:   return in ? (in->note - 60) * 512 : 0;      /* bipolar, C4 = 0 */
    case MSRC_MODWHEEL:   return in ? in->mod_wheel * 258 : 0;
    case MSRC_AFTERTOUCH: return in ? in->aftertouch * 258 : 0;
    case MSRC_ENV1:       return v ? v->amp_env.level_q15 : 0;        /* hardwired, never inverted */
    case MSRC_ENV2:       return v ? (c->eng->env[1].invert ? 32767 - v->mod_env.level_q15 : v->mod_env.level_q15) : 0;
    case MSRC_ENV3:       return v ? (c->eng->env[2].invert ? 32767 - v->aux_env.level_q15 : v->aux_env.level_q15) : 0;
    case MSRC_LFO1:
        if (c->lfo_offset) return lfo_offset_q15(&global_lfo1_state[c->layer], c->eng->lfo[0].shape, c->lfo_offset);
        return lfo1_out_q15[c->layer];
    case MSRC_LFO2:                                                    /* worked out once per tick */
    case MSRC_LFO3: {
        int i = src - MSRC_LFO2;
        if (!v) return 0;
        if (c->lfo_offset) return lfo_offset_q15(&v->poly_lfos.engines[i], c->eng->lfo[i + 1].shape, c->lfo_offset);
        return v->lfo_q15[i];
    }
    case MSRC_MODSLOT1:
    case MSRC_MODSLOT2:
    case MSRC_MODSLOT3:   return slot_output(c, src - MSRC_MODSLOT1, depth);
    case MSRC_CUSTOMCC1:  return in ? in->custom_cc1 * 258 : 0;
    case MSRC_CUSTOMCC2:  return in ? in->custom_cc2 * 258 : 0;
    default:              return 0;                                    /* MSRC_NONE */
    }
}

/* ------------------------------------------------------------------ LFOs --- */

void update_poly_lfo_engines(SynthVoiceNode *voice, const VoiceInput *in) {
    const EngineState *e = &engine_state[voice->layer];
    SourceCtx c = {e, voice->layer, voice, in, 0};
    for (int i = 0; i < 2; i++) {
        const LfoParams *cfg = &e->lfo[i + 1];                      /* LFO2, LFO3 */
        LfoRuntimeState *state = &voice->poly_lfos.engines[i];
        lfo_advance(cfg, state, resolve_source(&c, cfg->speed_mod_src, 0), in->gate, SYNTH_CTRL_SHIFT,
                    in->frame);                                     /* REL: waits for the key release */
        /* once per tick: the shape also redraws its random values here, so it must not be read twice */
        int32_t raw = compute_morphed_lfo_fixed(state, cfg->shape);
        voice->lfo_q15[i] = (raw * state->envelope_amp_q15) >> 15;
    }
}

/* Whether any voice of the layer holds its key. LFO1 is per layer, so its REL mode waits on this. */
static int layer_key_held(uint8_t layer) {
    for (int v = 0; v < POLYPHONY_VOICES; v++)
        if (poly_bank[v].layer == layer && poly_bank[v].is_gate_open) return 1;
    return 0;
}

void update_global_lfo1(uint32_t frame) {
    for (unsigned l = 0; l < NLAYER; l++) {
        const EngineState *e = &engine_state[l];
        const LfoParams *cfg = &e->lfo[0];
        LfoRuntimeState *state = &global_lfo1_state[l];
        SourceCtx c = {e, (int)l, NULL, NULL, 0};
        lfo_advance(cfg, state, resolve_source(&c, cfg->speed_mod_src, 0), layer_key_held((uint8_t)l), SYNTH_CTRL_SHIFT,
                    frame);
        int32_t raw = compute_morphed_lfo_fixed(state, cfg->shape);
        lfo1_out_q15[l] = (raw * state->envelope_amp_q15) >> 15;
    }
}

/* ------------------------------------------------------------------ control tick --- */

static void refresh_osc_mixer(int l) {
    const OscMixerParams *m = &engine_state[l].oscmix;
    OscillatorMixer *x = &osc_mixer[l];
    x->osc_balance = m->balance;
    x->xmod_amount = m->xmod_amt;
    x->xmod_type = clampi(m->xmod_type, 0, OSCMIX_XMOD_COUNT - 1);
    x->sub_level = m->sub_level;
    x->sub_shift = 1 + clampi(m->sub_octave, 0, 1);                 /* SUBOCT_DOWN1 / DOWN2 */
    x->sub_waveform = clampi(m->sub_waveform, 0, SUBWAVE_COUNT - 1);
    x->noise_level = m->noise_level;
    x->noise_colour = clampi(m->noise_colour, 0, 127);
}

/* The slot the screen shows runs once more at the end of a pass, after every real reader of it: a slot nothing
 * reads still has a value to show, and one something does read answers from that pass's cache, so the sound is
 * unchanged. */
static void watch_slot(const SourceCtx *c) {
    if (synth_mod_watch >= 0 && synth_mod_watch < (int)NMODSLOT)
        slot_output(c, synth_mod_watch, 0);
}

int32_t synth_mod_layer_out(int layer, int slot) {
    return layer >= 0 && layer < (int)NLAYER && slot >= 0 && slot < (int)NMODSLOT ? layer_mod_fn[layer][slot].out_q15 : 0;
}

void synth_voice_begin_tick(uint32_t frame) {
    tune_cents_now = instrument_tune_cents();       /* once a tick, not once a voice */
    layer_mod_pass++;                                               /* the LFO1 pass is a pass of its own */
    update_global_lfo1(frame);
    for (unsigned l = 0; l < NLAYER; l++) {
        SourceCtx c = {&engine_state[l], (int)l, NULL, NULL, 0};
        watch_slot(&c);
        refresh_osc_mixer(l);
    }
}

/* ------------------------------------------------------------------ voice --- */

/* ENV time modulation (spec 4.4): velocity and key position, in envelope steps */
static int time_mod(const EnvParams *p, const VoiceInput *in) {
    int vel = (bip(p->vel_to_time) * in->velocity) >> 7;             /* about +-64 at full velocity */
    int key = (bip(p->key_to_time) * (in->note - 60)) >> 6;          /* about +-60 at the keyboard ends */
    return vel + key;
}

/* Velocity to level, hardwired to the amp as ENV1 is: 127 is unity, a square law down to 4096 (-18 dB) at 1.
 * 100 is about -3.5 dB, 64 about -9 dB. */
static int32_t velocity_gain_q15(int velocity) {
    return ctrl_velocity_gain_q15[clampi(velocity, 1, 127)];          /* 4096 + v^2 (32767 - 4096) / 127^2 */
}

/* KEY TRACK (spec 4.1): 127 = full 1V/oct, 64 = none, below 64 inverts. Q15 scale, 32767 at 127. */
static int32_t key_track_q15(uint8_t kt) {
    if (kt < 128u) return ctrl_key_track_q15[kt];                     /* the formula below, as a table */
    return clamp32(((int32_t)kt - (int32_t)BIPOLAR_CENTER) * 32767 / 63, -32768, 32767);
}

/* An oscillator's base note: the keyboard note through key track, plus transpose and the octave knob.
 * Key track can land between semitones, so the remainder comes back as cents. */
static void base_pitch(const OscParams *o, const VoiceInput *in, int *note, int *cents) {
    int octave = ((int)o->octave * 7 >> 7) - 3;                      /* knob 0..127 -> -3..+3 */
    int32_t track_cents = (((int32_t)in->note - 60) * 100 * key_track_q15(o->key_track)) >> 15;
    *note = clampi(60 + in->transpose + octave * 12 + track_cents / 100, 0, 127);
    *cents = track_cents % 100;
}

/* CHOKE (a voice over its layer's limit, or still on its layer's old architecture): the level fades to 0 over
 * 2^CHOKE_SHIFT control ticks, 256 samples (5.8 ms), then the voice is silenced and reset. */
#define CHOKE_SHIFT 5

/* BINAURAL right ear: 0.25 Hz per BEAT step, in Q16 */
#define BIN_BEAT_STEP_Q16 16384u

/* A choked voice's fade has ended: everything back to rest, ready for a clean note on any architecture. */
static void voice_choke_done(SynthVoiceNode *voice) {
    adsr_init(&voice->amp_env);
    adsr_init(&voice->mod_env);
    adsr_init(&voice->aux_env);
    voice->is_gate_open = 0;
    voice->choke = 0;
    voice->choke_q15 = 32767;
}

static void voice_go_silent(SynthVoiceNode *voice) {
    voice->amp_env.level_q15 = 0;
    voice->amp_env.level_q30 = 0;
    voice->vol = voice->vol_step = voice->vol_b = voice->vol_b_step = 0.f;
    voice->silent = 1;
}

/* the cutoff of a filter in Q8 knob units: knob plus a mod offset, clamped to the whole knob (20 Hz .. 14 kHz) */
static int cutoff_q8(uint8_t knob, int32_t mod_q8) { return clampi(((int)knob << 8) + (int)mod_q8, 0, 127 << 8); }

/* a filter's own mod source and amount (spec 4.2), as a Q8 cutoff offset. Kept in Q8 so the filter interpolates
 * between the table steps and a modulated cutoff does not zipper. */
static int32_t filter_mod_q8(const SourceCtx *c, const FilterParams *f) {
    return (resolve_source(c, f->mod_src, 0) * bip(f->mod_amt)) >> 7;
}

/* A FILTER page's type morph plus its own mod source and amount (spec 4.2, every architecture), the knob's way round
 * (dsp/filter.h). An amount of 0 resolves no source. */
static int filter_morph(const SourceCtx *c, const FilterParams *f) {
    int m = f->morph;
    if (f->morph_mod_amt != BIPOLAR_CENTER)
        m = clampi(m + (int)((resolve_source(c, f->morph_mod_src, 0) * bip(f->morph_mod_amt)) >> 15), 0, 127);
    return m;
}

/* One core from a FILTER page, its morph modulated through c (the BINAURAL right ear passes its phase-offset context). */
static void filter_prepare_one(SvfCore *k, const SourceCtx *c, const FilterParams *f, int cut_q8, int snap) {
    filter_core_prepare(k, cut_q8, f->resonance, filter_morph(c, f), f->drive, f->flavour, SYNTH_CTRL_SHIFT, snap);
}

/* Wavetable scan: knob 0..127 plus its mod and an offset, kept in Q8 so a mod sweep (LFO, envelope) moves smoothly
 * instead of in whole knob steps, then spread continuously across the 8 morph frames. */
static int scan_morph_q12(const SourceCtx *c, const OscParams *o, int offset_knob) {
    int32_t scan_q8 = ((int32_t)o->scan + offset_knob) << 8;
    scan_q8 += (resolve_source(c, o->scan_mod_src, 0) * o->scan_mod_amt) >> 7;
    return scan_q8_to_morph_q12(clampi(scan_q8, 0, 127 << 8));
}

/* One path's mix-bus gains (MIX_* order) from its Q15 left and right gains: the dry pair times `dry`, the sends from
 * the pair's mono sum (to_delay, to_reverb carry the 1/2 and the routing weight). */
static void mix_gains(float *g, int32_t l_q15, int32_t r_q15, float dry, float to_delay, float to_reverb) {
    float l = (float)l_q15, r = (float)r_q15;
    g[MIX_L] = l * dry;
    g[MIX_R] = r * dry;
    g[MIX_DELAY] = (l + r) * to_delay;
    g[MIX_REVERB] = (l + r) * to_reverb;
}

void voice_control_tick(SynthVoiceNode *voice, const VoiceInput *in) {
    const EngineState *e = &engine_state[voice->layer];
    SourceCtx c = {e, voice->layer, voice, in, 0};
    uint8_t arch = voice->arch;
    voice->mod_pass++;                                              /* this voice's pass: its slots run afresh */
    SPROF_DECL;
    SPROF_START();

    /* LFO2/3 first, so their phases never stall while the voice is still rendering. */
    update_poly_lfo_engines(voice, in);
    SPROF_LAP(SPROF_C_LFO);

    /* 1. Envelopes, a tick at a time. ENV2 and ENV3 invert when read as mod sources (see resolve_source). In STACK,
     * ENV2 is also stack B's amp envelope, read uninverted as ENV1 is. */
    int32_t amp_env_q15 = adsr_advance(&voice->amp_env, &e->env[0], time_mod(&e->env[0], in),
                                       voice->release_boost_q15, SYNTH_CTRL_SHIFT, in->frame);
    int32_t env2_q15 = adsr_advance(&voice->mod_env, &e->env[1], time_mod(&e->env[1], in),
                                    voice->release_boost_q15, SYNTH_CTRL_SHIFT, in->frame);
    adsr_advance(&voice->aux_env, &e->env[2], time_mod(&e->env[2], in), voice->release_boost_q15, SYNTH_CTRL_SHIFT,
                 in->frame);
    int32_t amp_b_env_q15 = arch == VARCH_STACK ? env2_q15 : 0;
    SPROF_LAP(SPROF_C_ENV);

    if (voice->choke) {
        voice->choke_q15 -= 32767 >> CHOKE_SHIFT;
        if (voice->choke_q15 <= 0) {
            voice_choke_done(voice);
            voice_go_silent(voice);
            return;
        }
    }
    if (amp_env_q15 <= 0 && amp_b_env_q15 <= 0 && !voice->is_gate_open) {
        voice_go_silent(voice);
        return;
    }
    voice->silent = 0;

    /* 2. ENV1 (and ENV2 for STACK B) and the note's velocity are hardwired to the amp. The amp's secondary mod source
     * scales it: depth 0 = no effect, depth 127 = the source alone sets the level (a unipolar source; negative values
     * count as 0). A unison stack and a choke fade scale the level too. */
    const AmpParams *a = &e->amp;
    int32_t amp_gain_q15 = 32767;
    if (a->amp_mod_src != MSRC_NONE) {
        int32_t depth_q15 = (int32_t)a->amp_mod_depth * 258;
        int32_t mod_uni_q15 = clamp32(resolve_source(&c, a->amp_mod_src, 0), 0, 32767);
        amp_gain_q15 = clamp32(32767 - depth_q15 + ((mod_uni_q15 * depth_q15) >> 15), 0, 32767);
    }
    int32_t level_q15 = (((amp_gain_q15 * velocity_gain_q15(in->velocity)) >> 15) * voice->unison_gain_q15) >> 15;
    if (voice->choke) level_q15 = (level_q15 * voice->choke_q15) >> 15;
    int32_t vol_q15 = (amp_env_q15 * level_q15) >> 15;
    int32_t vol_b_q15 = (amp_b_env_q15 * level_q15) >> 15;

    /* 3. Oscillators. Pitch is in cents: fine tune, pitch bend, key track and pitch mod add together. Portamento and
     * unison detune come from the voice, not the patch: every oscillator of the voice moves together. */
    const OscParams *o1 = &e->osc[0];
    const OscParams *o2 = &e->osc[1];
    int bend_range = clampi(perf_state.bend_range, 0, 24);          /* semitones at full wheel */
    int32_t base_cents = (in->pitch_bend * bend_range * 100) / 64 + in->glide_cents + in->detune_cents +
                         tune_cents_now;              /* GLOBAL > MASTER TUNE (synth_voice_begin_tick) */

    int note1, track1;
    base_pitch(o1, in, &note1, &track1);
    int32_t pitch1_cents = osc_fine_cents(o1->fine) + track1 + base_cents;
    uint32_t freq1_q16 = apply_cents_q16(mtof_q16(note1),
                                         pitch1_cents + pitch_mod_cents(resolve_source(&c, o1->pitch_mod_src, 0), o1->pitch_mod_amt));
    voice->osc1.bank = o1->wavebank % NUM_BANKS;
    voice->osc1.morph_q12 = scan_morph_q12(&c, o1, 0);

    uint32_t freq2_q16 = 0;
    if (varch_uses_osc2(arch)) {
        int note2, track2;
        base_pitch(o2, in, &note2, &track2);
        freq2_q16 = apply_cents_q16(mtof_q16(note2), osc_fine_cents(o2->fine) + track2 + base_cents
                                    + pitch_mod_cents(resolve_source(&c, o2->pitch_mod_src, 0), o2->pitch_mod_amt));
        voice->osc2.bank = o2->wavebank % NUM_BANKS;
        voice->osc2.morph_q12 = scan_morph_q12(&c, o2, 0);
    }

    /* 4. OSCMIXER. Only the sources the architecture uses are resolved; mod offsets are in 0..127 knob units. */
    const OscMixerParams *mx = &e->oscmix;
    const FilterMixerParams *fm = &e->fltmix;
    int32_t balance_off = 0, xmod_off = 0, spread = 0;
    int32_t noise_off = (resolve_source(&c, mx->noise_mod_src, 0) * bip(mx->noise_mod_amt)) >> 15, colour_off = 0;
    if (mx->noise_col_mod_amt != BIPOLAR_CENTER)
        colour_off = (resolve_source(&c, mx->noise_col_mod_src, 0) * bip(mx->noise_col_mod_amt)) >> 15;
    if (arch == VARCH_DUAL_OSC)
        balance_off = (resolve_source(&c, mx->mix_mod_src, 0) * mx->mix_mod_amt) >> 15;
    if (varch_uses_osc2(arch))
        xmod_off = (resolve_source(&c, mx->xmod_mod_src, 0) * mx->xmod_mod_amt) >> 15;
    if (arch == VARCH_SPREAD)
        spread = clampi(mx->spread + ((resolve_source(&c, mx->spread_mod_src, 0) * mx->spread_mod_amt) >> 15), 0, 127);
    osc_mixer_prepare(&voice->mix, &osc_mixer[voice->layer], freq1_q16, freq2_q16,
                      (int)balance_off, (int)xmod_off, (int)noise_off, (int)colour_off);
    osc_prepare(&voice->osc1, freq1_q16, (int)spread);
    if (varch_uses_osc2(arch))
        osc_prepare(&voice->osc2, freq2_q16, 0);
    SPROF_LAP(SPROF_C_OSC);

    /* 5. Filters: each architecture prepares only the cores its path runs. A new note's first tick sets the
     * coefficients at once; later ticks ramp to them. */
    const FilterParams *f1 = &e->filter[0];
    const FilterParams *f2 = &e->filter[1];
    SvfCore *core = voice->synth_filters.core;
    int32_t f1_mod_q8 = filter_mod_q8(&c, f1);
    int snap = voice->snap;

    /* BINAURAL: the two phase offsets, each knob plus the shared PHASE MOD, wrapping round the cycle */
    int32_t phase_mod = 0;
    if (arch == VARCH_BINAURAL)
        phase_mod = (resolve_source(&c, fm->bin_phase_mod_src, 0) * bip(fm->bin_phase_mod_amt)) >> 15;

    switch (arch) {
    case VARCH_DUAL_FILTER: {
        /* FILTER MIXER: balance and routing with their own mod sources (spec 4.2) */
        int32_t balance_mod = (resolve_source(&c, fm->balance_mod_src, 0) * bip(fm->balance_mod_amt)) >> 15;
        int32_t routing_mod = (resolve_source(&c, fm->routing_mod_src, 0) * bip(fm->routing_mod_amt)) >> 15;
        dual_filter_prepare(&voice->synth_filters,
                            cutoff_q8(f1->cutoff, f1_mod_q8), f1->resonance, filter_morph(&c, f1), f1->drive, f1->flavour,
                            cutoff_q8(f2->cutoff, filter_mod_q8(&c, f2)), f2->resonance, filter_morph(&c, f2), f2->drive, f2->flavour,
                            clampi(fm->routing + (int)routing_mod, 0, 127), clampi(fm->balance + (int)balance_mod, 0, 127),
                            SYNTH_CTRL_SHIFT, snap);
        break;
    }
    case VARCH_STACK: {
        /* ENV3 is hardwired to both cutoffs (FILTER MIXER knob 4), on top of each filter's own mod */
        int32_t env3_q8 = (resolve_source(&c, MSRC_ENV3, 0) * bip(fm->env3_cutoff)) >> 7;
        filter_prepare_one(&core[0], &c, f1, cutoff_q8(f1->cutoff, f1_mod_q8 + env3_q8), snap);
        filter_prepare_one(&core[1], &c, f2, cutoff_q8(f2->cutoff, filter_mod_q8(&c, f2) + env3_q8), snap);
        break;
    }
    case VARCH_BINAURAL: {
        /* The right ear: OSC1's settings again, BEAT Hz higher, its LFO mod read OSC phase offset ahead; FILTER1's
         * settings again, its LFO mod read FILTER phase offset ahead. CUTOFF OFFSET opens one ear and closes the other. */
        SourceCtx co = c, cf = c;
        co.lfo_offset = (uint32_t)((mx->bin_mod_phase + phase_mod) & 127) << 25;
        cf.lfo_offset = (uint32_t)((fm->bin_mod_phase + phase_mod) & 127) << 25;
        uint32_t freq_r_q16 = apply_cents_q16(mtof_q16(note1), pitch1_cents
                                              + pitch_mod_cents(resolve_source(&co, o1->pitch_mod_src, 0), o1->pitch_mod_amt));
        freq_r_q16 += (uint32_t)mx->bin_beat * BIN_BEAT_STEP_Q16;
        if (freq_r_q16 > PITCH_MAX_Q16) freq_r_q16 = PITCH_MAX_Q16;
        voice->osc2.bank = voice->osc1.bank;
        voice->osc2.morph_q12 = scan_morph_q12(&co, o1, bip(mx->bin_scan_offset));
        osc_prepare(&voice->osc2, freq_r_q16, 0);

        /* CUTOFF L/R plus its own mod source and amount, in knob units, held to the knob's -64..+63 */
        int offset = bip(fm->bin_cutoff_offset);
        if (fm->bin_cutoff_mod_amt != BIPOLAR_CENTER)
            offset = clampi(offset + (int)((resolve_source(&c, fm->bin_cutoff_mod_src, 0) * bip(fm->bin_cutoff_mod_amt)) >> 15),
                            -64, 63);
        int32_t offset_q8 = (int32_t)offset << 7;                   /* up to +-32 cutoff steps per ear */
        filter_prepare_one(&core[0], &c, f1, cutoff_q8(f1->cutoff, f1_mod_q8 - offset_q8), snap);
        filter_prepare_one(&core[1], &cf, f1, cutoff_q8(f1->cutoff, filter_mod_q8(&cf, f1) + offset_q8), snap);
        break;
    }
    default:                                                        /* SINGLE, DUAL OSC, SPREAD: FILTER1 alone */
        filter_prepare_one(&core[0], &c, f1, cutoff_q8(f1->cutoff, f1_mod_q8), snap);
        break;
    }
    voice->snap = 0;
    SPROF_LAP(SPROF_C_FILT);

    /* 6. AMP (spec 4.6): each level ramps to this tick's value one step per sample (no zipper on the envelope), then
     * the drive stage, then pan with its mod source. Float from here (the FM1's FPU beats its integer multiplies). */
    const float tick_inv = 1.f / (float)SYNTH_CTRL_PERIOD, q15_inv = 1.f / 32768.f;
    voice->vol_step = ((float)vol_q15 * q15_inv - voice->vol) * tick_inv;
    voice->vol_b_step = ((float)vol_b_q15 * q15_inv - voice->vol_b) * tick_inv;
    /* STRIKE's attack. ATK TRACK: AUTO follows OSC1's pitch (bend, glide, pitch mod and all) for a pitched sound and
     * plays an unpitched one at its own rate; FIXED never follows; TRACK always does (an unpitched sound as if its root
     * were C4). TUNE transposes either way. ATK DECAY fades it (127: off). ATK > FILTER splits it: the part that skips
     * the filter has the amp's level (amp mod, unison, choke, velocity by ATK VEL) without ENV1, so a slow pad attack
     * never swallows it; the part into FILTER1 is ATK LEVEL alone, the voice's own level and ENV1 applying after the
     * filter. A new note starts both at their levels, no ramp up. */
    if (arch == VARCH_STRIKE && voice->atk_p) {
        uint16_t tt = (uint16_t)(mx->atk_tune | (uint16_t)mx->atk_track << 8);
        if (freq1_q16 != voice->atk_f1 || tt != voice->atk_tt) {    /* (2026-10-16: PERF TEST, not every tick) */
            const AttackInfo *ai = &attack_info[voice->atk_sample];
            float step = (float)apply_cents_q16(ai->rate_q16, clampi(bip(mx->atk_tune), -24, 24) * 100);
            uint8_t trk = mx->atk_track;
            if (trk == ATKTRK_TRACK || (trk != ATKTRK_FIXED && ai->root != ATK_UNPITCHED))
                step *= (float)freq1_q16 / (float)mtof_q16(ai->root != ATK_UNPITCHED ? ai->root : 60);
            voice->atk_base = step > 8.f * 65536.f ? 8u << 16 : (step < 1.f ? 1u : (uint32_t)step);   /* <= 3 octaves up */
            voice->atk_step = voice->atk_base;
            voice->atk_f1 = freq1_q16;
            voice->atk_tt = tt;
        }
        if (voice->atk_glide != 0.f) {                  /* BEND: off pitch at the note's start, gliding back, tau ~40 ms */
            int32_t steps_q15 = bip(mx->atk_bend) << 15;     /* half a semitone a step; BEND MOD's source adds to it */
            if (mx->atk_bend_mod_amt != BIPOLAR_CENTER)
                steps_q15 += resolve_source(&c, mx->atk_bend_mod_src, 0) * bip(mx->atk_bend_mod_amt);
            uint32_t b = apply_cents_q16(voice->atk_base, (int32_t)((float)steps_q15 * (50.f / 32768.f) * voice->atk_glide));
            voice->atk_step = b > (8u << 16) ? 8u << 16 : (b < 1u ? 1u : b);
            voice->atk_glide *= 0.991f;                 /* exp(-16 / (44100 * 0.04)) a tick */
            if (voice->atk_glide < (1.f / 8192.f)) {    /* under half a cent even from 64 steps off */
                voice->atk_glide = 0.f;
                voice->atk_step = voice->atk_base;
            }
        }
        if (mx->atk_decay < 127u) {                     /* a time constant of 2 .. ~1000 ticks (0.7 ms .. 0.36 s; 64: 94 ms) */
            float t = 2.f + (float)mx->atk_decay * (float)mx->atk_decay * (1.f / 16.f);
            voice->atk_fade -= voice->atk_fade / t;
        }
        int32_t vel_q15 = velocity_gain_q15(in->velocity);
        if (mx->atk_vel < 127u)                         /* 127: the voice's velocity as it is */
            vel_q15 = 32767 - (((32767 - vel_q15) * ((int32_t)mx->atk_vel * 258)) >> 15);   /* 258 / 32768 ~ 1 / 127 */
        int32_t lev_q15 = (((amp_gain_q15 * vel_q15) >> 15) * voice->unison_gain_q15) >> 15;
        if (voice->choke) lev_q15 = (lev_q15 * voice->choke_q15) >> 15;
        float lvl = (float)mx->atk_level * (1.f / 127.f) * voice->atk_fade, f = (float)mx->atk_filter * (1.f / 127.f);
        float post = (float)lev_q15 * q15_inv * lvl * (1.f - f), pre = lvl * f;
        if (snap) {
            voice->atk_vol = post;
            voice->atk_pre = pre;
        }
        voice->atk_vol_step = (post - voice->atk_vol) * tick_inv;
        voice->atk_pre_step = (pre - voice->atk_pre) * tick_inv;
    }
    voice->drive = a->drive;
    voice->drive_model = a->drive_model;
    int pan = clampi(in->pan + (int)((resolve_source(&c, a->pan_mod_src, 0) * a->pan_mod_depth) >> 15), 0, 127);
    /* 7. AMP sends: the layer's output as mono into the shared delay and reverb (the sends are the knob values), and
     * the layer's routing weight on everything; the dry side gets the mix gain as well. */
    float route = (float)voice->route_q15 * q15_inv;
    float dry = route * ((float)MIX_HEADROOM_Q15 * q15_inv) * q15_inv;
    float to_delay = route * (float)((int32_t)a->delay_send * 258) * (0.5f * q15_inv * q15_inv);   /* 0.5: the mono */
    float to_reverb = route * (float)((int32_t)a->reverb_send * 258) * (0.5f * q15_inv * q15_inv);
    if (arch == VARCH_STACK) {
        /* FILTER MIXER: BLEND (knob 1 + its mod) weighs A against B; WIDTH pulls A left and B right of the pan */
        int32_t blend_mod = (resolve_source(&c, fm->balance_mod_src, 0) * bip(fm->balance_mod_amt)) >> 15;
        int32_t gain_b = clampi(fm->balance + (int)blend_mod, 0, 127) * 258, gain_a = 32768 - gain_b;
        int half = fm->stack_width >> 1;
        int32_t al, ar, bl, br;
        amp_pan_gains(&voice->master_amp, clampi(pan - half, 0, 127), &al, &ar);
        amp_pan_gains(&voice->master_amp, clampi(pan + (fm->stack_width - half), 0, 127), &bl, &br);
        mix_gains(voice->mix_a, (al * gain_a) >> 15, (ar * gain_a) >> 15, dry, to_delay, to_reverb);
        mix_gains(voice->mix_b, (bl * gain_b) >> 15, (br * gain_b) >> 15, dry, to_delay, to_reverb);
    } else if (arch == VARCH_BINAURAL) {
        /* WIDTH: 127 keeps each ear on its own side, 0 sums them; the pan then balances the pair */
        int32_t pl, pr, w = (int32_t)fm->bin_width * 258;
        amp_pan_gains(&voice->master_amp, pan, &pl, &pr);
        mix_gains(voice->mix_a, (pl * ((32768 + w) >> 1)) >> 15, (pr * ((32768 - w) >> 1)) >> 15,   /* left ear */
                  dry, to_delay, to_reverb);
        mix_gains(voice->mix_b, (pl * ((32768 - w) >> 1)) >> 15, (pr * ((32768 + w) >> 1)) >> 15,   /* right ear */
                  dry, to_delay, to_reverb);
    } else {
        int32_t pl, pr;
        amp_pan_gains(&voice->master_amp, pan, &pl, &pr);
        mix_gains(voice->mix_a, pl, pr, dry, to_delay, to_reverb);
        mix_gains(voice->mix_b, 0, 0, 0.f, 0.f, 0.f);              /* no path B (the AMP page's pan reads both) */
    }
    SPROF_LAP(SPROF_C_AMP);
    watch_slot(&c);
}

/* ------------------------------------------------------------------ render, one function per architecture --- */


/* ------------------------------------------------------------------ blocks --- */
/* A voice renders a block at a time: the samples between two of its control ticks (at most SYNTH_CTRL_PERIOD), stage
 * by stage, each stage's state in registers over the block (2026-10-15: the FM1 pays ~8 cycles a load, and the
 * per-sample calls reloaded every stage's state each sample). The arithmetic and its order per sample are as before:
 * the output is bit-identical (render hashes). mix is the block's first frame of the bus, MIX_N floats a frame. */
#define VB SYNTH_CTRL_PERIOD

/* the int sum of a path, held to Q15, as the filter's float input */
static inline void q15_to_float(const int32_t *x, float *y, int n) {
    for (int i = 0; i < n; i++) y[i] = (float)clamp_q15(x[i]);
}

/* one path: amp level (ramped), drive, then into the mix: four multiply-adds by the tick's gains (pan, routing, mix
 * gain and sends folded together). SINGLE, DUAL OSC, SPREAD and DUAL FILTER end here. */
static void finish_mono_block(SynthVoiceNode *voice, const float *y, float *mix, int n) {
    float vol = voice->vol, step = voice->vol_step;
    const float g0 = voice->mix_a[MIX_L], g1 = voice->mix_a[MIX_R], g2 = voice->mix_a[MIX_DELAY],
                g3 = voice->mix_a[MIX_REVERB];
    int drive = voice->drive, model = voice->drive_model;
    for (int i = 0; i < n; i++, mix += MIX_N) {
        float s = y[i] * vol;
        vol += step;
        if (drive) s = amp_drive_f(s, drive, model);
        mix[MIX_L] += s * g0;
        mix[MIX_R] += s * g1;
        mix[MIX_DELAY] += s * g2;
        mix[MIX_REVERB] += s * g3;
    }
    voice->vol = vol;
}

/* two paths, each through its own amp level and drive, each with its own gains. STACK: B has ENV2's level; BINAURAL:
 * both ears share ENV1's. */
static void finish_pair_block(SynthVoiceNode *voice, const float *ya, const float *yb, int own_b_level, float *mix,
                              int n) {
    float vol = voice->vol, step = voice->vol_step, vol_b = voice->vol_b, step_b = voice->vol_b_step;
    const float *ga = voice->mix_a, *gb = voice->mix_b;
    int drive = voice->drive, model = voice->drive_model;
    for (int i = 0; i < n; i++, mix += MIX_N) {
        float va = vol, vb = va;
        vol += step;
        if (own_b_level) {
            vb = vol_b;
            vol_b += step_b;
        }
        float a = ya[i] * va, b = yb[i] * vb;
        if (drive) {
            a = amp_drive_f(a, drive, model);
            b = amp_drive_f(b, drive, model);
        }
        mix[MIX_L] += a * ga[MIX_L] + b * gb[MIX_L];
        mix[MIX_R] += a * ga[MIX_R] + b * gb[MIX_R];
        mix[MIX_DELAY] += a * ga[MIX_DELAY] + b * gb[MIX_DELAY];
        mix[MIX_REVERB] += a * ga[MIX_REVERB] + b * gb[MIX_REVERB];
    }
    voice->vol = vol;
    voice->vol_b = vol_b;
}

/* SINGLE and SPREAD: OSC1 (SPREAD: its 4 detuned playheads, set by osc_prepare) + sub + noise -> FILTER1 */
static void render_single(SynthVoiceNode *v, float *mix, int n) {
    int32_t o[VB];
    float x[VB];
    SPROF_DECL;
    SPROF_START();
    osc_render_block(&v->osc1, o, n);
    SPROF_LAP(SPROF_OSC);
    osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, &v->noise[0], o, n);
    SPROF_LAP(SPROF_EXTRAS);
    q15_to_float(o, x, n);
    filter_core_render_block(&v->synth_filters.core[0], x, x, n);
    SPROF_LAP(SPROF_FILTER);
    finish_mono_block(v, x, mix, n);
    SPROF_LAP(SPROF_AMP);
}

/* DUAL OSC: OSC1 + cross-modulated OSC2 by balance, + sub + noise -> FILTER1 */
static void render_dual_osc(SynthVoiceNode *v, float *mix, int n) {
    int32_t o[VB];
    float x[VB];
    for (int i = 0; i < n; i++)                /* the cross-modulation reads OSC1 sample by sample */
        o[i] = osc_mixer_render(&v->mix, &v->osc1, &v->osc2, &v->sub_phase, &v->noise[0]);
    q15_to_float(o, x, n);
    filter_core_render_block(&v->synth_filters.core[0], x, x, n);
    finish_mono_block(v, x, mix, n);
}

/* DUAL FILTER: OSC1 + sub + noise -> FILTER1 / FILTER2, serial .. parallel, balanced */
static void render_dual_filter(SynthVoiceNode *v, float *mix, int n) {
    int32_t o[VB];
    float x[VB];
    osc_render_block(&v->osc1, o, n);
    osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, &v->noise[0], o, n);
    q15_to_float(o, x, n);
    dual_filter_render_block(&v->synth_filters, x, x, n);
    finish_mono_block(v, x, mix, n);
}

/* STACK: A = OSC1 + sub + noise -> FILTER1 (ENV1), B = cross-modulated OSC2 -> FILTER2 (ENV2) */
static void render_stack(SynthVoiceNode *v, float *mix, int n) {
    int32_t w1[VB], w2[VB];
    float a[VB], b[VB];
    osc_render_block(&v->osc1, w1, n);
    for (int i = 0; i < n; i++)
        w2[i] = osc_mixer_osc2(&v->mix, (int16_t)w1[i], &v->osc2);
    osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, &v->noise[0], w1, n);   /* A: w1 + sub + noise */
    q15_to_float(w1, a, n);
    for (int i = 0; i < n; i++) b[i] = (float)w2[i];
    filter_core_render_block(&v->synth_filters.core[0], a, a, n);
    filter_core_render_block(&v->synth_filters.core[1], b, b, n);
    finish_pair_block(v, a, b, 1, mix, n);
}

/* BINAURAL: left ear OSC1 -> core 0, right ear (OSC2's playhead running OSC1's settings) -> core 1. The sub is
 * shared, each ear has its own noise generator so the noise decorrelates. */
static void render_binaural(SynthVoiceNode *v, float *mix, int n) {
    int32_t sub[VB], l[VB], r[VB];
    float fl[VB], fr[VB];
    for (int i = 0; i < n; i++) sub[i] = 0;
    osc_mixer_sub_block(&v->mix, &v->osc1, &v->sub_phase, sub, n);      /* once a sample, both ears */
    osc_render_block(&v->osc1, l, n);
    osc_render_block(&v->osc2, r, n);
    for (int i = 0; i < n; i++) {
        l[i] += sub[i];
        r[i] += sub[i];
    }
    osc_mixer_noise_block(&v->mix, &v->noise[0], l, n);
    osc_mixer_noise_block(&v->mix, &v->noise[1], r, n);
    q15_to_float(l, fl, n);
    q15_to_float(r, fr, n);
    filter_core_render_block(&v->synth_filters.core[0], fl, fl, n);
    filter_core_render_block(&v->synth_filters.core[1], fr, fr, n);
    finish_pair_block(v, fl, fr, 0, mix, n);
}

/* STRIKE's attack, in the voice's last loop: the mu-law byte pair under the read position, interpolated, at the
 * attack's own level, added to the amped (and driven) voice before the four gains (pan, routing, sends), so the mix is
 * written once a sample. The samples left in the attack are counted once a block, not tested every sample.
 * 2026-10-16: the first version, its own pass over the mix, cost 4.8 % in PERF TEST SEQ+ARP STRIKE (70.6 %, budget
 * 70): on the FM1 the cost is instructions (XIP, ~7 cycles each), not the loads (fetching the pair only on a new byte
 * changed nothing). The decode table is read from a RAM copy (attack_mulaw sits in flash). */
static float atk_dec[256];
void voice_attack_init(void) {
    for (int i = 0; i < 256; i++) atk_dec[i] = attack_mulaw[i];
}

static void finish_strike_block(SynthVoiceNode *voice, const float *y, float *mix, int n) {
    uint32_t pos = voice->atk_pos, step = voice->atk_step, end = voice->atk_end;
    int na = 0;
    if (voice->atk_p && pos < end)
        na = (int)((end - pos + step - 1u) / step);           /* step >= 1 (the tick's) */
    if (na > n) na = n;
    if (na > 0) {
        const uint8_t *p = voice->atk_p;
        float vol = voice->vol, vstep = voice->vol_step, av = voice->atk_vol, astep = voice->atk_vol_step;
        const float g0 = voice->mix_a[MIX_L], g1 = voice->mix_a[MIX_R], g2 = voice->mix_a[MIX_DELAY],
                    g3 = voice->mix_a[MIX_REVERB];
        int drive = voice->drive, model = voice->drive_model;
        for (int i = 0; i < na; i++, mix += MIX_N) {
            float s = y[i] * vol;
            vol += vstep;
            if (drive) s = amp_drive_f(s, drive, model);
            uint32_t k = pos >> 16;
            float a = atk_dec[p[k]], b = atk_dec[p[k + 1u]];
            s += (a + (b - a) * (float)(pos & 0xFFFFu) * (1.f / 65536.f)) * av;
            pos += step;
            av += astep;
            mix[MIX_L] += s * g0;
            mix[MIX_R] += s * g1;
            mix[MIX_DELAY] += s * g2;
            mix[MIX_REVERB] += s * g3;
        }
        voice->vol = vol;
        voice->atk_pos = pos;
        voice->atk_vol = av;
        y += na;
    }
    if (voice->atk_p && pos >= end)
        voice->atk_p = NULL;
    if (na < n)
        finish_mono_block(voice, y, mix, n - na);             /* the voice alone, after the attack */
}

/* ATK > FILTER above 0: the attack is read once, into att, as the oscillator's sum becomes the filter's input (the part
 * into the filter added there); the last loop adds the part that skips the filter from att. */
static void render_strike_split(SynthVoiceNode *v, const int32_t *o, float *mix, int n) {
    float x[VB], att[VB];
    uint32_t pos = v->atk_pos, step = v->atk_step, end = v->atk_end;
    int na = 0;
    if (pos < end)
        na = (int)((end - pos + step - 1u) / step);
    if (na > n) na = n;
    const uint8_t *p = v->atk_p;
    float pre = v->atk_pre, pstep = v->atk_pre_step;
    for (int i = 0; i < na; i++) {
        uint32_t k = pos >> 16;
        float a = atk_dec[p[k]], b = atk_dec[p[k + 1u]];
        float s = a + (b - a) * (float)(pos & 0xFFFFu) * (1.f / 65536.f);
        att[i] = s;
        x[i] = (float)clamp_q15(o[i]) + s * pre;
        pos += step;
        pre += pstep;
    }
    for (int i = na; i < n; i++) x[i] = (float)clamp_q15(o[i]);
    v->atk_pos = pos;
    v->atk_pre = pre;
    if (pos >= end)
        v->atk_p = NULL;
    filter_core_render_block(&v->synth_filters.core[0], x, x, n);
    float vol = v->vol, vstep = v->vol_step, av = v->atk_vol, astep = v->atk_vol_step;
    const float g0 = v->mix_a[MIX_L], g1 = v->mix_a[MIX_R], g2 = v->mix_a[MIX_DELAY], g3 = v->mix_a[MIX_REVERB];
    int drive = v->drive, model = v->drive_model;
    for (int i = 0; i < na; i++, mix += MIX_N) {
        float s = x[i] * vol;
        vol += vstep;
        if (drive) s = amp_drive_f(s, drive, model);
        s += att[i] * av;
        av += astep;
        mix[MIX_L] += s * g0;
        mix[MIX_R] += s * g1;
        mix[MIX_DELAY] += s * g2;
        mix[MIX_REVERB] += s * g3;
    }
    v->vol = vol;
    v->atk_vol = av;
    if (na < n)
        finish_mono_block(v, x + na, mix, n - na);
}

/* STRIKE: SINGLE's path, its last loop carrying the attack while it lasts; ATK > FILTER above 0 reads the attack
 * before the filter (render_strike_split) */
static void render_strike(SynthVoiceNode *v, float *mix, int n) {
    int32_t o[VB];
    float x[VB];
    osc_render_block(&v->osc1, o, n);
    osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, &v->noise[0], o, n);
    if (v->atk_p && (v->atk_pre != 0.f || v->atk_pre_step != 0.f)) {
        render_strike_split(v, o, mix, n);
        return;
    }
    q15_to_float(o, x, n);
    filter_core_render_block(&v->synth_filters.core[0], x, x, n);
    finish_strike_block(v, x, mix, n);
}

/* the render function of each architecture, VARCH_* order: chosen once per block by the voice's arch, so each inner
 * path carries only what that architecture uses */
static void (*const render_arch[VARCH_COUNT])(SynthVoiceNode *, float *, int) = {
    render_single,       /* SINGLE */
    render_dual_osc,     /* DUAL OSC */
    render_single,       /* SPREAD: the same path, OSC1 prepared with its spread */
    render_dual_filter,  /* DUAL FILTER */
    render_stack,        /* STACK */
    render_binaural,     /* BINAURAL */
    render_strike,       /* STRIKE */
};

void voice_render_block(SynthVoiceNode *voice, float *mix, int n) {
    if (n <= 0)
        return;
    if (voice->silent) {
        /* Free-running oscillators: a silent voice's playheads keep turning at their last pitch, so a note never
         * starts from a fixed phase. One multiply-add per playhead per block. */
        uint32_t k = (uint32_t)(n > (int)VB ? (int)VB : n);
        uint32_t ph = voice->osc1.center_phase + voice->osc1.center_step * k;
        /* The voice's own oscillators keep their usual alignment: OSC2, the spread playheads (the crossfade between
         * them assumes they start in phase) and the sub-oscillator start where OSC1 is, so only the voice as a whole
         * is free-running. Independent phases would set the level at random: two oscillators at one pitch add or
         * cancel by their fixed phase difference. */
        osc_set_phase(&voice->osc1, ph);
        osc_set_phase(&voice->osc2, ph);
        voice->sub_phase = ph >> (voice->osc1.center_step > voice->mix.sub_step * 3u ? 2 : 1);
        return;
    }
    render_arch[voice->arch < VARCH_COUNT ? voice->arch : VARCH_SINGLE](voice, mix, n > (int)VB ? (int)VB : n);
}

/* Portamento: moves the glide offset toward its target by one control tick's worth of per-sample steps. */
void voice_glide_advance(SynthVoiceNode *v) {
    int32_t step = v->glide_step_q16 << SYNTH_CTRL_SHIFT;
    if (v->glide_q16 > 0) {
        v->glide_q16 -= step;
        if (v->glide_q16 < 0) v->glide_q16 = 0;
    } else if (v->glide_q16 < 0) {
        v->glide_q16 += step;
        if (v->glide_q16 > 0) v->glide_q16 = 0;
    }
}

/* ------------------------------------------------------------------ adapters --- */

void voice_input_from_midi(VoiceInput *in, const MidiInterfaceState *midi, int note, int velocity, int gate) {
    in->note = note;
    in->velocity = velocity;
    in->gate = gate;
    in->tie = 0;
    in->pitch_bend = midi->pitch_bend_val;
    in->mod_wheel = midi->mod_wheel_val;
    in->aftertouch = midi->aftertouch_val;
    in->custom_cc1 = midi->custom_cc1_val;
    in->custom_cc2 = midi->custom_cc2_val;
    in->transpose = 0;
    in->pan = 64;
    in->glide_cents = 0;
    in->detune_cents = 0;
    in->frame = 0;
}

void voice_input_from_home_knobs(VoiceInput *in, int knob1_velocity, int knob2_pitch_bend, int knob3_mod_wheel) {
    in->velocity = clampi(knob1_velocity, 1, 127);          /* 0 would be a note-off */
    in->pitch_bend = clampi(knob2_pitch_bend - 64, -64, 63);
    in->mod_wheel = clampi(knob3_mod_wheel, 0, 127);
}

int voice_input_from_seq_step(VoiceInput *in, uint8_t event_type, const SequencerStep *step, int note_slot) {
    switch (event_type) {
    case SEQEV_NOTE: {
        if (note_slot < 0 || note_slot >= (int)SEQ_NNOTE) return 0;
        if (step->flags & (SEQ_FLAG_MUTE | SEQ_FLAG_SKIP)) return 0;
        uint8_t n = step->note_buffer[note_slot];
        if (n == SEQ_NOTE_NULL) return 0;
        if (note_slot == 0 && n == SEQ_NOTE_TIE) {          /* full CW in slot 1: PAUSE, or TIE with the flag */
            in->tie = (step->flags & SEQ_FLAG_TIE) != 0;
            return in->tie;
        }
        in->note = n;
        in->tie = (step->flags & SEQ_FLAG_TIE) != 0;
        in->gate = 1;
        return 1;
    }
    case SEQEV_DYNAMICS:   in->velocity = step->value; return 0;
    case SEQEV_TRANSPOSE:  in->transpose = (int)step->value - 64; return 0;   /* centre 64 = no offset */
    case SEQEV_MODWHEEL:   in->mod_wheel = step->value; return 0;
    case SEQEV_AFTERTOUCH: in->aftertouch = step->value; return 0;
    case SEQEV_CUSTOMCC1:  in->custom_cc1 = step->value; return 0;
    case SEQEV_CUSTOMCC2:  in->custom_cc2 = step->value; return 0;
    default:               return 0;   /* timing, gate, ratchet, probability, prog change, layer */
    }
}
