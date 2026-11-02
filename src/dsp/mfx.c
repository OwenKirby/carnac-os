/* MFX: the layers' modulation effects (mfx.h). Each model is worked out from its unit's circuit: the component values
 * set the filters (first-order sections, bilinear with the cutoff prewarped), lean enough for the FM1 (its FPU costs
 * ~6-7 cycles an add or multiply: a model's cost is its operation count).
 *
 * STONE PHASER, from the schematic (docs/effect schematics, the GGG-corrected one), node by node:
 *  - B2, the input stage: gain 1 + R8/R2 = 1.57, with R1 + C2 a pre-emphasis the output's R6 + C1 takes back out (they
 *    cut the chain's hiss). Not quite all: the pair leaves a treble lift of 3.5 dB from ~1.3 kHz, modelled as one
 *    shelf at the output (x + 0.5 HP(x) at 1,325 Hz, within 0.06 dB of the pair).
 *  - Node M, the summing point: B2 through R9 2.2K, the feedback through R27 1K + the FEEDBACK pot (100K), the output
 *    node N through R5 4.7K; R10 4.7K / R11 510 take M down to the chain's input (D2, a buffer).
 *  - D1 A1 A2 C1 C2: five all-pass stages (4.7K / 4.7K, 47 nF from the input to +, + to ground through 27K in parallel
 *    with one CD4009 output): each (sRC - 1) / (sRC + 1), all at one swept frequency. The 4009's sixth buffer sits in
 *    the control op-amp's loop (F1) so the five track the control voltage: modelled as a conductance growing linearly
 *    from a threshold (the MOSFET's triode region), soft at the knee. 27K alone: 125 Hz, the sweep's floor.
 *  - B1, the output mixer: + through C11 47 nF / R21 3.3K (a high-pass at 1.03 kHz), - through C8 from R29 470 / R22
 *    510, R30 4.7K back: X = 20.2 HP(w) - 10.0 w, a sixth all-pass, fixed at 1.03 kHz, with gain 10. So the circuit
 *    has six stages (three notches), one of them fixed, and the chain from M to X has a gain of about 1.
 *  - Node N, the output: M through R5 and X through R28, equal: N = (M + X) / 2, the deepest notches.
 *  - The LFO (F2 / F3, a triangle) or the manual control (F4) drive F1, the 4009's control (here both: MANUAL + DEPTH).
 * Feedback is positive at the peaks between the notches: about 0.14 with the pot at 100K (R5's path: X -> N -> M),
 * 0.62 at 0. KCL at M with N = (M + X) / 2: M (g9 + gF + gL + g5 / 2) = g9 B2 + X (gF + g5 / 2). The loop is taken a
 * sample late (X of the sample before); the op-amps never reach their rails at these gains. Stereo: each side its own
 * chain and sweep, one LFO (L/R PHASE: the right side reads it behind the left). The unit's AUTO / MANUAL switch
 * picks the LFO or MANUAL SHIFT; here MANUAL is the sweep's centre and DEPTH its width (mfx.h). */
#include "hardware/synth_libc.h"
#include "mfx.h"
#include "amplifier.h"                 /* amp_exp2f (no libm on the FM1) */

#define MFX_SR 44100.f
#define MFX_SUB 16u                      /* frames a control step: the sweep, the LFO */
#define MFX_PI 3.14159265f

MfxParams mfx_params[2];

/* ---- STONE PHASER ---- */
/* resistances in kilohms, conductances in mS */
#define SP_R9 2.2f
#define SP_R10 4.7f
#define SP_R11 0.51f
#define SP_R5 4.7f
#define SP_R27 1.0f
#define SP_POT 100.f
#define SP_KIN (SP_R11 / (SP_R10 + SP_R11))           /* M -> the chain's input */
#define SP_KP 20.215f                                  /* B1: 1 + R30 / (R29 || R22) */
#define SP_KN 10.0f                                    /*     R30 / (R29 || R22) * R22 / (R22 + R29) */
#define SP_B2 1.5745f                                  /* B2: 1 + R8 / R2 */
#define SP_G_B1 0.06822507f                            /* B1's C11 / R21, 1.026 kHz: TPT g / (1 + g) at 44.1 kHz */
#define SP_G_SH 0.08648400f                            /* the emphasis pair's shelf, 1,325 Hz: the same */
#define SP_F_LO 125.4f                                 /* the stages at 27K alone, 47 nF */
#define SP_F_HI 3400.f                                 /* ... with the 4009 fully on */
#define SP_OUT 1.30f                                   /* output trim: a pad ~1 dB under dry, pink noise +2 dB (test_mfx) */

