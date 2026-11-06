
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
#include "ctrl_tables.h"
#include "tuning.h"
#include "mfx.h"                     /* mfx_mod_off: the MOD SENDS to the MFX knobs */

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
    uint32_t ratio_q16 = 65536u + ctrl_cents_ratio_q16_lo[rem];   /* the semitones of 2^(i/12) interpolated (gen_ctrl_tables.py) */
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
    uint32_t sends;              /* bit MDST_* (under MDST_VOICE_BITS): some MOD SEND goes there (send_mask) */
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
    return src == MSRC_KEYBOARD || src == MSRC_RANDOM || (src >= MSRC_LFO1 && src <= MSRC_MODSLOT4);
}

/* ENV2 / ENV3 with INVERT run 0 -> -1 (2026-10-27): the knob is their rest point and they dip below it */
static int src_is_inverted_env(const EngineState *e, int src) {
    return (src == MSRC_ENV2 || src == MSRC_ENV3) && e->env[src - MSRC_ENV1].invert;
}

/* A level read from a source for DEPTH MOD: 0..32767. An inverted envelope reads as 1 -> 0 here, so it still opens
 * the depth rather than holding it shut. */
static int32_t source_level_q15(const EngineState *e, int src, int32_t s) {
    if (src == MSRC_KEYBOARD || src == MSRC_RANDOM) s = (s + 32768) >> 1;   /* bottom to top (bipolar) */
    else if (src_is_inverted_env(e, src)) s += 32767;
    return clamp32(s, 0, 32767);
}

/* MODSLOT n (spec 4.5): function of two sources, scaled by the slot amount */
static int32_t tune_cents_now;                      /* GLOBAL > MASTER TUNE, read once a tick */
static uint8_t tuning_now, tuning_root_now;         /* GLOBAL > TUNING and ROOT (the performance's), the same */

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
        int side = a >= (src_is_bipolar(m->src_a) ? 0 : src_is_inverted_env(c->eng, m->src_a) ? -16384 : 16384);
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
    case MSRC_RANDOM:     return v ? v->rand_q15 : 0;                 /* bipolar, a value per note */
    case MSRC_POLY_AT:                                                 /* the note's own, else the channel's */
        return v && v->poly_seen ? v->poly_at * 258 : in ? in->aftertouch * 258 : 0;
    /* an envelope at this note's VEL > LEVEL; ENV2 / ENV3 then invert (a soft note dips less) */
    case MSRC_ENV1:       return v ? (v->amp_env.level_q15 * v->env_vel_q15[0]) >> 15 : 0;   /* never inverted */
    case MSRC_ENV2:
    case MSRC_ENV3: {
        int i = src - MSRC_ENV1;
        if (!v) return 0;
        int32_t l = ((i == 1 ? v->mod_env.level_q15 : v->aux_env.level_q15) * v->env_vel_q15[i]) >> 15;
        return c->eng->env[i].invert ? -l : l;                        /* inverted: 0 -> -1, the knob is the rest point */
    }
    case MSRC_LFO1:                                                    /* the layer's, at this voice's depth */
        if (c->lfo_offset)
            return (lfo_offset_q15(&global_lfo1_state[c->layer], c->eng->lfo[0].shape, c->lfo_offset) *
                    (v ? v->lfo_gain_q15[0] : 32768)) >> 15;
        return v ? (lfo1_out_q15[c->layer] * v->lfo_gain_q15[0]) >> 15 : lfo1_out_q15[c->layer];
    case MSRC_LFO2:                                                    /* worked out once per tick */
    case MSRC_LFO3: {
        int i = src - MSRC_LFO2;
        if (!v) return 0;
        if (c->lfo_offset)
            return (lfo_offset_q15(&v->poly_lfos.engines[i], c->eng->lfo[i + 1].shape, c->lfo_offset) *
                    v->lfo_gain_q15[i + 1]) >> 15;
        return v->lfo_q15[i];
    }
    case MSRC_MODSLOT1:
    case MSRC_MODSLOT2:
    case MSRC_MODSLOT3:
    case MSRC_MODSLOT4:   return slot_output(c, src - MSRC_MODSLOT1, depth);
    case MSRC_CUSTOMCC1:  return in ? in->custom_cc1 * 258 : 0;
    case MSRC_CUSTOMCC2:  return in ? in->custom_cc2 * 258 : 0;
    default:              return 0;                                    /* MSRC_NONE */
    }
}

/* ------------------------------------------------------------- MOD SENDS --- */
/* (2026-11-03) A slot sent straight to a destination adds to what the destination's own source and amount do there:
 * the slot's output times the send amount through the destination's own amount (send_sum, send_cents, send_gain).
 * Which destinations have a send, as a mask, once a tick: a patch without sends pays one bit test a destination. */
static __attribute__((noinline)) uint32_t send_mask(const EngineState *e, uint32_t *hi) {
    uint32_t m = 0, h = 0;
    for (unsigned i = 0; i < NMODSEND; i++) {
        unsigned d = MSEND_DEST(e->send[i].dest);
        if (d < MDST_VOICE_BITS) m |= 1u << d;
        else if (d < MDST_COUNT) h |= 1u << (d - MDST_VOICE_BITS);
    }
    *hi = h;
    return m & ~1u;
}
/* The send helpers stay out of line and out of the hot path (2026-11-03): inlined at each destination they grew
 * voice_control_tick by 1.2 KB of code a patch without sends never runs, and as the tick and the voice's render take
 * turns in the FM1's I-cache, BINAURAL measured +8..10 PERF TEST points for it. */
#define SEND_COLD __attribute__((noinline, cold))
/* each layer's mask, worked out once a tick (synth_voice_begin_tick) before any voice reads it */
static uint32_t layer_sends[NLAYER];
static uint32_t layer_sends_hi[NLAYER];             /* bit (MDST_* - MDST_VOICE_BITS): the layer-wide ones, DLY / REV SEND */
int16_t synth_fx_send_mod[NLAYER][2];

/* the sends to d, summed, in the units of its own source times its own amount: scale 1 for a bipolar amount (-64..+63),
 * 2 for a 0..127 one (the send's -64..+63 doubled, either sign) */
static SEND_COLD int32_t send_sum_any(const SourceCtx *c, unsigned d, int32_t scale) {
    int32_t s = 0;
    for (unsigned i = 0; i < NMODSEND; i++) {
        uint8_t b = c->eng->send[i].dest;
        if (MSEND_DEST(b) == d && MSEND_SLOT(b) < NMODSLOT)
            s += slot_output(c, MSEND_SLOT(b), 0) * (bip(c->eng->send[i].amount) * scale);
    }
    return s;
}

/* the pitch sends to d, in cents: each through the PITCH MOD AMOUNT curve at twice the send (a negative send turns
 * the slot over) */
