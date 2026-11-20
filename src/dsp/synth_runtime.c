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
#include "amplifier.h"
#include "fx.h"
#include "mfx.h"
#include "maq_seq.h"
#include "arp_seq.h"
#include "krec.h"
#include "hardware/mem_section.h"     /* the recorder's loop in .pool */
#include "dsp_cold.h"

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

typedef struct { uint8_t note; uint8_t velocity; uint8_t src; } HeldNote;   /* src: VSRC_* */

extern MidiInterfaceState master_midi;
#if !defined(FELUCCA_ID)
static int32_t web_lr[SYNTH_WEB_MAX_FRAMES * 2u];  /* the browser emulator's output; the FM1 renders into the DMA half */
#endif
static uint32_t sample_clock;                  /* audio frames since boot */
static uint32_t last_note_on[NLAYER];          /* sample_clock at the last note-on of each layer */
static HeldNote held[NLAYER][HELD_MAX];        /* keys down per layer, oldest first */
static uint8_t held_count[NLAYER];
/* The channel controllers (the CC map, step 1, 2026-11-18). SUSTAIN (CC 64) holds the keys' note-offs on the layers of
 * its channel: a key let go under the pedal leaves its voices' gates open, a bit each in sus_voices, and the pedal's
 * release lets them go (a key down again by then keeps its note). The sequencers' notes are not held. */
static uint8_t sus_on[NLAYER];                 /* the pedal down on the layer's channel */
static uint8_t sus_voices;                     /* the voices the pedal holds, bit = voice (voice_play clears its own) */
PS_STATIC_ASSERT(POLYPHONY_VOICES <= 8u, "sus_voices: a bit a voice");
/* BANK SELECT (CC 0, the MSB; CC 32 ignored) per channel: what a Program Change on it loads (SYNTH_PCBANK_*). A patch
 * loads at once, into every layer of the channel; a performance, sequence or arp pattern (Layer 1's channel only) is a
 * request the panel's main loop takes (synth_pc_take): those come from flash and the UI shows them */
static uint8_t pc_bank[16];
static volatile uint8_t pc_req_bank = 0xFFu, pc_req_slot;   /* 0xFF: none */
/* The sound's controllers (the CC map, step 2): channel, controller, value, for the panel's main loop (synth_cc_take),
 * which moves the knob or row the controller is mapped to (UIStateMachine.c CC_MAP). One entry a controller: a newer
 * value replaces a waiting one, so a fast sweep never fills the queue */
#define CC_Q_MAX 32u
static uint8_t cc_q[CC_Q_MAX][5];              /* channel | SYNTH_CCK_* << 4, the controller (or NRPN / RPN MSB, LSB),
                                                  * value (DATA ENTRY MSB), DATA ENTRY LSB */
static volatile uint8_t cc_qh, cc_qt;          /* written: head by the audio side, tail by the main loop */
static uint8_t par_kind[16], par_hi[16], par_lo[16];   /* each channel's selected NRPN / RPN (step 3) */
static uint8_t par_dm[16];                     /* its DATA ENTRY MSB so far (0xFF: none since the parameter was picked) */
static void cc_queue(uint8_t k, uint8_t a, uint8_t b, uint8_t v, uint8_t lsb)
{
    uint8_t i, n = (uint8_t)(cc_qh - cc_qt), *e;
    for (i = 0; i < n; i++) {                      /* one waiting for this controller already: just the newer value */
        e = cc_q[(uint8_t)(cc_qt + i) % CC_Q_MAX];
        if (e[0] == k && e[1] == a && e[2] == b) {
            e[3] = v;
            e[4] = lsb;
            return;
        }
    }
    if (n >= CC_Q_MAX) return;
    e = cc_q[cc_qh % CC_Q_MAX];
    e[0] = k;
    e[1] = a;
    e[2] = b;
    e[3] = v;
    e[4] = lsb;
    cc_qh++;
}
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
static struct { uint8_t st, d1, d2, src; } seq_q[SEQ_Q_MAX];
static uint8_t seq_qn;
static void midi_apply(uint8_t status, uint8_t data1, uint8_t data2, uint8_t src);
static void rt_seq_out(uint8_t status, uint8_t data1, uint8_t data2);
static void rt_arp_out(uint8_t status, uint8_t data1, uint8_t data2);
/* Timed MIDI in (synth_midi_at, 2026-10-24): each event waits here for its frame, in order, and the render applies it
 * there (the voices brought up to it first). A full queue applies the event at once, as synth_midi_3 would. */
#define EXT_Q_MAX 64u                          /* the FM1 drains at most 64 a half */
static struct { uint32_t at; uint8_t st, d1, d2; } ext_q[EXT_Q_MAX];
static uint8_t ext_h, ext_n;
static MaqSeq maq;                             /* the MAQ 16/4 sequencer (spec 7), clocked from the render below */
static CarpSeq carp;                           /* the ARP chord pattern (spec 8), on the same clock (its own transport) */
static Krec krec SYNTH_POOL;                   /* the REC page's loop (2026-11-04), on the same clock */
static void rt_rec_out(uint8_t status, uint8_t data1, uint8_t data2);

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

static void held_remove(uint8_t layer, uint8_t note, uint8_t src)
{
    for (uint8_t i = 0; i < held_count[layer]; i++) {
        if (held[layer][i].note != note || held[layer][i].src != src) continue;
        for (uint8_t j = i; j + 1u < held_count[layer]; j++)
            held[layer][j] = held[layer][j + 1u];
        held_count[layer]--;
        return;
    }
}