typedef struct {
    float z[2][6];                           /* each side's chain: the last input, then each stage's last output */
    float b1[2], x[2], sh[2];                /* B1's C11 (TPT state); X, the sample before (the loop); the shelf */
    float lfo;                               /* the triangle's phase, 0..1 (the left side's) */
    float ctl[2];                            /* each side's 4009 control, 0..1, glided */
    float sp_g[3];                           /* M's input and loop gains and the output's, as last set (each control
                                              * step ramps from them: FEEDBACK turns without stepping) */
    /* BULLROARER (and ENSEMBLE: the brigades, the write place, the clock's drift, the compander, the noise) */
    int16_t br_line[2][512];                 /* each side's bucket brigade (scaled, saturated: the BBD's headroom) */
    uint32_t br_w;                           /* the write place */
    float br_orbit, br_twist;                /* the whirl's phase, the cord's (0..1) */
    float br_d[2];                           /* each side's delay at the last control step, samples */
    float br_pre[2][2], br_post[2][2];       /* the anti-alias and reconstruction filters (two poles each) */
    float br_env[2];                         /* the compander's level follower */
    float br_wow, br_wow_to;                 /* the aging clock's drift, samples, and where it heads */
    uint32_t br_rng, br_wow_n;
    /* ENSEMBLE (its chorus oscillator's phase is lfo) */
    float en_fast;                           /* the tremolo oscillator's phase */
    float en_d[3];                           /* each line's delay at the last control step, samples */
    float en_s1[2], en_s2[2], en_p[2];       /* each side's output filters: the 2-pole's states, the 1-pole's */
    float kn[5];                             /* the knobs and the row, glided (0..127): a turn moves no setting in steps */
    uint8_t type;                           /* what the state is for: a new type starts clean */
} MfxState;

static MfxState mfx_st[2];

/* tan(x) for the sweep's prewarp, x <= ~0.25 (3.4 kHz): within 0.01 % */
static inline float mfx_tan_small(float x)
{
    float x2 = x * x;
    return x * (1.f + x2 * (1.f / 3.f + x2 * (2.f / 15.f)));
}

/* the FEEDBACK makeup, r^0.31 for r = 1 .. 2.3 (a cubic fit, within 0.003 dB): the pot's load on M takes the input
 * down as FEEDBACK goes up (B2's share), made up here in part (the feedback's own peaks give some back), so FEEDBACK
 * moves the tone and not the level (not the circuit's) */
static inline float sp_fb_makeup(float r)
{
    float d = r - 1.f;
    return 1.0003521f + d * (0.30394314f + d * (-0.08184029f + d * 0.01715217f));
}

void mfx_init(void)
{
    memset(mfx_st, 0, sizeof mfx_st);
}