static SEND_COLD int32_t send_cents_any(const SourceCtx *c, unsigned d) {
    int32_t cents = 0;
    for (unsigned i = 0; i < NMODSEND; i++) {
        uint8_t b = c->eng->send[i].dest;
        if (MSEND_DEST(b) == d && MSEND_SLOT(b) < NMODSLOT) {
            int a = bip(c->eng->send[i].amount) * 2, sg = a < 0 ? -1 : 1;
            cents += sg * pitch_mod_cents(slot_output(c, MSEND_SLOT(b), 0), (uint8_t)clampi(a * sg, 0, 127));
        }
    }
    return cents;
}

/* the bit test at the destination, the call only for a send (most patches have none: a call a destination a voice
 * tick is what the FM1's XIP code would pay) */
#define send_sum(c, d, scale) ((((c)->sends >> (d)) & 1u) ? send_sum_any((c), (d), (scale)) : 0)
#define send_cents(c, d) ((((c)->sends >> (d)) & 1u) ? send_cents_any((c), (d)) : 0)

/* AMP LEVEL's sends: each a gain as AMP MOD's (amount 0 full level, full the slot alone sets it; a negative send the
 * slot turned over), multiplied together */
static SEND_COLD int32_t send_gain(const SourceCtx *c) {
    int32_t g = -1;                                 /* (none yet: the first is taken as it is, no rounding) */
    for (unsigned i = 0; i < NMODSEND; i++) {
        uint8_t b = c->eng->send[i].dest;
        if (MSEND_DEST(b) == MDST_AMP_LEVEL && MSEND_SLOT(b) < NMODSLOT) {
            int a = bip(c->eng->send[i].amount) * 2;
            int32_t d = (int32_t)clampi(a < 0 ? -a : a, 0, 127) * 258, l = slot_output(c, MSEND_SLOT(b), 0);
            l = clamp32(a < 0 ? 32767 - l : l, 0, 32767);
            int32_t t = clamp32(32767 - d + ((l * d) >> 15), 0, 32767);
            g = g < 0 ? t : (g * t) >> 15;
        }
    }
    return g < 0 ? 32767 : g;
}

/* The layer's MFX plays the layer's sum, so its sends follow one voice: the newest sounding (as the MOD page's portal
 * does), a tick behind. With none sounding they hold, so a tail rings out as it was; with no MFX send they are 0. */
static __attribute__((noinline)) void mfx_mod_follow(unsigned l, uint32_t sends, uint32_t hi) {
    int best = -1;
    if ((sends & (15u << MDST_MFX_K1)) == 0u)
        for (int k = 0; k < 4; k++) mfx_mod_off[l][k] = 0.f;
    if (hi == 0u)
        synth_fx_send_mod[l][0] = synth_fx_send_mod[l][1] = 0;
    if ((sends & (15u << MDST_MFX_K1)) == 0u && hi == 0u)
        return;
    for (int v = 0; v < POLYPHONY_VOICES; v++)
        if (!poly_bank[v].silent && poly_bank[v].layer == l &&
            (best < 0 || poly_bank[v].age_counter < poly_bank[best].age_counter))
            best = v;
    if (best < 0)
        return;
    if (sends & (15u << MDST_MFX_K1))
        for (int k = 0; k < 4; k++) mfx_mod_off[l][k] = (float)poly_bank[best].mfx_mod[k];
    if (hi)              /* (the DELAY / REVERB SEND: the same newest voice, 2026-11-03) */
        for (int k = 0; k < 2; k++) synth_fx_send_mod[l][k] = poly_bank[best].fx_mod[k];
}

/* ------------------------------------------------------------------ LFOs --- */

void update_poly_lfo_engines(SynthVoiceNode *voice, const VoiceInput *in) {
    const EngineState *e = &engine_state[voice->layer];
    SourceCtx c = {e, voice->layer, voice, in, 0, layer_sends[voice->layer & 1]};
    for (int i = 0; i < 2; i++) {
        const LfoParams *cfg = &e->lfo[i + 1];                      /* LFO2, LFO3 */
        LfoRuntimeState *state = &voice->poly_lfos.engines[i];
        lfo_advance(cfg, state, resolve_source(&c, cfg->speed_mod_src, 0) * bip(cfg->speed_mod_amt) +
                    send_sum(&c, MDST_LFO2_SPEED + (unsigned)i, 1), in->gate, SYNTH_CTRL_SHIFT,
                    in->frame);                                     /* REL: waits for the key release */
        /* once per tick: the shape also redraws its random values here, so it must not be read twice */
        int32_t raw = compute_morphed_lfo_fixed(state, cfg->shape);
        voice->lfo_q15[i] = (((raw * state->envelope_amp_q15) >> 15) * voice->lfo_gain_q15[i + 1]) >> 15;
    }
}

/* The gains below are Q15 with 32768 = unity, so a gain of "no change" leaves a level exactly as it was. */
#define GAIN_UNITY 32768

/* VEL > LEVEL (2026-10-26): an envelope's level for velocity v, unity at 127 and with an amount of 0. The amount sets
 * how far a soft note falls, on a square law: 127 goes to silence at velocity 1, ENV1_VEL_LEVEL_V9 (111) to -18 dB as
 * the old fixed velocity-to-amp curve did. */
static int32_t vel_level_q15(uint8_t amt, int velocity) {
    if (amt == 0u) return GAIN_UNITY;
    int32_t v = clampi(velocity, 1, 127);
    int32_t sq = (v * v * 16645) >> 13;                             /* v^2 * 32768 / 127^2 */
    if (sq > GAIN_UNITY) sq = GAIN_UNITY;
    return GAIN_UNITY - (((GAIN_UNITY - sq) * ((int32_t)amt * 258)) >> 15);   /* 258 / 32768 ~ 1 / 127 */
}

/* DEPTH MOD (2026-10-26): an LFO's depth, as AMP MOD sets the amp's level: amount 0 full depth, 127 the source alone
 * (unipolar; the keyboard from its bottom to its top). An LFO or a MOD slot reads as none: depth is worked out before
 * the LFOs it would scale. */
static int32_t lfo_depth_q15(const SourceCtx *c, uint8_t src, uint8_t amt) {
    if (amt == 0u || src == MSRC_NONE || (src >= MSRC_LFO1 && src <= MSRC_MODSLOT4)) return GAIN_UNITY;
    int32_t s = resolve_source(c, src, 0), d = (int32_t)amt * 258;
    s = clamp32(source_level_q15(c->eng, src, s), 0, GAIN_UNITY);
    return clamp32(GAIN_UNITY - d + ((s * d) >> 15), 0, GAIN_UNITY);
}

