/* The "90s digital" MFX prototypes (2026-11-04, host only, the user's: "give the same 90s treatment to the expensive
 * ones", "cheap is the name of the game"). Not in the firmware: each runs through mfx.c's host-only mfx_proto_hook
 * in place of a layer's MFX, for renders and host costs, kept to refer to later.
 *
 * The 90s multi-effects way: plain algorithms at the full rate (the lo-fi reverb at a quarter), no analog modelling,
 * no oversampling, no anti-aliasing, small code (on the FM1 the floor is code fetched again from the flash: small
 * loops miss less). Each runs a pass a side in one loop, its settings worked out once a pass.
 *  D90 DRIVE      pre-emphasis, gain, one table waveshaper at 1x (the fizz is the era's), a speaker roll-off, a gate
 *  D90 ENSEMBLE   one line a side, three taps swept 120 degrees apart, a 4-stage phaser on the wet (PHASE)
 *  D90 PHASFLANG  a 4-stage phaser into one swept tap with feedback
 *  D90 PITCH      two heads circling a short line, a triangle crossfade (the classic harmonizer)
 *  D90 RESONATOR  four tuned combs a side (a minor chord on ROOT), a one-pole in each loop
 *  D90 REVERB     a cheap rack's plate: four combs and two all-passes a side at a quarter rate, 12-bit
 * Knobs k[0..3] 0..127 each. */
#ifndef MFX90_PROTO_H
#define MFX90_PROTO_H
#include <math.h>
#include <stdint.h>
#include <string.h>

enum { D90_DRIVE, D90_ENSEMBLE, D90_PHASFLANG, D90_PITCH, D90_RESONATOR, D90_REVERB, D90_COUNT };
static const char *const D90_NAMES[D90_COUNT] = {"D90 DRIVE", "D90 ENSEMBLE", "D90 PHASFLANG", "D90 PITCH",
                                                 "D90 RESONATOR", "D90 REVERB"};
#define D90_SR 44100.f
#define D90_LEN 4096u                            /* a side's line (93 ms): every type's fits */

typedef struct {
    float line[D90_LEN];
    uint32_t w;
    float z[16];                                  /* the type's filter / all-pass states */
    float dly[4];                                 /* the resonator's / reverb's per-line places */
    uint32_t q;                                   /* the reverb's quarter-rate phase */
    float acc, prev, cur;                         /* ... its input sum, its last two outputs (the interpolation) */
} D90Side;

typedef struct {
    int kind;
    float k[4];
    float lfo;
    D90Side side[2];
} D90State;

static D90State d90[2];
static float d90_tab[513];                        /* D90 DRIVE's curve over -4 .. 4 */

static inline float d90_tri(float p) { p -= (float)(int)p; return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p; }

static void d90_begin(int layer, int kind, const float *k)
{
    D90State *s = &d90[layer & 1];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    for (int j = 0; j < 4; j++) s->k[j] = k[j];
    for (int i = 0; i <= 512; i++) {             /* asymmetric: the positive half a touch harder */
        float x = (float)(i - 256) * (1.f / 64.f);
        d90_tab[i] = x >= 0.f ? tanhf(1.3f * x) : 0.85f * tanhf(x);
    }
}

/* a line read d samples back (linear) */
static inline float d90_read(const D90Side *sd, float d)
{
    int di = (int)d;
    float fr = d - (float)di;
    uint32_t a = (sd->w - (uint32_t)di) & (D90_LEN - 1u), b = (a - 1u) & (D90_LEN - 1u);
    return sd->line[a] + fr * (sd->line[b] - sd->line[a]);
}

/* four first-order all-passes at coefficient c, states z[0..4] (the chain's in/out), on x: the chain's output */
static inline float d90_ap4(float *z, float c, float x)
{
    float o = c * (x + z[1]) - z[0]; z[0] = x;
    float v = c * (o + z[2]) - z[1]; z[1] = o;
    o = c * (v + z[3]) - z[2]; z[2] = v;
    v = c * (o + z[4]) - z[3]; z[3] = o; z[4] = v;
    return v;
}

