/* The oscillators' PCM waves (pcm_osc.h). */
#include <stddef.h>
#include "pcm_osc.h"
#include "pcm_store.h"
#include "attacks.h"
#include "oscillators.h"
#include "wavetables.h"
#include "synth_voice.h"                    /* SYNTH_CTRL_PERIOD */
#include "dsp_cold.h"
#include "amplifier.h"                     /* amp_exp2f (the FM1 links no libm) */

/* mu-law byte -> Q15 sample, in RAM: int16, so a read interpolates in int like the wavetables (on the FM1 an int16
 * lerp beats float, which pays an int -> float conversion; fm1-cpu-profile) */
static int16_t pcm_dec[256];

void pcm_osc_init(void)
{
    for (int i = 0; i < 256; i++) {
        float v = attack_mulaw[i];
        pcm_dec[i] = (int16_t)(v > 32767.f ? 32767 : (v < -32767.f ? -32767 : (int)v));
    }
}

/* ---- the wave list */

int wave_kind(uint8_t code)
{
    if (code < WAVE_ATK_FIRST) return 0;
#if ATK_COUNT
    if (code < WAVE_PCM_FIRST) return code - WAVE_ATK_FIRST < ATK_COUNT ? 1 : -1;
#else
    if (code < WAVE_PCM_FIRST) return -1;   /* (no attack-waves since 2026-11-05) */
#endif
    return code - WAVE_PCM_FIRST < pcm_wave_count() ? 2 : -1;
}

uint32_t wave_list_count(void) { return NUM_BANKS + pcm_wave_count() + ATK_COUNT; }

uint8_t wave_list_code(uint32_t pos)
{
    uint32_t w = pcm_wave_count();
    if (pos < NUM_BANKS) return (uint8_t)pos;
    pos -= NUM_BANKS;
    if (pos < w) return (uint8_t)(WAVE_PCM_FIRST + pos);
    pos -= w;
#if ATK_COUNT
    return (uint8_t)(WAVE_ATK_FIRST + (pos < ATK_COUNT ? pos : ATK_COUNT - 1u));
#else
    return (uint8_t)(w ? WAVE_PCM_FIRST + w - 1u : NUM_BANKS - 1u);   /* past the end: the last */
#endif
}

uint32_t wave_list_pos(uint8_t code)
{
    switch (wave_kind(code)) {
    case 1: return NUM_BANKS + pcm_wave_count() + (code - WAVE_ATK_FIRST);
    case 2: return NUM_BANKS + (code - WAVE_PCM_FIRST);
    case 0: return code % NUM_BANKS;
    default: return 0;
    }
}

const char *wave_name(uint8_t code, char *buf13)
{
    switch (wave_kind(code)) {
    case 0: return wavetable_bank_names[code % NUM_BANKS];
#if ATK_COUNT
    case 1: return attack_names[code - WAVE_ATK_FIRST];
#endif
    case 2: {
        const PcmEntry *e = pcm_wave(code - WAVE_PCM_FIRST);
        for (uint32_t i = 0; i < PCM_NAME_LEN; i++) buf13[i] = e->name[i];
        buf13[PCM_NAME_LEN] = 0;
        return buf13;
    }
    default: return "NO WAVE";
    }
}

static int32_t dec_at(const uint8_t *p, uint32_t i) { return pcm_dec[p[i]]; }

SYNTH_COLD int32_t wave_preview(uint8_t code, int morph_q12, uint32_t phase)
{
    switch (wave_kind(code)) {
#if ATK_COUNT
    case 1: {
        const AttackInfo *ai = &attack_info[code - WAVE_ATK_FIRST];
        uint32_t n = ai->length < 1024u ? ai->length : 1024u;
        return dec_at(attack_data + ai->offset, (uint32_t)(((uint64_t)phase * n) >> 32));
    }
#endif
    case 2: {
        const PcmEntry *w = pcm_wave(code - WAVE_PCM_FIRST), *a, *b = NULL;
        const uint8_t *da, *db = NULL;
        a = pcm_sample(w->u.w.loop_a, PCM_LOOP, &da);
        if (w->u.w.loop_b != PCM_REF_NONE)
            b = pcm_sample(w->u.w.loop_b, PCM_LOOP, &db);
        if (!a)
            return 0;
        int32_t xf = b ? (morph_q12 >= (7 << 12) ? 32768 : (int32_t)(((uint32_t)morph_q12 << 15) / (7u << 12))) : 0;
        uint32_t na = a->u.s.len < 512u ? a->u.s.len : 512u;
        int32_t va = dec_at(da, (uint32_t)(((uint64_t)phase * na) >> 32));
        if (!b || xf == 0)
            return va;
        uint32_t nb = b->u.s.len < 512u ? b->u.s.len : 512u;
        int32_t vb = dec_at(db, (uint32_t)(((uint64_t)phase * nb) >> 32));
        return va + (((vb - va) * xf) >> 15);
    }
    default:
        if (code < WAVE_ATK_FIRST && code % NUM_BANKS == 0 && osc_bank0_vco)
            return osc_vco_preview(morph_q12, phase);      /* the VCO: its own shape, not Foundations' frames */
        return wt_read_morph(code < WAVE_ATK_FIRST ? code % NUM_BANKS : 0, morph_q12, 1, phase);
    }
}