/* a voice's VEL > LEVEL and DEPTH MOD gains for the tick, before anything reads its envelopes or LFOs */
static void voice_mod_gains(SynthVoiceNode *voice, const SourceCtx *c) {
    const EngineState *e = c->eng;
    for (int i = 0; i < 3; i++)
        voice->env_vel_q15[i] = vel_level_q15(e->env_vel_level[i], c->in->velocity);
    for (int i = 0; i < 3; i++)
        voice->lfo_gain_q15[i] = lfo_depth_q15(c, e->lfo_depth_src[i], e->lfo_depth_amt[i]);
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
        SourceCtx c = {e, (int)l, NULL, NULL, 0, layer_sends[l]};
        lfo_advance(cfg, state, resolve_source(&c, cfg->speed_mod_src, 0) * bip(cfg->speed_mod_amt) +
                    send_sum(&c, MDST_LFO1_SPEED, 1), layer_key_held((uint8_t)l), SYNTH_CTRL_SHIFT, frame);
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

/* Knob smoothing (2026-10-21). The knobs are encoders a step at a time, and a cutoff step is 89 cents: turned slowly,
 * a filter sweep stepped audibly. CUTOFF, RESONANCE and MORPH of both filters and both oscillators' SCAN glide to the
 * knob instead, once a control tick per layer (every voice of the layer reads the same value), Q16 knob units, by
 * 1 / 2^KNOB_GLIDE_SHIFT a tick: a time constant of 64 ticks, 23 ms (a turn settles in about 70). A MIDI CC or a
 * sequencer lane writing the same knob glides as well. A patch or performance load (engine_load_count) jumps. */
#ifndef KNOB_GLIDE_SHIFT
#define KNOB_GLIDE_SHIFT 6
#endif
typedef struct {
    int32_t cutoff[2], res[2], morph[2], scan[2];   /* filter / oscillator 1 and 2, Q16 knob units */
} KnobGlide;
static KnobGlide knob_glide[NLAYER];
static uint8_t knob_glide_seen[NLAYER];
static uint8_t knob_glide_live;                     /* 0 until the first tick: start on the knobs, no sweep up from 0 */

static void glide_one(int32_t *g, uint8_t knob, int snap) {
    int32_t t = (int32_t)knob << 16;
    *g = snap ? t : *g + ((t - *g) >> KNOB_GLIDE_SHIFT);
}

static void glide_knobs(unsigned l) {
    const EngineState *e = &engine_state[l];
    KnobGlide *g = &knob_glide[l];
    uint8_t seen = engine_load_count[l];
    int snap = !knob_glide_live || seen != knob_glide_seen[l];
    knob_glide_seen[l] = seen;
    for (unsigned i = 0; i < 2; i++) {
        glide_one(&g->cutoff[i], e->filter[i].cutoff, snap);
        glide_one(&g->res[i], e->filter[i].resonance, snap);
        glide_one(&g->morph[i], e->filter[i].morph, snap);
        glide_one(&g->scan[i], e->osc[i].scan, snap);
    }
}

/* a glided knob in Q8 knob units */
static inline int32_t glide_q8(int32_t g_q16) { return g_q16 >> 8; }

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
    tuning_now = PERF_TUNING(&perf_state);
    tuning_root_now = PERF_TUNING_ROOT(&perf_state);
    analog_begin_tick();                            /* ANALOG's warm-up drift, shared by every voice */
    layer_mod_pass++;                                               /* the LFO1 pass is a pass of its own */
    for (unsigned l = 0; l < NLAYER; l++)
        layer_sends[l] = send_mask(&engine_state[l], &layer_sends_hi[l]);
    update_global_lfo1(frame);
    for (unsigned l = 0; l < NLAYER; l++) {
        SourceCtx c = {&engine_state[l], (int)l, NULL, NULL, 0, layer_sends[l]};
        watch_slot(&c);
        refresh_osc_mixer(l);
        glide_knobs(l);
        mfx_mod_follow(l, c.sends, layer_sends_hi[l]);
    }
    knob_glide_live = 1;
}

/* ------------------------------------------------------------------ voice --- */

/* ENV time modulation (spec 4.4), in envelope steps: KEY > TIME moves every stage, VEL > ATTACK (2026-11-03, was
 * VEL > TIME on every stage) the attack only */
