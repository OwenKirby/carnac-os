#include "synth_prof.h"
#include <stddef.h>
#include "synth_runtime.h"
#include "global.h"
#include "midi_io.h"
#include "voice_allocator.h"
#include "synth_voice.h"
#include "envelopes.h"
#include "lfo.h"
#include "midi_clock.h"
#include "preset.h"
#include "attacks.h"
#include "amplifier.h"
#include "fx.h"
#include "maq_seq.h"
#include "arp_seq.h"

#define Q15_MAX 32767
#define Q15_MIN (-32768)
/* Master limiter: the ceiling, -1 dBFS, and the release, 1/4096 of the gap per frame (about 93 ms). The peak
 * envelope is kept in Q8 so the release does not stall just above the ceiling. */
#define LIMIT_CEILING 29204
#define LIMIT_RELEASE_SHIFT 12
/* AUTO DECAY: a note struck within this many frames of the previous note on its layer counts as a repeat.
 * 0.5 s at 44.1 kHz. */
#define AUTO_DECAY_WINDOW 22050u
/* UNISON_STACK: each voice of an n-voice stack plays at 1 / sqrt(n), Q15, so the stack is about as loud as one voice */
static const int16_t unison_gain_q15[POLYPHONY_VOICES] = { 32767, 23170, 18919, 16384, 14654, 13377, 12385, 11585 };
/* Keys held per layer, for last-note priority in the mono modes. */
#define HELD_MAX 10u

typedef struct { uint8_t note; uint8_t velocity; } HeldNote;

extern MidiInterfaceState master_midi;
#if !defined(FELUCCA_ID)
static int32_t web_lr[SYNTH_WEB_MAX_FRAMES * 2u];  /* the browser emulator's output; the FM1 renders into the DMA half */
#endif
static uint32_t sample_clock;                  /* audio frames since boot */
static uint32_t last_note_on[NLAYER];          /* sample_clock at the last note-on of each layer */
static HeldNote held[NLAYER][HELD_MAX];        /* keys down per layer, oldest first */
static uint8_t held_count[NLAYER];
/* poly portamento: the note each layer played last, and the note the chord now starting glides from (notes struck
 * within GLIDE_CHORD_WINDOW of each other are one chord: they all fan out from the note before it). NOTE_NONE: none */
#define GLIDE_NOTE_NONE 0xFFu
#define GLIDE_CHORD_WINDOW 1323u                       /* 30 ms */
static uint8_t glide_last[NLAYER] = {GLIDE_NOTE_NONE, GLIDE_NOTE_NONE};
static uint8_t glide_from[NLAYER] = {GLIDE_NOTE_NONE, GLIDE_NOTE_NONE};
static int32_t limit_env_q8 = (int32_t)LIMIT_CEILING << 8;   /* master limiter peak envelope, never below the ceiling */
static uint8_t layer_ctrl_left;                /* samples until the next layer-wide control tick (LFO1, OSCMIXER) */
static uint32_t pass_frame;            /* the frame render_pass's global work is on */
static uint8_t in_pass;                /* inside a pass: a MIDI event renders the voices up to its frame first */
static void voices_up_to(uint32_t frame);
static uint32_t voices_done;           /* (defined with the render, below) */
/* The sequencers' events between the voices' run boundaries (2026-10-16). A MAQ / ARP event mid-pass used to make
 * every voice catch up to its exact frame first (voices_up_to): a run cut short and, on the FM1, a hand-off to the
 * second core, for each ratchet burst, strum note and gate end, ~2000 a second in the SEQ+ARP torture test (host:
 * ~7 % of the load). Now an event on a frame where the voices already stand (a clock pulse, a layer tick: most steps'
 * notes) applies at once, exactly as before; one between waits here and applies at the next boundary, at most
 * SYNTH_CTRL_PERIOD - 1 frames (0.34 ms) later, in order. A full queue falls back to the exact catch-up. */
#define SEQ_Q_MAX 48u
static struct { uint8_t st, d1, d2; } seq_q[SEQ_Q_MAX];
static uint8_t seq_qn;
static void midi_apply(uint8_t status, uint8_t data1, uint8_t data2);
/* Timed MIDI in (synth_midi_at, 2026-10-24): each event waits here for its frame, in order, and the render applies it
 * there (the voices brought up to it first). A full queue applies the event at once, as synth_midi_3 would. */
#define EXT_Q_MAX 64u                          /* the FM1 drains at most 64 a half */
static struct { uint32_t at; uint8_t st, d1, d2; } ext_q[EXT_Q_MAX];
static uint8_t ext_h, ext_n;
static MaqSeq maq;                             /* the MAQ 16/4 sequencer (spec 7), clocked from the render below */
static CarpSeq carp;                           /* the ARP chord pattern (spec 8), on the same clock (its own transport) */

static int32_t clamp_q15_64(int64_t value)
{
    if (value > Q15_MAX) return Q15_MAX;
    if (value < Q15_MIN) return Q15_MIN;
    return (int32_t)value;
}

static uint8_t rt_perf_mode(void)
{
    uint8_t m = perf_state.perf_mode;
    return m < PERF_COUNT ? m : (uint8_t)PERF_SINGL_SYNTH;
}

/* A MIDI channel reaches the synth when it is a layer's channel (SINGL: layer 1 only). */
static int channel_routed(uint8_t channel)
{
    uint8_t c1 = perf_state.midi_ch[0] & 0x0Fu, c2 = perf_state.midi_ch[1] & 0x0Fu;
    return channel == c1 || (rt_perf_mode() != PERF_SINGL_SYNTH && channel == c2);
}