int wave_stones(uint8_t code)
{
    switch (wave_kind(code)) {
    case 1: return 1;
    case 2: return pcm_wave(code - WAVE_PCM_FIRST)->u.w.loop_b != PCM_REF_NONE ? 2 : 1;
    default: return NUM_MORPH_STEPS;
    }
}

/* ---- control rate */

/* a sample's step for a frequency: its rate in engine samples (Q16 per engine sample), times freq / its root's */
static uint32_t step_for(float rate_q16, uint8_t root, uint32_t freq_q16)
{
    float s = rate_q16 * (float)freq_q16 / (float)mtof_q16(root);
    return s >= 4.f * 65536.f ? 4u << 16 : (s < 1.f ? 1u : (uint32_t)s);
}

#define W1 32768                             /* a weight of 1 */

static void set_targets(PcmOsc *q, int32_t ta, int32_t tb, int32_t tk)
{
    q->ta = ta; q->tb = tb; q->tk = tk;
    if (ta == q->wa && tb == q->wb && tk == q->wk) {   /* nothing moves: the fast steady loop (render_steady) */
        q->ramp = 0;
        return;
    }
    q->da = (ta - q->wa) >> SYNTH_CTRL_SHIFT;  /* (the last ramp sample lands exactly on the target) */
    q->db = (tb - q->wb) >> SYNTH_CTRL_SHIFT;
    q->dk = (tk - q->wk) >> SYNTH_CTRL_SHIFT;
    q->ramp = (uint8_t)SYNTH_CTRL_PERIOD;
}