static int time_mod(const EnvParams *p, const VoiceInput *in) {
    return (bip(p->key_to_time) * (in->note - 60)) >> 6;             /* about +-60 at the keyboard ends */
}
static int attack_mod(const EnvParams *p, const VoiceInput *in) {
    return (bip(p->vel_to_attack) * in->velocity) >> 7;              /* about +-64 at full velocity */
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

/* An oscillator's base note: the keyboard note (key_cents: its TUNING offset) through key track, plus transpose and
 * the octave knob. Key track can land between semitones, so the remainder comes back as cents. KEY TRACK scales the
 * tuning with the keyboard: an oscillator that does not track (a drone) stays put. */
static void base_pitch(const OscParams *o, const VoiceInput *in, int32_t key_cents, int *note, int *cents) {
    int octave = ((int)o->octave * 7 >> 7) - 3;                      /* knob 0..127 -> -3..+3 */
    int32_t track_cents = ((((int32_t)in->note - 60) * 100 + key_cents) * key_track_q15(o->key_track)) >> 15;
    *note = clampi(60 + in->transpose + octave * 12 + track_cents / 100, 0, 127);
    *cents = track_cents % 100;
}

/* CHOKE (a voice over its layer's limit, or still on its layer's old architecture): the level fades to 0 over
 * 2^CHOKE_SHIFT control ticks, 256 samples (5.8 ms), then the voice is silenced and reset. */
#define CHOKE_SHIFT 5

/* BINAURAL right ear: 0.125 Hz per BEAT step, in Q16 */
#define BIN_BEAT_STEP_Q16 8192u

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

/* the cutoff of a filter in Q8 knob units: knob plus a mod offset plus KEY TRACK, clamped to the whole knob (20 Hz ..
 * 14 kHz). KEY TRACK pivots on C4; a knob step is 89.3 cents, so a semitone at 127 is 286.7 Q8 = 127 * 578 / 256. */
static int cutoff_q8(const SourceCtx *c, int fi, int32_t mod_q8) {   /* FILTER fi's glided knob */
    int32_t kt = c->eng->filter_key_track[fi];
    if (kt != 0 && c->in)
        mod_q8 += (((int32_t)c->in->note - 60) * kt * 578) >> 8;
    return clampi((int)glide_q8(knob_glide[c->layer].cutoff[fi]) + (int)mod_q8, 0, 127 << 8);
}

/* a filter's own mod source and amount (spec 4.2), as a Q8 cutoff offset: the amount 0..127, positive only (2026-10-26,
 * was bipolar; twice the steps). Kept in Q8 so the filter interpolates
 * between the table steps and a modulated cutoff does not zipper. */
static int32_t filter_mod_q8(const SourceCtx *c, const FilterParams *f, int fi) {
    return (resolve_source(c, f->mod_src, 0) * (int32_t)f->mod_amt + send_sum(c, fi ? MDST_F2_CUTOFF : MDST_F1_CUTOFF, 2)) >> 8;
}

/* A FILTER page's type morph plus its own mod source and amount (spec 4.2, every architecture), the knob's way round
 * (dsp/filter.h). An amount of 0 resolves no source. */
static int filter_morph(const SourceCtx *c, const FilterParams *f, int fi) {
    int m = (int)glide_q8(knob_glide[c->layer].morph[fi]);              /* Q8 knob units (2026-10-21) */
    unsigned d = fi ? MDST_F2_MORPH : MDST_F1_MORPH;
    if (f->morph_mod_amt != BIPOLAR_CENTER || ((c->sends >> d) & 1u))
        m = clampi(m + (int)((resolve_source(c, f->morph_mod_src, 0) * bip(f->morph_mod_amt) + send_sum(c, d, 1)) >> 7), 0,
                   127 << 8);
    return m;
}

/* a resonance plus its MOD SENDS (2026-11-03: full moves the knob all the way) and ANALOG's offset for the voice */
static int res_plus(const SourceCtx *c, int fi, int off) {   /* FILTER fi's glided knob, Q8 knob units */
    int r = (int)glide_q8(knob_glide[c->layer].res[fi]);
    r += (int)(send_sum(c, fi ? MDST_F2_RES : MDST_F1_RES, 2) >> 7);
    return clampi(r + (off << 8), 0, 127 << 8);
}

/* a filter's DRIVE plus its MOD SENDS (2026-11-03), knob units */
static int drive_plus(const SourceCtx *c, const FilterParams *f, int fi) {
    unsigned d = fi ? MDST_F2_DRIVE : MDST_F1_DRIVE;
    return ((c->sends >> d) & 1u) ? clampi((int)f->drive + (int)(send_sum_any(c, d, 2) >> 15), 0, 127) : (int)f->drive;
}

/* One core from a FILTER page, its morph modulated through c (the BINAURAL right ear passes its phase-offset context),
 * res_off ANALOG's resonance offset. */
static void filter_prepare_one(SvfCore *k, const SourceCtx *c, const FilterParams *f, int fi, int cut_q8, int res_off,
                               int snap) {
    filter_core_prepare(k, cut_q8, res_plus(c, fi, res_off), filter_morph(c, f, fi), drive_plus(c, f, fi), f->flavour,
                        SYNTH_CTRL_SHIFT, snap);
}

/* ANALOG's pitch offset: a frequency times a ratio near 1 (in float, so a fraction of a cent counts) */
static uint32_t analog_freq(uint32_t f_q16, float ratio) {
    float f = (float)f_q16 * ratio;
    return f >= (float)PITCH_MAX_Q16 ? PITCH_MAX_Q16 : (uint32_t)f;
}

/* Wavetable scan: knob 0..127 plus its mod and an offset, kept in Q8 so a mod sweep (LFO, envelope) moves smoothly
 * instead of in whole knob steps, then spread continuously across the 8 morph frames. */
static int scan_morph_q12(const SourceCtx *c, const OscParams *o, int oi, int offset_knob) {   /* OSC oi, glided */
    int32_t scan_q8 = glide_q8(knob_glide[c->layer].scan[oi]) + ((int32_t)offset_knob << 8);
    scan_q8 += (resolve_source(c, o->scan_mod_src, 0) * o->scan_mod_amt +
                send_sum(c, oi ? MDST_OSC2_SCAN : MDST_OSC1_SCAN, 2)) >> 7;
    return scan_q8_to_morph_q12(clampi(scan_q8, 0, 127 << 8));
}

/* One path's mix-bus gains (MIX_L, MIX_R) from its Q15 left and right gains: the pair times `dry`. The FX sends are
 * taken from the layer's sum (synth_runtime.c, after the layer's MFX). */
static void mix_gains(float *g, int32_t l_q15, int32_t r_q15, float dry) {
    g[MIX_L] = (float)l_q15 * dry;
    g[MIX_R] = (float)r_q15 * dry;
}

void voice_control_tick(SynthVoiceNode *voice, const VoiceInput *in) {
    const EngineState *e = &engine_state[voice->layer];
    SourceCtx c = {e, voice->layer, voice, in, 0, layer_sends[voice->layer & 1]};
    uint8_t arch = voice->arch;
    voice->mod_pass++;                                              /* this voice's pass: its slots run afresh */
    SPROF_DECL;
    SPROF_START();

    /* ANALOG (dsp/analog.h): this voice's offsets for the tick. 0 skips all of it (the patch plays as before, exactly) */
    int analog = e->analog != 0u;
    const AnalogTick *an = analog ? analog_voice_tick(&voice->analog, e->analog) : NULL;
    int an_res1 = analog ? an->res1 : 0, an_res2 = analog ? an->res2 : 0;
    int32_t an_cut2 = analog ? an->cut2_q8 : 0;
    voice->amp_env.time_dk_q16 = voice->mod_env.time_dk_q16 = voice->aux_env.time_dk_q16 =
        analog ? an->time_k_q16 - 65536 : 0;

    /* VEL > LEVEL and DEPTH MOD, then LFO2/3 first, so their phases never stall while the voice is still rendering. */
    voice_mod_gains(voice, &c);
    update_poly_lfo_engines(voice, in);
    SPROF_LAP(SPROF_C_LFO);

    /* 1. Envelopes, a tick at a time. ENV2 and ENV3 invert when read as mod sources (see resolve_source). In STACK,
     * ENV2 is also stack B's amp envelope, read uninverted as ENV1 is. */
    int32_t amp_env_q15 = adsr_advance(&voice->amp_env, &e->env[0], time_mod(&e->env[0], in), attack_mod(&e->env[0], in),
                                       voice->release_boost_q15, SYNTH_CTRL_SHIFT, in->frame);
    int32_t env2_q15 = adsr_advance(&voice->mod_env, &e->env[1], time_mod(&e->env[1], in), attack_mod(&e->env[1], in),
                                    voice->release_boost_q15, SYNTH_CTRL_SHIFT, in->frame);
    adsr_advance(&voice->aux_env, &e->env[2], time_mod(&e->env[2], in), attack_mod(&e->env[2], in),
                 voice->release_boost_q15, SYNTH_CTRL_SHIFT, in->frame);
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

    /* 2. ENV1 (and ENV2 for STACK B), each at its VEL > LEVEL, is hardwired to the amp. The amp's secondary mod source
     * scales it: depth 0 = no effect, depth 127 = the source alone sets the level (a unipolar source; negative values
     * count as 0; an inverted envelope reads 1 -> 0). A unison stack and a choke fade scale the level too. */
    const AmpParams *a = &e->amp;
    int32_t amp_gain_q15 = 32767;
    if (a->amp_mod_src != MSRC_NONE) {
        int32_t depth_q15 = (int32_t)a->amp_mod_depth * 258;
        int32_t mod_uni_q15 = resolve_source(&c, a->amp_mod_src, 0);
        if (src_is_inverted_env(e, a->amp_mod_src)) mod_uni_q15 += 32767;
        mod_uni_q15 = clamp32(mod_uni_q15, 0, 32767);
        amp_gain_q15 = clamp32(32767 - depth_q15 + ((mod_uni_q15 * depth_q15) >> 15), 0, 32767);
    }
    if ((c.sends >> MDST_AMP_LEVEL) & 1u)
        amp_gain_q15 = a->amp_mod_src == MSRC_NONE ? send_gain(&c) : (amp_gain_q15 * send_gain(&c)) >> 15;
    int32_t level_q15 = (amp_gain_q15 * voice->unison_gain_q15) >> 15;
    if (voice->choke) level_q15 = (level_q15 * voice->choke_q15) >> 15;
    if (analog) level_q15 = (level_q15 * an->level_q15) >> 15;      /* (a little over 32767 is fine: float from here) */
    int32_t vol_q15 = (((amp_env_q15 * voice->env_vel_q15[0]) >> 15) * level_q15) >> 15;
    int32_t vol_b_q15 = (((amp_b_env_q15 * voice->env_vel_q15[1]) >> 15) * level_q15) >> 15;

    /* 3. Oscillators. Pitch is in cents: fine tune, pitch bend, key track and pitch mod add together. Portamento and
     * unison detune come from the voice, not the patch: every oscillator of the voice moves together. */
    const OscParams *o1 = &e->osc[0];
    const OscParams *o2 = &e->osc[1];
    int bend_range = clampi(perf_state.bend_range, 0, 24);          /* semitones at full wheel */
    int32_t base_cents = (in->pitch_bend * bend_range * 100) / 64 + in->glide_cents + in->detune_cents +
                         tune_cents_now;              /* GLOBAL > MASTER TUNE (synth_voice_begin_tick) */
    int32_t key_cents = tuning_key_cents(tuning_now, tuning_root_now, (uint8_t)in->note);   /* GLOBAL > TUNING */

    int note1, track1;
    base_pitch(o1, in, key_cents, &note1, &track1);
    int32_t pitch1_cents = osc_fine_cents(o1->fine) + track1 + base_cents;
    uint32_t freq1_q16 = apply_cents_q16(mtof_q16(note1),
                                         pitch1_cents + pitch_mod_cents(resolve_source(&c, o1->pitch_mod_src, 0), o1->pitch_mod_amt) +
                                         send_cents(&c, MDST_OSC1_PITCH));
    if (analog) freq1_q16 = analog_freq(freq1_q16, an->pitch1);
    osc_select_wave(&voice->osc1, o1->wavebank);
    voice->osc1.morph_q12 = scan_morph_q12(&c, o1, 0, 0);

    uint32_t freq2_q16 = 0;
    if (varch_uses_osc2(arch)) {
        int note2, track2;
        base_pitch(o2, in, key_cents, &note2, &track2);
        freq2_q16 = apply_cents_q16(mtof_q16(note2), osc_fine_cents(o2->fine) + track2 + base_cents
                                    + pitch_mod_cents(resolve_source(&c, o2->pitch_mod_src, 0), o2->pitch_mod_amt)
                                    + send_cents(&c, MDST_OSC2_PITCH));
        if (analog) freq2_q16 = analog_freq(freq2_q16, an->pitch2);
        osc_select_wave(&voice->osc2, o2->wavebank);
        voice->osc2.morph_q12 = scan_morph_q12(&c, o2, 1, 0);
    }

    /* 4. OSCMIXER. Only the sources the architecture uses are resolved; mod offsets are in 0..127 knob units. */
    const OscMixerParams *mx = &e->oscmix;
    const FilterMixerParams *fm = &e->fltmix;
    int32_t balance_off = 0, xmod_off = 0, spread = 0;
    int32_t noise_off = (resolve_source(&c, mx->noise_mod_src, 0) * bip(mx->noise_mod_amt) +
                         send_sum(&c, MDST_NOISE_LEVEL, 1)) >> 15, colour_off = 0;
    if (mx->noise_col_mod_amt != BIPOLAR_CENTER || ((c.sends >> MDST_NOISE_COLOUR) & 1u))
        colour_off = (resolve_source(&c, mx->noise_col_mod_src, 0) * bip(mx->noise_col_mod_amt) +
                      send_sum(&c, MDST_NOISE_COLOUR, 1)) >> 15;
    if (arch == VARCH_DUAL_OSC || arch == VARCH_FULL || arch == VARCH_DUAL_SPREAD)
        balance_off = (resolve_source(&c, mx->mix_mod_src, 0) * mx->mix_mod_amt + send_sum(&c, MDST_OSC_MIX, 2)) >> 15;
    if (varch_uses_osc2(arch))
        xmod_off = (resolve_source(&c, mx->xmod_mod_src, 0) * mx->xmod_mod_amt + send_sum(&c, MDST_XMOD, 2)) >> 15;
    if (arch == VARCH_SPREAD || arch == VARCH_DUAL_SPREAD)
        spread = clampi(mx->spread + ((resolve_source(&c, mx->spread_mod_src, 0) * mx->spread_mod_amt +
                                       send_sum(&c, MDST_SPREAD, 2)) >> 15), 0, 127);
    osc_mixer_prepare(&voice->mix, &osc_mixer[voice->layer], freq1_q16, freq2_q16,
                      (int)balance_off, (int)xmod_off, (int)noise_off, (int)colour_off);
    osc_prepare(&voice->osc1, freq1_q16, (int)spread);
    if (varch_uses_osc2(arch))
        osc_prepare(&voice->osc2, freq2_q16, arch == VARCH_DUAL_SPREAD ? (int)spread : 0);
    SPROF_LAP(SPROF_C_OSC);

    /* 5. Filters: each architecture prepares only the cores its path runs. A new note's first tick sets the
     * coefficients at once; later ticks ramp to them. */
    const FilterParams *f1 = &e->filter[0];
    const FilterParams *f2 = &e->filter[1];
    SvfCore *core = voice->synth_filters.core;
    int32_t f1_mod_q8 = filter_mod_q8(&c, f1, 0) + (analog ? an->cut1_q8 : 0);
    int snap = voice->snap;

    /* BINAURAL: the two phase offsets, each knob plus the shared PHASE MOD, wrapping round the cycle */
    int32_t phase_mod = 0;
    if (arch == VARCH_BINAURAL)
        phase_mod = (resolve_source(&c, fm->bin_phase_mod_src, 0) * bip(fm->bin_phase_mod_amt) +
                     send_sum(&c, MDST_PHASE, 1)) >> 15;

    switch (arch) {
    case VARCH_DUAL_FILTER:
    case VARCH_FULL: {
        /* FILTER MIXER: balance and routing with their own mod sources (spec 4.2) */
        int32_t balance_mod = (resolve_source(&c, fm->balance_mod_src, 0) * bip(fm->balance_mod_amt) +
                               send_sum(&c, MDST_BALANCE, 1)) >> 15;
        int32_t routing_mod = (resolve_source(&c, fm->routing_mod_src, 0) * bip(fm->routing_mod_amt) +
                               send_sum(&c, MDST_ROUTING, 1)) >> 15;
        dual_filter_prepare(&voice->synth_filters,
                            cutoff_q8(&c, 0, f1_mod_q8), res_plus(&c, 0, an_res1), filter_morph(&c, f1, 0),
                            drive_plus(&c, f1, 0), f1->flavour,
                            cutoff_q8(&c, 1, filter_mod_q8(&c, f2, 1) + an_cut2), res_plus(&c, 1, an_res2),
                            filter_morph(&c, f2, 1), drive_plus(&c, f2, 1), f2->flavour,
                            clampi(fm->routing + (int)routing_mod, 0, 127), clampi(fm->balance + (int)balance_mod, 0, 127),
                            SYNTH_CTRL_SHIFT, snap);
        break;
    }
    case VARCH_STACK: {
        /* ENV3 is hardwired to both cutoffs (FILTER MIXER knob 4), on top of each filter's own mod */
        int32_t env3_q8 = (resolve_source(&c, MSRC_ENV3, 0) * bip(fm->env3_cutoff)) >> 7;
        filter_prepare_one(&core[0], &c, f1, 0, cutoff_q8(&c, 0, f1_mod_q8 + env3_q8), an_res1, snap);
        filter_prepare_one(&core[1], &c, f2, 1, cutoff_q8(&c, 1, filter_mod_q8(&c, f2, 1) + env3_q8 + an_cut2), an_res2,
                           snap);
        break;
    }
    case VARCH_BINAURAL: {
        /* The right ear: OSC1's settings again, BEAT Hz higher, its LFO mod read OSC phase offset ahead; FILTER1's
         * settings again, its LFO mod read FILTER phase offset ahead. CUTOFF OFFSET opens one ear and closes the other. */
        SourceCtx co = c, cf = c;
        co.lfo_offset = (uint32_t)((mx->bin_mod_phase + phase_mod) & 127) << 25;
        cf.lfo_offset = (uint32_t)((fm->bin_mod_phase + phase_mod) & 127) << 25;
        uint32_t freq_r_q16 = apply_cents_q16(mtof_q16(note1), pitch1_cents
                                              + pitch_mod_cents(resolve_source(&co, o1->pitch_mod_src, 0), o1->pitch_mod_amt)
                                              + send_cents(&co, MDST_OSC1_PITCH));
        if (analog) freq_r_q16 = analog_freq(freq_r_q16, an->pitch2);   /* the right ear drifts on its own */
        freq_r_q16 += (uint32_t)mx->bin_beat * BIN_BEAT_STEP_Q16;
        if (freq_r_q16 > PITCH_MAX_Q16) freq_r_q16 = PITCH_MAX_Q16;
        osc_select_wave(&voice->osc2, o1->wavebank);
        voice->osc2.morph_q12 = scan_morph_q12(&co, o1, 0, bip(mx->bin_scan_offset));
        osc_prepare(&voice->osc2, freq_r_q16, 0);

        /* CUTOFF L/R plus its own mod source and amount, in knob units, held to the knob's -64..+63 */
        int offset = bip(fm->bin_cutoff_offset);
        if (fm->bin_cutoff_mod_amt != BIPOLAR_CENTER || ((c.sends >> MDST_CUTOFF_LR) & 1u))
            offset = clampi(offset + (int)((resolve_source(&c, fm->bin_cutoff_mod_src, 0) * bip(fm->bin_cutoff_mod_amt) +
                                            send_sum(&c, MDST_CUTOFF_LR, 1)) >> 15), -64, 63);
        int32_t offset_q8 = (int32_t)offset << 7;                   /* up to +-32 cutoff steps per ear */
        filter_prepare_one(&core[0], &c, f1, 0, cutoff_q8(&c, 0, f1_mod_q8 - offset_q8), an_res1, snap);
        filter_prepare_one(&core[1], &cf, f1, 0, cutoff_q8(&c, 0, filter_mod_q8(&cf, f1, 0) + offset_q8 + an_cut2), an_res2,
                           snap);
        break;
    }
    default:                                                        /* SINGLE, DUAL OSC, SPREAD: FILTER1 alone */
        filter_prepare_one(&core[0], &c, f1, 0, cutoff_q8(&c, 0, f1_mod_q8), an_res1, snap);
        break;
    }
    voice->snap = 0;
    SPROF_LAP(SPROF_C_FILT);

    /* 6. AMP (spec 4.6): each level ramps to this tick's value one step per sample (no zipper on the envelope), then
     * the drive stage, then pan with its mod source. Float from here (the FM1's FPU beats its integer multiplies). */
    const float tick_inv = 1.f / (float)SYNTH_CTRL_PERIOD, q15_inv = 1.f / 32768.f;
    voice->vol_step = ((float)vol_q15 * q15_inv - voice->vol) * tick_inv;
    voice->vol_b_step = ((float)vol_b_q15 * q15_inv - voice->vol_b) * tick_inv;
    voice->drive = a->drive;
    if (a->drive) {
        if (a->drive_model != voice->drv.model)   /* each model reads its state its own way: a new one starts clean */
            voice->drv_s[0][0] = voice->drv_s[0][1] = voice->drv_s[1][0] = voice->drv_s[1][1] = 0.f;
        amp_drive_prepare(&voice->drv, a->drive, a->drive_model);
    }
    int pan = clampi(in->pan + (int)((resolve_source(&c, a->pan_mod_src, 0) * a->pan_mod_depth +
                                      send_sum(&c, MDST_PAN, 2)) >> 15), 0, 127);
    /* 7. AMP: the layer's routing weight and the mix gain (the delay and reverb sends: synth_runtime.c, from the
     * layer's sum) */
    float route = (float)voice->route_q15 * q15_inv;
    float dry = route * ((float)MIX_HEADROOM_Q15 * q15_inv) * q15_inv;
    if (arch == VARCH_STACK) {
        /* FILTER MIXER: BLEND (knob 1 + its mod) weighs A against B; both sit at the one pan, the layers play as one
         * (WIDTH, which pulled them apart, removed 2026-11-06, user) */
        int32_t blend_mod = (resolve_source(&c, fm->balance_mod_src, 0) * bip(fm->balance_mod_amt) +
                             send_sum(&c, MDST_BALANCE, 1)) >> 15;
        int32_t gain_b = clampi(fm->balance + (int)blend_mod, 0, 127) * 258, gain_a = 32768 - gain_b;
        int32_t pl, pr;
        amp_pan_gains(&voice->master_amp, pan, &pl, &pr);
        mix_gains(voice->mix_a, (pl * gain_a) >> 15, (pr * gain_a) >> 15, dry);
        mix_gains(voice->mix_b, (pl * gain_b) >> 15, (pr * gain_b) >> 15, dry);
    } else if (arch == VARCH_BINAURAL) {
        /* WIDTH: 127 keeps each ear on its own side, 0 sums them; the pan then balances the pair */
        int32_t pl, pr, w = (int32_t)fm->bin_width * 258;
        amp_pan_gains(&voice->master_amp, pan, &pl, &pr);
        mix_gains(voice->mix_a, (pl * ((32768 + w) >> 1)) >> 15, (pr * ((32768 - w) >> 1)) >> 15,   /* left ear */
                  dry);
        mix_gains(voice->mix_b, (pl * ((32768 - w) >> 1)) >> 15, (pr * ((32768 + w) >> 1)) >> 15,   /* right ear */
                  dry);
    } else {
        int32_t pl, pr;
        amp_pan_gains(&voice->master_amp, pan, &pl, &pr);
        mix_gains(voice->mix_a, pl, pr, dry);
        mix_gains(voice->mix_b, 0, 0, 0.f);              /* no path B (the AMP page's pan reads both) */
    }
    SPROF_LAP(SPROF_C_AMP);
    if (c.sends & (15u << MDST_MFX_K1)) {             /* MOD SENDS to the MFX knobs: full moves a knob all the way */
        for (int k = 0; k < 4; k++)
            voice->mfx_mod[k] = (int16_t)clampi((int)(send_sum(&c, MDST_MFX_K1 + (unsigned)k, 2) >> 15), -127, 127);
    }
    if (layer_sends_hi[voice->layer & 1]) {           /* .. and the DELAY / REVERB SEND, the same way */
        for (int k = 0; k < 2; k++)
            voice->fx_mod[k] = (int16_t)clampi((int)(send_sum_any(&c, MDST_DLY_SEND + (unsigned)k, 2) >> 15), -127, 127);
    }
    voice->glide_mod = (int16_t)((c.sends >> MDST_GLIDE) & 1u ?        /* GLIDE: read by the next glide step */
                                 clampi((int)(send_sum_any(&c, MDST_GLIDE, 2) >> 15), -127, 127) : 0);
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

/* A driven path: the amp level (ramped from vol by step) and the drive, in place over its block; the mix loops then
 * take it at level 1. Returns the level at the block's end. (One drive loop in the firmware, amp_drive_block: the
 * FM1's flash has no room for one inlined in every mix loop.) */
static float level_drive(SynthVoiceNode *voice, int path, float *y, int n, float vol, float step) {
    for (int i = 0; i < n; i++) {
        y[i] *= vol;
        vol += step;
    }
    amp_drive_block(&voice->drv, voice->drv_s[path], y, n);
    return vol;
}

/* one path: amp level (ramped), drive, then into its layer's pair on the mix: two multiply-adds by the tick's gains
 * (pan, routing and mix gain folded together). SINGLE, DUAL OSC, SPREAD and DUAL FILTER end here. */
static void finish_mono_block(SynthVoiceNode *voice, float *y, float *mix, int n) {
    float vol = voice->vol, step = voice->vol_step, end = 0.f;
    const float g0 = voice->mix_a[MIX_L], g1 = voice->mix_a[MIX_R];
    if (voice->drive) {
        end = level_drive(voice, 0, y, n, vol, step);
        vol = 1.f;
        step = 0.f;
    }
    for (int i = 0; i < n; i++, mix += MIX_N) {
        float s = y[i] * vol;
        vol += step;
        mix[MIX_L] += s * g0;
        mix[MIX_R] += s * g1;
    }
    voice->vol = voice->drive ? end : vol;
}

/* two paths, each through its own amp level and drive, each with its own gains. STACK: B has ENV2's level; BINAURAL:
 * both ears share ENV1's. */
static void finish_pair_block(SynthVoiceNode *voice, float *ya, float *yb, int own_b_level, float *mix, int n) {
    float vol = voice->vol, step = voice->vol_step, vol_b = voice->vol_b, step_b = voice->vol_b_step;
    const float *ga = voice->mix_a, *gb = voice->mix_b;
    int drive = voice->drive;
    float end = 0.f, end_b = vol_b;
    if (drive) {
        end = level_drive(voice, 0, ya, n, vol, step);
        if (own_b_level)
            end_b = level_drive(voice, 1, yb, n, vol_b, step_b);
        else
            level_drive(voice, 1, yb, n, vol, step);
        vol = vol_b = 1.f;
        step = step_b = 0.f;
    }
    for (int i = 0; i < n; i++, mix += MIX_N) {
        float va = vol, vb = va;
        vol += step;
        if (own_b_level) {
            vb = vol_b;
            vol_b += step_b;
        }
        float a = ya[i] * va, b = yb[i] * vb;
        mix[MIX_L] += a * ga[MIX_L] + b * gb[MIX_L];
        mix[MIX_R] += a * ga[MIX_R] + b * gb[MIX_R];
    }
    voice->vol = drive ? end : vol;
    voice->vol_b = drive ? end_b : vol_b;
}

/* SINGLE and SPREAD: OSC1 (SPREAD: its 4 detuned playheads, set by osc_prepare) + sub + noise -> FILTER1 */
static void render_single(SynthVoiceNode *v, float *mix, int n) {
    int32_t o[VB];
    float x[VB];
    SPROF_DECL;
    SPROF_START();
    osc_render_block(&v->osc1, o, n);
    SPROF_LAP(SPROF_OSC);
    osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, v->noise_at, o, n);
    SPROF_LAP(SPROF_EXTRAS);
    q15_to_float(o, x, n);
    filter_core_render_block(&v->synth_filters.core[0], x, x, n);
    SPROF_LAP(SPROF_FILTER);
    finish_mono_block(v, x, mix, n);
    SPROF_LAP(SPROF_AMP);
}

/* DUAL OSC and FULL: OSC1 + cross-modulated OSC2 by balance, + sub + noise, as the filter's float input. With a PCM
 * wave or the VCO (bank 0) on either oscillator: OSC1 as a block, OSC2 against it (osc_mixer_osc2_block), mixed as
 * osc_mixer_render mixes; otherwise the cross-modulation reads OSC1 sample by sample (2026-11-02: the VCO sample by
 * sample cost DUAL OSC ~55 profile ticks) */
static void dual_osc_block(SynthVoiceNode *v, float *x, int n) {
    int32_t o[VB];
    if (v->osc1.pcm.on || v->osc2.pcm.on || v->osc1.vco || v->osc2.vco) {
        int32_t w1[VB], w2[VB];
        osc_render_block(&v->osc1, w1, n);
        osc_mixer_osc2_block(&v->mix, w1, &v->osc2, w2, n);
        int32_t g1 = v->mix.osc1_gain_q15, g2 = v->mix.osc2_gain_q15;
        for (int i = 0; i < n; i++)
            o[i] = (((int32_t)(int16_t)w1[i] * g1) >> 15) + (((int32_t)(int16_t)w2[i] * g2) >> 15);
        osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, v->noise_at, o, n);
    } else {
        for (int i = 0; i < n; i++)
            o[i] = osc_mixer_render(&v->mix, &v->osc1, &v->osc2, &v->sub_phase, v->noise_at + i);
    }
    q15_to_float(o, x, n);
}