/* Each layer's own voice mode, SPLIT included (2026-10-25: a mono bass under poly chords; the spec's 5.3 View 2 had
 * SPLIT's layer 2 take layer 1's). */
static uint8_t layer_voice_mode(uint8_t layer)
{
    uint8_t m = perf_state.voice_mode[layer];
    return m < VMODE_COUNT ? m : (uint8_t)VMODE_POLY_REUSE;
}

static uint8_t layer_unison_detune(uint8_t layer)
{
    return perf_state.unison_detune[layer];
}

static uint8_t layer_porta_mode(uint8_t layer)
{
    uint8_t m = perf_state.porta_mode[layer];
    return m < PORTA_COUNT ? m : (uint8_t)PORTA_CONST_RATE;
}

/* The modes with one note at a time (spec 5.3 View 1): they share one voice, or the whole stack for UNISON. */
static int is_mono_mode(uint8_t mode)
{
    return mode == VMODE_MONO_RETRIG || mode == VMODE_MONO_LEGATO || mode == VMODE_UNISON_STACK;
}

/* ----------------------------------------------------------- held keys --- */

static void held_remove(uint8_t layer, uint8_t note)
{
    for (uint8_t i = 0; i < held_count[layer]; i++) {
        if (held[layer][i].note != note) continue;
        for (uint8_t j = i; j + 1u < held_count[layer]; j++)
            held[layer][j] = held[layer][j + 1u];
        held_count[layer]--;
        return;
    }
}

static void held_push(uint8_t layer, uint8_t note, uint8_t velocity)
{
    held_remove(layer, note);
    if (held_count[layer] == HELD_MAX) {                       /* full: forget the oldest key */
        for (uint8_t j = 0; j + 1u < HELD_MAX; j++)
            held[layer][j] = held[layer][j + 1u];
        held_count[layer]--;
    }
    held[layer][held_count[layer]].note = note;
    held[layer][held_count[layer]].velocity = velocity;
    held_count[layer]++;
}

/* ----------------------------------------------------------- routing --- */

/* Layer 1 and layer 2 gains for a value against a threshold, with a fade window (spec 5.3 boundary rules):
 * below threshold - (fade >> 1) layer 1 alone, above threshold + (fade >> 1) layer 2 alone, and inside the
 * window a constant-power crossfade. fade 0 is a hard cut: below the threshold is layer 1. */
static void split_gains(int value, int threshold, int fade, int32_t *g1, int32_t *g2)
{
    int half = fade >> 1;
    if (half == 0) {
        *g1 = value < threshold ? Q15_MAX : 0;
        *g2 = Q15_MAX - *g1;
        return;
    }
    int lo = threshold - half, hi = threshold + half;
    if (value < lo) { *g1 = Q15_MAX; *g2 = 0; return; }
    if (value > hi) { *g1 = 0; *g2 = Q15_MAX; return; }
    int idx = ((value - lo) * 127) / (2 * half);              /* 0..127 across the window */
    *g1 = amp_const_power_q15(idx);
    *g2 = amp_const_power_q15(127 - idx);
}

/* Layer weights for a note-on, Q15 (spec 5.3). A weight of 0 means the note does not reach that layer.
 * SINGL: channel 1 only. DUAL: each layer on its own channel, both when the channels match.
 * SPLIT: the key split weighs the layers, then the velocity split gates them (a layer plays when the key
 * and the velocity both allow it). vel_split 127 is the no-split setting and the default: every velocity is
 * layer 1 and the velocity gate is open for both layers. */
static void route_note(uint8_t channel, uint8_t note, uint8_t velocity, int32_t weight[NLAYER])
{
    uint8_t c1 = perf_state.midi_ch[0] & 0x0Fu, c2 = perf_state.midi_ch[1] & 0x0Fu;
    int on1 = channel == c1;
    int on2 = rt_perf_mode() != PERF_SINGL_SYNTH && channel == c2;
    weight[0] = weight[1] = 0;
    if (!on1 && !on2) return;

    if (rt_perf_mode() != PERF_SPLIT_SYNTH) {
        weight[0] = on1 ? Q15_MAX : 0;
        weight[1] = on2 ? Q15_MAX : 0;
        return;
    }

    int32_t key1, key2;
    split_gains(note, perf_state.split_note, perf_state.split_fade, &key1, &key2);
    if (perf_state.vel_split >= 127) {                        /* no velocity split */
        weight[0] = key1;
        weight[1] = key2;
        return;
    }
    /* vel_split is the top of layer 1: a velocity equal to it is still layer 1 */
    int32_t vel1, vel2;
    split_gains(velocity, perf_state.vel_split + 1, perf_state.vel_fade, &vel1, &vel2);
    weight[0] = (key1 * vel1 + 16383) / 32767;                /* unity is 32767: a full gate keeps full scale */
    weight[1] = (key2 * vel2 + 16383) / 32767;
}

/* AUTO DECAY input: how closely this note followed the last one on its layer, 0..32767. Starts the next count. */
static uint16_t note_repeat_boost(uint8_t layer)
{
    uint32_t gap = sample_clock - last_note_on[layer];
    last_note_on[layer] = sample_clock;
    if (gap >= AUTO_DECAY_WINDOW) return 0;
    return (uint16_t)(((AUTO_DECAY_WINDOW - gap) * 32767u) / AUTO_DECAY_WINDOW);
}

/* ----------------------------------------------------------- voices --- */

/* Portamento (spec 5.3): the voice glides from the note it sounds to the new target. Constant rate: an
 * octave takes the same time whatever the interval. Constant time: every glide takes the same time.
 * Legato closed-tied: glides only between overlapping notes. Portamento time 0 is no glide. A glide still under way
 * starts the next from where its pitch is now, so a quick run of notes never jumps. */