static void d90_hook(int layer, float *l, float *r, uint32_t n)
{
    D90State *s = &d90[layer & 1];
    const float *k = s->k;
    const float rate = 0.05f * exp2f(k[0] * (7.f / 127.f));        /* (the sweep types: knob 0 their RATE) */
    const float lfo0 = s->lfo;
    if (s->kind == D90_ENSEMBLE || s->kind == D90_PHASFLANG) {
        s->lfo += rate * (float)n / D90_SR;
        s->lfo -= (float)(int)s->lfo;
    }
    for (int ch = 0; ch < 2; ch++) {
        D90Side *sd = &s->side[ch];
        float *y = ch ? r : l, *z = sd->z;
        switch (s->kind) {
        case D90_DRIVE: {                        /* GAIN, TONE (presence), LEVEL, GATE */
            const float g = 2.f * exp2f(k[0] * (7.f / 127.f)) * (1.f / 32768.f), lv = 2800.f * (k[2] / 64.f);
            const float hp = 0.0170f, em = 0.0950f, thr = k[3] * (300.f / 127.f);
            const float pres = (k[1] - 64.f) * (1.f / 64.f), lp = 0.45f;    /* the speaker: ~5 kHz, two poles */
            for (uint32_t i = 0; i < n; i++) {
                float x = y[i], a = x < 0.f ? -x : x;
                z[6] += (a > z[6] ? 0.05f : 0.0005f) * (a - z[6]);           /* the gate's follower */
                z[0] += hp * (x - z[0]); x -= z[0];                          /* 120 Hz high-pass */
                z[1] += em * (x - z[1]); x += x - z[1];                      /* pre-emphasis */
                float t = x * g * 64.f + 256.f;
                t = t < 0.f ? 0.f : t > 511.99f ? 511.99f : t;
                int ti = (int)t;
                x = d90_tab[ti] + (t - (float)ti) * (d90_tab[ti + 1] - d90_tab[ti]);
                z[2] += lp * (x - z[2]); z[3] += lp * (z[2] - z[3]);         /* the speaker */
                x = z[3] + pres * (z[2] - z[3]);                             /* presence: the poles apart */
                float gg = z[6] >= thr ? 1.f : z[6] / (thr + 1e-6f);
                z[7] += 0.002f * (gg * gg - z[7]);
                y[i] = x * lv * z[7];
            }
            break;
        }
        case D90_ENSEMBLE: {                     /* RATE, DEPTH, PHASE (the phaser on the wet), MIX */
            const float base = 0.007f * D90_SR, sw = k[1] * (0.004f * D90_SR / 127.f), ph = k[2] / 127.f;
            const float mix = k[3] / 127.f, off = ch ? 1.f / 6.f : 0.f;
            for (uint32_t i = 0; i < n; i++) {
                float p = lfo0 + rate * (float)i / D90_SR + off;
                sd->line[sd->w] = y[i];
                float wet = (d90_read(sd, base + sw * d90_tri(p)) + d90_read(sd, base + sw * d90_tri(p + 1.f / 3.f))
                             + d90_read(sd, base + sw * d90_tri(p + 2.f / 3.f))) * (1.f / 3.f);
                if (ph > 0.f) {
                    float c = 0.55f + 0.4f * d90_tri(0.25f * p);
                    wet += ph * 0.5f * (d90_ap4(z, c, wet) - wet);
                }
                y[i] = y[i] * (1.f - 0.5f * mix) + wet * mix;
                sd->w = (sd->w + 1u) & (D90_LEN - 1u);
            }
            break;
        }
        case D90_PHASFLANG: {                    /* RATE, MANUAL, FEEDBACK, PHASER (0: the flanger alone) */
            const float man = 0.0003f * D90_SR * exp2f(k[1] * (4.7f / 127.f)), fb = k[2] * (0.9f / 127.f);
            const float ph = k[3] / 127.f, off = ch ? 0.5f : 0.f;
            for (uint32_t i = 0; i < n; i++) {
                float p = lfo0 + rate * (float)i / D90_SR + off, t = d90_tri(p), x = y[i];
                if (ph > 0.f) x += ph * 0.5f * (d90_ap4(z, 0.5f + 0.42f * t, x) - x);
                float d = man * (1.f + 0.8f * t), wet = d90_read(sd, d < 2.f ? 2.f : d);
                sd->line[sd->w] = x + fb * wet;
                y[i] = 0.7f * (x + wet);
                sd->w = (sd->w + 1u) & (D90_LEN - 1u);
            }
            break;
        }
        case D90_PITCH: {                        /* PITCH (0..24, 12 none), FINE, FEEDBACK, MIX */
            const float ratio = exp2f(((float)(int)(k[0] + 0.5f) - 12.f + (k[1] - 64.f) * (0.5f / 64.f)) / 12.f);
            const float win = 1600.f, step = 1.f - ratio, fb = k[2] * (0.7f / 127.f), mix = k[3] / 127.f;
            float d = z[0];
            for (uint32_t i = 0; i < n; i++) {
                d += step;
                if (d < 1.f) d += win; else if (d >= win + 1.f) d -= win;
                float d2 = d + 0.5f * win; if (d2 >= win + 1.f) d2 -= win;
                float a = (d - 1.f) / win, g1 = a < 0.5f ? 2.f * a : 2.f - 2.f * a;   /* the crossfade */
                float wet = g1 * d90_read(sd, d) + (1.f - g1) * d90_read(sd, d2);
                sd->line[sd->w] = y[i] + fb * wet;
                y[i] = y[i] * (1.f - mix) + wet * mix;
                sd->w = (sd->w + 1u) & (D90_LEN - 1u);
            }
            z[0] = d;
            break;
        }
        case D90_RESONATOR: {                    /* DECAY, TONE, MIX, ROOT (0..23 from C2): A minor's four on ROOT */
            static const int ch_notes[2][4] = {{0, 7, 15, 24}, {12, 19, 27, 31}};   /* 1 5 b3 8 / 8 5 b3 5 */
            const float t60 = 0.3f * exp2f(k[0] * (7.f / 127.f)), c = 0.25f + 0.7f * k[1] / 127.f, mix = k[2] / 127.f;
            float dd[4], gg[4];
            for (int j = 0; j < 4; j++) {
                float f = 65.41f * exp2f(((float)(int)(k[3] * 23.f / 127.f + 0.5f) + (float)ch_notes[ch][j]) / 12.f);
                dd[j] = D90_SR / f - (1.f - c) / c;
                gg[j] = exp2f(-9.966f / (f * t60));
            }
            float *lines = sd->line;                 /* four combs of 1024 in the one line */
            for (uint32_t i = 0; i < n; i++) {
                float x = y[i] * 0.06f, o = 0.f;
                for (int j = 0; j < 4; j++) {
                    float *ln = lines + 1024u * (uint32_t)j;
                    int di = (int)dd[j];
                    float fr = dd[j] - (float)di;
                    uint32_t a = (sd->w - (uint32_t)di) & 1023u, b = (a - 1u) & 1023u;
                    float v = ln[a] + fr * (ln[b] - ln[a]);
                    z[j] += c * (v - z[j]);
                    ln[sd->w & 1023u] = x + gg[j] * z[j];
                    o += z[j];
                }
                y[i] = y[i] * (1.f - 0.5f * mix) + o * mix;
                sd->w = (sd->w + 1u) & 1023u;
            }
            break;
        }
        case D90_REVERB: {                       /* DECAY, TONE (damping), MIX, AGE (12 bits at 127: grit) */
            static const uint32_t cl[2][4] = {{327, 409, 453, 482}, {341, 397, 467, 491}}, al[2][2] = {{55, 19}, {61, 23}};
            const float fb = 0.72f + 0.26f * k[0] / 127.f, damp = 0.15f + 0.6f * (1.f - k[1] / 127.f), mix = k[2] / 127.f;
            const float qn = 32768.f / exp2f(15.f - 4.f * k[3] / 127.f);   /* the converters: 16 .. 12 bits */
            float *ln = sd->line;                    /* combs at 0, 512, 1024, 1536; all-passes at 2048, 2560 */
            for (uint32_t i = 0; i < n; i++) {
                sd->acc += y[i];
                if (++sd->q == 4u) {                 /* a quarter-rate sample: four frames in */
                    float x = sd->acc * (0.25f / 32768.f), o = 0.f;
                    sd->acc = 0.f;
                    sd->q = 0u;
                    x = (float)(int)(x * qn) / qn;
                    uint32_t w = sd->w & 511u;
                    for (int j = 0; j < 4; j++) {
                        float *c = ln + 512u * (uint32_t)j;
                        float v = c[(w - cl[ch][j]) & 511u];
                        z[j] += damp * (v - z[j]);
                        c[w] = x + fb * z[j];
                        o += v;
                    }
                    o *= 0.25f;
                    for (int j = 0; j < 2; j++) {    /* the all-passes, g 0.6 */
                        float *a = ln + 2048u + 512u * (uint32_t)j, v = a[(w - al[ch][j]) & 511u], u = o + 0.6f * v;
                        a[w] = u;
                        o = v - 0.6f * u;
                    }
                    sd->prev = sd->cur;
                    sd->cur = (float)(int)(o * qn) / qn * 32768.f;
                    sd->w++;
                }
                float wet = sd->prev + (sd->cur - sd->prev) * (float)(sd->q + 1u) * 0.25f;
                y[i] = y[i] * (1.f - 0.5f * mix) + wet * mix * 1.6f;
            }
            break;
        }
        }
    }
}

#endif