static void stone_phaser(MfxState *s, const float *k, float lr, float *l, float *r, uint32_t n)
{
    /* RATE: the triangle 0.08 .. 25 Hz, even in octaves (the circuit's 1M pot spans 0.25 .. 25 Hz; slower added for
     * pads). FEEDBACK: the pot, reverse-log (most of its travel where the feedback is heard). */
    float rate = 0.08f * amp_exp2f((float)k[MFX_K_RATE] * (8.29f / 127.f));
    float x = 1.f - (float)k[MFX_K_FEEDBACK] * (1.f / 127.f);
    const float g9 = 1.f / SP_R9, g_l = 1.f / (SP_R10 + SP_R11), g5h = 0.5f / SP_R5;
    const float g_f = 1.f / (SP_R27 + SP_POT * x * x * x), d = g9 + g_f + g_l + g5h;
    const float cin = SP_B2 * g9 / d, cfb = (g_f + g5h) / d;     /* M = cin in + cfb X */
    const float h = 0.5f * SP_OUT * sp_fb_makeup(d / (g9 + g_l + g5h + 1.f / (SP_R27 + SP_POT)));
    const float centre = (float)k[MFX_K_MANUAL] * (1.f / 127.f), depth = (float)k[MFX_K_DEPTH] * (1.f / 127.f);
    const float offset = (float)lr * (0.5f / 127.f);                /* L/R PHASE: the right side up to half a cycle behind */

    for (uint32_t f0 = 0; f0 < n; f0 += MFX_SUB) {
        uint32_t m = n - f0 < MFX_SUB ? n - f0 : MFX_SUB;
        float glide = 0.0155f * (float)m / (float)MFX_SUB;
        for (int ch = 0; ch < 2; ch++) {
            /* the control: MANUAL + DEPTH x the triangle about it (the right side's phase L/R PHASE behind), held to
             * the 4009's range, glided (~23 ms) as F1's integrator rounds it */
            float ph = ch ? s->lfo - offset : s->lfo;
            if (ph < 0.f) ph += 1.f;
            float tri = ph < 0.5f ? 2.f * ph : 2.f - 2.f * ph;
            float want = centre + depth * (tri - 0.5f);
            want = want < 0.f ? 0.f : want > 1.f ? 1.f : want;
            s->ctl[ch] += (want - s->ctl[ch]) * glide;
            /* the 4009's conductance: from a threshold, linear, its knee soft (a quadratic 0.1 wide); the stages'
             * frequency follows it. A stage: (c - z^-1) / (1 - c z^-1), c = (1 - t) / (1 + t), t = tan(pi f / fs) */
            float u = s->ctl[ch] - 0.1f;
            float on = (u <= -0.05f ? 0.f : u >= 0.05f ? u : (u + 0.05f) * (u + 0.05f) * 5.f) * (1.f / 0.9f);
            float t = mfx_tan_small(MFX_PI * (SP_F_LO + (SP_F_HI - SP_F_LO) * on) / MFX_SR);
            float c = (1.f - t) / (1.f + t);
            float *y = ch ? r + f0 : l + f0;
            float *z = s->z[ch];
            float z0 = z[0], z1 = z[1], z2 = z[2], z3 = z[3], z4 = z[4], z5 = z[5], sb = s->b1[ch], xp = s->x[ch];
            float sh = s->sh[ch];
            float gi = s->sp_g[0], gf = s->sp_g[1], go = s->sp_g[2], im = 1.f / (float)m;
            const float di = (cin - gi) * im, df = (cfb - gf) * im, dg = (h - go) * im;
            for (uint32_t i = 0; i < m; i++) {
                gi += di; gf += df; go += dg;
                float mm = gi * y[i] + gf * xp;                    /* node M */
                float w = SP_KIN * mm, o;
                o = c * (w + z1) - z0; z0 = w;                      /* the five swept stages */
                w = c * (o + z2) - z1; z1 = o;
                o = c * (w + z3) - z2; z2 = w;
                w = c * (o + z4) - z3; z3 = o;
                o = c * (w + z5) - z4; z4 = w; z5 = o;
                float v = SP_G_B1 * (o - sb), lp = v + sb;          /* B1 */
                sb = lp + v;
                xp = SP_KP * (o - lp) - SP_KN * o;
                float nn = mm + xp;                                 /* node N (x 2), then the emphasis shelf */
                v = SP_G_SH * (nn - sh); lp = v + sh;
                sh = lp + v;
                y[i] = (1.5f * nn - 0.5f * lp) * go;
            }
            z[0] = z0; z[1] = z1; z[2] = z2; z[3] = z3; z[4] = z4; z[5] = z5;
            s->b1[ch] = sb;
            s->sh[ch] = sh;
            s->x[ch] = xp;
        }
        s->sp_g[0] = cin; s->sp_g[1] = cfb; s->sp_g[2] = h;
        s->lfo += rate * (float)m * (1.f / MFX_SR);
        if (s->lfo >= 1.f) s->lfo -= 1.f;
    }
}