static void glide_to(SynthVoiceNode *v, int from_note, int to_note, uint8_t porta_mode, uint8_t porta_time,
                     int overlap)
{
    int32_t now = v->glide_q16;                               /* the old glide's offset from from_note, Q16 cents */
    v->glide_q16 = 0;
    v->glide_step_q16 = 0;
    if (porta_time == 0u) return;
    if (porta_mode == PORTA_LEGATO_TIED && !overlap) return;

    int32_t off = (from_note - to_note) * 100 * 65536 + now;  /* Q16 cents, from minus to (|off| < 2^31: 127 notes) */
    if (off == 0) return;
    uint32_t t = 441u + (uint32_t)porta_time * porta_time * 13u;   /* samples per octave, or per glide */
    int32_t step;
    if (porta_mode == PORTA_CONST_RATE) {
        step = (int32_t)((1200u << 16) / t);                  /* cents per sample, Q16 */
    } else {
        uint32_t mag = (uint32_t)(off < 0 ? -off : off);
        step = (int32_t)((mag + t - 1u) / t);                 /* finishes in t samples */
    }
    if (step < 1) step = 1;
    v->glide_q16 = off;
    v->glide_step_q16 = step;
}

/* Points one voice at a note. retrig = 0 keeps the envelopes running (a legato note over a held one). A voice that is
 * fading out, or still on another architecture, always restarts clean: envelopes from 0, and its filters
 * cleared when the architecture changes, so no path starts half-initialised. */
static void voice_play(SynthVoiceNode *v, uint8_t layer, uint8_t note, uint8_t velocity, int32_t route,
                       int retrig, int other_held, uint16_t boost)
{
    const EngineState *e = &engine_state[layer];
    uint8_t arch = varch_of(e);
    if (v->choke || v->arch != arch) {
        retrig = 1;
        if (v->choke) {
            adsr_init(&v->amp_env);
            adsr_init(&v->mod_env);
            adsr_init(&v->aux_env);
            v->choke = 0;
            v->choke_q15 = 32767;
        }
        if (v->arch != arch) {
            dual_filter_init(&v->synth_filters);
            v->arch = arch;
        }
    }
    v->active_note = note;
    v->is_gate_open = 1;
    v->velocity = velocity;
    v->layer = layer;
    v->route_q15 = route;
    v->ctrl_left = 0;                          /* the new pitch (and envelopes) take effect from the next sample */
    if (!retrig) return;

    v->snap = 1;
    analog_note_on(&v->analog);                /* ANALOG: this note's own small pitch and speed offsets */
    v->osc1.pcm.trig = v->osc2.pcm.trig = 1;   /* a PCM wave's attack starts at the next tick (pcm_osc.c) */
    if (arch == VARCH_STRIKE) {                /* the attack starts over with the note (a legato note keeps it) */
        const AttackInfo *ai = &attack_info[e->oscmix.atk_sample % ATK_COUNT];
        v->atk_sample = (uint8_t)(e->oscmix.atk_sample % ATK_COUNT);
        v->atk_p = attack_data + ai->offset;
        v->atk_pos = ((uint32_t)ai->length * e->oscmix.atk_start / 254u) << 16;   /* ATK START: up to halfway in */
        v->atk_end = (uint32_t)(ai->length - 1u) << 16;
        v->atk_fade = 1.f;
        v->atk_tt = 0xFFFFu;                   /* (the step is worked out on the first tick) */
        v->atk_glide = (e->oscmix.atk_bend != 64u || e->oscmix.atk_bend_mod_amt != 64u) ? 1.f : 0.f;   /* BEND */
    } else {
        v->atk_p = NULL;
    }

    v->age_counter = 0;
    v->release_boost_q15 = boost;
    adsr_note_on_legato(&v->amp_env, &e->env[0], other_held);
    adsr_note_on_legato(&v->mod_env, &e->env[1], other_held);
    adsr_note_on_legato(&v->aux_env, &e->env[2], other_held);
    for (int s = 0; s < (int)NMODSLOT; s++)
        voice_mod_fn_reset(v, s);

    /* KEY RESET (spec 4.4): LFO2 and LFO3 of this voice restart at their phase start if they ask for it. */
    lfo_key_reset(&e->lfo[1], &v->poly_lfos.engines[0]);
    lfo_key_reset(&e->lfo[2], &v->poly_lfos.engines[1]);
}

static void voice_release_one(SynthVoiceNode *v)
{
    v->is_gate_open = 0;
    adsr_note_off(&v->amp_env);
    adsr_note_off(&v->mod_env);
    adsr_note_off(&v->aux_env);
}