/* DUAL OSC: the two oscillators -> FILTER1 */
static void render_dual_osc(SynthVoiceNode *v, float *mix, int n) {
    float x[VB];
    dual_osc_block(v, x, n);
    filter_core_render_block(&v->synth_filters.core[0], x, x, n);
    finish_mono_block(v, x, mix, n);
}

/* DUAL FILTER: OSC1 + sub + noise -> FILTER1 / FILTER2, serial .. parallel, balanced */
static void render_dual_filter(SynthVoiceNode *v, float *mix, int n) {
    int32_t o[VB];
    float x[VB];
    osc_render_block(&v->osc1, o, n);
    osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, v->noise_at, o, n);
    q15_to_float(o, x, n);
    dual_filter_render_block(&v->synth_filters, x, x, n);
    finish_mono_block(v, x, mix, n);
}

/* FULL: the two oscillators -> FILTER1 / FILTER2, serial .. parallel, balanced */
static void render_full(SynthVoiceNode *v, float *mix, int n) {
    float x[VB];
    dual_osc_block(v, x, n);
    dual_filter_render_block(&v->synth_filters, x, x, n);
    finish_mono_block(v, x, mix, n);
}

/* DUAL SPREAD: OSC1 and OSC2, each its 4 detuned playheads, as blocks, by balance (no cross-mod), + sub + noise
 * -> FILTER1 */