static void held_push(uint8_t layer, uint8_t note, uint8_t velocity, uint8_t src)
{
    held_remove(layer, note, src);
    if (held_count[layer] == HELD_MAX) {                       /* full: forget the oldest key */
        for (uint8_t j = 0; j + 1u < HELD_MAX; j++)
            held[layer][j] = held[layer][j + 1u];
        held_count[layer]--;
    }
    held[layer][held_count[layer]].note = note;
    held[layer][held_count[layer]].velocity = velocity;
    held[layer][held_count[layer]].src = src;
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

static uint32_t rt_rand = 0x9E3779B9u;         /* RANDOM's generator (voice_play) */

/* Points one voice at a note. retrig = 0 keeps the envelopes running (a legato note over a held one). A voice that is
 * fading out, or still on another architecture, always restarts clean: envelopes from 0, and its filters
 * cleared when the architecture changes, so no path starts half-initialised. */
SYNTH_COLD static void voice_play(SynthVoiceNode *v, uint8_t layer, uint8_t note, uint8_t velocity, uint8_t src, int32_t route,
                       int retrig, int other_held, uint16_t boost)
{
    const EngineState *e = &engine_state[layer];
    uint8_t arch = varch_of(e);
    sus_voices &= (uint8_t)~(1u << (v - poly_bank));   /* (a new note: the pedal no longer holds this voice) */
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
    v->src = src;
    v->route_q15 = route;
    rt_rand = rt_rand ^ (rt_rand << 13);       /* RANDOM: a value per note, legato notes too (xorshift32) */
    rt_rand = rt_rand ^ (rt_rand >> 17);
    rt_rand = rt_rand ^ (rt_rand << 5);
    v->rand_q15 = (int16_t)(rt_rand >> 16);
    v->poly_at = v->poly_seen = 0;             /* POLY AT: the channel's AFTERTOUCH until this note has its own */
    v->ctrl_left = 0;                          /* the new pitch (and envelopes) take effect from the next sample */
    if (!retrig) return;

    v->snap = 1;
    analog_note_on(&v->analog);                /* ANALOG: this note's own small pitch and speed offsets */
    v->osc1.pcm.trig = v->osc2.pcm.trig = 1;   /* a PCM wave's attack starts at the next tick (pcm_osc.c) */
    /* a held note (its amp envelope sustains) keeps a percussive wave's ring: no HANDOFF tail (2026-11-05, user: the
     * drums serve as sustained tones too); STACK's OSC2 has ENV2 for its amp */
    v->osc1.pcm.hold = (uint8_t)(e->env[0].sustain > 0u);
    v->osc2.pcm.hold = (uint8_t)(e->env[e->arch == VARCH_STACK ? 1 : 0].sustain > 0u);

    v->age_counter = 0;
    v->release_boost_q15 = boost;
    adsr_note_on_legato(&v->amp_env, &e->env[0], other_held);
    adsr_note_on_legato(&v->mod_env, &e->env[1], other_held);
    adsr_note_on_legato(&v->aux_env, &e->env[2], other_held);

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
#if defined(SYNTH_TICK_COUNT)
uint32_t synth_tick_count[2];
#endif
#define VOICE_GUARD_HALF 512u        /* the FM1's half: the deadline the guard watches */
#ifndef VOICE_GUARD_PM
#define VOICE_GUARD_PM 900u          /* .. guarded when the half's time so far plus the passes to come at the last one's
                                      * rate would pass this, per mille (late = past 1000, less the glue's own) */
#endif
static uint32_t hs_notes, hs_steals;      /* the spike watch (synth_half_take): notes started this half, on a sounding voice */

/* a unison stack's spread over its n voices: UNISON DETUNE evenly between +- the outer cents, the stereo spread, and
 * 1 / sqrt(n) so a stack is no louder than one voice */
static void unison_spread(SynthVoiceNode *v, int i, int n, int outer)
{
    v->detune_cents = n > 1 ? (outer * (2 * i - (n - 1))) / (n - 1) : 0;
    v->unison_pan = varch_unison_pan(i, n);
    v->unison_gain_q15 = unison_gain_q15[n - 1];
}

/* POLY UNISON (2026-11-20, user design after the Jupiter-6): every note held is a unison stack, and the layer's voices
 * are shared between them: 8 voices are one note's 8, two notes' 4 + 4, three notes' 3 + 3 + 2, four 2 each, eight or
 * more 1 each (the oldest notes give theirs up, as POLY steals). A share's remainder goes to the newest notes. A new
 * note takes, in turn: free voices (round-robin), release tails and its own pitch re-struck (oldest first), then the
 * held notes' voices past their new share (the biggest surplus first, the oldest note of a tie), only from notes of
 * its own rank or below (keys > REC > SEQ > ARP): the notes held thin, their stacks re-spread over the voices they
 * keep. A note let go releases its voices (they ring out, nobody grows back: user 2026-11-20). Every source splits. */
static uint8_t pun_rr[NLAYER] = {0xFFu, 0xFFu};   /* each layer's last free voice taken (round-robin, as POLY) */
SYNTH_COLD static void poly_unison_start(uint8_t layer, uint8_t note, uint8_t velocity, uint8_t src, int32_t route,
                                         uint16_t boost, uint8_t porta_mode, uint8_t porta_time, int overlap)
{
    int first, count;
    pool_alloc_range(layer, &first, &count);       /* (inside the measured room, as allocate_voice_slot) */
    if (count <= 0) return;
    if (count > POLYPHONY_VOICES) count = POLYPHONY_VOICES;
    int outer = varch_unison_outer_cents(layer_unison_detune(layer));

    /* the notes held now: a group a (note, source) with an open gate; g_of: each voice's group, -1 free, -2 a tail */
    int8_t g_of[POLYPHONY_VOICES];
    uint8_t g_note[POLYPHONY_VOICES], g_src[POLYPHONY_VOICES], g_n[POLYPHONY_VOICES], g_lost[POLYPHONY_VOICES];
    uint32_t g_age[POLYPHONY_VOICES];
    int ng = 0;
    for (int i = 0; i < count; i++) {
        const SynthVoiceNode *s = &poly_bank[first + i];
        g_of[i] = voice_is_free(s) ? -1 : -2;
        if (!s->is_gate_open || s->choke || s->layer != layer || (s->active_note == note && s->src == src))
            continue;                              /* (a tail; the new note's own pitch re-struck counts as one) */
        int g = 0;
        while (g < ng && !(g_note[g] == s->active_note && g_src[g] == s->src)) g++;
        if (g == ng) {
            g_note[g] = (uint8_t)s->active_note;
            g_src[g] = s->src;
            g_n[g] = 0;
            g_lost[g] = 0;
            g_age[g] = 0;
            ng++;
        }
        g_of[i] = (int8_t)g;
        g_n[g]++;
        if (s->age_counter > g_age[g]) g_age[g] = s->age_counter;
    }

    /* the shares, newest note first: the new note is rank 0, the held ones by age (rank = 1 + the newer ones) */
    int total = ng + 1, base = count / total, extra = count % total;
    int want = base + (extra > 0);                 /* (base 0: more notes than voices, the newest get one each) */
    int surplus[POLYPHONY_VOICES];
    for (int g = 0; g < ng; g++) {
        int rank = 1;
        for (int h = 0; h < ng; h++)
            rank += g_age[h] < g_age[g] || (g_age[h] == g_age[g] && h < g);
        surplus[g] = g_src[g] <= src ? (int)g_n[g] - (base + (rank < extra)) : 0;   /* (a higher rank keeps its own) */
    }

    /* the new note's voices (g_of -3: taken) */
    int take[POLYPHONY_VOICES], nt = 0;
    int start = (int)pun_rr[layer] + 1 - first;
    if (start < 0 || start >= count) start = 0;
    for (int k = 0; k < count && nt < want; k++) {   /* 1. free voices, round-robin */
        int i = (start + k) % count;
        if (g_of[i] == -1) {
            take[nt++] = i;
            g_of[i] = -3;
            pun_rr[layer] = (uint8_t)(first + i);
        }
    }
    while (nt < want) {                              /* 2. tails, the oldest first */
        int best = -1;
        for (int i = 0; i < count; i++)
            if (g_of[i] == -2 && (best < 0 || poly_bank[first + i].age_counter >= poly_bank[first + best].age_counter))
                best = i;
        if (best < 0) break;
        take[nt++] = best;
        g_of[best] = -3;
    }
    while (nt < want) {                              /* 3. the held notes' surplus, the biggest first, the oldest of a tie */
        int bg = -1;
        for (int g = 0; g < ng; g++)
            if (surplus[g] > 0 && (bg < 0 || surplus[g] > surplus[bg] || (surplus[g] == surplus[bg] && g_age[g] > g_age[bg])))
                bg = g;
        if (bg < 0) break;
        int i = count - 1;                           /* (its highest voice: the stack keeps its lowest) */
        while (g_of[i] != bg) i--;
        take[nt++] = i;
        g_of[i] = -3;
        surplus[bg]--;
        g_n[bg]--;
        g_lost[bg] = 1;
    }
    if (nt == 0) return;                             /* every voice is a higher rank's: the note is dropped */

    /* the notes that gave voices up: their stacks re-spread over what they keep (no retrigger) */
    int other = 0;
    for (int g = 0; g < ng; g++) {
        other |= g_n[g] > 0;
        if (!g_lost[g]) continue;
        for (int i = 0, j = 0; i < count; i++)
            if (g_of[i] == g) unison_spread(&poly_bank[first + i], j++, g_n[g], outer);
    }

    /* the new note's stack, in voice order */
    for (int a = 0; a < nt; a++)
        for (int b = a + 1; b < nt; b++)
            if (take[b] < take[a]) { int t = take[a]; take[a] = take[b]; take[b] = t; }
    hs_notes++;
    for (int k = 0; k < nt; k++) {
        SynthVoiceNode *v = &poly_bank[first + take[k]];
        hs_steals += !voice_is_free(v);
        v->glide_q16 = 0;
        v->glide_step_q16 = 0;
        if (glide_from[layer] != GLIDE_NOTE_NONE)
            glide_to(v, glide_from[layer], note, porta_mode, porta_time, overlap);
        unison_spread(v, k, nt, outer);
        voice_play(v, layer, note, velocity, src, route, 1, other, boost);
    }
}

static void rt_voice_start(uint8_t layer, uint8_t note, uint8_t velocity, uint8_t src, int32_t route)
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
    held_push(layer, note, velocity, src);

    if (mode == VMODE_POLY_UNISON) {
        poly_unison_start(layer, note, velocity, src, route, boost, porta_mode, porta_time, overlap);
        if (!overlap)                                           /* LFO1: only a new phrase restarts it, as POLY */
            lfo_phrase_reset(&e->lfo[0], &global_lfo1_state[layer]);
        return;
    }

    if (!is_mono_mode(mode)) {
        int slot = -1;
        if (mode == VMODE_POLY_REUSE) {
            /* restriking a pitch that still sounds recycles its voice (spec 5.3); one another source still holds is
             * left alone. Only inside the measured room (2026-11-18): a voice above it is never taken again, or a
             * BLOCK chord re-struck every cell (ALL SOULS) kept all 8 voices past a room of 6 */
            int rf, rn;
            pool_alloc_range(layer, &rf, &rn);
            for (int v = rf; v < rf + rn && slot < 0; v++) {
                SynthVoiceNode *s = &poly_bank[v];
                if (s->layer == layer && s->active_note == note && (s->src == src || !s->is_gate_open) &&
                    !voice_is_free(s) && !s->choke)
                    slot = v;
            }
        }
        if (slot < 0)
            slot = allocate_voice_slot(layer, src);
        if (slot < 0) return;
        SynthVoiceNode *v = &poly_bank[slot];
        hs_notes++;                                             /* (the spike watch) */
        hs_steals += !voice_is_free(v);
        v->glide_q16 = 0;
        v->glide_step_q16 = 0;
        if (glide_from[layer] != GLIDE_NOTE_NONE)               /* poly: every new note slides in from the last one */
            glide_to(v, glide_from[layer], note, porta_mode, porta_time, overlap);
        v->detune_cents = 0;
        v->unison_gain_q15 = 32767;
        v->unison_pan = 0;
        int other = check_if_any_other_voice_held(slot, layer);
        voice_play(v, layer, note, velocity, src, route, 1, other, boost);
        if (!overlap)                                           /* LFO1: only a new phrase restarts it (Juno-style) */
            lfo_phrase_reset(&e->lfo[0], &global_lfo1_state[layer]);
        return;
    }

    /* a higher rank holds the mono voice: the note waits on the held stack, and sounds if it is still down when that
     * one is let go (note_off_layer) */
    SynthVoiceNode *lead = &poly_bank[first];
    if (lead->is_gate_open && !lead->choke && lead->src > src) return;

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
        voice_play(v, layer, note, velocity, src, route, retrig, 0, boost);
    }
    if (!overlap)
        lfo_phrase_reset(&e->lfo[0], &global_lfo1_state[layer]);
}

/* A note-off for one layer, its own source's notes only. Mono modes fall back to the newest key still held of the
 * highest rank held (last-note priority within it). */
static void note_off_layer(uint8_t layer, uint8_t note, uint8_t src)
{
    uint8_t mode = layer_voice_mode(layer);
    int first, count;
    pool_range(layer, &first, &count);
    if (count <= 0) return;

    if (!is_mono_mode(mode)) {
        for (int v = first; v < first + count; v++) {
            SynthVoiceNode *s = &poly_bank[v];
            if (s->is_gate_open && s->active_note == note && s->src == src) voice_release_one(s);
        }
        return;
    }

    int n = mode == VMODE_UNISON_STACK ? count : 1;
    SynthVoiceNode *lead = &poly_bank[first];
    if (!(lead->is_gate_open && lead->active_note == note && lead->src == src)) return;   /* not the one sounding */

    if (held_count[layer] == 0) {
        for (int i = 0; i < n; i++)
            if (poly_bank[first + i].is_gate_open) voice_release_one(&poly_bank[first + i]);
        return;
    }

    HeldNote top = held[layer][held_count[layer] - 1u];
    for (uint8_t i = held_count[layer]; i-- > 0u;)
        if (held[layer][i].src > top.src) top = held[layer][i];
    uint8_t porta_mode = layer_porta_mode(layer);
    uint8_t porta_time = perf_state.porta_time[layer];
    int retrig = mode != VMODE_MONO_LEGATO;
    for (int i = 0; i < n; i++) {
        SynthVoiceNode *v = &poly_bank[first + i];
        glide_to(v, v->active_note, top.note, porta_mode, porta_time, 1);
        voice_play(v, layer, top.note, top.velocity, top.src, v->route_q15, retrig, 0, v->release_boost_q15);
    }
}

/* a note-off on one layer: under SUSTAIN a key's voices stay open, marked for the pedal's release */
SYNTH_COLD static void layer_note_off(uint8_t layer, uint8_t note, uint8_t src)
{
    held_remove(layer, note, src);
    if (src == VSRC_KEYS && sus_on[layer]) {
        for (int v = 0; v < POLYPHONY_VOICES; v++) {
            const SynthVoiceNode *s = &poly_bank[v];
            if (s->layer == layer && s->is_gate_open && s->active_note == note && s->src == src)
                sus_voices |= (uint8_t)(1u << v);
        }
        return;
    }
    note_off_layer(layer, note, src);
}

static void note_off(uint8_t note, uint8_t src)
{
    for (uint8_t l = 0; l < NLAYER; l++)
        layer_note_off(l, note, src);
}

SYNTH_COLD static int held_has(uint8_t layer, uint8_t note, uint8_t src)
{
    for (uint8_t i = 0; i < held_count[layer]; i++)
        if (held[layer][i].note == note && held[layer][i].src == src) return 1;
    return 0;
}

/* A layer on a MIDI channel (SINGL: layer 1 only), as the Program Change and the controllers find it. */
static int layer_on_channel(uint8_t layer, uint8_t channel)
{
    return (perf_state.midi_ch[layer] & 0x0Fu) == channel && (layer == 0u || rt_perf_mode() != PERF_SINGL_SYNTH);
}

/* SUSTAIN up or down on one layer. Up: each voice it held whose key is not down again is let go (a mono layer falls
 * back to a key still down, as a note-off does) */
SYNTH_COLD static void sustain_set(uint8_t layer, uint8_t down)
{
    sus_on[layer] = down;
    if (down) return;
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        if (!(sus_voices & (1u << v))) continue;
        SynthVoiceNode *s = &poly_bank[v];
        if (s->layer != layer) continue;
        sus_voices &= (uint8_t)~(1u << v);
        if (s->is_gate_open && !held_has(layer, s->active_note, s->src))
            note_off_layer(layer, s->active_note, s->src);
    }
}