void pcm_prepare(PcmOsc *q, uint32_t freq_q16, int morph_q12)
{
    const uint8_t *da = NULL, *db = NULL, *dk = NULL;
    const PcmEntry *a = NULL, *b = NULL, *k = NULL;
    float k_rate = 0.f, b_mul = 1.f;
    int32_t k_level = W1;
    uint8_t k_root = 60;
    uint32_t k_len = 0, b_off = 0;
    int new_wave = q->code == 0, hand_wave = 0, trig = 0;
    const uint8_t *dkb = NULL;              /* HANDOFF: the attack that opens loop B */
    float kb_rate = 0.f;
    uint8_t kb_root = 60;
    uint32_t kb_len = 0;
    float tail_a = 0.f, tail_b = 0.f;
    if (q->code >= WAVE_PCM_FIRST) {
        const PcmEntry *w = pcm_wave(q->code - WAVE_PCM_FIRST);
        if (w) {
            hand_wave = (w->u.w.flags & PCM_WF_HANDOFF) != 0u;
            tail_a = (float)w->u.w.tail_a; tail_b = (float)w->u.w.tail_b;
            if (hand_wave && w->u.w.atk_ref_b != PCM_REF_NONE) {
                const PcmEntry *kb = pcm_sample(w->u.w.atk_ref_b, PCM_ONESHOT, &dkb);
                if (kb) { kb_rate = (float)kb->u.s.rate * (65536.f / 44100.f); kb_root = kb->root; kb_len = kb->u.s.len; }
                else dkb = NULL;
            }
            a = pcm_sample(w->u.w.loop_a, PCM_LOOP, &da);
            if (w->u.w.loop_b != PCM_REF_NONE)
                b = pcm_sample(w->u.w.loop_b, PCM_LOOP, &db);
            b_mul = (float)w->u.w.b_rate_q12 * (1.f / 4096.f);
            b_off = w->u.w.b_off_q16;
            k_level = (int32_t)w->u.w.atk_level * W1 / 127;
            if (w->u.w.atk_ref != PCM_REF_NONE) {
                k = pcm_sample(w->u.w.atk_ref, PCM_ONESHOT, &dk);
                if (k) { k_rate = (float)k->u.s.rate * (65536.f / 44100.f); k_root = k->root; k_len = k->u.s.len; }
            }
#if ATK_COUNT
            else if (w->u.w.atk_fw < ATK_COUNT) {
                const AttackInfo *ai = &attack_info[w->u.w.atk_fw];
                dk = attack_data + ai->offset;
                k_rate = (float)ai->rate_q16;
                k_root = ai->root != ATK_UNPITCHED ? ai->root : 60u;
                k_len = ai->length;
            }
#endif
        }
    }
#if ATK_COUNT
    else if (q->code >= WAVE_ATK_FIRST && q->code - WAVE_ATK_FIRST < ATK_COUNT) {
        const AttackInfo *ai = &attack_info[q->code - WAVE_ATK_FIRST];   /* an attack alone: a one-shot wave */
        dk = attack_data + ai->offset;
        k_rate = (float)ai->rate_q16;
        k_root = ai->root != ATK_UNPITCHED ? ai->root : 60u;
        k_len = ai->length;
    }
#endif
    /* the loops: a new wave (or one that lost its samples) starts them where B's offset says; else they run on */
    if (a) {
        uint32_t len = a->u.s.len << 16;
        if (q->pa != da || q->len_a != len) {
            q->pos_a = q->pos_a % len;
            new_wave = 1;
        }
        q->pa = da;
        q->len_a = len;
        q->step_a = step_for((float)a->u.s.rate * (65536.f / 44100.f), a->root, freq_q16);
    } else {
        q->pa = NULL;
    }
    if (b) {
        uint32_t len = b->u.s.len << 16;
        if (q->pb != db || q->len_b != len || new_wave) {
            uint32_t frac = q->len_a ? (uint32_t)(((uint64_t)q->pos_a << 16) / q->len_a) : 0u;
            q->pos_b = (uint32_t)(((uint64_t)((frac + b_off) & 0xFFFFu) * len) >> 16);
        }
        q->pb = db;
        q->len_b = len;
        uint32_t s = step_for((float)b->u.s.rate * (65536.f / 44100.f) * b_mul, b->root, freq_q16);
        q->step_b = s;
    } else {
        q->pb = NULL;
    }
    int32_t xf = morph_q12 >= (7 << 12) ? W1 : (int32_t)(((uint32_t)morph_q12 << 15) / (7u << 12));
    if (!q->pb) xf = 0;
    if (!q->pa) xf = q->pb ? W1 : 0;
    q->xf = xf;
    /* the attack: from the start at a new note; it stops if its wave changes under it. HANDOFF: the attack nearer
     * SCAN, chosen at the note, plays on whatever SCAN does after */
    if (q->trig) {
        q->trig = 0;
        trig = 1;
#ifdef PCM_RETAKE_OLD
        int can_hand = q->idle;
#else
        int can_hand = 1;
#endif
        /* HANDOFF with SCAN between the ends: both attacks, A's as the attack (pk), B's beside it (pk2). Decided here,
         * before the nearer attack is picked: 2026-11-05 test_pcm_handoff caught SCAN 64..126 playing B's twice */
        int both = can_hand && hand_wave && dk && k_len > 1u && dkb && kb_len > 1u && xf > 0 && xf < W1;
        if (hand_wave && dkb && xf >= W1 / 2 && !both) {
            dk = dkb; k_rate = kb_rate; k_root = kb_root; k_len = kb_len;
        }
        q->pk = dk && k_len > 1u ? dk : NULL;
        q->pos_k = 0;
        q->end_k = k_len > 1u ? (k_len - 1u) << 16 : 0u;
        q->dk = 0;
        /* a voice still sounding (retaken): its loops fade out under the new attack over this tick, then are placed
         * as an idle voice's (hand 2 -> 1 at the next tick); an idle voice's are silent at once */
#ifdef PCM_RETAKE_OLD                        /* host A/B only: the old way (attack over the sounding loops) */
        q->hand = (uint8_t)(hand_wave && q->pk && q->idle);
#else
        q->hand = (uint8_t)(hand_wave && q->pk ? (q->idle ? 1 : 2) : 0);
#endif
        if (q->hand == 1)
            q->wa = q->wb = 0;
        /* HANDOFF with SCAN between the ends: both attacks, each opening its own loop (two whole notes, crossfaded) */
        q->pk2 = NULL;
        q->ksplit = W1;
        if (both && q->hand) {
            q->pk2 = dkb;
            q->pos_k2 = 0;
            q->end_k2 = (kb_len - 1u) << 16;
            q->wk2 = (int32_t)(((int64_t)k_level * xf) >> 15);
            q->ksplit = W1 - xf;
        }
        q->wk = q->pk ? (int32_t)(((int64_t)k_level * q->ksplit) >> 15) : 0;
        q->tail_a = q->tail_b = W1;         /* the tails: each loop decays on as its attack did */
        q->tmul_a = hand_wave && tail_a > 0.f && !q->hold ?
            (int32_t)(W1 * amp_exp2f(-tail_a * (float)SYNTH_CTRL_PERIOD * (3.3219281f / (20.f * 44100.f)))) : 0;
        q->tmul_b = hand_wave && tail_b > 0.f && !q->hold ?
            (int32_t)(W1 * amp_exp2f(-tail_b * (float)SYNTH_CTRL_PERIOD * (3.3219281f / (20.f * 44100.f)))) : 0;
    } else if (q->pk == dkb && dkb && q->hand) {
        k_rate = kb_rate; k_root = kb_root; k_len = kb_len;
    } else if (q->pk && q->pk != dk) {
        q->pk = NULL;
        q->hand = 0;
    }
    if (q->pk)
        q->step_k = step_for(k_rate, k_root, freq_q16);
    if (q->pk2)
        q->step_k2 = step_for(kb_rate, kb_root, freq_q16);
    q->inv_freq = freq_q16 ? 1.f / (float)freq_q16 : 0.f;
    {                                       /* PM: a cycle of the note is 44100 / Hz engine samples of each read */
        float c = 44100.f * q->inv_freq * (1.f / 65536.f);   /* (Q16 Hz in inv_freq; 2^32 phase = 2^16 * 2^16) */
        q->pm_a = (float)q->step_a * c;
        q->pm_b = (float)q->step_b * c;
        q->pm_k = (float)q->step_k * c;
        q->pm_k2 = (float)q->step_k2 * c;
    }
    if (!trig && q->hand == 2 && q->pk) {   /* retaken: the loops faded out last tick; now placed to reach index 0
                                             * as the attacks end (the samples each has left, at its pace) */
        uint64_t ra = q->end_k > q->pos_k ? (uint64_t)(q->end_k - q->pos_k) : 0u;
        if (q->pa) q->pos_a = q->len_a - (uint32_t)((ra * q->step_a / q->step_k) % q->len_a);
        if (q->pb && q->pk2) {
            uint64_t rb = q->end_k2 > q->pos_k2 ? (uint64_t)(q->end_k2 - q->pos_k2) : 0u;
            q->pos_b = q->len_b - (uint32_t)((rb * q->step_b / q->step_k2) % q->len_b);
        } else if (q->pb) q->pos_b = q->len_b - (uint32_t)((ra * q->step_b / q->step_k) % q->len_b);
        if (q->pa && q->pos_a >= q->len_a) q->pos_a -= q->len_a;
        if (q->pb && q->pos_b >= q->len_b) q->pos_b -= q->len_b;
        for (int h = 0; h < 4; h++) {
            q->spa[h] = q->pos_a;
            q->spb[h] = q->pos_b;
        }
        q->wa = q->wb = 0;
        q->hand = 1;
    }
    if (trig && q->hand == 1) {             /* the loops reach index 0 as the attack ends (its length, at its pace) */
        uint64_t t16 = (uint64_t)k_len << 16;
        if (q->pa) q->pos_a = q->len_a - (uint32_t)((t16 * q->step_a / q->step_k) % q->len_a);
        if (q->pb && q->pk2)                /* both attacks: B's loop reaches 0 as B's attack ends */
            q->pos_b = q->len_b - (uint32_t)((((uint64_t)kb_len << 16) * q->step_b / q->step_k2) % q->len_b);
        else if (q->pb) q->pos_b = q->len_b - (uint32_t)((t16 * q->step_b / q->step_k) % q->len_b);
        if (q->pa && q->pos_a >= q->len_a) q->pos_a -= q->len_a;
        if (q->pb && q->pos_b >= q->len_b) q->pos_b -= q->len_b;
        for (int h = 0; h < 4; h++) {
            q->spa[h] = q->pos_a;
            q->spb[h] = q->pos_b;
        }
    }

    /* the weights: SCAN's crossfade; while the attack plays, the loop nearer SCAN alone (two reads); HANDOFF: none */
    int32_t ta = q->pa ? W1 - xf : 0, tb = q->pb ? xf : 0;
    if (q->pk && q->hand) {
        ta = tb = 0;
    } else if (q->pk) {
        ta = q->pa && xf < W1 / 2 ? W1 : 0;
        tb = q->pb && !ta ? W1 : 0;
    } else if (q->tmul_a || q->tmul_b) {    /* HANDOFF's tails: each loop goes on decaying as its attack did */
        if (q->tmul_a) q->tail_a = (int32_t)(((int64_t)q->tail_a * q->tmul_a) >> 15);
        if (q->tmul_b) q->tail_b = (int32_t)(((int64_t)q->tail_b * q->tmul_b) >> 15);
        ta = (int32_t)(((int64_t)ta * q->tail_a) >> 15);
        tb = (int32_t)(((int64_t)tb * q->tail_b) >> 15);
    }
    if (new_wave) {                         /* a new wave: its weights at once (it starts from silence anyway) */
        q->wa = ta;
        q->wb = tb;
        q->sxf = 0;                         /* SPREAD's playheads restart from the new loops (pcm_spread) */
    }
    set_targets(q, ta, tb, q->pk ? (int32_t)(((int64_t)k_level * q->ksplit) >> 15) : 0);
}