/* ---- BULLROARER ---- */
#define BR_LEN 512u                              /* the line, a power of two (max delay ~8.5 ms) */
#define BR_BASE 176.4f                           /* the centre delay, 4 ms */
#define BR_SWING 154.f                           /* REACH 127: +- 3.5 ms */
#define BR_SCALE (1.f / 65536.f)                 /* bus units into the line's +-1 */
#define BR_OUT 0.62f                             /* dry and wet each (test_mfx: the level) */

/* sin(2 pi p), p any phase (its fraction is used): a parabola with its correction, within 0.1 % */
static inline float br_sin(float p)
{
    p -= (float)(int32_t)p;
    if (p < 0.f) p += 1.f;
    float t = 2.f * p - 1.f;                     /* sin(2 pi p) = -sin(pi t) */
    float y = 4.f * (t - t * (t < 0.f ? -t : t));
    y = 0.225f * (y * (y < 0.f ? -y : y) - y) + y;
    return -y;
}

/* the line's headroom: soft, flat at +-1 */
static inline float br_sat(float u)
{
    if (u > 1.f) return 1.f;
    if (u < -1.f) return -1.f;
    return u * (1.5f - 0.5f * u * u);
}

/* a one-pole's coefficient for fc Hz */
static inline float br_g(float fc) { return 1.f - amp_exp2f(-fc * (2.f * MFX_PI * 1.442695f / MFX_SR)); }