/* A layer's note-on (spec 5.3 View 1: Layer 1 Voice Mode). */
static void rt_voice_start(uint8_t layer, uint8_t note, uint8_t velocity, int32_t route)
{
    const EngineState *e = &engine_state[layer];
    uint8_t mode = layer_voice_mode(layer);
    uint8_t porta_mode = layer_porta_mode(layer);
    uint8_t porta_time = perf_state.porta_time[layer];
    int overlap = held_count[layer] > 0;                       /* another key of this layer is down */
    if (sample_clock - last_note_on[layer] >= GLIDE_CHORD_WINDOW)
        glide_from[layer] = glide_last[layer];                 /* a new chord (or note): it glides from the last note */
    glide_last[layer] = note;
    uint16_t boost = note_repeat_boost(layer);
    int first, count;
    pool_range(layer, &first, &count);                         /* count: the layer's voice limit (5.3) */
    if (count <= 0) return;                                    /* the layer has no voices (LAYER2 under SINGL) */
    held_push(layer, note, velocity);

    if (!is_mono_mode(mode)) {
        int slot = -1;
        if (mode == VMODE_POLY_REUSE) {
            /* restriking a pitch that still sounds recycles its voice (spec 5.3) */
            for (int v = first; v < first + count && slot < 0; v++) {
                SynthVoiceNode *s = &poly_bank[v];
                if (s->layer == layer && s->active_note == note && !voice_is_free(s) && !s->choke)
                    slot = v;
            }
        }
        if (slot < 0)
            slot = allocate_voice_slot(layer);
        if (slot < 0) return;
        SynthVoiceNode *v = &poly_bank[slot];
        v->glide_q16 = 0;
        v->glide_step_q16 = 0;
        if (glide_from[layer] != GLIDE_NOTE_NONE)               /* poly: every new note slides in from the last one */
            glide_to(v, glide_from[layer], note, porta_mode, porta_time, overlap);
        v->detune_cents = 0;
        v->unison_gain_q15 = 32767;
        v->unison_pan = 0;
        int other = check_if_any_other_voice_held(slot, layer);
        voice_play(v, layer, note, velocity, route, 1, other, boost);
        lfo_key_reset(&e->lfo[0], &global_lfo1_state[layer]);
        return;
    }

    /* Mono modes: one voice, or every voice the layer may use stacked for UNISON_STACK (so the stack size follows
     * the architecture's voice limit). UNISON DETUNE spreads the stack evenly between +-its outer cents; 0 is no
     * detune. The stack is also spread across the stereo field (varch_unison_pan). The oscillators never restart on a
     * note: each voice's run free (voice_render_block keeps them turning while it is silent), so the stack's phases
     * are whatever the voices drifted to. A legato note over a held one only moves the pitch; every other note retriggers the envelopes. */
    int retrig = !(mode == VMODE_MONO_LEGATO && overlap);
    int n = mode == VMODE_UNISON_STACK ? count : 1;
    int outer = mode == VMODE_UNISON_STACK ? varch_unison_outer_cents(layer_unison_detune(layer)) : 0;
    for (int i = 0; i < n; i++) {
        SynthVoiceNode *v = &poly_bank[first + i];
        int sounding = !voice_is_free(v) && !v->choke;
        if (sounding) glide_to(v, v->active_note, note, porta_mode, porta_time, overlap);
        else { v->glide_q16 = 0; v->glide_step_q16 = 0; }
        v->detune_cents = n > 1 ? (outer * (2 * i - (n - 1))) / (n - 1) : 0;
        v->unison_pan = varch_unison_pan(i, n);
        v->unison_gain_q15 = unison_gain_q15[n - 1];
        voice_play(v, layer, note, velocity, route, retrig, 0, boost);
    }
    if (retrig)
        lfo_key_reset(&e->lfo[0], &global_lfo1_state[layer]);
}

/* A note-off for one layer. Mono modes fall back to the newest key still held (last-note priority). */
static void note_off_layer(uint8_t layer, uint8_t note)
{
    uint8_t mode = layer_voice_mode(layer);
    int first, count;
    pool_range(layer, &first, &count);
    if (count <= 0) return;

    if (!is_mono_mode(mode)) {
        for (int v = first; v < first + count; v++) {
            SynthVoiceNode *s = &poly_bank[v];
            if (s->is_gate_open && s->active_note == note) voice_release_one(s);
        }
        return;
    }

    int n = mode == VMODE_UNISON_STACK ? count : 1;
    SynthVoiceNode *lead = &poly_bank[first];
    if (!(lead->is_gate_open && lead->active_note == note)) return;   /* that key is not the one sounding */

    if (held_count[layer] == 0) {
        for (int i = 0; i < n; i++)
            if (poly_bank[first + i].is_gate_open) voice_release_one(&poly_bank[first + i]);
        return;
    }

    HeldNote top = held[layer][held_count[layer] - 1u];
    uint8_t porta_mode = layer_porta_mode(layer);
    uint8_t porta_time = perf_state.porta_time[layer];
    int retrig = mode != VMODE_MONO_LEGATO;
    for (int i = 0; i < n; i++) {
        SynthVoiceNode *v = &poly_bank[first + i];
        glide_to(v, v->active_note, top.note, porta_mode, porta_time, 1);
        voice_play(v, layer, top.note, top.velocity, v->route_q15, retrig, 0, v->release_boost_q15);
    }
}

static void note_off(uint8_t note)
{
    for (uint8_t l = 0; l < NLAYER; l++) {
        held_remove(l, note);
        note_off_layer(l, note);
    }
}

/* Voice limits and architecture changes (5.3, 4.0): a voice outside its layer's range (the limit fell: an
 * architecture or filter change, a performance mode change), or one still playing its layer's old architecture, is
 * choked: its gate closes and it fades out over a few milliseconds instead of cutting. Once per layer control tick. */
static void enforce_voice_limits(void)
{
    int first[NLAYER], count[NLAYER];
    uint8_t arch[NLAYER];
    for (uint8_t l = 0; l < NLAYER; l++) {
        pool_range(l, &first[l], &count[l]);
        arch[l] = varch_of(&engine_state[l]);
    }
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        SynthVoiceNode *s = &poly_bank[v];
        uint8_t l = s->layer < NLAYER ? s->layer : 0u;
        if (s->choke || voice_is_free(s)) continue;
        if (v < first[l] || v >= first[l] + count[l] || s->arch != arch[l]) {
            s->choke = 1;
            s->choke_q15 = 32767;
            s->is_gate_open = 0;
        }
    }
}

/* ----------------------------------------------------------- lifecycle --- */