void pcm_spread(PcmOsc *q, const uint32_t *ratio_q28, int32_t xfade_q15)
{
    if (!ratio_q28 || xfade_q15 <= 0 || (!q->pa && !q->pb)) {
        q->sxf = 0;
        return;
    }
    /* spread 0 -> on (or a new wave): the detuned playheads start from the undetuned ones, one coherent sound */
    if (q->sxf == 0)
        for (int h = 0; h < 4; h++) {
            q->spa[h] = q->pos_a;
            q->spb[h] = q->pos_b;
        }
    q->slive = 0;
    for (int h = 0; h < 4; h++) {
        uint64_t a = ((uint64_t)q->step_a * ratio_q28[h]) >> 28, b = ((uint64_t)q->step_b * ratio_q28[h]) >> 28;
        /* past 4 samples a step (a high note's +2 octave stack) a playhead is silent, as a wavetable playhead past
         * Nyquist; it keeps its position */
        if ((q->pa && a > (4u << 16)) || (q->pb && b > (4u << 16)))
            continue;
        q->ssa[h] = a ? (uint32_t)a : 1u;
        q->ssb[h] = b ? (uint32_t)b : 1u;
        q->slive |= (uint8_t)(1u << h);
    }
    q->sxf = xfade_q15;
}

/* ---- audio rate */

/* the mu-law pair under a Q16 position, interpolated in int (as wt_read_frames) */
static inline int32_t rd(const uint8_t *p, uint32_t pos)
{
    uint32_t i = pos >> 16;
    int32_t a = pcm_dec[p[i]], b = pcm_dec[p[i + 1u]];
    return a + (((b - a) * (int32_t)((pos & 0xFFFFu) >> 1)) >> 15);
}

static inline uint32_t wrap(uint32_t pos, uint32_t len) { return pos >= len ? pos - len : pos; }
/* pos moved by d (either sign, |d| under len) round a loop of len */
static inline uint32_t wrap_by(uint32_t pos, int32_t d, uint32_t len)
{
    int64_t p = (int64_t)pos + d;
    return (uint32_t)(p < 0 ? p + len : p >= (int64_t)len ? p - len : p);
}

static inline int32_t clamp15(int32_t y) { return y > 32767 ? 32767 : (y < -32767 ? -32767 : y); }

/* the attack ended: its loop alone fades to SCAN's crossfade over the next tick */
static void attack_done(PcmOsc *q)
{
    q->pk = NULL;
    q->pk2 = NULL;
    q->wk = 0;
    if (q->hand) {                          /* HANDOFF: the loops take over where the attack ends, at once */
        q->hand = 0;
        q->wa = q->ta = q->pa ? W1 - q->xf : 0;
        q->wb = q->tb = q->pb ? q->xf : 0;
        q->tk = 0;
        q->da = q->db = q->dk = 0;
        q->ramp = 0;
        return;
    }
    set_targets(q, q->pa ? W1 - q->xf : 0, q->pb ? q->xf : 0, 0);
}