static void render_dual_spread(SynthVoiceNode *v, float *mix, int n) {
    int32_t w1[VB], w2[VB];
    float x[VB];
    osc_render_block(&v->osc1, w1, n);
    osc_render_block(&v->osc2, w2, n);
    int32_t g1 = v->mix.osc1_gain_q15, g2 = v->mix.osc2_gain_q15;
    for (int i = 0; i < n; i++)
        w1[i] = ((w1[i] * g1) >> 15) + ((w2[i] * g2) >> 15);
    osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, v->noise_at, w1, n);
    q15_to_float(w1, x, n);
    filter_core_render_block(&v->synth_filters.core[0], x, x, n);
    finish_mono_block(v, x, mix, n);
}

/* STACK: A = OSC1 + sub + noise -> FILTER1 (ENV1), B = cross-modulated OSC2 -> FILTER2 (ENV2) */
static void render_stack(SynthVoiceNode *v, float *mix, int n) {
    int32_t w1[VB], w2[VB];
    float a[VB], b[VB];
    osc_render_block(&v->osc1, w1, n);
    osc_mixer_osc2_block(&v->mix, w1, &v->osc2, w2, n);   /* (a bank: osc_mixer_osc2 sample by sample) */
    osc_mixer_extras_block(&v->mix, &v->osc1, &v->sub_phase, v->noise_at, w1, n);   /* A: w1 + sub + noise */
    q15_to_float(w1, a, n);
    for (int i = 0; i < n; i++) b[i] = (float)w2[i];
    filter_core_render_block(&v->synth_filters.core[0], a, a, n);
    filter_core_render_block(&v->synth_filters.core[1], b, b, n);
    finish_pair_block(v, a, b, 1, mix, n);
}