static void bullroarer(MfxState *s, const float *k, float lr, float *l, float *r, uint32_t n)
{
    /* WHIRL 0.1 .. 8 Hz (even in octaves); the cord winds every 6 turns (no slower than once in 30 s) */
    const float whirl = 0.1f * amp_exp2f((float)k[MFX_K_WHIRL] * (6.32f / 127.f));
    const float tw_rate = whirl * (1.f / 6.f) < 0.033f ? 0.033f : whirl * (1.f / 6.f);
    const float reach = (float)k[MFX_K_REACH] * (1.f / 127.f), twist = (float)k[MFX_K_TWIST] * (1.f / 127.f);
    const float dust = (float)k[MFX_K_DUST] * (1.f / 127.f);
    const float offset = (float)lr * (0.5f / 127.f);
    const float drive = BR_SCALE * (1.f + 2.5f * dust);            /* hotter into the line as it ages */
    const float bw = 7500.f - 4200.f * dust;                        /* the BBD's bandwidth, 7.5 .. 3.3 kHz */
    const float g_pre = br_g(bw);
    const float hiss = 0.004f + 0.05f * dust * dust;                /* under the compander, of the level */
    const float wow_amt = 22.f * dust;                              /* the clock's wander, up to +-0.5 ms */

    for (uint32_t f0 = 0; f0 < n; f0 += MFX_SUB) {
        uint32_t m = n - f0 < MFX_SUB ? n - f0 : MFX_SUB;
        float dt = (float)m * (1.f / MFX_SR);
        /* the cord: the whirl's speed 1 - TWIST (1 - cos): steady, stalling, swinging back */
        float speed = 1.f - twist * (1.f - br_sin(s->br_twist + 0.25f));
        s->br_twist += tw_rate * dt;
        if (s->br_twist >= 1.f) s->br_twist -= 1.f;
        s->br_orbit += whirl * speed * dt;
        s->br_orbit -= (float)(int32_t)s->br_orbit;
        if (s->br_orbit < 0.f) s->br_orbit += 1.f;
        if (s->br_wow_n == 0u) {                                     /* a new heading for the clock every ~0.2 s */
            s->br_rng = s->br_rng * 1664525u + 1013904223u;
            s->br_wow_to = wow_amt * ((float)(int32_t)s->br_rng * (1.f / 2147483648.f));
            s->br_wow_n = 560u;
        }
        s->br_wow_n--;
        s->br_wow += (s->br_wow_to - s->br_wow) * 0.01f;
        for (int ch = 0; ch < 2; ch++) {
            float pos = br_sin(ch ? s->br_orbit - offset : s->br_orbit);      /* +1: the far side */
            float d1 = BR_BASE + BR_SWING * reach * pos + s->br_wow;
            float d0 = s->br_d[ch], dd = (d1 - d0) * (1.f / (float)m);
            float far = reach * (0.5f + 0.5f * pos);
            float g_post = br_g(bw * (1.f - 0.45f * far));                     /* the far side darker */
            float gain = BR_OUT * (1.f - 0.3f * far) / drive;                  /* ... and quieter */
            float *y = ch ? r + f0 : l + f0;
            int16_t *line = s->br_line[ch];
            float a0 = s->br_pre[ch][0], a1 = s->br_pre[ch][1], b0 = s->br_post[ch][0], b1 = s->br_post[ch][1];
            float env = s->br_env[ch];
            uint32_t w = s->br_w;
            for (uint32_t i = 0; i < m; i++) {
                float x = y[i];
                a0 += g_pre * (x - a0);                                  /* the anti-alias filter */
                a1 += g_pre * (a0 - a1);
                line[w] = (int16_t)(br_sat(a1 * drive) * 32767.f);       /* into the brigade */
                float d = d0 + dd * (float)(i + 1u), fl = (float)(int32_t)d, fr = d - fl;
                uint32_t p = (w - (uint32_t)fl) & (BR_LEN - 1u), q = (p - 1u) & (BR_LEN - 1u);
                float v = ((float)line[p] + fr * (float)(line[q] - line[p])) * (1.f / 32767.f);
                float av = v < 0.f ? -v : v;                             /* the compander: its hiss rides the level */
                env += (av > env ? 0.02f : 0.0005f) * (av - env);
                s->br_rng = s->br_rng * 1664525u + 1013904223u;
                v += env * hiss * ((float)(int32_t)s->br_rng * (1.f / 2147483648.f));
                b0 += g_post * (v - b0);                                 /* the reconstruction filter */
                b1 += g_post * (b0 - b1);
                y[i] = BR_OUT * x + gain * b1;
                w = (w + 1u) & (BR_LEN - 1u);
            }
            s->br_pre[ch][0] = a0; s->br_pre[ch][1] = a1; s->br_post[ch][0] = b0; s->br_post[ch][1] = b1;
            s->br_env[ch] = env;
            s->br_d[ch] = d1;
            if (ch) s->br_w = w;
        }
    }
}