/* a loop's position after m unread samples (it keeps turning): a block moves it at most 64 samples (16 at 4x), less
 * than a loop's length, so a subtraction or two does what a (slow, 64-bit) modulo would */
static inline uint32_t skip(uint32_t pos, uint32_t step, uint32_t len, int m)
{
    uint64_t x = (uint64_t)pos + (uint64_t)step * (uint32_t)m;
    while (x >= len) x -= len;
    return (uint32_t)x;
}

/* the weights' ramp, one sample: a step, and exactly the target on the last */
#define RAMP_STEP(w, d, t) do { (w) += (d); if (ramp == 1) (w) = (t); } while (0)

/* m samples while the attack plays: the attack and each loop that has weight (normally the one nearer SCAN alone;
 * the other only while it ramps out, e.g. a voice retaken while it sounds). Short: a tenth of a second a note. */
static void render_attack(PcmOsc *q, int32_t *out, int m)
{
    const uint8_t *pk = q->pk, *pa = q->pa, *pb = q->pb;
    uint32_t xk = q->pos_k, sk = q->step_k, xa = q->pos_a, sa = q->step_a, la = q->len_a, xb = q->pos_b,
             sb = q->step_b, lb = q->len_b;
    int32_t wk = q->wk, wa = q->wa, wb = q->wb, dk = q->dk, da = q->da, db = q->db;
    int ramp = q->ramp;
    int use_a = pa && (wa != 0 || (ramp && q->ta != 0)), use_b = pb && (wb != 0 || (ramp && q->tb != 0));
    for (int i = 0; i < m; i++) {
        int32_t y = (rd(pk, xk) * wk) >> 15;
        xk += sk;
        if (q->pk2 && q->pos_k2 < q->end_k2) {
            y += (rd(q->pk2, q->pos_k2) * q->wk2) >> 15;
            q->pos_k2 += q->step_k2;
        }
        if (use_a) y += (rd(pa, xa) * wa) >> 15;
        if (use_b) y += (rd(pb, xb) * wb) >> 15;
        if (pa) xa = wrap(xa + sa, la);
        if (pb) xb = wrap(xb + sb, lb);
        if (ramp) {
            RAMP_STEP(wk, dk, q->tk);
            RAMP_STEP(wa, da, q->ta);
            RAMP_STEP(wb, db, q->tb);
            ramp--;
        }
        out[i] = clamp15(y);
    }
    q->pos_k = xk; q->pos_a = xa; q->pos_b = xb;
    q->wk = wk; q->wa = wa; q->wb = wb;
    q->ramp = (uint8_t)ramp;
}

/* m samples of the loops while their weights ramp (SCAN moved, the attack ended): A and B crossfaded, or the one
 * that has weight alone, or silence (they keep turning) */
static void render_ramp(PcmOsc *q, int32_t *out, int m)
{
    const uint8_t *pa = q->pa, *pb = q->pb;
    uint32_t xa = q->pos_a, sa = q->step_a, la = q->len_a, xb = q->pos_b, sb = q->step_b, lb = q->len_b;
    int32_t wa = q->wa, wb = q->wb, da = q->da, db = q->db, ta = q->ta, tb = q->tb;
    int ramp = q->ramp;
    int use_a = pa && (wa != 0 || ta != 0), use_b = pb && (wb != 0 || tb != 0);
    for (int i = 0; i < m; i++) {
        int32_t y = 0;
        if (use_a) y += (rd(pa, xa) * wa) >> 15;
        if (use_b) y += (rd(pb, xb) * wb) >> 15;
        if (pa) xa = wrap(xa + sa, la);
        if (pb) xb = wrap(xb + sb, lb);
        if (ramp) {
            RAMP_STEP(wa, da, ta);
            RAMP_STEP(wb, db, tb);
            ramp--;
        }
        out[i] = clamp15(y);
    }
    q->pos_a = xa; q->pos_b = xb;
    q->wa = wa; q->wb = wb;
    q->ramp = (uint8_t)ramp;
}

/* samples before a loop's position passes its end (the reads up to then need no wrap test) */
static inline int to_end(uint32_t pos, uint32_t step, uint32_t len) { return (int)((len - pos + step - 1u) / step); }

/* m samples of the loops with the weights still (most of the time): the reads alone, the wrap tested once per stretch
 * between two loop ends, no clamp (the weights add up to at most 1: the sum stays in range) */