void synth_init(void)
{
    global_init();
    preset_defaults();
    voice_attack_init();
    pcm_osc_init();
    synth_restart();
}

void synth_restart(void)
{
    midi_io_init(&master_midi);
    midi_clock_init();
    voice_allocator_init();
    noise_shared_init();
    fx_init();
    for (unsigned l = 0; l < NLAYER; l++) {
        global_lfo1_state[l].phase_accumulator = 0;
        global_lfo1_state[l].envelope_amp_q15 = 32767;
    }
    seq_qn = 0;
    ext_h = ext_n = 0;
    maq_init(&maq, seq_lanes, &perf_state, synth_midi_3);   /* its notes and controllers take the MIDI path */
    carp_init(&carp, &arp_state, &perf_state, synth_midi_3);
    sample_clock = AUTO_DECAY_WINDOW;          /* the first note counts as an isolated one */
    limit_env_q8 = (int32_t)LIMIT_CEILING << 8;
    layer_ctrl_left = 0;
    for (unsigned l = 0; l < NLAYER; l++)
        glide_last[l] = glide_from[l] = GLIDE_NOTE_NONE;
}

/* Realtime bytes (0xF8..0xFF) carry no channel: the HAL passes the status byte alone, data bytes ignored. */
void synth_custom_cc(uint8_t which, uint8_t value)
{
    if (which == 0u) master_midi.custom_cc1_val = value;
    else master_midi.custom_cc2_val = value;
}

void synth_midi_3(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (in_pass) {                             /* a MAQ / ARP event mid-pass */
        if (status < 0xF8u && voices_done < pass_frame && seq_qn < SEQ_Q_MAX) {
            seq_q[seq_qn].st = status;         /* between boundaries: at the next one (voices_up_to) */
            seq_q[seq_qn].d1 = data1;
            seq_q[seq_qn].d2 = data2;
            seq_qn++;
            return;
        }
        voices_up_to(pass_frame);              /* on a boundary (no-op), realtime, or the queue full: exact */
    }
    midi_apply(status, data1, data2);
}

void synth_midi_at(uint32_t delay, uint8_t status, uint8_t data1, uint8_t data2)
{
    uint32_t at = sample_clock + delay;
    if (in_pass || ext_n >= EXT_Q_MAX || (status & 0xF0u) == 0xC0u) {   /* (a Program Change loads between renders) */
        synth_midi_3(status, data1, data2);
        return;
    }
    if (ext_n) {                               /* never before the event ahead of it: the order is kept */
        uint32_t prev = ext_q[(ext_h + ext_n - 1u) % EXT_Q_MAX].at;
        if ((int32_t)(at - prev) < 0) at = prev;
    }
    uint8_t i = (uint8_t)((ext_h + ext_n) % EXT_Q_MAX);
    ext_q[i].at = at;
    ext_q[i].st = status;
    ext_q[i].d1 = data1;
    ext_q[i].d2 = data2;
    ext_n++;
}

static void midi_apply(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t kind = status & 0xF0u;
    uint8_t channel = status & 0x0Fu;
    if (status >= 0xF8u) {
        midi_clock_realtime(status);
        return;
    }
    if (!channel_routed(channel)) return;          /* not on a layer's channel */

    if (kind == 0x90u && data2 != 0u) {
        int32_t weight[NLAYER];
        route_note(channel, data1, data2, weight);
        midi_parse_three_bytes(&master_midi, status, data1, data2);
        for (uint8_t l = 0; l < NLAYER; l++)
            if (weight[l] > 0) rt_voice_start(l, data1, data2, weight[l]);
    } else if (kind == 0x80u || (kind == 0x90u && data2 == 0u)) {
        midi_parse_three_bytes(&master_midi, status, data1, data2);
        note_off(data1);
    } else if (kind == 0xC0u) {                    /* Program Change: the stored patch into every layer on this channel */
        for (uint8_t l = 0; l < NLAYER; l++)
            if ((perf_state.midi_ch[l] & 0x0Fu) == channel && (l == 0u || rt_perf_mode() != PERF_SINGL_SYNTH))
                patch_load(l, data1 & 0x7Fu);
    } else {
        midi_parse_three_bytes(&master_midi, status, data1, data2);
    }
}

/* ------------------------------------------------------------ render --- */
/* Block-major render (docs/dual-core-plan.md, step 2). The global work runs frame by frame, as before: the clock, the
 * MAQ and the ARP with their MIDI events, and the layer-wide control tick (LFO1, OSCMIXER, the voice limits). The voices
 * lag behind it: each renders a run of frames on its own, voice after voice, and a run ends just before anything a
 * voice reads changes (an event, a clock pulse, the layer tick), so every voice sees what it saw rendered frame by
 * frame and the output is bit-identical to the frame-major loop this replaced. A run is at most SYNTH_CTRL_PERIOD
 * frames. On the FM1, those runs are what the second core will share (render_voice_run over half the voices, into a
 * bus of its own); the FX and the master limiter run afterwards, over the pass's sums. */
#define RENDER_PASS_FRAMES 128u        /* frames per pass (the FM1's half buffer); longer requests take several */

/* One core's share of the mix: each frame of the pass, MIX_N floats (synth_voice.h MIX_*): the voices' dry sum and
 * effect sends, routed. Float since 2026-10-15 (the FM1's float multiply-add beats its integer one, and the voice's
 * float amp writes straight in). Two cores' buses are added in a fixed order, so the sum stays deterministic. */
typedef struct {
    float mix[RENDER_PASS_FRAMES][MIX_N];
} MixBus;

