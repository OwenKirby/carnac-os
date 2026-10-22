/* The oscillators' PCM waves (pcm_osc.h). */
#include <stddef.h>
#include "pcm_osc.h"
#include "pcm_store.h"
#include "attacks.h"
#include "oscillators.h"
#include "wavetables.h"
#include "synth_voice.h"                    /* SYNTH_CTRL_PERIOD */

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
    if (code < WAVE_PCM_FIRST) return code - WAVE_ATK_FIRST < ATK_COUNT ? 1 : -1;
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
    return (uint8_t)(WAVE_ATK_FIRST + (pos < ATK_COUNT ? pos : ATK_COUNT - 1u));
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
    case 1: return attack_names[code - WAVE_ATK_FIRST];
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

int32_t wave_preview(uint8_t code, int morph_q12, uint32_t phase)
{
    switch (wave_kind(code)) {
    case 1: {
        const AttackInfo *ai = &attack_info[code - WAVE_ATK_FIRST];
        uint32_t n = ai->length < 1024u ? ai->length : 1024u;
        return dec_at(attack_data + ai->offset, (uint32_t)(((uint64_t)phase * n) >> 32));
    }
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
    int new_wave = q->code == 0;
    if (q->code >= WAVE_PCM_FIRST) {
        const PcmEntry *w = pcm_wave(q->code - WAVE_PCM_FIRST);
        if (w) {
            a = pcm_sample(w->u.w.loop_a, PCM_LOOP, &da);
            if (w->u.w.loop_b != PCM_REF_NONE)
                b = pcm_sample(w->u.w.loop_b, PCM_LOOP, &db);
            b_mul = (float)w->u.w.b_rate_q12 * (1.f / 4096.f);
            b_off = w->u.w.b_off_q16;
            k_level = (int32_t)w->u.w.atk_level * W1 / 127;
            if (w->u.w.atk_ref != PCM_REF_NONE) {
                k = pcm_sample(w->u.w.atk_ref, PCM_ONESHOT, &dk);
                if (k) { k_rate = (float)k->u.s.rate * (65536.f / 44100.f); k_root = k->root; k_len = k->u.s.len; }
            } else if (w->u.w.atk_fw < ATK_COUNT) {
                const AttackInfo *ai = &attack_info[w->u.w.atk_fw];
                dk = attack_data + ai->offset;
                k_rate = (float)ai->rate_q16;
                k_root = ai->root != ATK_UNPITCHED ? ai->root : 60u;
                k_len = ai->length;
            }
        }
    } else if (q->code >= WAVE_ATK_FIRST && q->code - WAVE_ATK_FIRST < ATK_COUNT) {
        const AttackInfo *ai = &attack_info[q->code - WAVE_ATK_FIRST];   /* an attack alone: a one-shot wave */
        dk = attack_data + ai->offset;
        k_rate = (float)ai->rate_q16;
        k_root = ai->root != ATK_UNPITCHED ? ai->root : 60u;
        k_len = ai->length;
    }
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
    /* the attack: from the start at a new note; it stops if its wave changes under it */
    if (q->trig) {
        q->trig = 0;
        q->pk = dk && k_len > 1u ? dk : NULL;
        q->pos_k = 0;
        q->end_k = k_len > 1u ? (k_len - 1u) << 16 : 0u;
        q->wk = q->pk ? k_level : 0;
        q->dk = 0;
    } else if (q->pk && q->pk != dk) {
        q->pk = NULL;
    }
    if (q->pk)
        q->step_k = step_for(k_rate, k_root, freq_q16);
    q->inv_freq = freq_q16 ? 1.f / (float)freq_q16 : 0.f;

    /* the weights: SCAN's crossfade; while the attack plays, the loop nearer SCAN alone (two reads) */
    int32_t xf = morph_q12 >= (7 << 12) ? W1 : (int32_t)(((uint32_t)morph_q12 << 15) / (7u << 12));
    if (!q->pb) xf = 0;
    if (!q->pa) xf = q->pb ? W1 : 0;
    q->xf = xf;
    int32_t ta = q->pa ? W1 - xf : 0, tb = q->pb ? xf : 0;
    if (q->pk) {
        ta = q->pa && xf < W1 / 2 ? W1 : 0;
        tb = q->pb && !ta ? W1 : 0;
    }
    if (new_wave) {                         /* a new wave: its weights at once (it starts from silence anyway) */
        q->wa = ta;
        q->wb = tb;
    }
    set_targets(q, ta, tb, q->pk ? k_level : 0);
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

static inline int32_t clamp15(int32_t y) { return y > 32767 ? 32767 : (y < -32767 ? -32767 : y); }

/* the attack ended: its loop alone fades to SCAN's crossfade over the next tick */
static void attack_done(PcmOsc *q)
{
    q->pk = NULL;
    q->wk = 0;
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

void pcm_render_block(PcmOsc *q, int32_t *out, int n)
{
    int i = 0;
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
    if (q->pk) {
        uint32_t sk = mul == 1.f ? q->step_k : (uint32_t)((float)q->step_k * mul);
        y += (rd(q->pk, q->pos_k) * q->wk) >> 15;
        q->pos_k += sk ? sk : 1u;
    }
    if (q->pa && q->wa != 0) y += (rd(q->pa, q->pos_a) * q->wa) >> 15;
    if (q->pb && q->wb != 0) y += (rd(q->pb, q->pos_b) * q->wb) >> 15;
    if (q->pa) q->pos_a = wrap(q->pos_a + (mul == 1.f ? q->step_a : (uint32_t)((float)q->step_a * mul)), q->len_a);
    if (q->pb) q->pos_b = wrap(q->pos_b + (mul == 1.f ? q->step_b : (uint32_t)((float)q->step_b * mul)), q->len_b);
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

int32_t pcm_render_one_fm(PcmOsc *q, uint32_t freq_q16)
{
    float mul = (float)freq_q16 * q->inv_freq;
    return one(q, mul > 4.f ? 4.f : mul);
}

void pcm_idle(PcmOsc *q, int n)
{
    if (q->pa) q->pos_a = skip(q->pos_a, q->step_a, q->len_a, n);
    if (q->pb) q->pos_b = skip(q->pos_b, q->step_b, q->len_b, n);
    q->pk = NULL;
}