static void render_steady(PcmOsc *q, int32_t *out, int m)
{
    const uint8_t *pa = q->pa, *pb = q->pb;
    uint32_t xa = q->pos_a, sa = q->step_a, la = q->len_a, xb = q->pos_b, sb = q->step_b, lb = q->len_b;
    int32_t wa = q->wa, wb = q->wb;
    int i = 0;
    if (pa && wa && pb && wb) {
        while (i < m) {
            int seg = m - i, ka = to_end(xa, sa, la), kb = to_end(xb, sb, lb);
            if (ka < seg) seg = ka;
            if (kb < seg) seg = kb;
            for (int k = 0; k < seg; k++) {
                out[i++] = ((rd(pa, xa) * wa) >> 15) + ((rd(pb, xb) * wb) >> 15);
                xa += sa;
                xb += sb;
            }
            xa = wrap(xa, la);
            xb = wrap(xb, lb);
        }
    } else if ((pa && wa) || (pb && wb)) {
        int on_a = pa && wa;
        const uint8_t *p = on_a ? pa : pb;
        uint32_t x = on_a ? xa : xb, s = on_a ? sa : sb, l = on_a ? la : lb;
        int32_t w = on_a ? wa : wb;
        while (i < m) {
            int seg = m - i, kx = to_end(x, s, l);
            if (kx < seg) seg = kx;
            if (w == W1) {                      /* SCAN at an end: the read alone */
                for (int k = 0; k < seg; k++) {
                    out[i++] = rd(p, x);
                    x += s;
                }
            } else {
                for (int k = 0; k < seg; k++) {
                    out[i++] = (rd(p, x) * w) >> 15;
                    x += s;
                }
            }
            x = wrap(x, l);
        }
        if (on_a) {
            xa = x;
            if (pb) xb = skip(xb, sb, lb, m);
        } else {
            xb = x;
            if (pa) xa = skip(xa, sa, la, m);
        }
    } else {
        for (; i < m; i++) out[i] = 0;
        if (pa) xa = skip(xa, sa, la, m);
        if (pb) xb = skip(xb, sb, lb, m);
    }
    q->pos_a = xa;
    q->pos_b = xb;
}

static void render_loops(PcmOsc *q, int32_t *out, int m)
{
    int r = q->ramp < m ? q->ramp : m;
    if (r > 0)
        render_ramp(q, out, r);
    if (r < m)
        render_steady(q, out + r, m - r);
}

/* m samples of SPREAD (pcm_spread): each loop read at its four detuned playheads and averaged, under the loops'
 * weights; below SPREAD_XFADE crossfaded from the undetuned reads (which always turn); the attack undetuned on top */
static void render_spread(PcmOsc *q, int32_t *out, int m)
{
    const uint8_t *pa = q->pa, *pb = q->pb, *pk = q->pk;
    const unsigned live = q->slive;
    const int32_t sxf = q->sxf;
    uint32_t xa = q->pos_a, sa = q->step_a, la = q->len_a, xb = q->pos_b, sb = q->step_b, lb = q->len_b;
    uint32_t xk = q->pos_k, sk = q->step_k, ek = q->end_k;
    uint32_t ax[4], bx[4], as[4], bs[4];
    for (int h = 0; h < 4; h++) {
        ax[h] = q->spa[h]; bx[h] = q->spb[h];
        as[h] = q->ssa[h]; bs[h] = q->ssb[h];
    }
    int32_t wa = q->wa, wb = q->wb, wk = q->wk;
    int ramp = q->ramp;
    int use_a = pa && (wa != 0 || q->ta != 0), use_b = pb && (wb != 0 || q->tb != 0);
    for (int i = 0; i < m; i++) {
        int32_t ya = 0, yb = 0;
        for (int h = 0; h < 4; h++) {
            if (!(live & (1u << h))) continue;
            if (use_a) ya += rd(pa, ax[h]);
            if (use_b) yb += rd(pb, bx[h]);
        }
        int32_t y = (((ya >> 2) * wa) >> 15) + (((yb >> 2) * wb) >> 15);
        if (sxf < W1) {                     /* low spread: the crossfade from the undetuned reads */
            int32_t c = 0;
            if (use_a) c += (rd(pa, xa) * wa) >> 15;
            if (use_b) c += (rd(pb, xb) * wb) >> 15;
            y = c + (((y - c) * sxf) >> 15);
        }
        if (pk) {
            y += (rd(pk, xk) * wk) >> 15;
            xk += sk;
            if (q->pk2 && q->pos_k2 < q->end_k2) {
                y += (rd(q->pk2, q->pos_k2) * q->wk2) >> 15;
                q->pos_k2 += q->step_k2;
            }
        }
        out[i] = clamp15(y);
        if (pa) {
            xa = wrap(xa + sa, la);
            for (int h = 0; h < 4; h++)
                if (live & (1u << h)) ax[h] = wrap(ax[h] + as[h], la);
        }
        if (pb) {
            xb = wrap(xb + sb, lb);
            for (int h = 0; h < 4; h++)
                if (live & (1u << h)) bx[h] = wrap(bx[h] + bs[h], lb);
        }
        if (ramp) {
            RAMP_STEP(wk, q->dk, q->tk);
            RAMP_STEP(wa, q->da, q->ta);
            RAMP_STEP(wb, q->db, q->tb);
            ramp--;
        }
        if (pk && xk >= ek) {               /* the attack ended: its loops fade to SCAN's crossfade (attack_done) */
            q->pos_k = xk;
            q->wa = wa; q->wb = wb;
            q->ramp = (uint8_t)ramp;
            attack_done(q);
            pk = NULL;
            wa = q->wa; wb = q->wb; wk = 0;
            ramp = q->ramp;
            use_a = pa && (wa != 0 || q->ta != 0);
            use_b = pb && (wb != 0 || q->tb != 0);
        }
    }
    for (int h = 0; h < 4; h++) {
        q->spa[h] = ax[h];
        q->spb[h] = bx[h];
    }
    q->pos_a = xa; q->pos_b = xb; q->pos_k = xk;
    q->wa = wa; q->wb = wb; q->wk = wk;
    q->ramp = (uint8_t)ramp;
}