/* ALL NOTES OFF (CC 123, and the mode messages 124..127): the keys' notes on the layer, as if every key were let go
 * (the pedal still holds them, as the MIDI spec has it). The sequencers' notes play on. */
SYNTH_COLD static void layer_keys_off(uint8_t layer)
{
    for (uint8_t i = held_count[layer]; i-- > 0u;)
        if (held[layer][i].src == VSRC_KEYS) held_remove(layer, held[layer][i].note, VSRC_KEYS);
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        const SynthVoiceNode *s = &poly_bank[v];
        if (s->layer == layer && s->is_gate_open && s->src == VSRC_KEYS)
            layer_note_off(layer, s->active_note, VSRC_KEYS);
    }
}

/* ALL SOUND OFF (CC 120): every voice of the layer, whoever struck it, fades out over a few ms (a choke, no click);
 * the effects' tails ring on */
SYNTH_COLD static void layer_sound_off(uint8_t layer)
{
    held_count[layer] = 0;
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        SynthVoiceNode *s = &poly_bank[v];
        if (s->layer != layer || voice_is_free(s)) continue;
        sus_voices &= (uint8_t)~(1u << v);
        s->is_gate_open = 0;
        if (!s->choke) {
            s->choke = 1;
            s->choke_q15 = 32767;
        }
    }
}

/* A Control Change the map acts on itself; 0 hands it on to midi_io (the wheel, CUSTOM CC1 / CC2). A CUSTOM CC set to
 * one of these numbers keeps it (GLOBAL's choice wins), the channel mode messages excepted. */
SYNTH_COLD static int cc_apply(uint8_t channel, uint8_t cc, uint8_t value)
{
    if (cc < 120u && ((perf_state.cc1_num != 0u && cc == perf_state.cc1_num) ||
                      (perf_state.cc2_num != 0u && cc == perf_state.cc2_num)))
        return 0;
    switch (cc) {
    case 0u:                                       /* BANK SELECT MSB: what the next Program Change loads */
        if (value < SYNTH_PCBANK_COUNT) pc_bank[channel] = value;
        return 1;
    case 32u:                                      /* BANK SELECT LSB: one bank of each kind, nothing to pick */
        return 1;
    case 64u:                                      /* SUSTAIN */
        for (uint8_t l = 0; l < NLAYER; l++)
            if (layer_on_channel(l, channel) && sus_on[l] != (value >= 64u)) sustain_set(l, (uint8_t)(value >= 64u));
        return 1;
    case 120u:                                     /* ALL SOUND OFF */
        for (uint8_t l = 0; l < NLAYER; l++)
            if (layer_on_channel(l, channel)) layer_sound_off(l);
        return 1;
    case 121u:                                     /* RESET ALL CONTROLLERS (RP-015: the bank and the volumes stay) */
        master_midi.mod_wheel_val = 0;
        master_midi.aftertouch_val = 0;
        master_midi.pitch_bend_val = 0;
        master_midi.custom_cc1_val = master_midi.custom_cc2_val = 0;
        for (uint8_t l = 0; l < NLAYER; l++) {
            if (!layer_on_channel(l, channel)) continue;
            if (sus_on[l]) sustain_set(l, 0u);
            for (int v = 0; v < POLYPHONY_VOICES; v++)
                if (poly_bank[v].layer == l) poly_bank[v].poly_at = 0;
        }
        return 1;
    case 122u:                                     /* LOCAL CONTROL: the panel's keys always play */
        return 1;
    default:
        if (cc >= 123u) {                          /* ALL NOTES OFF; OMNI OFF / ON, MONO, POLY count as one */
            for (uint8_t l = 0; l < NLAYER; l++)
                if (layer_on_channel(l, channel)) layer_keys_off(l);
            return 1;
        }
        if (cc == 1u)                              /* the wheel: midi_io's */
            return 0;
        if (cc == 99u || cc == 98u || cc == 101u || cc == 100u) {   /* NRPN / RPN number: MSB (odd), LSB (even) */
            par_kind[channel] = cc >= 100u ? SYNTH_CCK_RPN : SYNTH_CCK_NRPN;
            if (cc & 1u) par_hi[channel] = value;
            else par_lo[channel] = value;
            par_dm[channel] = 0xFFu;
            return 1;
        }
        if (!instrument.cc_in)                     /* GLOBAL CC IN off: the map, NRPNs and RPNs not taken */
            return 1;
        if (cc == 6u || cc == 38u) {               /* DATA ENTRY MSB, LSB: the selected parameter (not the null 127 /
                                                    * 127). The LSB refines the MSB before it (a value past 127: the
                                                    * tempo, the tuning); an MSB alone is LSB 0 */
            if (par_kind[channel] == SYNTH_CCK_CC || (par_hi[channel] & par_lo[channel]) == 127u)
                return 1;
            if (cc == 6u)
                par_dm[channel] = value;
            else if (par_dm[channel] == 0xFFu)
                return 1;                          /* (an LSB with no MSB: nothing to refine) */
            cc_queue((uint8_t)(channel | par_kind[channel] << 4), par_hi[channel], par_lo[channel], par_dm[channel],
                     cc == 6u ? 0u : value);
            return 1;
        }
        if (cc == 96u || cc == 97u)                /* INCREMENT, DECREMENT: not taken */
            return 1;
        cc_queue(channel, cc, 0u, value, 0u);      /* the sound's controllers: for the panel (synth_cc_take) */
        return 1;
    }
}

SYNTH_COLD int synth_cc_take(uint8_t e[5])
{
    const uint8_t *q;
    if (cc_qt == cc_qh) return 0;
    q = cc_q[cc_qt % CC_Q_MAX];
    e[0] = q[0];
    e[1] = q[1];
    e[2] = q[2];
    e[3] = q[3];
    e[4] = q[4];
    cc_qt++;                                       /* (after the read: the audio side may overwrite q[3] till then) */
    return 1;
}

SYNTH_COLD int synth_pc_take(uint8_t *bank, uint8_t *slot)
{
    uint8_t b = pc_req_bank;
    if (b == 0xFFu) return 0;
    *slot = pc_req_slot;
    *bank = b;
    pc_req_bank = 0xFFu;
    return 1;
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
    if (voice_room)                                /* the measured room (voice_allocator.h): a released voice above it is stolen */
        for (uint8_t l = 0; l < NLAYER; l++) {
            int af, an;
            pool_alloc_range(l, &af, &an);
            if (an >= count[l]) continue;
            for (int v = first[l]; v < first[l] + count[l]; v++) {
                SynthVoiceNode *s = &poly_bank[v];
                if (v >= af && v < af + an) continue;
                if (s->is_gate_open || s->silent || s->choke || s->layer != l) continue;
                s->choke = 1;
                s->choke_q15 = 32767;
            }
        }
    for (; voice_tail_cut; voice_tail_cut--) {      /* the overload's tail cuts: the quietest ringing release first */
        SynthVoiceNode *q = 0;
        for (int v = 0; v < POLYPHONY_VOICES; v++) {
            SynthVoiceNode *s = &poly_bank[v];
            if (s->is_gate_open || s->silent || s->choke) continue;
            if (!q || s->amp_env.level_q15 < q->amp_env.level_q15) q = s;
        }
        if (!q) {
            voice_tail_cut = 0;
            break;
        }
        q->choke = 1;
        q->choke_q15 = 32767;
    }
}

/* ----------------------------------------------------------- lifecycle --- */

SYNTH_COLD void synth_init(void)
{
    lfo_tables_init();                         /* (the LFO shapes' RAM tables, before anything reads one) */
    global_init();
    preset_defaults();
    pcm_osc_init();
    synth_restart();
}