/* The voices are split into two fixed sets, each summed into its own bus, and the two buses are added in one order:
 * so one core or two render the same sums (float addition depends on its order). Core 1 takes the set in
 * CORE1_VOICES, core 0 the rest. Half each: the clock, the sequencers and the FX run on core 0 between the runs, not
 * beside them, so a run lasts as long as the bigger set (measured 2026-10-15: 3 + 5 gave 43.5 % at 8 SINGLE voices,
 * the time of 5). Interleaved, so a few held notes (the allocator fills from the low voices) and each layer of a DUAL
 * performance (voices 0..3 and 4..7) keep both cores busy. */
#define CORE1_VOICES 0xAAu                     /* voices 1, 3, 5, 7; core 0: 0, 2, 4, 6 */
static MixBus mix_bus[2] __attribute__((aligned(64)));   /* (each bus on cache lines of its own: fx.c FX_LINE) */
PS_STATIC_ASSERT(sizeof(MixBus) % 64u == 0u, "the buses meet at a cache line");
/* the meters (2026-10-16, the FM1's STATS): the reverb's wet, the delay's wet, the dry left, peak |Q15| since read */
static volatile int32_t lvl_peak[4];               /* [3]: the reverb's send, going in */
static volatile uint8_t lvl_on;                     /* the meters run once synth_levels has been asked (no cost before) */
static volatile uint8_t fx_one_core;                /* 1: the reverb's back on core 0 too (synth_fx_one_core) */
void synth_fx_one_core(int on) { fx_one_core = (uint8_t)(on != 0); }
static int (*core2_run)(void (*fn)(uint32_t), uint32_t arg);
static int (*core2_wait)(void);
static uint32_t core1_from, core1_to;          /* the run core 1 renders (set before it is handed the job) */

void synth_set_core2(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void))
{
    core2_run = run;
    core2_wait = wait;
}

/* a bus sum (Q15 units) as an integer for the FX and the limiter: held inside +-2^30 (8 voices reach about 2^19) */
static inline int32_t mix_q15(float x)
{
    return x > 1073741824.f ? 1073741824 : (x < -1073741824.f ? -1073741824 : (int32_t)x);
}
#if defined(SYNTH_PROF)
volatile uint32_t synth_prof_acc[SPROF_N];
#endif
static uint32_t pass_base;             /* sample_clock at the pass's frame 0 */
static uint32_t voices_done;           /* frames [0, voices_done) of the pass have their voices rendered */

/* The voices in set through frames [from, to) of the pass, summed into bus. Writes only those voices and bus; reads
 * state that holds still over the run (the global work calls voices_up_to before it changes any). Each voice renders
 * a block at a time, from one of its control ticks to the next (voice_render_block); the bookkeeping is the one the
 * frame-by-frame loop did (a voice its tick frees counts its one silent frame, then stops), so the voice ages, and
 * with them the allocator's choices, are unchanged. */
static void render_voice_run(MixBus *bus, uint32_t set, uint32_t from, uint32_t to)
{
    SPROF_DECL;
    for (int v = 0; v < POLYPHONY_VOICES; ++v) {
        if (!(set & (1u << v))) continue;
        SynthVoiceNode *voice = &poly_bank[v];
        for (uint32_t f = from; f < to; ) {
            if (voice_is_free(voice)) break;       /* only a note-on wakes it, and an event ends the run */
            /* Control tick: every voice runs its own, every SYNTH_CTRL_PERIOD samples, and at once after a note-on. */
            if (voice->ctrl_left == 0) {
                SPROF_START();
                VoiceInput in;
                voice_input_from_midi(&in, &master_midi, voice->active_note, voice->velocity, voice->is_gate_open);
                in.glide_cents = voice->glide_q16 / 65536;
                in.detune_cents = voice->detune_cents;
                in.pan += voice->unison_pan;           /* UNISON stereo spread; the voice clamps the sum to 0..127 */
                in.frame = pass_base + f;              /* the clock-synced LFOs and retriggers ask about this frame */
                voice_glide_advance(voice);
                voice_control_tick(voice, &in);
                voice->ctrl_left = SYNTH_CTRL_PERIOD;
                SPROF_LAP(SPROF_CTRL);
            }
            uint32_t n = to - f;
            if (n > voice->ctrl_left) n = voice->ctrl_left;
            if (voice_is_free(voice)) n = 1u;      /* its tick ended it: the one frame the per-sample loop counted */
            SPROF_START();
            voice->noise_at = (uint8_t)f;
            voice_render_block(voice, bus->mix[f], (int)n);   /* routed, mix gain and sends included (tick gains) */
            SPROF_LAP(SPROF_VOICE);
            voice->ctrl_left = (uint8_t)(voice->ctrl_left - n);
            voice->age_counter += n;
            f += n;
            SPROF_LAP(SPROF_MIX);
        }
    }
}

/* core 1's share of a run: its voice set into its own bus. Reads what core 0 holds still until it has waited. */
static void core1_job(uint32_t arg)
{
    (void)arg;
    render_voice_run(&mix_bus[1], CORE1_VOICES & ((1u << POLYPHONY_VOICES) - 1u), core1_from, core1_to);
}

/* the effects' buffers for one pass, and core 1's job on them: the reverb's back (fx_reverb_back: the tank) */
static int32_t fx_dly[RENDER_PASS_FRAMES], fx_dly_l[RENDER_PASS_FRAMES], fx_dly_r[RENDER_PASS_FRAMES];
static int32_t fx_rev_in[RENDER_PASS_FRAMES];
static int32_t fx_rev_l[RENDER_PASS_FRAMES] __attribute__((aligned(64)));   /* core 1 writes these two (fx.c FX_LINE) */
static int32_t fx_rev_r[RENDER_PASS_FRAMES] __attribute__((aligned(64)));
PS_STATIC_ASSERT(sizeof fx_rev_l % 64u == 0u, "core 1's output buffers end on a cache line");
static uint32_t fx_frames;
static void fx_reverb_job(uint32_t arg)
{
    (void)arg;
    fx_reverb_back(&perf_state, fx_rev_l, fx_rev_r, fx_frames);
}