/* ---- ENSEMBLE ---- */
/* From the schematic (docs/effect schematics, the Solina sheets 015.0212 / 015.0214): the control circuit and the
 * three modulator boards.
 *  - The chorus oscillator: a 741 square-wave relaxation oscillator, 1M8 + the 1M CHORUS FREQ trimmer into 680 nF,
 *    hysteresis 15K / 71K: 0.61 .. 0.95 Hz. Low-passed hard (1M / 120n, 1M / 680n, the 741's 1M / 56n): nearly a
 *    triangle with its corners rounded (here a triangle through x (1.5 - 0.5 x^2)). RATE spans 0.08 .. 4 Hz, the
 *    unit's range at about 72 .. 86.
 *  - The tremolo oscillator: the same with 2M2 + 1M TREMOLO FREQ into 68 nF, 5.4 .. 7.8 Hz, filtered to a sine; here
 *    6.5 Hz, the trimmer's middle. Two 22K resistors add it into the chorus's paths (SHIMMER: how much).
 *  - The phases: C1 = the two filters' sum; C2 = each through its own all-pass, about 120 degrees on; C3 = the
 *    inverter on C1 + C2, -(C1 + C2): the third phase (for sines 240 degrees exactly; for the rounded triangle not
 *    quite, as on the unit). Each drives its board's control oscillator.
 *  - A board: the control oscillator (a transistor multivibrator, 39K / 560p: about 33 kHz) clocking a TCA350Y; the
 *    control voltage moves the clock, so the delay goes as 1 / clock (further on the slow side: the BBD's lopsided
 *    swing). Here 3 ms at AGE 0, the clock swung up to +-40 % (DEPTH 127); the three clocks differ by their parts'
 *    tolerance (+-3 %). Then two Sallen-Key low-passes: 22K / 23K / 2n7 / 560p, 5.75 kHz Q 1.1 (modelled), and
 *    22K / 23K / 8n2 / 47p, 11.4 kHz (its Q works out at 6.6, very likely a misprint: here a 1-pole at 11.4 kHz).
 *    Linear and alike on the three boards, they run once on each side's sum.
 *  - The output amplifier sums the three boards (15K each); with MODULATION on, the dry signal is not added.
 *    Stereo: line 1 reads the left side, line 3 the right, line 2 both; WIDTH pans 1 and 3 out from the centre.
 *  - AGE, the unit grown old: its clocks slower (the delay to 6 ms), the filters darker (to 3.2 / 4.6 kHz), the
 *    brigades driven harder into their headroom, their hiss up a little (to -40 dB, a bed; riding the level like a
 *    compander's, which the unit lacks: a quiet layer stays quiet), the clocks wandering (to +-2.5 %). */
#define EN_D0 132.3f                             /* the delay at AGE 0, 3 ms */
#define EN_DMAX 500.f                            /* the line's reach (BR_LEN, less the interpolation's sample) */
#define EN_SWING 0.4f                            /* the clocks' swing at DEPTH 127 */
#define EN_FAST 6.5f                             /* the tremolo oscillator, Hz */
#define EN_OUT 0.81f                             /* output trim (test_mfx: pink noise within 0.4 dB of dry) */

static const float en_tol[3] = {1.f, 1.031f, 0.968f};   /* the three clocks' parts */

/* the chorus oscillator: a rounded triangle, -1..1, at phase p (its fraction) */
static inline float en_slow(float p)
{
    p -= (float)(int32_t)p;
    if (p < 0.f) p += 1.f;
    float t = p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p;
    return t * (1.5f - 0.5f * t * t);
}

/* a brigade read d samples behind the write place w, interpolated */
static inline float en_tap(const int16_t *line, uint32_t w, float d)
{
    float fl = (float)(int32_t)d, fr = d - fl;
    uint32_t p = (w - (uint32_t)fl) & (BR_LEN - 1u), q = (p - 1u) & (BR_LEN - 1u);
    return (float)line[p] + fr * (float)(line[q] - line[p]);
}