/* one loop's four detuned reads, averaged */
#define RD4(p, x0, x1, x2, x3) ((rd(p, x0) + rd(p, x1) + rd(p, x2) + rd(p, x3)) >> 2)

/* render_spread with the weights still, no attack and all four playheads live (most of the time): the positions and
 * steps in locals, the loop with no weight only skipped on at the end. Bit-exact with render_spread (no clamp: the
 * weights add up to 1, the sum stays in range). */
static void render_spread_steady(PcmOsc *q, int32_t *out, int m)
{
    const uint8_t *pa = q->pa, *pb = q->pb;
    const int32_t wa = q->wa, wb = q->wb, sxf = q->sxf;
    const int ua = pa && wa != 0, ub = pb && wb != 0;
    if (ua && ub) {
        uint32_t xa = q->pos_a, sa = q->step_a, la = q->len_a, xb = q->pos_b, sb = q->step_b, lb = q->len_b;
        uint32_t a0 = q->spa[0], a1 = q->spa[1], a2 = q->spa[2], a3 = q->spa[3];
        uint32_t b0 = q->spb[0], b1 = q->spb[1], b2 = q->spb[2], b3 = q->spb[3];
        const uint32_t ta0 = q->ssa[0], ta1 = q->ssa[1], ta2 = q->ssa[2], ta3 = q->ssa[3];
        const uint32_t tb0 = q->ssb[0], tb1 = q->ssb[1], tb2 = q->ssb[2], tb3 = q->ssb[3];
        for (int i = 0; i < m; i++) {
            int32_t y = ((RD4(pa, a0, a1, a2, a3) * wa) >> 15) + ((RD4(pb, b0, b1, b2, b3) * wb) >> 15);
            if (sxf < W1) {
                int32_t c = ((rd(pa, xa) * wa) >> 15) + ((rd(pb, xb) * wb) >> 15);
                y = c + (((y - c) * sxf) >> 15);
            }
            out[i] = y;
            xa = wrap(xa + sa, la);
            a0 = wrap(a0 + ta0, la); a1 = wrap(a1 + ta1, la); a2 = wrap(a2 + ta2, la); a3 = wrap(a3 + ta3, la);
            xb = wrap(xb + sb, lb);
            b0 = wrap(b0 + tb0, lb); b1 = wrap(b1 + tb1, lb); b2 = wrap(b2 + tb2, lb); b3 = wrap(b3 + tb3, lb);
        }
        q->pos_a = xa; q->pos_b = xb;
        q->spa[0] = a0; q->spa[1] = a1; q->spa[2] = a2; q->spa[3] = a3;
        q->spb[0] = b0; q->spb[1] = b1; q->spb[2] = b2; q->spb[3] = b3;
        return;
    }
    if (ua || ub) {                         /* one loop sounds (SCAN at an end, or one loop); the other turns on */
        const uint8_t *p = ua ? pa : pb;
        uint32_t *sp = ua ? q->spa : q->spb, *ss = ua ? q->ssa : q->ssb;
        uint32_t x = ua ? q->pos_a : q->pos_b, s = ua ? q->step_a : q->step_b, l = ua ? q->len_a : q->len_b;
        const int32_t w = ua ? wa : wb;
        uint32_t x0 = sp[0], x1 = sp[1], x2 = sp[2], x3 = sp[3];
        const uint32_t s0 = ss[0], s1 = ss[1], s2 = ss[2], s3 = ss[3];
        for (int i = 0; i < m; i++) {
            int32_t y = (RD4(p, x0, x1, x2, x3) * w) >> 15;
            if (sxf < W1) {
                int32_t c = (rd(p, x) * w) >> 15;
                y = c + (((y - c) * sxf) >> 15);
            }
            out[i] = y;
            x = wrap(x + s, l);
            x0 = wrap(x0 + s0, l); x1 = wrap(x1 + s1, l); x2 = wrap(x2 + s2, l); x3 = wrap(x3 + s3, l);
        }
        sp[0] = x0; sp[1] = x1; sp[2] = x2; sp[3] = x3;
        if (ua) {
            q->pos_a = x;
            if (pb) {
                q->pos_b = skip(q->pos_b, q->step_b, q->len_b, m);
                for (int h = 0; h < 4; h++) q->spb[h] = skip(q->spb[h], q->ssb[h], q->len_b, m);
            }
        } else {
            q->pos_b = x;
            if (pa) {
                q->pos_a = skip(q->pos_a, q->step_a, q->len_a, m);
                for (int h = 0; h < 4; h++) q->spa[h] = skip(q->spa[h], q->ssa[h], q->len_a, m);
            }
        }
        return;
    }
    render_spread(q, out, m);               /* (no loop has weight: rare) */
}

void pcm_render_block(PcmOsc *q, int32_t *out, int n)
{
    int i = 0;
    q->idle = 0;
    if (q->sxf) {
        int r = q->pk ? n : (q->ramp < n ? q->ramp : n);
        if (r > 0)
            render_spread(q, out, r);
        if (r < n) {
            if (q->slive == 0xFu)
                render_spread_steady(q, out + r, n - r);
            else
                render_spread(q, out + r, n - r);
        }
        return;
    }
    if (q->pk) {
        uint32_t xk = q->pos_k, sk = q->step_k, end = q->end_k;
        int na = xk < end ? (int)((end - xk + sk - 1u) / sk) : 0;
        if (na > n) na = n;
        render_attack(q, out, na);
        i = na;
        if (q->pos_k >= end)
            attack_done(q);
    }
    if (i < n)
        render_loops(q, out + i, n - i);
}