/* Every voice through the frame before `frame`: called before the global work changes what the voices read. */
static void voices_up_to(uint32_t frame)
{
    if (voices_done >= frame) return;
    uint32_t all = (1u << POLYPHONY_VOICES) - 1u;
    core1_from = voices_done;
    core1_to = frame;
    int on2 = core2_run && core2_run(core1_job, 0u);       /* core 1 starts on its set ... */
    render_voice_run(&mix_bus[0], all & ~CORE1_VOICES, voices_done, frame);   /* ... while core 0 renders its own */
    if (!on2 || core2_wait())
        core1_job(0u);                         /* one core (or the second failed): here */
    voices_done = frame;
    for (uint8_t i = 0; i < seq_qn; i++)       /* the sequencers' events that waited for this boundary, in order */
        midi_apply(seq_q[i].st, seq_q[i].d1, seq_q[i].d2);
    seq_qn = 0;
}

/* The shared noise for the pass (oscmixer.h noise_buf), before any voice reads it: an ear and its colours only when a
 * playing voice's layer can make them (its noise level, or a source on it; its colour morph, or a source on that). */
static void noise_pass_fill(uint32_t frames)
{
    uint8_t want[2] = {0, 0};                  /* bit 0 white, 1 pink, 2 brown */
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        const SynthVoiceNode *s = &poly_bank[v];
        if (voice_is_free(s)) continue;
        const OscMixerParams *m = &engine_state[s->layer < NLAYER ? s->layer : 0u].oscmix;
        if (m->noise_level == 0 && m->noise_mod_amt == BIPOLAR_CENTER) continue;
        int col_mod = m->noise_col_mod_amt != BIPOLAR_CENTER;
        uint8_t w = (uint8_t)(1u | (m->noise_colour > 0 || col_mod ? 2u : 0u) |
                              (m->noise_colour > NOISE_PINK || col_mod ? 4u : 0u));
        want[0] |= w;
        if (s->arch == VARCH_BINAURAL) want[1] |= w;
    }
    for (int e = 0; e < 2; e++)
        if (want[e]) noise_shared_fill(e, frames, want[e] & 2u, want[e] & 4u);
}