static void room_reset(void);
SYNTH_COLD void synth_restart(void)
{
    room_reset();                              /* (a fresh start measures afresh: the table until it has) */
    midi_io_init(&master_midi);
    midi_clock_init();
    voice_allocator_init();
    noise_shared_init();
    fx_init();
    mfx_init();
    for (unsigned l = 0; l < NLAYER; l++) {
        global_lfo1_state[l].phase_accumulator = 0;
        global_lfo1_state[l].envelope_amp_q15 = 32767;
    }
    for (unsigned l = 0; l < NLAYER; l++)
        held_count[l] = sus_on[l] = 0;         /* (the voices are gone: a key still down at restart has no note to stack) */
    sus_voices = 0;
    for (unsigned c = 0; c < 16u; c++) {
        pc_bank[c] = SYNTH_PCBANK_PATCH;
        par_kind[c] = SYNTH_CCK_CC;                /* (no NRPN / RPN selected) */
        par_dm[c] = 0xFFu;
    }
    pc_req_bank = 0xFFu;
    seq_qn = 0;
    ext_h = ext_n = 0;
    maq_init(&maq, seq_lanes, &perf_state, rt_seq_out);     /* its notes and controllers take the MIDI path */
    carp_init(&carp, &arp_state, &perf_state, rt_arp_out);
    krec_init(&krec, rt_rec_out);
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

static void midi_in(uint8_t status, uint8_t data1, uint8_t data2, uint8_t src)
{
    if (in_pass) {                             /* a MAQ / ARP event mid-pass */
        if (status < 0xF8u && voices_done < pass_frame && seq_qn < SEQ_Q_MAX) {
            seq_q[seq_qn].st = status;         /* between boundaries: at the next one (voices_up_to) */
            seq_q[seq_qn].d1 = data1;
            seq_q[seq_qn].d2 = data2;
            seq_q[seq_qn].src = src;
            seq_qn++;
            return;
        }
        voices_up_to(pass_frame);              /* on a boundary (no-op), realtime, or the queue full: exact */
    }
    midi_apply(status, data1, data2, src);
}

void synth_midi_3(uint8_t status, uint8_t data1, uint8_t data2)
{
    midi_in(status, data1, data2, VSRC_KEYS);
}

/* the sequencers' MIDI out, each note tagged with its source (VSRC_*) */
static void rt_seq_out(uint8_t status, uint8_t data1, uint8_t data2) { midi_in(status, data1, data2, VSRC_SEQ); }
static void rt_arp_out(uint8_t status, uint8_t data1, uint8_t data2) { midi_in(status, data1, data2, VSRC_ARP); }
static void rt_rec_out(uint8_t status, uint8_t data1, uint8_t data2) { midi_in(status, data1, data2, VSRC_REC); }

/* The REC page's CLICK (2026-11-13): a beat's blip, a sine rung by the recorder's tick (krec.click_hit) and decaying
 * over ~35 ms, added to the output after the limiter (no voice, no FX). A pass without one adds nothing. */
#define CLICK_E_BAR 9300                       /* 2 sin(pi f / fs), Q15: ~2.0 kHz on a bar's first beat */
#define CLICK_E_BEAT 6524                      /* .. ~1.4 kHz on the others */
static int32_t click_x, click_y, click_env, click_e;
static uint32_t pass_base;                     /* (its definition below, with the pass) */
static void rec_click_mix(int32_t *out, uint32_t frames)
{
    uint32_t f = 0;
    if (krec.click_hit) {
        uint32_t off = krec.click_frame - pass_base;
        f = off < frames ? off : 0u;           /* (a click due earlier: at once) */
        click_e = krec.click_hit == 2u ? CLICK_E_BAR : CLICK_E_BEAT;
        click_env = krec.click_hit == 2u ? 32767 : 21000;
        click_x = 0;
        click_y = 12000;
        krec.click_hit = 0;
    }
    if (click_env <= 0)
        return;
    for (; f < frames && click_env > 0; f++) {
        int32_t v, l, r;
        click_x += (click_e * click_y) >> 15;  /* (the magic circle: a steady sine in integers) */
        click_y -= (click_e * click_x) >> 15;
        v = (click_y * click_env) >> 15;
        click_env -= (click_env >> 8) + 1;     /* (+1: >> 8 alone stalls at 255 and the sine rings on as a whine) */
        l = out[f * 2u] + v;
        r = out[f * 2u + 1u] + v;
        out[f * 2u] = l > Q15_MAX ? Q15_MAX : l < Q15_MIN ? Q15_MIN : l;
        out[f * 2u + 1u] = r > Q15_MAX ? Q15_MAX : r < Q15_MIN ? Q15_MIN : r;
    }
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

SYNTH_COLD static void midi_apply(uint8_t status, uint8_t data1, uint8_t data2, uint8_t src)
{
    uint8_t kind = status & 0xF0u;
    uint8_t channel = status & 0x0Fu;
    if (status >= 0xF8u) {
        midi_clock_realtime(status);
        return;
    }
    if (!channel_routed(channel)) return;          /* not on a layer's channel */
    if (src == VSRC_KEYS)                          /* the keys (the panel's, MIDI in): the REC page's loop hears them */
        krec_input(&krec, status, data1, data2, sample_clock);

    if (kind == 0x90u && data2 != 0u) {
        int32_t weight[NLAYER];
        route_note(channel, data1, data2, weight);
        midi_parse_three_bytes(&master_midi, status, data1, data2);
        for (uint8_t l = 0; l < NLAYER; l++)
            if (weight[l] > 0) rt_voice_start(l, data1, data2, src, weight[l]);
    } else if (kind == 0x80u || (kind == 0x90u && data2 == 0u)) {
        midi_parse_three_bytes(&master_midi, status, data1, data2);
        note_off(data1, src);
    } else if (kind == 0xA0u) {                    /* POLY AT (2026-11-03): the note's own pressure, on its layers' voices */
        for (int i = 0; i < POLYPHONY_VOICES; i++) {
            SynthVoiceNode *v = &poly_bank[i];
            if (v->active_note != data1 || voice_is_free(v) || v->layer >= NLAYER ||
                (perf_state.midi_ch[v->layer] & 0x0Fu) != channel || (src == VSRC_SEQ && v->src != VSRC_SEQ))
                continue;                          /* (the SEQ's presses only its own notes) */
            v->poly_at = data2 & 0x7Fu;
            v->poly_seen = 1u;
        }
    } else if (kind == 0xC0u) {                    /* Program Change: what the channel's BANK SELECT picked */
        uint8_t bank = pc_bank[channel];
        if (!instrument.pc_in) {
            /* (GLOBAL PC IN off) */
        } else if (bank == SYNTH_PCBANK_PATCH) {          /* the stored patch into every layer on this channel */
            for (uint8_t l = 0; l < NLAYER; l++)
                if (layer_on_channel(l, channel))
                    patch_load(l, data1 & 0x7Fu);
        } else if (channel == (perf_state.midi_ch[0] & 0x0Fu)) {   /* the rest: Layer 1's channel, the panel loads */
            pc_req_slot = data1 & 0x7Fu;
            pc_req_bank = bank;
        }
    } else if (kind == 0xB0u && cc_apply(channel, data1 & 0x7Fu, data2 & 0x7Fu)) {
        /* (the map's own controllers) */
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

/* the measured room (synth_runtime.h): the clock, this pass's voice runs, the floor's tracker (per mille, Q8) and the
 * voices' cost a priced tenth (per mille x 256: Q8) */
static uint32_t (*pass_clock)(void);
static uint32_t pass_tpf;
static uint32_t vrun_ticks;
static uint32_t floor_q8, floor_passes, slope_q8 = 95u * 256u / 10u;   /* (before any is measured: the table's 0.95 %) */
static uint32_t slope_q16 = 95u * 65536u / 10u;                        /* (slope_q8's mean, 8 more bits) */
static uint32_t half_ticks, half_frames, load_q8, spike_q8;   /* the halves' loads: their mean, the spikes over it */
static uint32_t hs_total, hs_vrun, hs_frames, hs_passes, hs_c0, hs_c1, hs_guard, hs_last_t, hs_last_f;   /* the spike watch (hs_notes, hs_steals: above) */
static uint32_t hs_last[4];
const uint32_t *synth_half_last(void) { return hs_last; }
void synth_half_take(uint32_t o[4])
{
    uint32_t b = hs_frames * pass_tpf;
    o[0] = b ? (hs_total > hs_vrun ? hs_total - hs_vrun : 0u) * 1000u / b : 0u;
    o[1] = b ? hs_vrun * 1000u / b : 0u;
    o[2] = (hs_notes > 255u ? 255u : hs_notes) | (hs_steals > 255u ? 255u : hs_steals) << 8 |
           (hs_passes > 255u ? 255u : hs_passes) << 16 | (hs_guard > 255u ? 255u : hs_guard) << 24;
    o[3] = (hs_c0 > 255u ? 255u : hs_c0) | (hs_c1 > 255u ? 255u : hs_c1) << 8;
    hs_total = hs_vrun = hs_frames = hs_notes = hs_steals = hs_passes = hs_c0 = hs_c1 = hs_guard = 0;
    synth_ctrl_guard = 0;                          /* (a new half starts unguarded) */
    for (unsigned i = 0; i < 4u; i++) hs_last[i] = o[i];
}
void synth_set_clock(uint32_t (*now)(void), uint32_t ticks_per_frame)
{
    pass_clock = now;
    pass_tpf = ticks_per_frame;
}
uint32_t synth_floor_pm(void) { return floor_q8 >> 8; }
uint32_t synth_slope_q8(void) { return slope_q8; }

/* A pass into the measured room (voice_allocator.h voice_room), 2026-11-14:
 *  - the floor, its time less its voice runs (the effects, the MFX, the clock and sequencers, the notes' handling),
 *    its mean over ~32 passes (90 ms) both ways (a first version followed its peaks, up in 8 passes and down in 500:
 *    a light patch with the SEQ running lost 2 of 8 voices, and a chord struck 1 s after a heavy sequence stopped
 *    got half its voices);
 *  - the slope, what a priced tenth of voices (PresetStructures.h varch_voice_cost) on the busier core costs in wall
 *    time this pass, up over ~64 passes and down over ~512 (~1.5 s), measured only while 2 voices' worth or more sound.
 *    The table's fixed 0.95 % was ~2x high for SINGLE voices and low for STACK + PCM ones (the PERF TEST with
 *    SYNTH_ROOM_OFF, 2026-11-14). Over ~32 passes (until 2026-11-18) it followed each chord: a sustained PCM choir
 *    costs ~0.75 a tenth, its strike ~1.2..1.5, so between strikes the room opened to 8 voices and the next chord
 *    struck all 8 (ALL SOULS played live: 8 voices a third of the time, peaks 97..106 %, a late half every ~40 s;
 *    SAMHAIN peaks 108 %). Up fast and down slow (A/B on the FM1, 40 s of PLAY + a chord a second, 3 runs): ALL SOULS
 *    4..6 voices, peaks 89..93, no late half; CAIRN SPIRIT 4, SAMHAIN 3 on layer 2, no late half; SIDHE DOOR and WILD
 *    HUNT keep their 8. (Down slow alone, 512 both ways, still let 8 in: late halves in 3 of 5 runs.);
 *  - the spikes: each half's load over the halves' mean, the biggest held and let down over ~3 s;
 *  - the room: the tenths a core may carry for the pass to land on VOICE_ROOM_TARGET_PM, (target - floor) / slope,
 *    and for the mean plus the held spike to stay under VOICE_ROOM_PEAK_PM (whichever is less),
 *    moved only by 2 or more (no flapping). Fitted on that run: the rows that passed keep their voices, CAIRN SPIRIT
 *    (74 %, peaks 82..89) loses one on layer 2. */
static void floor_pass(uint32_t total, uint32_t frames)
{
    uint32_t budget = frames * pass_tpf, pm, vpm, room, w[NLAYER], core[2] = {0u, 0u}, t;
    if (!budget) return;
    pm = (total > vrun_ticks ? total - vrun_ticks : 0u) * 1000u / budget;
    vpm = vrun_ticks * 1000u / budget;
    if (pm > 1000u) pm = 1000u;
    for (unsigned l = 0; l < NLAYER; l++) w[l] = varch_voice_cost(&engine_state[l]);
    for (unsigned v = 0; v < POLYPHONY_VOICES; v++)
        if (!voice_is_free(&poly_bank[v]))
            core[(CORE1_VOICES >> v) & 1u] += w[poly_bank[v].layer < NLAYER ? poly_bank[v].layer : 0u];
    t = core[0] > core[1] ? core[0] : core[1];
    hs_total += total;                             /* (the spike watch) */
    hs_vrun += vrun_ticks;
    hs_frames += frames;
    hs_passes++;
    hs_last_t = total;                             /* (the deadline guard's estimate of the passes to come) */
    hs_last_f = frames;
    if (t > (hs_c0 > hs_c1 ? hs_c0 : hs_c1)) {
        hs_c0 = core[0];
        hs_c1 = core[1];
    }
    half_ticks += total;                           /* the spikes, by halves (512 frames: the FM1's deadline) */
    half_frames += frames;
    if (half_frames >= 512u) {
        uint32_t h = half_ticks * 1000u / (half_frames * pass_tpf) << 8, s;
        half_ticks = half_frames = 0;
        if (!load_q8) load_q8 = h;
        if (h > load_q8) load_q8 += (h - load_q8) >> 4;   /* (the mean, ~0.2 s) */
        else load_q8 -= (load_q8 - h) >> 4;
        s = h > load_q8 ? h - load_q8 : 0u;        /* this half's spike: held, then let down over ~3 s */
        spike_q8 = s > spike_q8 ? s : spike_q8 - (spike_q8 >> 8);
    }
    if (floor_passes < 64u) {                      /* (the first passes: the start, settling) */
        floor_passes++;
        floor_q8 = pm << 8;
        return;
    }
    if ((pm << 8) > floor_q8) floor_q8 += ((pm << 8) - floor_q8) >> 5;   /* (its mean: the target's margin takes */
    else floor_q8 -= (floor_q8 - (pm << 8)) >> 5;                        /* the bursts; ~90 ms both ways) */
    if (t >= 20u) {
        uint32_t s = (vpm << 8) / t;
        s <<= 8;                                   /* (Q16: a 512-pass mean in Q8 would stick 0.2 % a tenth wide) */
        if (s > slope_q16) slope_q16 += (s - slope_q16) >> 6;   /* (up in ~64 passes: a strike's cost counts at once */
        else slope_q16 -= (slope_q16 - s) >> 9;                 /* .. down in ~512, ~1.5 s: a held chord's lull does not) */
        if (slope_q16 < 64u << 8) slope_q16 = 64u << 8;   /* (0.25 % a tenth at least: a room stays finite) */
        slope_q8 = slope_q16 >> 8;
    }
    pm = floor_q8 >> 8;
    room = pm < VOICE_ROOM_TARGET_PM ? ((VOICE_ROOM_TARGET_PM - pm) << 8) / slope_q8 : 1u;
    {                                              /* the peak's limit: the mean plus the held spike under the peak target */
        uint32_t sp = spike_q8 >> 8, rp = pm + sp < VOICE_ROOM_PEAK_PM ? ((VOICE_ROOM_PEAK_PM - pm - sp) << 8) / slope_q8 : 1u;
        if (rp < room) room = rp;
    }
    if (room < 1u) room = 1u;
    if (room > 255u) room = 255u;
#if !defined(SYNTH_ROOM_OFF)                       /* (-D SYNTH_ROOM_OFF: measured, not applied: the calibration) */
    if (!voice_room || room + 2u <= voice_room || room >= voice_room + 2u)
        voice_room = (uint8_t)room;
#else
    (void)room;
#endif
}

static void room_reset(void)
{
    floor_passes = 0;
    voice_room = 0;
    half_ticks = half_frames = load_q8 = spike_q8 = 0;
}

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
                voice_tick_shift(voice);               /* the tick's length (SYNTH_CTRL_ADAPT) */
#if defined(SYNTH_TICK_COUNT)                      /* (host studies only: the ticks a pass, fast and all) */
                synth_tick_count[0]++;
                synth_tick_count[1] += voice->ctrl_shift == SYNTH_CTRL_SHIFT;
#endif
                voice_glide_advance(voice);
                voice_control_tick(voice, &in);
                voice->ctrl_left = (uint8_t)(1u << voice->ctrl_shift);
                SPROF_LAP(SPROF_CTRL);
            }
            uint32_t n = to - f;
            if (n > voice->ctrl_left) n = voice->ctrl_left;
            if (n > SYNTH_CTRL_PERIOD) n = SYNTH_CTRL_PERIOD;   /* (a long tick renders in blocks) */
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

/* WHICH VOICES CORE 1 RENDERS, RUN BY RUN (2026-11-12). A run lasts as long as the busier core, and the fixed sets
 * (CORE1_VOICES: the odd voices) are only even when the sounding voices happen to alternate: three notes held with the
 * middle one let go, a layer on an odd count, a cheap layer below a dear one (DUAL: SPREAD under STACK) leave one core
 * carrying up to a whole voice more than the other while that one waits. So each run splits the voices that sound by
 * what they cost (the pool's price of their layer's patch, varch_voice_cost): the split whose busier core carries the
 * least, searched over how many of each layer's voices go to core 1 (at most 9 x 9 counts). The fixed sets are kept
 * whenever they are already as good (so a full chord renders exactly as before: the same voices into the same bus);
 * otherwise the voices that were on core 1 stay there first, then the odd ones, so few change core. A split is worked
 * out again only when the sounding voices, their layers or the prices change. With nothing for core 1 it is not
 * called at all. The buses are summed in the one order as ever; a voice that changes bus changes the float sums'
 * rounding, nothing else (measured over the render-hash scenes: 0.03 % of the samples differ, by at most 4 of 32768).
 * SYNTH_CORE_BALANCE 0 (the default until the user has decided on that rounding change: the render hashes stay as
 * they are): the fixed sets, exactly as before. Measured on the FM1 2026-11-13 (same build, PERF TEST): +0.1 points
 * median with it on, the rows being even already; what it gains is for uneven DUAL / SPLIT pairs, not yet measured. */
#ifndef SYNTH_CORE_BALANCE
#define SYNTH_CORE_BALANCE 0
#endif
#if SYNTH_CORE_BALANCE
static uint8_t layer_w[NLAYER] = {10u, 10u};   /* each layer's voice price for the pass (render_pass) */
#endif
static uint32_t core1_set = CORE1_VOICES & ((1u << POLYPHONY_VOICES) - 1u);   /* the run's voices on core 1 */
#if SYNTH_CORE_BALANCE
static uint32_t split_key = 0xFFFFFFFFu, split_set;
#if defined(SYNTH_BAL_STAT)                    /* (host: what the fixed sets and the split each leave the busier core) */
uint32_t synth_bal_stat[3];
#endif

static uint32_t core1_pick(uint32_t frames)
{
    const uint32_t all = (1u << POLYPHONY_VOICES) - 1u, odd = CORE1_VOICES & all;
    uint32_t live = 0, lay = 0;
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        const SynthVoiceNode *s = &poly_bank[v];
        if (voice_is_free(s)) continue;
        live |= 1u << v;
        if (s->layer == 1u) lay |= 1u << v;
    }
    (void)frames;
    if (!live) return 0u;
    uint32_t key = live | lay << 8 | (uint32_t)layer_w[0] << 16 | (uint32_t)layer_w[1] << 24;
    if (key != split_key) {
        const uint32_t wa = layer_w[0], wb = layer_w[1], la = live & ~lay, lb = live & lay;
        uint32_t na = 0, nb = 0, pa = 0, pb = 0;           /* each layer's sounding voices, and those on the odd set */
        for (int v = 0; v < POLYPHONY_VOICES; v++) {
            na += la >> v & 1u;
            nb += lb >> v & 1u;
            pa += la & odd & 1u << v ? 1u : 0u;
            pb += lb & odd & 1u << v ? 1u : 0u;
        }
        const uint32_t total = na * wa + nb * wb;
        uint32_t c1 = pa * wa + pb * wb, fixed = c1 > total - c1 ? c1 : total - c1, best = fixed, bi = pa, bj = pb;
        for (uint32_t i = 0; i <= na; i++)
            for (uint32_t j = 0; j <= nb; j++) {
                c1 = i * wa + j * wb;
                /* (core 1 starts a hand-off late: a tie leaves core 0 the larger share) */
                uint32_t m = c1 > total - c1 ? c1 : total - c1;
                if (m < best || (m == best && best < fixed && c1 < bi * wa + bj * wb)) {
                    best = m;
                    bi = i;
                    bj = j;
                }
            }
        uint32_t set = odd & live;
        if (best < fixed) {                                 /* bi of layer 1's, bj of layer 2's: last run's first, then the odd */
            static const uint32_t pref[3] = {0u, CORE1_VOICES, 0xFFFFFFFFu};
            set = 0;
            for (int p = 0; p < 3; p++) {
                uint32_t from = p == 0 ? split_set : pref[p];
                for (int v = 0; v < POLYPHONY_VOICES; v++) {
                    uint32_t bit = 1u << v;
                    if (!(live & from & bit) || (set & bit)) continue;
                    if (lay & bit) {
                        if (bj) { set |= bit; bj--; }
                    } else if (bi) { set |= bit; bi--; }
                }
            }
        }
        split_key = key;
        split_set = set;
#if defined(SYNTH_BAL_STAT)
        synth_bal_stat[2] = fixed << 16 | best;             /* (kept for the frames below) */
#endif
    }
#if defined(SYNTH_BAL_STAT)
    synth_bal_stat[0] += (synth_bal_stat[2] >> 16) * frames;
    synth_bal_stat[1] += (synth_bal_stat[2] & 0xFFFFu) * frames;
#endif
    return split_set;
}
#endif

/* core 1's share of a run: its voice set into its own bus. Reads what core 0 holds still until it has waited. */
static void core1_job(uint32_t arg)
{
    (void)arg;
    render_voice_run(&mix_bus[1], core1_set, core1_from, core1_to);
}

/* the effects' buffers for one pass, and core 1's job on them: the reverb's back (fx_reverb_back: the tank) */
static int32_t fx_dly[RENDER_PASS_FRAMES], fx_dly_l[RENDER_PASS_FRAMES], fx_dly_r[RENDER_PASS_FRAMES];
static int32_t fx_rev_in[RENDER_PASS_FRAMES];
/* each layer's dry sum (each row on cache lines of its own: layer 2's MFX may run on core 1, mfx_job) */
static float layer_l[NLAYER][RENDER_PASS_FRAMES] __attribute__((aligned(64)));
static float layer_r[NLAYER][RENDER_PASS_FRAMES] __attribute__((aligned(64)));
PS_STATIC_ASSERT(sizeof layer_l[0] % 64u == 0u, "a layer's sum ends on a cache line");
static int32_t fx_rev_l[RENDER_PASS_FRAMES] __attribute__((aligned(64)));   /* core 1 writes these two (fx.c FX_LINE) */
static int32_t fx_rev_r[RENDER_PASS_FRAMES] __attribute__((aligned(64)));
PS_STATIC_ASSERT(sizeof fx_rev_l % 64u == 0u, "core 1's output buffers end on a cache line");
static uint32_t fx_frames;
/* core 1's share of the MFX (2026-11-02): arg the layer; + 2: only its right side, of the pass planned (one MFX on:
 * core 0 runs the left), else the whole layer (both on: core 0 runs layer 1's) */
PS_STATIC_ASSERT(RENDER_PASS_FRAMES <= MFX_PASS_MAX, "a pass fits one MFX plan");
static void mfx_job(uint32_t arg)
{
    int l = (int)(arg & 1u);
    if (arg & 2u)
        mfx_layer_side(l, 1, layer_r[l]);
    else
        mfx_layer_block(l, layer_l[l], layer_r[l], fx_frames);
}
/* The MIX page's BALANCE (fx.h fx_live, 2026-11-02): DUAL / SPLIT the layers' faders, SINGL DRY / WET. 64: all at 1
 * (and nothing done: the render as before, bit for bit); towards an end the other side falls as the square of the
 * distance (-20 dB at ~70 % of the way, silent at the end). Each gain glides (~23 ms, as the knobs), ramped over a pass */
enum { MIXG_L1, MIXG_L2, MIXG_DRY, MIXG_WET, MIXG_N };
static float mix_g[MIXG_N] = {1.f, 1.f, 1.f, 1.f};

static void mix_targets(int single, float *t)
{
    uint32_t k = fx_live[FX_L_BALANCE] > 127u ? 127u : fx_live[FX_L_BALANCE];
    float lo = k < 64u ? (float)k * (1.f / 64.f) : 1.f, hi = k > 64u ? (float)(127u - k) * (1.f / 63.f) : 1.f;
    for (int i = 0; i < MIXG_N; i++) t[i] = 1.f;
    lo *= lo;
    hi *= hi;
    if (single) { t[MIXG_WET] = lo; t[MIXG_DRY] = hi; }      /* towards 0 the effects fade, towards 127 the dry */
    else { t[MIXG_L2] = lo; t[MIXG_L1] = hi; }               /* towards 0 layer 2 fades, towards 127 layer 1 */
}

/* gain i a pass on towards `to`: its start and end (*g0, *g1); 0 when it is 1 throughout (nothing to do) */
static int mix_step(int i, float to, float *g0, float *g1)
{
    float g = mix_g[i];
    *g0 = g;
    if (g != to) {
        g += (to - g) * 0.12f;                                /* (a pass, 2.9 ms: ~23 ms) */
        if (g - to < 1e-4f && to - g < 1e-4f) g = to;
        mix_g[i] = g;
    }
    *g1 = g;
    return *g0 != 1.f || g != 1.f;
}

static void mix_apply(float g0, float g1, uint32_t n, float *a, float *b)
{
    float d = (g1 - g0) * (1.f / (float)n), g = g0;
    for (uint32_t s = 0; s < n; s++) {
        g += d;
        a[s] *= g;
        b[s] *= g;
    }
}

static void fx_reverb_job(uint32_t arg)
{
    (void)arg;
    fx_reverb_back(&perf_state, fx_rev_l, fx_rev_r, fx_frames);
    fx_cross_rev_job(fx_rev_l, fx_rev_r, fx_frames);   /* REV > DLY's chain on what it just made, for the next pass */
}

/* The sequencers' frame: the MAQ, the ARP, and what each tells the other for the next. 1 when both were idle: their
 * frames then change nothing but their own frame and pulse stamps until a MIDI event (the UI writes only between
 * passes; MIDI Start / Stop are events), so the render skips them (seq_quiet) and runs the last one skipped before
 * the next event, or at the pass's end: the same writes, at the same clock (2026-11-02: ~29 of a sample's 544 ticks
 * went here at rest, every frame, while core 1 waited) */
static uint8_t seq_frame(uint32_t clock, int late, uint32_t pulses_before)
{
    uint8_t idle = late ? maq_frame_late(&maq, clock, pulses_before)
                        : maq_frame(&maq, clock);   /* sequencer events land on this frame (synth_midi_3 catches up) */
    idle &= carp_frame(&carp, clock, &maq);    /* the ARP: its own transport, a start synced to the MAQ's bar */
    maq_set_ext_transpose(&maq, carp_maq_transpose(&carp));   /* 8.4: the MAQ reads it on the next frame */
    maq_set_sync(&maq, carp_running(&carp), carp_origin(&carp));   /* (.. and the ARP's bar, for a start) */
    maq_set_genre(&maq, arp_state.current_genre_bank);       /* (SCALE = GENRE lanes follow it) */
    krec_set_sync(&krec, (uint8_t)(maq_free_running(&maq) | carp_running(&carp)),   /* (a loop starts on their bar) */
                  maq_free_running(&maq) ? maq_origin(&maq) : carp_origin(&carp));
    idle &= krec_frame(&krec, clock);          /* the REC page's loop */
    return idle;
}

/* Playing (2026-11-02): the earliest frame either sequencer has something due on (1), or none scheduled (0). Between
 * clock pulses a frame before it writes only their stamps: skipped as above, up to the next pulse, event or this
 * frame. The clock itself runs every frame (an external clock's lock plays its pulses on their frames), and a frame
 * with a pulse or anything due runs in full, so every note goes out on the frame it did. */
static uint8_t seq_next_due(uint32_t *wake)
{
    uint32_t a, b;
    uint32_t c;
    uint8_t ha = maq_next_due(&maq, &a), hb = carp_next_due(&carp, &b), hc = krec_next_due(&krec, &c);
    *wake = ha && (!hb || (int32_t)(a - b) < 0) ? a : b;
    if (hc && (!(ha | hb) || (int32_t)(c - *wake) < 0)) *wake = c;
    return (uint8_t)(ha | hb | hc);
}

/* Every voice through the frame before `frame`: called before the global work changes what the voices read. */
static void voices_up_to(uint32_t frame)
{
    if (voices_done >= frame) return;
    uint32_t all = (1u << POLYPHONY_VOICES) - 1u;
#if SYNTH_CORE_BALANCE
    uint32_t set1 = core1_pick(frame - voices_done);       /* (0: no voice of core 1's sounds, it is not called) */
#else
    uint32_t set1 = CORE1_VOICES & all;
#endif
    core1_from = voices_done;
    core1_to = frame;
    core1_set = set1;
    uint32_t vt0 = pass_clock ? pass_clock() : 0u;
    int on2 = set1 && core2_run && core2_run(core1_job, 0u);   /* core 1 starts on its set ... */
    render_voice_run(&mix_bus[0], all & ~set1, voices_done, frame);   /* ... while core 0 renders its own */
    if (set1 && (!on2 || core2_wait()))
        core1_job(0u);                         /* one core (or the second failed): here */
    if (pass_clock) vrun_ticks += pass_clock() - vt0;   /* (the measured room: the voices' wall time) */
    voices_done = frame;
    for (uint8_t i = 0; i < seq_qn; i++)       /* the sequencers' events that waited for this boundary, in order */
        midi_apply(seq_q[i].st, seq_q[i].d1, seq_q[i].d2, seq_q[i].src);
    seq_qn = 0;
}

/* The shared noise for the pass (oscmixer.h noise_buf), before any voice reads it: an ear and its colours only when a
 * playing voice's layer can make them (its noise level, or a source on it; its colour morph, or a source on that),
 * or the voice's own colour segment needs them now (a MOD SEND on the colour). */
static void noise_pass_fill(uint32_t frames)
{
    static const uint8_t seg_wants[8] = {2u, 6u, 4u, 4u};   /* the colours each segment reads (4: the voice's own) */
    uint8_t want[2] = {0, 0};                  /* bit 0 white, 1 pink, 2 brown */
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        const SynthVoiceNode *s = &poly_bank[v];
        if (voice_is_free(s)) continue;
        const OscMixerParams *m = &engine_state[s->layer < NLAYER ? s->layer : 0u].oscmix;
        if (m->noise_level == 0 && m->noise_mod_amt == BIPOLAR_CENTER && s->mix.noise_gain_q15 <= 0) continue;
        int col_mod = m->noise_col_mod_amt != BIPOLAR_CENTER, c = m->noise_colour;
        uint8_t w = (uint8_t)(1u | ((c > 0 && c < (int)NOISE_BROWN) || col_mod ? 2u : 0u) |
                              ((c > (int)NOISE_PINK && c < (int)NOISE_BAND) || col_mod ? 4u : 0u) |
                              seg_wants[s->mix.noise_seg & 7u]);
        want[0] |= w;
        if (s->arch == VARCH_BINAURAL) want[1] |= w;
    }
    for (int e = 0; e < 2; e++)
        if (want[e]) noise_shared_fill(e, frames, want[e] & 2u, want[e] & 4u);
}

/* The clock's and the sequencers' skipped frames carry over from one pass of a synth_render_q15 call to its next
 * (2026-11-12). They were settled at every pass's end and asked afresh at the next one's first frame: the full
 * sequencer frame and its late twin 345 times a second each, most of their code cold by then (fetched from the flash
 * again), though nothing can reach the clock or the sequencers between the passes of one call (the UI and the MIDI
 * input write between calls; an event inside one ends the skip as ever). So only a call's first pass starts afresh
 * and only its last settles: the passes between are skipped through as the frames inside a pass are, by the same
 * rules, to the same output (test_seq_idle at blocks of several passes). */
static struct {
    uint32_t seq_wake, seq_skip, pc_cur, pc_prev, clk_left, clk_skip, clk_pc;
    uint8_t seq_quiet, seq_timed;
} carry;

static void render_pass(int32_t *out, uint32_t frames, int first, int last)
{
#if defined(SYNTH_PROF)
    uint32_t sprof_total = synth_prof_now();
#endif
    SPROF_DECL;
    uint32_t pass_t0 = pass_clock ? pass_clock() : 0u;
    vrun_ticks = 0;
    if (pass_clock && hs_last_f && hs_frames < VOICE_GUARD_HALF) {   /* THE DEADLINE GUARD (synth_voice.h) */
        uint32_t proj = hs_total + hs_last_t / hs_last_f * (VOICE_GUARD_HALF - hs_frames);   /* (24 MHz ticks: a half is
                                                      * ~280 k, so all of it fits 32 bits, x1000 too) */
        synth_ctrl_guard = (uint8_t)(proj / VOICE_GUARD_HALF * 1000u > VOICE_GUARD_PM * pass_tpf);
        hs_guard += synth_ctrl_guard;
    }
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
#if SYNTH_CORE_BALANCE
    for (unsigned l = 0; l < NLAYER; l++)      /* the voices' prices, for the cores' split (core1_pick) */
        layer_w[l] = varch_voice_cost(&engine_state[l]);
#endif
    pool_pass = 1u;                            /* pool_range and the layer tick keep what a pass cannot change */
    synth_voice_begin_pass();
    pass_base = sample_clock;
    voices_done = 0;
    in_pass = 1;
    if (first) {                 /* a call's first pass: nothing skipped yet (carry, above) */
        carry.seq_wake = carry.seq_skip = carry.pc_cur = carry.pc_prev = 0;
        carry.clk_left = carry.clk_skip = carry.clk_pc = 0;
        carry.seq_quiet = carry.seq_timed = 0;
    }
    uint8_t seq_quiet = carry.seq_quiet;       /* the sequencers' frames skipped: 1 idle (seq_frame), till an event; 2
                                                * playing, till an event, a pulse or seq_wake (seq_next_due) ... */
    uint8_t seq_timed = carry.seq_timed;
    uint32_t seq_wake = carry.seq_wake;
    uint32_t seq_skip = carry.seq_skip;        /* ... this many since the last one run */
    uint32_t pc_cur = carry.pc_cur, pc_prev = carry.pc_prev;   /* the clock's pulse count at the end of the frame before, and before that */
    uint32_t clk_left = carry.clk_left, clk_skip = carry.clk_skip, clk_pc = carry.clk_pc;   /* the internal clock's quiet frames still to skip, and skipped since its last call */
    for (uint32_t f = 0; f < frames; ++f) {
        pass_frame = f;
        sample_clock = pass_base + f;
        uint8_t ext_now = ext_n && (int32_t)(ext_q[ext_h].at - sample_clock) <= 0;
        int clk_quiet = clk_left && !ext_now;  /* (2026-11-07) a frame with no pulse: the clock's call skipped, settled in one go */
        if (clk_quiet) {
            clk_left--;
            clk_skip++;
            SPROF_START();
            pc_prev = pc_cur;
            pc_cur = clk_pc;                   /* (no pulse since the call: its count after the frame) */
        } else {
            if (clk_skip) {
                midi_clock_skipped(clk_skip, sample_clock - 1u);
                clk_skip = 0;
            }
            clk_left = 0;
            if (ext_now || midi_clock_frame_changes()) voices_up_to(f);   /* a pulse or an event: the voices before it as they were */
            SPROF_START();
            pc_prev = pc_cur;
            pc_cur = midi_clock_pulse_count();
        }
        if (ext_now) {                             /* an event may wake them: the skipped frames' last one first, */
            if (seq_skip) seq_frame(sample_clock - 1u, 1, pc_prev);   /* at its clock (before this frame's) */
            seq_quiet = 0;
            seq_skip = 0;
        }
        if (!clk_quiet) {
            midi_clock_frame(sample_clock);        /* the clock is stamped before the LFOs read it this frame */
#if !defined(SYNTH_CLK_NO_SKIP)                    /* (the test's reference: every frame run) */
            clk_left = midi_clock_quiet();
#endif
            clk_pc = midi_clock_pulse_count();
        }
        while (ext_now) {                          /* timed MIDI in on this frame (a 0xF8 after the clock's own pulse) */
            midi_apply(ext_q[ext_h].st, ext_q[ext_h].d1, ext_q[ext_h].d2, VSRC_KEYS);
            clk_left = 0;                          /* (a tempo or source change: the clock is asked next frame) */
            ext_h = (uint8_t)((ext_h + 1u) % EXT_Q_MAX);
            ext_n--;
            ext_now = ext_n && (int32_t)(ext_q[ext_h].at - sample_clock) <= 0;
        }
        SPROF_LAP(SPROF_CLOCK);
        if (seq_quiet == 1u || (seq_quiet == 2u && midi_clock_pulse_count() == pc_cur
                                && !(seq_timed && (int32_t)(sample_clock - seq_wake) >= 0)))
            seq_skip++;
        else {
            seq_skip = 0;
            seq_quiet = seq_frame(sample_clock, 0, 0u) ? 1u : 2u;
            seq_timed = seq_quiet == 2u && seq_next_due(&seq_wake);
#if defined(SYNTH_SEQ_NO_IDLE)                 /* (test_seq_idle's reference: every frame run) */
            seq_quiet = 0;
#endif
        }
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
    if (last) {
        if (clk_skip)
            midi_clock_skipped(clk_skip, sample_clock);
        if (seq_skip)                          /* the call's last frame, skipped: run now (the UI reads them next) */
            seq_frame(sample_clock, 1, pc_cur);
    } else {                                   /* (the next pass of this call goes on from here) */
        carry.seq_quiet = seq_quiet;
        carry.seq_timed = seq_timed;
        carry.seq_wake = seq_wake;
        carry.seq_skip = seq_skip;
        carry.pc_cur = pc_cur;
        carry.pc_prev = pc_prev;
        carry.clk_left = clk_left;
        carry.clk_skip = clk_skip;
        carry.clk_pc = clk_pc;
    }
    voices_up_to(frames);
    in_pass = 0;
    pool_pass = 0u;
    sample_clock = pass_base + frames;
    SPROF_START();
    PF_START();

    /* each layer's dry sum over the pass (the two sets, in this order), then the shared effects' sends from those
     * sums: the layer's AMP DELAY / REVERB SEND (2026-11-01: per voice before; the same sum, the mix gain divided back
     * out). The layer's MFX goes between the two. */
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const float *m0 = mix_bus[0].mix[frame], *m1 = mix_bus[1].mix[frame];
        for (int l = 0; l < NLAYER; l++) {
            layer_l[l][frame] = m0[MIX_LAYER(l) + MIX_L] + m1[MIX_LAYER(l) + MIX_L];
            layer_r[l][frame] = m0[MIX_LAYER(l) + MIX_R] + m1[MIX_LAYER(l) + MIX_R];
        }
    }
    PF_LAP(SPROF_F_SUM);
    /* each layer's MFX, on its own sum (nothing at OFF), shared with core 1, which has nothing else to do here (one
     * on one core cost the pass ~3.3..5 points on the FM1): both on, a layer a core; one, a side a core */
    fx_frames = frames;
    {
        int nl = perf_state.perf_mode == PERF_SINGL_SYNTH ? 1 : NLAYER, can2 = !fx_one_core && core2_run != 0;
        if (nl == NLAYER && MFX_PARAMS(0).type != MFX_OFF && MFX_PARAMS(1).type != MFX_OFF) {
            int on2 = can2 && core2_run(mfx_job, 1u);
            mfx_layer_block(0, layer_l[0], layer_r[0], frames);
            if (!on2 || core2_wait())
                mfx_job(1u);                   /* one core (or the second failed): here */
        } else
            for (int l = 0; l < nl; l++)
                if (mfx_layer_plan(l, layer_l[l], layer_r[l], frames)) {   /* (core 0: what both sides share) */
                    int on2 = can2 && core2_run(mfx_job, (uint32_t)l | 2u);
                    mfx_layer_side(l, 0, layer_l[l]);
                    if (!on2 || core2_wait())
                        mfx_job((uint32_t)l | 2u);
                }
    }
    PF_LAP(SPROF_F_MFX);
    /* the MIX page's BALANCE (fx.h): DUAL / SPLIT, each layer's fader here (after its MFX, before its sends: a faded
     * layer's echoes fade with it); SINGL, DRY / WET, applied below */
    float g_to[MIXG_N], g0, g1;
    mix_targets(perf_state.perf_mode == PERF_SINGL_SYNTH, g_to);
    for (int l = 0; l < NLAYER; l++)
        if (mix_step(MIXG_L1 + l, g_to[MIXG_L1 + l], &g0, &g1))
            mix_apply(g0, g1, frames, layer_l[l], layer_r[l]);
    float to_dly[NLAYER], to_rev[NLAYER];          /* knob 0..127 -> 0..~1, the old per-voice 258 / 32768, mono 1/2 */
    for (int l = 0; l < NLAYER; l++) {
        int32_t sd = (int32_t)engine_state[l].amp.delay_send + synth_fx_send_mod[l][0];   /* + the MOD SENDS there */
        int32_t sr = (int32_t)engine_state[l].amp.reverb_send + synth_fx_send_mod[l][1];
        sd = sd < 0 ? 0 : sd > 127 ? 127 : sd;
        sr = sr < 0 ? 0 : sr > 127 ? 127 : sr;
        to_dly[l] = (float)(sd * 258) * (0.5f / (float)MIX_HEADROOM_Q15);
        to_rev[l] = (float)(sr * 258) * (0.5f / (float)MIX_HEADROOM_Q15);
    }
    for (uint32_t frame = 0; frame < frames; ++frame) {
        float m1 = layer_l[0][frame] + layer_r[0][frame], m2 = layer_l[1][frame] + layer_r[1][frame];
        fx_dly[frame] = mix_q15(m1 * to_dly[0] + m2 * to_dly[1]);   /* unclipped: each effect limits its send */
        fx_rev_in[frame] = mix_q15(m1 * to_rev[0] + m2 * to_rev[1]);
        if (lvl_on && (fx_rev_in[frame] < 0 ? -fx_rev_in[frame] : fx_rev_in[frame]) > lvl_peak[3])
            lvl_peak[3] = fx_rev_in[frame] < 0 ? -fx_rev_in[frame] : fx_rev_in[frame];
    }
    PF_LAP(SPROF_F_SEND);
    fx_frames = frames;
    fx_tone_sync(&perf_state);                         /* LOOP TONE's thinner half is DELAY HP (fx.h) */
    fx_pulse_q8 = midi_clock_period_q8();              /* DELAY SYNC follows the tempo, internal or MIDI */
    fx_cross_begin(frames);                            /* (both ways' settings, before core 1 starts: fx.h) */
    fx_cross_to_dly(fx_dly, frames);                   /* REV > DLY: the reverb's last pass (fx.h, 2026-11-02) */
    /* (2026-11-14: the delay alone on core 1 paid while the slowed tape cost more than the reverb's front and tank;
     * with the half-rate tape it is cheap at every DELAY TIME, and this way round balances the cores again) */
    int on2 = !fx_one_core && core2_run && core2_run(fx_reverb_job, 0u);
    fx_delay_block(&perf_state, fx_dly, fx_dly_l, fx_dly_r, frames);
    fx_cross_to_rev(fx_dly_l, fx_dly_r, fx_rev_in, frames);   /* DLY > REV: this pass's echoes */
    PF_LAP(SPROF_F_DLY);
    fx_reverb_front(&perf_state, fx_rev_in, frames);   /* (for the back's next pass: it reads a pass behind) */
    PF_LAP(SPROF_F_RVF);
    if (!on2 || core2_wait())
        fx_reverb_job(0u);
    PF_LAP(SPROF_F_RVB);

    for (uint32_t frame = 0; lvl_on && frame < frames; ++frame) {   /* the meters (synth_levels): before the wet sum */
        int32_t r = fx_rev_l[frame] < 0 ? -fx_rev_l[frame] : fx_rev_l[frame];
        int32_t d = fx_dly_l[frame] < 0 ? -fx_dly_l[frame] : fx_dly_l[frame];
        if (r > lvl_peak[0]) lvl_peak[0] = r;
        if (d > lvl_peak[1]) lvl_peak[1] = d;
    }
    int wet_on = mix_step(MIXG_WET, g_to[MIXG_WET], &g0, &g1);   /* (the wet gain moving: it scales the echoes' sum, below) */
    float wet_g0 = g0, wet_g1 = g1;
    if (wet_on)
        fx_mix_block(fx_dly_l, fx_dly_r, fx_rev_l, fx_rev_r, frames);   /* (else the output loop adds them: one pass fewer) */
    /* SINGL's DRY / WET: the dry after the sends (the echoes go on without it), the wet after the loop's taps */
    if (mix_step(MIXG_DRY, g_to[MIXG_DRY], &g0, &g1))
        for (int l = 0; l < NLAYER; l++)
            mix_apply(g0, g1, frames, layer_l[l], layer_r[l]);
    if (wet_on)
        for (uint32_t frame = 0; frame < frames; ++frame) {
            float g = wet_g0 + (wet_g1 - wet_g0) * (float)(frame + 1u) * (1.f / (float)frames);
            fx_rev_l[frame] = (int32_t)((float)fx_rev_l[frame] * g);
            fx_rev_r[frame] = (int32_t)((float)fx_rev_r[frame] * g);
        }
    /* The output: the layers' sum, the wet sum, the master limiter (2026-11-07: an int32 path for the frames that pass
     * inside +-2^23 with the limiter at rest, which is nearly all of them; the 64-bit one below is the same arithmetic
     * for the rest, so the output is bit for bit as before). The limiter: the envelope jumps to a peak over the
     * ceiling at once (so no frame passes it) and falls back toward the ceiling; the gain is ceiling / envelope, unity
     * whenever the envelope is at the ceiling. Only a frame that is being limited pays for the division. */
    const float *sl0 = layer_l[0], *sl1 = layer_l[1], *sr0 = layer_r[0], *sr1 = layer_r[1];
    int32_t env = limit_env_q8;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        int32_t li = mix_q15(sl0[frame] + sl1[frame]), ri = mix_q15(sr0[frame] + sr1[frame]);
        int32_t wl = fx_rev_l[frame], wr = fx_rev_r[frame];
        if (!wet_on) {
            wl += fx_dly_l[frame];
            wr += fx_dly_r[frame];
        }
        if (lvl_on && (li < 0 ? -li : li) > lvl_peak[2]) lvl_peak[2] = li < 0 ? -li : li;
        /* the fast frame: every term inside +-2^23, so the int32 sums cannot wrap and `peak << 8` fits */
        if ((uint32_t)li + 0x800000u < 0x1000000u && (uint32_t)ri + 0x800000u < 0x1000000u &&
            (uint32_t)wl + 0x800000u < 0x1000000u && (uint32_t)wr + 0x800000u < 0x1000000u) {
            int32_t left = li + wl, right = ri + wr;
            int32_t pl = left < 0 ? -left : left, pr = right < 0 ? -right : right;
            int32_t peak = pr > pl ? pr : pl;
            if (peak > 0x7FFFFF) peak = 0x7FFFFF;
            if (peak << 8 > env)
                env = peak << 8;
            else
                env -= (env - ((int32_t)LIMIT_CEILING << 8)) >> LIMIT_RELEASE_SHIFT;
            if (env > ((int32_t)LIMIT_CEILING << 8)) {
                int64_t g = ((uint32_t)LIMIT_CEILING << 15) / (uint32_t)(env >> 8);
                left = (int32_t)(((int64_t)left * g) >> 15);
                right = (int32_t)(((int64_t)right * g) >> 15);
            }
            out[frame * 2u] = left > Q15_MAX ? Q15_MAX : (left < Q15_MIN ? Q15_MIN : left);
            out[frame * 2u + 1u] = right > Q15_MAX ? Q15_MAX : (right < Q15_MIN ? Q15_MIN : right);
            continue;
        }
        int64_t left = (int64_t)li + wl, right = (int64_t)ri + wr;   /* the original, 64-bit */
        int64_t peak = left < 0 ? -left : left;
        int64_t peak_r = right < 0 ? -right : right;
        if (peak_r > peak) peak = peak_r;
        if (peak > 0x7FFFFF) peak = 0x7FFFFF;                       /* keeps peak << 8 inside int32 */
        if ((int32_t)peak << 8 > env)
            env = (int32_t)peak << 8;
        else
            env -= (env - ((int32_t)LIMIT_CEILING << 8)) >> LIMIT_RELEASE_SHIFT;
        if (env > ((int32_t)LIMIT_CEILING << 8)) {
            uint32_t gain_q15 = ((uint32_t)LIMIT_CEILING << 15) / (uint32_t)(env >> 8);
            left = (left * gain_q15) >> 15;
            right = (right * gain_q15) >> 15;
        }
        out[frame * 2u] = clamp_q15_64(left);
        out[frame * 2u + 1u] = clamp_q15_64(right);
    }
    limit_env_q8 = env;
    rec_click_mix(out, frames);
    if (pass_clock)
        floor_pass(pass_clock() - pass_t0, frames);
    PF_LAP(SPROF_F_TAIL);
    SPROF_LAP(SPROF_FX);
#if defined(SYNTH_PROF)
    synth_prof_acc[SPROF_TOTAL] += synth_prof_now() - sprof_total;
#endif
}

void synth_render_q15(int32_t *out, uint32_t frames)
{
    int first = 1;
    while (frames > 0u) {
        uint32_t n = frames < RENDER_PASS_FRAMES ? frames : RENDER_PASS_FRAMES;
        render_pass(out, n, first, frames == n);
        first = 0;
        out += 2u * n;
        frames -= n;
    }
    krec.frame_now = sample_clock;             /* (where the REC page's taps act, between renders) */
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
Krec *synth_rec(void) { return &krec; }
uint32_t synth_frame(void) { return sample_clock; }

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