/* one sample at the steps scaled by mul (1: the tick's pitch; FM: this sample's), the same sums as the block render */
static int32_t one(PcmOsc *q, float mul)
{
    int32_t y = 0;
    int ramp = q->ramp;
    q->idle = 0;
    if (q->sxf) {                           /* SPREAD (no FM reaches it: SPREAD has no OSC2) */
        render_spread(q, &y, 1);
        return y;
    }
    if (q->pk) {
        float ak = mul < 0.f ? -mul : mul;  /* (an attack never runs backwards) */
        uint32_t sk = mul == 1.f ? q->step_k : (uint32_t)((float)q->step_k * ak);
        y += (rd(q->pk, q->pos_k) * q->wk) >> 15;
        q->pos_k += sk ? sk : 1u;
        if (q->pk2 && q->pos_k2 < q->end_k2) {
            uint32_t s2 = mul == 1.f ? q->step_k2 : (uint32_t)((float)q->step_k2 * ak);
            y += (rd(q->pk2, q->pos_k2) * q->wk2) >> 15;
            q->pos_k2 += s2 ? s2 : 1u;
        }
    }
    if (q->pa && q->wa != 0) y += (rd(q->pa, q->pos_a) * q->wa) >> 15;
    if (q->pb && q->wb != 0) y += (rd(q->pb, q->pos_b) * q->wb) >> 15;
    if (mul == 1.f) {
        if (q->pa) q->pos_a = wrap(q->pos_a + q->step_a, q->len_a);
        if (q->pb) q->pos_b = wrap(q->pos_b + q->step_b, q->len_b);
    } else {                                /* FM: either way round (through zero) */
        if (q->pa) q->pos_a = wrap_by(q->pos_a, (int32_t)((float)q->step_a * mul), q->len_a);
        if (q->pb) q->pos_b = wrap_by(q->pos_b, (int32_t)((float)q->step_b * mul), q->len_b);
    }
    if (ramp) {
        RAMP_STEP(q->wk, q->dk, q->tk);
        RAMP_STEP(q->wa, q->da, q->ta);
        RAMP_STEP(q->wb, q->db, q->tb);
        q->ramp--;
    }
    if (q->pk && q->pos_k >= q->end_k)      /* after this sample's ramp step, as the block render does */
        attack_done(q);
    return clamp15(y);
}

int32_t pcm_render_one(PcmOsc *q) { return one(q, 1.f); }

int32_t pcm_render_one_fm(PcmOsc *q, int32_t freq_q16)
{
    float mul = (float)freq_q16 * q->inv_freq;
    return one(q, mul > 4.f ? 4.f : mul < -4.f ? -4.f : mul);
}

/* the reads moved by off of a cycle for this sample (the loops round, the attacks within their samples), then back */
int32_t pcm_render_one_pm(PcmOsc *q, int32_t off)
{
    float o = (float)off;
    int32_t da = (int32_t)(q->pm_a * o), db = (int32_t)(q->pm_b * o), dk = 0, dk2 = 0;
    if (q->pa) q->pos_a = wrap_by(q->pos_a, da, q->len_a);
    if (q->pb) q->pos_b = wrap_by(q->pos_b, db, q->len_b);
    if (q->pk) {
        int64_t p = (int64_t)q->pos_k + (int32_t)(q->pm_k * o);
        p = p < 0 ? 0 : p >= (int64_t)q->end_k ? (int64_t)q->end_k - 1 : p;
        dk = (int32_t)(p - (int64_t)q->pos_k);
        q->pos_k = (uint32_t)p;
        if (q->pk2 && q->pos_k2 < q->end_k2) {
            int64_t p2 = (int64_t)q->pos_k2 + (int32_t)(q->pm_k2 * o);
            p2 = p2 < 0 ? 0 : p2 >= (int64_t)q->end_k2 ? (int64_t)q->end_k2 - 1 : p2;
            dk2 = (int32_t)(p2 - (int64_t)q->pos_k2);
            q->pos_k2 = (uint32_t)p2;
        }
    }
    const uint8_t *pk = q->pk;
    int32_t y = one(q, 1.f);
    if (q->pa) q->pos_a = wrap_by(q->pos_a, -da, q->len_a);
    if (q->pb) q->pos_b = wrap_by(q->pos_b, -db, q->len_b);
    if (q->pk && q->pk == pk) {             /* (the attack still playing: back where it would be) */
        q->pos_k -= (uint32_t)dk;
        if (q->pk2) q->pos_k2 -= (uint32_t)dk2;
    }
    return y;
}

void pcm_idle(PcmOsc *q, int n)
{
    if (q->pa) q->pos_a = skip(q->pos_a, q->step_a, q->len_a, n);
    if (q->pb) q->pos_b = skip(q->pos_b, q->step_b, q->len_b, n);
    if (q->sxf)                             /* SPREAD's playheads turn on too */
        for (int h = 0; h < 4; h++) {
            if (!(q->slive & (1u << h))) continue;
            if (q->pa) q->spa[h] = skip(q->spa[h], q->ssa[h], q->len_a, n);
            if (q->pb) q->spb[h] = skip(q->spb[h], q->ssb[h], q->len_b, n);
        }
    q->pk = NULL;
    q->pk2 = NULL;
    q->hand = 0;
    q->idle = 1;
}