static void render_pass(int32_t *out, uint32_t frames)
{
#if defined(SYNTH_PROF)
    uint32_t sprof_total = synth_prof_now();
#endif
    SPROF_DECL;
    /* each bus cleared on its own (2026-10-16): the FM1's compiler (JieLi clang, -O2) turned the joint loop that was
     * here (both buses in one statement) into 64-bit stores for bus 0 placed 4 bytes low: every frame's reverb send was
     * cleared one iteration late, frame 127's never, and the word just below mix_bus was zeroed every pass. Frame 127's
     * send kept every voice's last value and went into the reverb every pass, for ever: a 345 Hz buzz (found on the
     * device with the STATS trap; the host's gcc compiled the loop right). Now each bus on its own, through a volatile
     * pointer: plain stores, one at a time, that the compiler can neither fuse nor turn into a memset call (the
     * firmware's memset, from the flash a byte at a time, cost ~3 % of the CPU here). */
    PS_STATIC_ASSERT(MIX_N == 4, "the clear writes a frame (4 floats) a step");
    for (int b = 0; b < 2; b++) {
        volatile float *z = &mix_bus[b].mix[0][0];
        for (uint32_t f = 0; f < frames; f++, z += MIX_N) {
            z[0] = 0.f;
            z[1] = 0.f;
            z[2] = 0.f;
            z[3] = 0.f;
        }
    }
    noise_pass_fill(frames);
    pass_base = sample_clock;
    voices_done = 0;
    in_pass = 1;
    for (uint32_t f = 0; f < frames; ++f) {
        pass_frame = f;
        sample_clock = pass_base + f;
        uint8_t ext_now = ext_n && (int32_t)(ext_q[ext_h].at - sample_clock) <= 0;
        if (ext_now || midi_clock_frame_changes()) voices_up_to(f);   /* a pulse or an event: the voices before it as they were */
        SPROF_START();
        midi_clock_frame(sample_clock);            /* the clock is stamped before the LFOs read it this frame */
        while (ext_now) {                          /* timed MIDI in on this frame (a 0xF8 after the clock's own pulse) */
            midi_apply(ext_q[ext_h].st, ext_q[ext_h].d1, ext_q[ext_h].d2);
            ext_h = (uint8_t)((ext_h + 1u) % EXT_Q_MAX);
            ext_n--;
            ext_now = ext_n && (int32_t)(ext_q[ext_h].at - sample_clock) <= 0;
        }
        SPROF_LAP(SPROF_CLOCK);
        maq_frame(&maq, sample_clock);             /* sequencer events land on this frame (synth_midi_3 catches up) */
        carp_frame(&carp, sample_clock, &maq);     /* the ARP: its own transport, a start synced to the MAQ's bar */
        maq_set_ext_transpose(&maq, carp_maq_transpose(&carp));   /* 8.4: the MAQ reads it on the next frame */
        maq_set_sync(&maq, carp_running(&carp), carp_origin(&carp));   /* (.. and the ARP's bar, for a start) */
        maq_set_genre(&maq, arp_state.current_genre_bank);       /* (SCALE = GENRE lanes follow it) */
        SPROF_LAP(SPROF_SEQ);
        if (layer_ctrl_left == 0) {
            voices_up_to(f);
            SPROF_START();
            enforce_voice_limits();
            synth_voice_begin_tick(sample_clock);
            SPROF_LAP(SPROF_LTICK);
            layer_ctrl_left = SYNTH_CTRL_PERIOD;
        }
        layer_ctrl_left--;
    }
    voices_up_to(frames);
    in_pass = 0;
    sample_clock = pass_base + frames;
    SPROF_START();

    /* the shared effects (delay and reverb, in parallel) over the pass: the reverb's back on core 1 while core 0 runs
     * the delay and the reverb's front (their states are separate: the same sums on one core), then their wet sum */
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const float *m0 = mix_bus[0].mix[frame], *m1 = mix_bus[1].mix[frame];   /* the two sets, in this order */
        fx_dly[frame] = mix_q15(m0[MIX_DELAY] + m1[MIX_DELAY]);         /* unclipped: each effect limits its send */
        fx_rev_in[frame] = mix_q15(m0[MIX_REVERB] + m1[MIX_REVERB]);
        if (lvl_on && (fx_rev_in[frame] < 0 ? -fx_rev_in[frame] : fx_rev_in[frame]) > lvl_peak[3])
            lvl_peak[3] = fx_rev_in[frame] < 0 ? -fx_rev_in[frame] : fx_rev_in[frame];
    }
    fx_frames = frames;
    int on2 = !fx_one_core && core2_run && core2_run(fx_reverb_job, 0u);
    fx_delay_block(&perf_state, fx_dly, fx_dly_l, fx_dly_r, frames);
    fx_reverb_front(&perf_state, fx_rev_in, frames);   /* (for the back's next pass: it reads a pass behind) */
    if (!on2 || core2_wait())
        fx_reverb_job(0u);
    for (uint32_t frame = 0; lvl_on && frame < frames; ++frame) {   /* the meters (synth_levels): before the wet sum */
        int32_t r = fx_rev_l[frame] < 0 ? -fx_rev_l[frame] : fx_rev_l[frame];
        int32_t d = fx_dly_l[frame] < 0 ? -fx_dly_l[frame] : fx_dly_l[frame];
        if (r > lvl_peak[0]) lvl_peak[0] = r;
        if (d > lvl_peak[1]) lvl_peak[1] = d;
    }
    fx_mix_block(fx_dly_l, fx_dly_r, fx_rev_l, fx_rev_r, frames);
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const float *m0 = mix_bus[0].mix[frame], *m1 = mix_bus[1].mix[frame];
        int64_t left = mix_q15(m0[MIX_L] + m1[MIX_L]), right = mix_q15(m0[MIX_R] + m1[MIX_R]);
        if (lvl_on && (left < 0 ? -left : left) > lvl_peak[2]) lvl_peak[2] = (int32_t)(left < 0 ? -left : left);
        left += fx_rev_l[frame];                  /* the wet sum, each side its own */
        right += fx_rev_r[frame];

        /* Master limiter: the envelope jumps to a peak over the ceiling at once (so no frame passes it) and
         * falls back toward the ceiling; the gain is ceiling / envelope, unity whenever the envelope is at the
         * ceiling. Only a frame that is being limited pays for the division. */
        int64_t peak = left < 0 ? -left : left;
        int64_t peak_r = right < 0 ? -right : right;
        if (peak_r > peak) peak = peak_r;
        if (peak > 0x7FFFFF) peak = 0x7FFFFF;                       /* keeps peak << 8 inside int32 */
        if ((int32_t)peak << 8 > limit_env_q8)
            limit_env_q8 = (int32_t)peak << 8;
        else
            limit_env_q8 -= (limit_env_q8 - ((int32_t)LIMIT_CEILING << 8)) >> LIMIT_RELEASE_SHIFT;
        if (limit_env_q8 > ((int32_t)LIMIT_CEILING << 8)) {
            uint32_t gain_q15 = ((uint32_t)LIMIT_CEILING << 15) / (uint32_t)(limit_env_q8 >> 8);
            left = (left * gain_q15) >> 15;
            right = (right * gain_q15) >> 15;
        }
        out[frame * 2u] = clamp_q15_64(left);
        out[frame * 2u + 1u] = clamp_q15_64(right);
    }
    SPROF_LAP(SPROF_FX);
#if defined(SYNTH_PROF)
    synth_prof_acc[SPROF_TOTAL] += synth_prof_now() - sprof_total;
#endif
}

void synth_render_q15(int32_t *out, uint32_t frames)
{
    while (frames > 0u) {
        uint32_t n = frames < RENDER_PASS_FRAMES ? frames : RENDER_PASS_FRAMES;
        render_pass(out, n);
        out += 2u * n;
        frames -= n;
    }
}

#if !defined(FELUCCA_ID)
int32_t *synth_web_buffer(void) { return web_lr; }
#endif

MaqSeq *synth_seq(void) { return &maq; }

void synth_levels(int32_t out[4])
{
    lvl_on = 1;
    for (int i = 0; i < 4; i++) {
        out[i] = lvl_peak[i];
        lvl_peak[i] = 0;
    }
}

CarpSeq *synth_arp(void) { return &carp; }

#if !defined(FELUCCA_ID)
void synth_web_render(uint32_t frames)
{
    uint32_t offset = 0;
    if (frames > SYNTH_WEB_MAX_FRAMES) frames = SYNTH_WEB_MAX_FRAMES;
    while (offset < frames) {
        uint32_t n = frames - offset;
        if (n > SYNTH_RENDER_BLOCK) n = SYNTH_RENDER_BLOCK;
        synth_render_q15(web_lr + offset * 2u, n);
        offset += n;
    }
}
#endif