static void ensemble(MfxState *s, const float *k, float lr, float *l, float *r, uint32_t n)
{
    /* RATE 0.08 .. 4 Hz, even in octaves; SHIMMER adds the tremolo, the control's peak held (so DEPTH stays DEPTH) */
    const float age = (float)k[MFX_K_AGE] * (1.f / 127.f);
    const float rate = 0.08f * amp_exp2f((float)k[MFX_K_RATE] * (5.644f / 127.f));
    const float shim = (float)k[MFX_K_SHIMMER] * (1.f / 127.f);
    const float swing = EN_SWING * (float)k[MFX_K_DEPTH] * (1.f / 127.f) / (1.f + shim);
    const float width = (float)lr * (1.f / 127.f);
    const float dc = EN_D0 * (1.f + age);
    const float drive = BR_SCALE * (1.f + 2.f * age), undrive = 32767.f / (1.f + 2.f * age);
    const float out = EN_OUT / (BR_SCALE * 32767.f);   /* (the line holds the level, not the drive: AGE turns smoothly) */
    const float gn = (1.f + width) * (1.f / 3.f), gf = (1.f - width) * (1.f / 3.f), gc = 1.f / 3.f;
    const float hiss = 0.0015f + 0.008f * age * age;                /* of the level: -40 dB at AGE 127, a bed */
    const float wow_amt = 0.025f * age;
    /* the output filters: the 2-pole as a TPT state-variable low-pass, the 1-pole plain */
    const float g = mfx_tan_small(MFX_PI * 5750.f * (1.f - 0.45f * age) / MFX_SR);
    const float a1 = 1.f / (1.f + g * (g + 1.f / 1.1f)), a2 = g * a1, a3 = g * a2;
    const float gp = br_g(11400.f * (1.f - 0.6f * age));

    for (uint32_t f0 = 0; f0 < n; f0 += MFX_SUB) {
        uint32_t m = n - f0 < MFX_SUB ? n - f0 : MFX_SUB;
        float dt = (float)m * (1.f / MFX_SR);
        /* the control circuit: C1, C2, the inverter's C3 */
        float c[3];
        c[0] = en_slow(s->lfo) + shim * br_sin(s->en_fast);
        c[1] = en_slow(s->lfo - 1.f / 3.f) + shim * br_sin(s->en_fast - 1.f / 3.f);
        c[2] = -(c[0] + c[1]);
        s->lfo += rate * dt;
        if (s->lfo >= 1.f) s->lfo -= 1.f;
        s->en_fast += EN_FAST * dt;
        if (s->en_fast >= 1.f) s->en_fast -= 1.f;
        if (s->br_wow_n == 0u) {                                     /* the aged clocks: a new heading every ~0.2 s */
            s->br_rng = s->br_rng * 1664525u + 1013904223u;
            s->br_wow_to = wow_amt * ((float)(int32_t)s->br_rng * (1.f / 2147483648.f));
            s->br_wow_n = 560u;
        }
        s->br_wow_n--;
        s->br_wow += (s->br_wow_to - s->br_wow) * 0.01f;
        /* each board's delay: its stages over twice its clock */
        float d0[3], dd[3];
        for (int j = 0; j < 3; j++) {
            float d = dc / (en_tol[j] * (1.f + swing * c[j]) * (1.f + s->br_wow));
            d = d > EN_DMAX ? EN_DMAX : d;
            d0[j] = s->en_d[j] == 0.f ? d : s->en_d[j];
            dd[j] = (d - d0[j]) * (1.f / (float)m);
            s->en_d[j] = d;
        }
        float *yl = l + f0, *yr = r + f0;
        int16_t *bl = s->br_line[0], *br = s->br_line[1];
        float s1l = s->en_s1[0], s2l = s->en_s2[0], pl = s->en_p[0];
        float s1r = s->en_s1[1], s2r = s->en_s2[1], pr = s->en_p[1];
        float env = s->br_env[0];
        uint32_t w = s->br_w, rng = s->br_rng;
        for (uint32_t i = 0; i < m; i++) {
            float xl = yl[i], xr = yr[i];
            bl[w] = (int16_t)(br_sat(xl * drive) * undrive);               /* into the brigades */
            br[w] = (int16_t)(br_sat(xr * drive) * undrive);
            float fi = (float)(i + 1u);
            float v1 = en_tap(bl, w, d0[0] + dd[0] * fi);
            float d2 = d0[1] + dd[1] * fi;
            float v2 = 0.5f * (en_tap(bl, w, d2) + en_tap(br, w, d2));
            float v3 = en_tap(br, w, d0[2] + dd[2] * fi);
            float av = ((xl < 0.f ? -xl : xl) + (xr < 0.f ? -xr : xr)) * (0.5f * 32767.f * BR_SCALE);
            env += (av > env ? 0.02f : 0.0005f) * (av - env);
            rng = rng * 1664525u + 1013904223u;
            float nl = (float)(int32_t)rng * (1.f / 2147483648.f);
            rng = rng * 1664525u + 1013904223u;
            float nr = (float)(int32_t)rng * (1.f / 2147483648.f);
            float sl = gn * v1 + gc * v2 + gf * v3 + env * hiss * nl;      /* the output amplifier, panned */
            float sr = gf * v1 + gc * v2 + gn * v3 + env * hiss * nr;
            float u3 = sl - s2l, u1 = a1 * s1l + a2 * u3, u2 = s2l + a2 * s1l + a3 * u3;    /* the boards' filters */
            s1l = 2.f * u1 - s1l; s2l = 2.f * u2 - s2l;
            pl += gp * (u2 - pl);
            u3 = sr - s2r; u1 = a1 * s1r + a2 * u3; u2 = s2r + a2 * s1r + a3 * u3;
            s1r = 2.f * u1 - s1r; s2r = 2.f * u2 - s2r;
            pr += gp * (u2 - pr);
            yl[i] = out * pl;
            yr[i] = out * pr;
            w = (w + 1u) & (BR_LEN - 1u);
        }
        s->en_s1[0] = s1l; s->en_s2[0] = s2l; s->en_p[0] = pl;
        s->en_s1[1] = s1r; s->en_s2[1] = s2r; s->en_p[1] = pr;
        s->br_env[0] = env;
        s->br_w = w;
        s->br_rng = rng;
    }
}