/* BINAURAL: left ear OSC1 -> core 0, right ear (OSC2's playhead running OSC1's settings) -> core 1. The sub is
 * shared, each ear reads its own noise (ear 1 for the right) so the noise decorrelates. */
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
    osc_mixer_noise_block(&v->mix, 0, v->noise_at, l, n);
    osc_mixer_noise_block(&v->mix, 1, v->noise_at, r, n);
    q15_to_float(l, fl, n);
    q15_to_float(r, fr, n);
    filter_core_render_block(&v->synth_filters.core[0], fl, fl, n);
    filter_core_render_block(&v->synth_filters.core[1], fr, fr, n);
    finish_pair_block(v, fl, fr, 0, mix, n);
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
    render_single,       /* STRIKE (retired 2026-11-04: loads as SPREAD; never reached) */
    render_full,         /* FULL */
    render_dual_spread,  /* DUAL SPREAD */
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
        if (voice->osc1.pcm.on) pcm_idle(&voice->osc1.pcm, (int)k);   /* PCM loops keep turning too */
        if (voice->osc2.pcm.on) pcm_idle(&voice->osc2.pcm, (int)k);
        voice->sub_phase = ph >> (voice->osc1.center_step > voice->mix.sub_step * 3u ? 2 : 1);
        return;
    }
    mix += MIX_LAYER(voice->layer < NLAYER ? voice->layer : 0);   /* its layer's pair */
    render_arch[voice->arch < VARCH_COUNT ? voice->arch : VARCH_SINGLE](voice, mix, n > (int)VB ? (int)VB : n);
}

/* Portamento: moves the glide offset toward its target by one control tick's worth of per-sample steps. */
void voice_glide_advance(SynthVoiceNode *v) {
    int32_t step = v->glide_step_q16 << SYNTH_CTRL_SHIFT;
    if (v->glide_mod != 0 && v->glide_q16 != 0) {   /* GLIDE's MOD SENDS: the step as PORTA TIME + the send would set it */
        int32_t pt = perf_state.porta_time[v->layer < NLAYER ? v->layer : 0u], pm = clampi(pt + v->glide_mod, 0, 127);
        float t = 441.f + (float)(pt * pt) * 13.f, tm = 441.f + (float)(pm * pm) * 13.f;   /* (synth_runtime.c glide_to) */
        float st = (float)step * (t / tm);
        step = st > 2147483000.f ? 2147483000 : st < 1.f ? 1 : (int32_t)st;
    }
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