float mfx_peek(int layer, int i)
{
    const MfxState *s = &mfx_st[layer & 1];
    uint8_t t = mfx_params[layer & 1].type;
    if (t != s->type && i < 3) return -1.f;          /* (not yet run as this type) */
    if (t == MFX_STONE_PHASER && i >= 0 && i < 2) return s->ctl[i];
    if (t == MFX_ENSEMBLE && i >= 0 && i < 3) return s->en_d[i] * (1.f / EN_DMAX);
    if (t == MFX_ENSEMBLE && (i == 3 || i == 4)) {   /* where its lines swing between: the clocks' fastest, slowest */
        const uint8_t *k = mfx_params[layer & 1].knob;
        float dc = EN_D0 * (1.f + (float)k[MFX_K_AGE] * (1.f / 127.f)), sw = EN_SWING * (float)k[MFX_K_DEPTH] * (1.f / 127.f);
        float d = dc / (i == 3 ? 1.f + sw : 1.f - sw);
        return (d > EN_DMAX ? EN_DMAX : d) * (1.f / EN_DMAX);
    }
    return -1.f;
}

/* the knobs' glide a control step (MFX_SUB frames): ~23 ms, as the patch's own knobs (knob smoothing) */
#define MFX_KGLIDE 0.0155f

void mfx_layer_block(int layer, float *l, float *r, uint32_t n)
{
    MfxState *s = &mfx_st[layer & 1];
    const MfxParams *p = &mfx_params[layer & 1];
    int j;
    if (p->type != s->type) {                    /* a new type (or back from OFF) starts from silence, at its knobs */
        float lfo = s->lfo;
        memset(s, 0, sizeof *s);
        s->lfo = lfo;
        s->type = p->type;
        for (j = 0; j < 4; j++) s->kn[j] = (float)p->knob[j];
        s->kn[4] = (float)p->lr;
    }
    if (p->type == MFX_OFF || p->type >= MFX_COUNT)
        return;
    /* a control step at a time: each model's settings follow the glided knobs (their set-up is a few dozen
     * operations, against the step's thousands) */
    for (uint32_t f0 = 0; f0 < n; f0 += MFX_SUB) {
        uint32_t m = n - f0 < MFX_SUB ? n - f0 : MFX_SUB;
        for (j = 0; j < 4; j++) s->kn[j] += ((float)p->knob[j] - s->kn[j]) * MFX_KGLIDE;
        s->kn[4] += ((float)p->lr - s->kn[4]) * MFX_KGLIDE;
        if (p->type == MFX_STONE_PHASER)
            stone_phaser(s, s->kn, s->kn[4], l + f0, r + f0, m);
        else if (p->type == MFX_BULLROARER)
            bullroarer(s, s->kn, s->kn[4], l + f0, r + f0, m);
        else
            ensemble(s, s->kn, s->kn[4], l + f0, r + f0, m);
    }
}
