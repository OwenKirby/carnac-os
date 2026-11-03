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

/* a TPT state-variable low-pass's coefficients (the half-rate types' filters: hr_svf, below) */
typedef struct { float a1, a2, a3; } HrSvf;

/* The models' settings, worked out from the glided knobs only when one of them has moved (key: the knobs they came
 * from; a key of -1 matches nothing): at rest a control step skips the exp2s, tans and divides (2026-11-02) */
typedef struct { float key[2], rate, cin, cfb, h; } SpSet;                          /* STONE PHASER */
typedef struct { float key[4], rate, swing, dc, drive, undrive, hiss; HrSvf f; } EnSet;   /* ENSEMBLE (ens_wet) */
typedef struct { float key[3], fast, rate, fb, h; } SsSet;                          /* ENS+PHASER's phaser */
typedef struct { float key[6], rate, depth, centre, gw, gd, drive, undrive, hiss; HrSvf f; } ChSet;   /* CHORUS */
typedef struct { float key[2], rate, f, sa, sb, xlim, ylim, hiss; } TfSet;            /* TAPE FLANGER */
typedef struct { float key[4], rate, d0, oct, fb; } BfSet;                          /* BBD FLANGER */
typedef struct { float key[6], ratio[2], fb, gd, gw, hiss; HrSvf f; int32_t mask; } PsSet;   /* PITCH SHIFT */

/* the knobs k[0..n-1] against a set's key: 1 if they differ (and the key takes them) */
static inline int mfx_moved(float *key, const float *k, int n)
{
    int j, moved = 0;
    for (j = 0; j < n; j++)
        if (key[j] != k[j]) {
            key[j] = k[j];
            moved = 1;
        }
    return moved;
}

/* One control step of a pass, as the plan (mfx_layer_plan, core 0) worked it out for both sides: the sides then
 * run from it apart, each on its own state, so one layer's two sides can run on the two cores (2026-11-02) */
typedef struct {
    uint32_t m;                              /* its frames (MFX_SUB, but for a block's last) */
    float lfo;                               /* the LFO's phase at its start */
    union {
        struct { float centre, depth, offset, g0[3], g1[3]; } sp;   /* STONE PHASER: the sweep; M's input and loop
                                                                     * gains and the output's, ramped g0 -> g1 */
        struct { HrSvf f; float drive, undrive, hiss, gn, gf, out, c, fb; int32_t d0[3], dd[3]; uint8_t phaser; } en;
                                             /* ENSEMBLE / ENS+PHASER: the boards' settings, their three read heads
                                              * (16.16, start and step), the phaser's coefficient and COLOUR */
        struct { HrSvf f; float drive, undrive, hiss, gw, gd, offset, centre, depth, wow; } ch;   /* CHORUS */
        struct { float f, sa, sb, xlim, ylim, hiss, pol, dpol; int32_t r0, rd, m0[2], md[2]; } tf;
                                             /* TAPE FLANGER: the decks' settings, the reference's read head and each
                                              * side's second deck's (16.16, start and step a frame) */
        struct { HrSvf f; float fb, g0, g1, hiss; int32_t d0[2], dd[2]; uint8_t cross; } bf;
                                             /* BBD FLANGER: the boards' filter, FEEDBACK, the delay's gain (with the
                                              * POLARITY, ramped g0 -> g1), each side's read head; MODE III's crossing */
        struct { HrSvf f; float fb, gd, gw, hiss, slope[2]; int32_t mask; } ps;
                                             /* PITCH SHIFT: the converters' filter, FEEDBACK, the mix, each side's
                                              * head's pace (1 - ratio), AGE's bits */
    } u;
} MfxStep;
#define MFX_STEPS (MFX_PASS_MAX / MFX_SUB)

/* a side's own state: what only its own run writes (on cache lines of its own: core 1 may run the right side) */
typedef struct {
    union {
        int16_t i[512];                      /* BULLROARER's bucket brigade (scaled, saturated: the BBD's headroom),
                                              * at the full rate */
        float f[256];                        /* the half-rate types' (2026-11-02: floats, no conversion a read; the
                                              * same bytes) */
    } line;
    float z[6], b1, x, sh, ctl;              /* STONE PHASER: the chain (the last input, each stage's last output);
                                              * B1's C11 (TPT state); X, the sample before (the loop); the shelf; the
                                              * 4009's control, 0..1, glided */
    float flt[4];                            /* the half-rate types' filters: ENSEMBLE's output 2-pole (0, 1); CHORUS's
                                              * anti-alias (0, 1) and reconstruction (2, 3); TAPE FLANGER's heads (0) */
    float ss_z[5];                           /* ENS+PHASER's phaser: its chain */
    float env, prev, ch_d;                   /* the compander's level follower; the last half-rate wet sample (the
                                              * interpolation); CHORUS's delay at the last step (half-rate samples) */
    uint32_t rng, w;                         /* the hiss's noise; the line's write place */
    float br_pre[2], br_post[2], br_d;       /* BULLROARER: its filters, its delay at the last step (samples) */
    float fbv;                               /* BBD FLANGER, PITCH SHIFT: the last wet sample (the feedback) */
    float ps_d, ps_d2;                       /* PITCH SHIFT: the head's delay, the new one's while it splices */
    uint32_t ps_xf;                          /* ... the splice's crossfade frames left (0: none) */
} __attribute__((aligned(64))) MfxSide;

typedef struct {
    MfxSide side[2];
    MfxStep plan[MFX_STEPS];                 /* the pass as planned (nstep steps) */
    uint32_t nstep;
    float lfo;                               /* the LFO's phase, 0..1 (the left side's; ENSEMBLE: its chorus
                                              * oscillator's) */
    float sp_g[3];                           /* STONE PHASER: M's and the output's gains, as last set (each control
                                              * step ramps from them: FEEDBACK turns without stepping) */
    float en_fast;                           /* ENSEMBLE: the tremolo oscillator's phase */
    float en_d[3];                           /* ... each line's delay at the last control step, samples */
    float ss_ctl, ss_lfo;                    /* ENS+PHASER: the OTAs' control current (0..1, glided), the LFO's phase */
    float br_wow, br_wow_to;                 /* the aging clock's drift, and where it heads */
    uint32_t br_rng, br_wow_n;               /* (its draws; BULLROARER's noise too) */
    uint32_t br_w;                           /* BULLROARER (run whole, on one core): the write place */
    float br_orbit, br_twist;                /* ... the whirl's phase, the cord's (0..1) */
    float tf_pol;                            /* TAPE FLANGER: the polarity at the last step (ramped a frame) */
    float tf_dr, tf_dm[2];                   /* ... the reference's delay and each side's second deck's at
                                              * the last step (samples; 0: none yet) */
    float tf_fl[2], tf_wow[2], tf_wow_to[2]; /* ... each deck's flutter (its phase), its wander and where it heads */
    uint32_t tf_wow_n[2];
    float bf_d[2];                           /* BBD FLANGER: each side's delay at the last step (half-rate samples) */
    float bf_g;                              /* ... the delay's gain at the last step (POLARITY: ramped) */
    float bf_in[2][MFX_PASS_MAX / 2u];       /* ... MODE III: both sides' half-rate input, for the crossed delays */
    float kn[7];                             /* the knobs and the rows (lr, row[]), glided (0..127): a turn moves no
                                              * setting in steps */
    SpSet sp_set;                            /* each model's settings as last worked out from the knobs (MfxSet) */
    EnSet en_set;
    SsSet ss_set;
    ChSet ch_set;
    TfSet tf_set;
    BfSet bf_set;
    PsSet ps_set;
    uint8_t type;                          /* what the state is for: a new type starts clean */
} __attribute__((aligned(64))) MfxState;     /* each layer's on cache lines of its own: layer 2's may run on core 1
                                               * (synth_runtime.c, as fx.c FX_LINE) */

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

/* The chain's input is SP_KIN M, so the loop is worked in those units (2026-11-02, a multiply less a sample): w = SP_KIN
 * M = cin' in + cfb X', X' = SP_KIN X (B1's two terms prescaled), N' = SP_KIN N = w + X'; the shelf is linear, so it
 * runs on N' and the output takes 1 / SP_KIN back (with the shelf's 1.5 N - 0.5 LP as (3 N - LP) / 2) */
#define SP_XA (SP_KIN * (SP_KP - SP_KN))                /* X' = SP_XA o - SP_XB LP(o) */
#define SP_XB (SP_KIN * SP_KP)

/* STONE PHASER's step, for both sides (the plan): the settings, the gains' ramp, the LFO */
static void sp_plan(MfxState *s, MfxStep *step, const float *k, float lr)
{
    /* RATE: the triangle 0.08 .. 25 Hz, even in octaves (the circuit's 1M pot spans 0.25 .. 25 Hz; slower added for
     * pads). FEEDBACK: the pot, reverse-log (most of its travel where the feedback is heard). */
    SpSet *st = &s->sp_set;
    const float kk[2] = {k[MFX_K_RATE], k[MFX_K_FEEDBACK]};
    int j;
    if (mfx_moved(st->key, kk, 2)) {
        float x = 1.f - kk[1] * (1.f / 127.f);
        const float g9 = 1.f / SP_R9, g_l = 1.f / (SP_R10 + SP_R11), g5h = 0.5f / SP_R5;
        const float g_f = 1.f / (SP_R27 + SP_POT * x * x * x), d = g9 + g_f + g_l + g5h;
        st->rate = 0.08f * amp_exp2f(kk[0] * (8.29f / 127.f));
        st->cin = SP_KIN * SP_B2 * g9 / d;                 /* SP_KIN M = cin in + cfb X' */
        st->cfb = (g_f + g5h) / d;
        st->h = (0.25f / SP_KIN) * SP_OUT * sp_fb_makeup(d / (g9 + g_l + g5h + 1.f / (SP_R27 + SP_POT)));
    }
    step->lfo = s->lfo;
    step->u.sp.centre = k[MFX_K_MANUAL] * (1.f / 127.f);
    step->u.sp.depth = k[MFX_K_DEPTH] * (1.f / 127.f);
    step->u.sp.offset = lr * (0.5f / 127.f);                       /* L/R PHASE: the right side up to half a cycle behind */
    for (j = 0; j < 3; j++) step->u.sp.g0[j] = s->sp_g[j];
    s->sp_g[0] = step->u.sp.g1[0] = st->cin;
    s->sp_g[1] = step->u.sp.g1[1] = st->cfb;
    s->sp_g[2] = step->u.sp.g1[2] = st->h;
    s->lfo += st->rate * (float)step->m * (1.f / MFX_SR);
    if (s->lfo >= 1.f) s->lfo -= 1.f;
}

/* STONE PHASER on one side (ch) over the planned pass, y in place */
static void sp_side(MfxState *s, int ch, float *y)
{
    MfxSide *sd = &s->side[ch];
    float z0 = sd->z[0], z1 = sd->z[1], z2 = sd->z[2], z3 = sd->z[3], z4 = sd->z[4], z5 = sd->z[5];
    float sb = sd->b1, xp = sd->x, sh = sd->sh;
    for (uint32_t k = 0; k < s->nstep; k++) {
        const MfxStep *step = &s->plan[k];
        const uint32_t m = step->m;
        /* the control: MANUAL + DEPTH x the triangle about it (the right side's phase L/R PHASE behind), held to the
         * 4009's range, glided (~23 ms) as F1's integrator rounds it */
        float ph = ch ? step->lfo - step->u.sp.offset : step->lfo;
        if (ph < 0.f) ph += 1.f;
        float tri = ph < 0.5f ? 2.f * ph : 2.f - 2.f * ph;
        float want = step->u.sp.centre + step->u.sp.depth * (tri - 0.5f);
        want = want < 0.f ? 0.f : want > 1.f ? 1.f : want;
        sd->ctl += (want - sd->ctl) * (0.0155f * (float)m / (float)MFX_SUB);
        /* the 4009's conductance: from a threshold, linear, its knee soft (a quadratic 0.1 wide); the stages'
         * frequency follows it. A stage: (c - z^-1) / (1 - c z^-1), c = (1 - t) / (1 + t), t = tan(pi f / fs) */
        float u = sd->ctl - 0.1f;
        float on = (u <= -0.05f ? 0.f : u >= 0.05f ? u : (u + 0.05f) * (u + 0.05f) * 5.f) * (1.f / 0.9f);
        float t = mfx_tan_small(MFX_PI * (SP_F_LO + (SP_F_HI - SP_F_LO) * on) / MFX_SR);
        float c = (1.f - t) / (1.f + t);
        float gi = step->u.sp.g0[0], gf = step->u.sp.g0[1], go = step->u.sp.g0[2];
        const float cin = step->u.sp.g1[0], cfb = step->u.sp.g1[1], h = step->u.sp.g1[2];
        /* one sample: node M (as SP_KIN M), the five swept stages, B1, node N (x 2 SP_KIN), the emphasis shelf */
#define SP_SAMPLE(GI, GF, GO) do { \
            float w = (GI) * y[i] + (GF) * xp, o; \
            o = c * (w + z1) - z0; z0 = w; \
            float nw = c * (o + z2) - z1; z1 = o; \
            o = c * (nw + z3) - z2; z2 = nw; \
            nw = c * (o + z4) - z3; z3 = o; \
            o = c * (nw + z5) - z4; z4 = nw; z5 = o; \
            float v = SP_G_B1 * (o - sb), lp = v + sb; \
            sb = lp + v; \
            xp = SP_XA * o - SP_XB * lp; \
            float nn = w + xp; \
            v = SP_G_SH * (nn - sh); lp = v + sh; \
            sh = lp + v; \
            y[i] = (3.f * nn - lp) * (GO); \
        } while (0)
        if (gi == cin && gf == cfb && go == h) {            /* FEEDBACK at rest: no ramps */
            for (uint32_t i = 0; i < m; i++)
                SP_SAMPLE(cin, cfb, h);
        } else {
            const float im = 1.f / (float)m, di = (cin - gi) * im, df = (cfb - gf) * im, dg = (h - go) * im;
            for (uint32_t i = 0; i < m; i++) {
                gi += di; gf += df; go += dg;
                SP_SAMPLE(gi, gf, go);
            }
        }
#undef SP_SAMPLE
        y += m;
    }
    sd->z[0] = z0; sd->z[1] = z1; sd->z[2] = z2; sd->z[3] = z3; sd->z[4] = z4; sd->z[5] = z5;
    sd->b1 = sb;
    sd->sh = sh;
    sd->x = xp;
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
            float d0 = s->side[ch].br_d, dd = (d1 - d0) * (1.f / (float)m);
            float far = reach * (0.5f + 0.5f * pos);
            float g_post = br_g(bw * (1.f - 0.45f * far));                     /* the far side darker */
            float gain = BR_OUT * (1.f - 0.3f * far) / drive;                  /* ... and quieter */
            float *y = ch ? r + f0 : l + f0;
            MfxSide *sd = &s->side[ch];
            int16_t *line = sd->line.i;
            float a0 = sd->br_pre[0], a1 = sd->br_pre[1], b0 = sd->br_post[0], b1 = sd->br_post[1];
            float env = sd->env;
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
            sd->br_pre[0] = a0; sd->br_pre[1] = a1; sd->br_post[0] = b0; sd->br_post[1] = b1;
            sd->env = env;
            sd->br_d = d1;
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
#define EN_D0 66.15f                             /* the delay at AGE 0, 3 ms (half-rate samples) */
#define EN_DMAX 250.f                            /* the lines' reach, 11.3 ms (half-rate samples; the line holds 512) */
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

/* ---- the brigades at half rate (2026-11-02) ----
 * ENSEMBLE, ENS+PHASER and CHORUS run their bucket brigades at 22.05 kHz, as a BBD's own clock does (the Solina's
 * ~33 kHz, the chorus's 24..77 kHz): their filters keep everything under ~10 kHz anyway. A pair of input frames is
 * averaged into one (a zero at 22.05 kHz, then the model's own anti-alias filter), and the wet comes back up by
 * linear interpolation (half a frame late: nothing beside a delay of milliseconds). Half the work a frame. Each side
 * keeps its own input: the stereo image is never summed away. A model runs one control step (m <= MFX_SUB frames,
 * even but for a block's odd last frame, which holds the last wet sample). */
#define HR_SR (MFX_SR * 0.5f)

/* tan(pi fc / HR_SR) for a half-rate filter (up to its Nyquist, where tan_small is not enough) */
static inline float hr_tan(float fc)
{
    float p = fc * (0.5f / HR_SR);
    return br_sin(p) / br_sin(p + 0.25f);
}

/* a TPT state-variable low-pass's coefficients at half rate: fc, 1 / Q */
static inline HrSvf hr_svf(float fc, float k)
{
    HrSvf f;
    float g = hr_tan(fc);
    f.a1 = 1.f / (1.f + g * (g + k));
    f.a2 = g * f.a1;
    f.a3 = g * f.a2;
    return f;
}

/* one sample through it (state s1, s2): its low-pass output */
static inline float hr_lp(const HrSvf *f, float x, float *s1, float *s2)
{
    float u3 = x - *s2, u1 = f->a1 * *s1 + f->a2 * u3, u2 = *s2 + f->a2 * *s1 + f->a3 * u3;
    *s1 = 2.f * u1 - *s1;
    *s2 = 2.f * u2 - *s2;
    return u2;
}

/* the wet's mp half-rate samples back to m frames on one side: y = dry y * gd + the wet, interpolated, * gw (gd 0:
 * all wet, the dry not read) */
static void hr_up(float *y, const float *w, uint32_t m, float *prev, float gw, float gd)
{
    uint32_t k, mp = m >> 1;
    float p = gw * *prev;
    if (gd == 0.f)
        for (k = 0; k < mp; k++) {
            float v = gw * w[k];
            y[2u * k] = 0.5f * (p + v);
            y[2u * k + 1u] = v;
            p = v;
        }
    else
        for (k = 0; k < mp; k++) {
            float v = gw * w[k];
            y[2u * k] = gd * y[2u * k] + 0.5f * (p + v);
            y[2u * k + 1u] = gd * y[2u * k + 1u] + v;
            p = v;
        }
    if (m & 1u)
        y[m - 1u] = gd * y[m - 1u] + p;
    *prev = mp ? w[mp - 1u] : *prev;
}

/* The half-rate brigades: HR_LEN floats a side (the reach EN_DMAX + the interpolation's neighbour). A read head is
 * its delay in 16.16 fixed point, stepped by an integer a sample (2026-11-02: no float -> int -> float a tap); where
 * (p, q, fr) it reads, alike for both sides' lines. */
#define HR_LEN 256u
#define HR_Q 65536.f
typedef struct { uint32_t p, q; float fr; } HrTap;
static inline HrTap hr_at(uint32_t w, int32_t d)
{
    HrTap t;
    t.fr = (float)(d & 0xFFFF) * (1.f / HR_Q);
    t.p = (w - (uint32_t)(d >> 16)) & (HR_LEN - 1u);
    t.q = (t.p - 1u) & (HR_LEN - 1u);
    return t;
}
static inline float hr_read(const float *line, const HrTap *t)
{
    float a = line[t->p];
    return a + t->fr * (line[t->q] - a);
}

/* the aged clocks' wander (ENSEMBLE, CHORUS): a new heading every ~0.2 s, amt the share of the clock */
static void hr_wow(MfxState *s, float amt)
{
    if (s->br_wow_n == 0u) {
        s->br_rng = s->br_rng * 1664525u + 1013904223u;
        s->br_wow_to = amt * ((float)(int32_t)s->br_rng * (1.f / 2147483648.f));
        s->br_wow_n = 560u;
    }
    s->br_wow_n--;
    s->br_wow += (s->br_wow_to - s->br_wow) * 0.01f;
}

/* ENSEMBLE's step, for both sides (the plan): its settings, the control circuit (C1, C2, the inverter's C3), the
 * clocks' wander, each board's read head. fast: the tremolo oscillator, Hz (ENSEMBLE: the unit's EN_FAST; ENS+PHASER:
 * following ENS RATE). out: the output gain */
static void en_plan(MfxState *s, MfxStep *step, const float *k, float lr, float fast, float out)
{
    EnSet *st = &s->en_set;
    const float kk[4] = {k[MFX_K_AGE], k[MFX_K_RATE], k[MFX_K_SHIMMER], k[MFX_K_DEPTH]};
    if (mfx_moved(st->key, kk, 4)) {
        /* RATE 0.08 .. 4 Hz, even in octaves; SHIMMER adds the tremolo, the control's peak held (so DEPTH stays
         * DEPTH) */
        const float age = kk[0] * (1.f / 127.f);
        st->rate = 0.08f * amp_exp2f(kk[1] * (5.644f / 127.f));
        st->swing = EN_SWING * kk[3] * (1.f / 127.f) / (1.f + kk[2] * (1.f / 127.f));
        st->dc = EN_D0 * (1.f + age);
        st->drive = 0.5f * BR_SCALE * (1.f + 2.f * age);            /* (x 0.5: the pair's average) */
        st->undrive = 32767.f / (1.f + 2.f * age);
        /* the hiss, of the level: -40 dB at AGE 127, a bed (x 3: the boards' sum; the noise's int32 scale) */
        st->hiss = 3.f * (0.0015f + 0.008f * age * age) * (1.f / 2147483648.f);
        /* the boards' output filters, 5.75 kHz Q 1.1 (the 11.4 kHz pole is past the half rate's reach: the pair
         * average and the interpolation stand in for it) */
        st->f = hr_svf(5750.f * (1.f - 0.45f * age), 1.f / 1.1f);
    }
    const float shim = kk[2] * (1.f / 127.f), width = lr * (1.f / 127.f);
    const float dt = (float)step->m * (1.f / MFX_SR);
    const uint32_t mp = step->m >> 1;
    float c[3];
    int j;
    step->lfo = s->lfo;
    step->u.en.f = st->f;
    step->u.en.drive = st->drive;
    step->u.en.undrive = st->undrive;
    step->u.en.hiss = st->hiss;
    step->u.en.gn = 1.f + width;                                   /* WIDTH: board 1 towards the left, 3 the right */
    step->u.en.gf = 1.f - width;
    step->u.en.out = out;
    step->u.en.phaser = 0;
    c[0] = en_slow(s->lfo) + shim * br_sin(s->en_fast);
    c[1] = en_slow(s->lfo - 1.f / 3.f) + shim * br_sin(s->en_fast - 1.f / 3.f);
    c[2] = -(c[0] + c[1]);
    s->lfo += st->rate * dt;
    if (s->lfo >= 1.f) s->lfo -= 1.f;
    s->en_fast += fast * dt;
    if (s->en_fast >= 1.f) s->en_fast -= 1.f;
    hr_wow(s, 0.025f * kk[0] * (1.f / 127.f));
    /* each board's delay: its stages over twice its clock (half-rate samples), its read head stepping there */
    const float im = mp ? 1.f / (float)mp : 0.f, wow = 1.f + s->br_wow;
    for (j = 0; j < 3; j++) {
        float d = st->dc / (en_tol[j] * (1.f + st->swing * c[j]) * wow);
        d = d > EN_DMAX ? EN_DMAX : d;
        float a = s->en_d[j] == 0.f ? d : s->en_d[j];
        step->u.en.d0[j] = (int32_t)(a * HR_Q);
        step->u.en.dd[j] = (int32_t)((d - a) * im * HR_Q);
        s->en_d[j] = d;
    }
}

/* ENSEMBLE (and ENS+PHASER) on one side (ch) over the planned pass, y in place: its own three lines from its own
 * input (the three boards, on each side), at half rate, then (ENS+PHASER) the phaser's chain, back to the full rate.
 * Each side its own compander follower (2026-11-02: one for both before; a side cannot read the other's input while
 * the other core writes it back) */
static void en_side(MfxState *s, int ch, float *y)
{
    MfxSide *sd = &s->side[ch];
    float *line = sd->line.f;
    float s1 = sd->flt[0], s2 = sd->flt[1], env = sd->env;
    float z0 = sd->ss_z[0], z1 = sd->ss_z[1], z2 = sd->ss_z[2], z3 = sd->ss_z[3], z4 = sd->ss_z[4];
    uint32_t w = sd->w, rng = sd->rng;
    for (uint32_t k = 0; k < s->nstep; k++) {
        const MfxStep *step = &s->plan[k];
        const uint32_t m = step->m, mp = m >> 1;
        const HrSvf f = step->u.en.f;
        const float drive = step->u.en.drive, undrive = step->u.en.undrive, hiss = step->u.en.hiss;
        const float g1 = ch ? step->u.en.gf : step->u.en.gn, g3 = ch ? step->u.en.gn : step->u.en.gf;
        float wet[MFX_SUB / 2];
        int32_t h1 = step->u.en.d0[0], h2 = step->u.en.d0[1], h3 = step->u.en.d0[2];
        const int32_t e1 = step->u.en.dd[0], e2 = step->u.en.dd[1], e3 = step->u.en.dd[2];
        for (uint32_t i = 0; i < mp; i++) {
            float x = y[2u * i] + y[2u * i + 1u];                  /* (the pair x 2) */
            line[w] = br_sat(x * drive) * undrive;                 /* into the brigades */
            h1 += e1; h2 += e2; h3 += e3;
            HrTap t1 = hr_at(w, h1), t2 = hr_at(w, h2), t3 = hr_at(w, h3);
            float av = (x < 0.f ? -x : x) * (0.5f * 32767.f * BR_SCALE);
            env += (av > env ? 0.04f : 0.001f) * (av - env);
            rng = rng * 1664525u + 1013904223u;
            float v = g1 * hr_read(line, &t1) + hr_read(line, &t2) + g3 * hr_read(line, &t3)
                      + env * hiss * (float)(int32_t)rng;
            wet[i] = hr_lp(&f, v, &s1, &s2);                       /* the boards' filters */
            w = (w + 1u) & (HR_LEN - 1u);
        }
        if (step->u.en.phaser) {                                   /* ENS+PHASER: the chain, on the wet */
            const float c = step->u.en.c, fb = step->u.en.fb;
            for (uint32_t i = 0; i < mp; i++) {
                float x = wet[i], v = x + fb * z4, o;                   /* COLOR: the chain's end back to its start */
                o = c * (v + z1) - z0; z0 = v;                          /* IC1 .. IC4 */
                v = c * (o + z2) - z1; z1 = o;
                o = c * (v + z3) - z2; z2 = v;
                v = c * (o + z4) - z3; z3 = o; z4 = v;
                wet[i] = x + v;                                         /* dry and the chain, alike (x h: out) */
            }
        }
        hr_up(y, wet, m, &sd->prev, step->u.en.out, 0.f);          /* all wet, as the unit */
        y += m;
    }
    sd->flt[0] = s1; sd->flt[1] = s2; sd->env = env;
    sd->ss_z[0] = z0; sd->ss_z[1] = z1; sd->ss_z[2] = z2; sd->ss_z[3] = z3; sd->ss_z[4] = z4;
    sd->w = w;
    sd->rng = rng;
}

/* the output gain: the line holds the level, not the drive (AGE turns smoothly); the boards' 1/3 */
#define EN_GAIN (EN_OUT / (3.f * BR_SCALE * 32767.f))

/* ---- ENS+PHASER ---- */
/* The phaser, from the schematic (docs/effect schematics, small_stone.png):
 *  - IC1..IC4, four CA3094 OTA all-pass stages (27K / 6.8 nF round each, a Darlington buffer after): each stage's
 *    frequency is the OTA's transconductance over its 6.8 nF, so linear in the one control current all four share
 *    (pin 5). The current's range is not on the sheet: taken as 70 Hz .. 3 kHz, the pedal's sweep by ear.
 *  - IC5 and its transistors, the LFO: a triangle (the integrator's 33 uF, the 1M RATE pot); here 0.04 .. 10 Hz.
 *  - The output: the input buffer's dry through 27K / 30K and the chain's end, alike: (dry + chain) / 2, two notches.
 *  - COLOR: the chain's end back to its start (27K / 3K3 / 4K7, 0.1 uF, 270K): positive feedback, sharper notches
 *    and peaks between. A switch on the pedal (on: about 0.45 here); a row here, 0 .. 0.7.
 * It runs on the ensemble's wet at half rate (its sweep tops out at 3 kHz), each side its own chain, one LFO. The
 * ensemble runs first, as ENSEMBLE, with its knobs folded: ENS RATE is its RATE, the tremolo at the unit's ratio to it
 * (6.5 : 0.78) but only a third as far in octaves; ENS DEPTH its DEPTH, SHIMMER held at the unit's own (45). */
#define SS_F_LO 70.f
#define SS_F_HI 3000.f
#define SS_FB 0.7f                               /* COLOUR 127 */
#define SS_SHIM 45.f                             /* the ensemble's SHIMMER, held */
#define SS_OUT 1.41f                             /* output trim: pink noise +2 dB, as STONE PHASER (a pad ~1 dB under dry) */

/* ENS+PHASER's step (the plan): the ensemble's, with its knobs folded, then the phaser's control and COLOUR */
static void ep_plan(MfxState *s, MfxStep *step, const float *k, const float *row, float lr)
{
    float ke[4];
    ke[MFX_K_AGE] = row[MFX_R_AGE];
    ke[MFX_K_RATE] = k[MFX_K_ENS_RATE];
    ke[MFX_K_SHIMMER] = SS_SHIM;
    ke[MFX_K_DEPTH] = k[MFX_K_ENS_DEPTH];
    SsSet *st = &s->ss_set;
    const float kk[3] = {k[MFX_K_ENS_RATE], k[MFX_K_PH_RATE], row[MFX_R_COLOUR]};
    if (mfx_moved(st->key, kk, 3)) {
        /* the tremolo: 6.5 Hz at the unit's 0.78 Hz chorus, a third of ENS RATE's octaves from there */
        st->fast = EN_FAST * amp_exp2f((kk[0] - 74.f) * (5.644f / 127.f / 3.f));
        st->rate = 0.04f * amp_exp2f(kk[1] * (7.966f / 127.f));
        st->fb = SS_FB * kk[2] * (1.f / 127.f);
        st->h = 0.5f * SS_OUT * (1.f - 0.68f * st->fb);           /* (the feedback's peaks give level back) */
    }
    en_plan(s, step, ke, lr, st->fast, st->h * EN_GAIN);
    const float depth = k[MFX_K_PH_DEPTH] * (1.f / 127.f);
    float tri = s->ss_lfo < 0.5f ? 2.f * s->ss_lfo : 2.f - 2.f * s->ss_lfo;
    float want = 0.5f + depth * (tri - 0.5f);
    s->ss_ctl += (want - s->ss_ctl) * (0.0155f * (float)step->m / (float)MFX_SUB);
    float t = hr_tan(SS_F_LO + (SS_F_HI - SS_F_LO) * s->ss_ctl);
    step->u.en.c = (1.f - t) / (1.f + t);
    step->u.en.fb = st->fb;
    step->u.en.phaser = 1;
    s->ss_lfo += st->rate * (float)step->m * (1.f / MFX_SR);
    if (s->ss_lfo >= 1.f) s->ss_lfo -= 1.f;
}

/* ---- CHORUS ---- */
/* From the schematic (docs/effect schematics, alpha juno chorus.png), each of its two channels (A, B) alike:
 *  - IC26a and R141 / C91, R84 / C53, the anti-alias filter: two 1-poles at 8.8 kHz and a Sallen-Key 2-pole (10K /
 *    10K, 6.8 nF / 390 pF: 9.8 kHz, Q 2.1); here lumped into one 2-pole at 9 kHz, Q 1.0 (the peak riding the 1-poles'
 *    fall), run at half rate.
 *  - IC23, an MN3009 (256 stages), clocked by IC24 (an MN3101) whose rate Q5 swings from the LFO: the delay follows
 *    the LFO, a triangle (the CPU's, through the S/H): 1.7 .. 5.4 ms on the classic setting.
 *  - IC25a and R111 / R112 / C70, the reconstruction filter: as the anti-alias one (lumped the same).
 *  - The detector (IC22) and IC31, a compander round the brigade: the BBD's hiss rides the signal's level.
 *  - Channel B's clock swings against A's (WIDTH 127): each side's wet moves the other way, the stereo chorus. The
 *    dry and the wet are summed after; here MIX.
 * Made variable (the user's): RATE 0.1 .. 10 Hz (the classic settings ~0.5, ~0.9, ~9.8 Hz), DEPTH (+-2.5 ms at 127),
 * DELAY (the sweep's centre, 1 .. 8 ms; the classic 3.5 ms at ~46), MIX (equal power, 64 the classic even sum). Rows:
 * WIDTH (B's LFO 0 .. half a cycle behind A's), AGE (as ENSEMBLE's: darker, hotter, hiss, the clock wandering). Each
 * side its own brigade from its own input: the stereo image kept. */
#define CH_DMIN 4.f                              /* the shortest delay, half-rate samples (0.18 ms) */
#define CH_OUT 1.0f                              /* the wet's trim (test_mfx: the level) */

/* CHORUS's step, for both sides (the plan) */
static void ch_plan(MfxState *s, MfxStep *step, const float *k, const float *row, float lr)
{
    ChSet *st = &s->ch_set;
    const float kk[5] = {k[MFX_K_CH_RATE], k[MFX_K_CH_DEPTH], k[MFX_K_CH_DELAY], k[MFX_K_CH_MIX], row[MFX_R_AGE]};
    if (mfx_moved(st->key, kk, 5)) {
        const float mix = kk[3] * (1.f / 127.f), age = kk[4] * (1.f / 127.f);
        /* equal power, less where the two meet (their low end adds: MIX turns no louder in the middle) */
        const float trim = 1.f / (1.f + 0.25f * br_sin(0.5f * mix));
        st->rate = 0.1f * amp_exp2f(kk[0] * (6.644f / 127.f));
        st->depth = kk[1] * (2.5f * HR_SR * 0.001f / 127.f);
        st->centre = (1.f + kk[2] * (7.f / 127.f)) * (HR_SR * 0.001f);
        st->gw = trim * br_sin(0.25f * mix) * (CH_OUT / (BR_SCALE * 32767.f));
        st->gd = trim * br_sin(0.25f * mix + 0.25f);
        st->drive = 0.5f * BR_SCALE * (1.f + 2.f * age);            /* (x 0.5: the pair's average) */
        st->undrive = 32767.f / (1.f + 2.f * age);
        st->hiss = (0.002f + 0.008f * age * age) * (1.f / 2147483648.f);   /* (the noise's int32 scale) */
        st->f = hr_svf(9000.f * (1.f - 0.5f * age), 1.f);
    }
    hr_wow(s, 0.02f * kk[4] * (1.f / 127.f));
    step->lfo = s->lfo;
    step->u.ch.f = st->f;
    step->u.ch.drive = st->drive;
    step->u.ch.undrive = st->undrive;
    step->u.ch.hiss = st->hiss;
    step->u.ch.gw = st->gw;
    step->u.ch.gd = st->gd;
    step->u.ch.offset = lr * (0.5f / 127.f);
    step->u.ch.centre = st->centre;
    step->u.ch.depth = st->depth;
    step->u.ch.wow = s->br_wow;
    s->lfo += st->rate * (float)step->m * (1.f / MFX_SR);
    if (s->lfo >= 1.f) s->lfo -= 1.f;
}

/* CHORUS on one side (ch) over the planned pass, y in place: its own brigade from its own input */
static void ch_side(MfxState *s, int ch, float *y)
{
    MfxSide *sd = &s->side[ch];
    float *line = sd->line.f;
    float a1 = sd->flt[0], a2 = sd->flt[1], b1 = sd->flt[2], b2 = sd->flt[3], env = sd->env;
    uint32_t w = sd->w, rng = sd->rng;
    for (uint32_t k = 0; k < s->nstep; k++) {
        const MfxStep *step = &s->plan[k];
        const uint32_t m = step->m, mp = m >> 1;
        const HrSvf f = step->u.ch.f;
        const float drive = step->u.ch.drive, undrive = step->u.ch.undrive, hiss = step->u.ch.hiss;
        float wet[MFX_SUB / 2];
        float ph = ch ? step->lfo - step->u.ch.offset : step->lfo;
        if (ph < 0.f) ph += 1.f;
        float tri = ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;
        float d = (step->u.ch.centre + step->u.ch.depth * tri) * (1.f + step->u.ch.wow);
        d = d < CH_DMIN ? CH_DMIN : d > EN_DMAX ? EN_DMAX : d;
        float d0 = sd->ch_d == 0.f ? d : sd->ch_d;
        int32_t hd = (int32_t)(d0 * HR_Q), dd = (int32_t)((d - d0) * (mp ? 1.f / (float)mp : 0.f) * HR_Q);
        sd->ch_d = d;
        for (uint32_t i = 0; i < mp; i++) {
            float a = y[2u * i] + y[2u * i + 1u];                   /* (the pair x 2) */
            float v = hr_lp(&f, a, &a1, &a2);                      /* the anti-alias filter */
            line[w] = br_sat(v * drive) * undrive;                 /* into the brigade */
            hd += dd;
            HrTap t = hr_at(w, hd);
            float av = (a < 0.f ? -a : a) * (0.5f * 32767.f * BR_SCALE);   /* the compander: its hiss rides the level */
            env += (av > env ? 0.04f : 0.001f) * (av - env);
            rng = rng * 1664525u + 1013904223u;
            v = hr_read(line, &t) + env * hiss * (float)(int32_t)rng;
            wet[i] = hr_lp(&f, v, &b1, &b2);                       /* the reconstruction filter */
            w = (w + 1u) & (HR_LEN - 1u);
        }
        hr_up(y, wet, m, &sd->prev, step->u.ch.gw, step->u.ch.gd);
        y += m;
    }
    sd->flt[0] = a1; sd->flt[1] = a2; sd->flt[2] = b1; sd->flt[3] = b2; sd->env = env;
    sd->w = w;
    sd->rng = rng;
}

/* ---- TAPE FLANGER ---- */
/* Two tape machines playing the same tape, summed, the second slowed by a thumb on its reel's flange (mfx.h):
 *  - The record head: the tape's soft headroom (TAPE hotter into it, the level held after), into one line a side (the
 *    one copy both decks play), int16 at the full rate (BULLROARER's 512: 11.6 ms). Through zero needs the full rate:
 *    as the decks meet the comb's notches run up past 20 kHz, the whoosh, which half a rate would cut off.
 *  - The reference deck reads TF_REF behind the head (the whole effect a 3 ms delay, as the studios' was); the second
 *    deck TF_REF + the lead, from 3 ms ahead of the reference to 8.5 ms behind it. The thumb's sweep is the rounded
 *    triangle the decks' inertia gives it (ENSEMBLE's chorus oscillator's shape), MANUAL its centre (-2 .. +6 ms, in
 *    step at ~32), DEPTH its width (to 8 ms).
 *  - Each deck its own flutter (a capstan's ~0.1 % at 6.3 and 7.1 Hz, the two machines never alike) and wander (a new
 *    heading every ~0.4 s), the reference's too: one tape for both sides, so the sides share them. TAPE 0 stills
 *    them: then the decks meet exactly and NEGATIVE nulls to silence.
 *  - The playback heads' loss (a 1-pole, 15 kHz at TAPE 0 to 6 kHz worn; linear and alike on both decks, run once on
 *    their sum) and the tape's hiss, riding the level as behind a noise reduction (a quiet layer stays quiet).
 *  - The sum: (reference +- second) / 2, unity where they meet in phase. */
#define TF_LEN 512u                              /* the line (as BR_LEN), a power of two */
#define TF_MS (MFX_SR * 0.001f)
#define TF_REF (3.f * TF_MS)                     /* the reference deck's delay */
#define TF_DMAX ((float)(TF_LEN - 3u))           /* the second deck's furthest (the interpolation's neighbour) */
#define TF_FLUT 1.2f                             /* each deck's flutter at TAPE 127, samples (+-27 us) */
#define TF_WOW 6.f                               /* ... its wander, samples (+-0.14 ms) */
#define TF_K0 (BR_SCALE * 32767.f)               /* bus units into the line's */
#define TF_OUT 1.f                               /* output trim (test_mfx: the level) */

/* a deck's wander: a new heading every ~0.4 s, amt samples */
static void tf_drift(MfxState *s, int j, float amt)
{
    if (s->tf_wow_n[j] == 0u) {
        s->br_rng = s->br_rng * 1664525u + 1013904223u;
        s->tf_wow_to[j] = amt * ((float)(int32_t)s->br_rng * (1.f / 2147483648.f));
        s->tf_wow_n[j] = 1100u + 97u * (uint32_t)j;
    }
    s->tf_wow_n[j]--;
    s->tf_wow[j] += (s->tf_wow_to[j] - s->tf_wow[j]) * 0.005f;
}

/* a read head's start and step over m frames, from the delay a (samples) at the last step to d */
static inline void tf_head(float a, float d, uint32_t m, int32_t *h0, int32_t *dh)
{
    *h0 = (int32_t)(a * HR_Q);
    *dh = (int32_t)((d - a) * (1.f / (float)m) * HR_Q);
}

/* TAPE FLANGER's step, for both sides (the plan): the settings, the thumb, both decks' wobbles, the read heads */
static void tf_plan(MfxState *s, MfxStep *step, const float *k, const float *row, float lr)
{
    TfSet *st = &s->tf_set;
    const float kk[2] = {k[MFX_K_TF_RATE], k[MFX_K_TF_TAPE]};
    const float tape = kk[1] * (1.f / 127.f), dt = (float)step->m * (1.f / MFX_SR);
    int j;
    if (mfx_moved(st->key, kk, 2)) {
        st->rate = 0.03f * amp_exp2f(kk[0] * (7.38f / 127.f));       /* 0.03 .. 5 Hz, even in octaves */
        /* the record head: sat(x drive) undrive (br_sat: u (1.5 - 0.5 u^2), flat at +-1 past 1), with the output's
         * gain G (the decks' (ref +- second) / 2 back to bus units) folded in: x (sa - sb x^2) below xlim, +-ylim past */
        const float drive = BR_SCALE * (1.f + 2.f * tape), undrive = 32767.f / (1.f + 2.f * tape);
        const float g = TF_OUT * 0.5f / TF_K0;
        st->f = br_g(15000.f - 9000.f * tape);
        st->sa = 1.5f * drive * undrive * g;
        st->sb = 0.5f * drive * drive * drive * undrive * g;
        st->xlim = 1.f / drive;
        st->ylim = undrive * g;
        st->hiss = (0.0015f + 0.01f * tape * tape) * g * (1.f / 2147483648.f);   /* (the noise's int32 scale) */
    }
    step->u.tf.f = st->f;
    step->u.tf.sa = st->sa;
    step->u.tf.sb = st->sb;
    step->u.tf.xlim = st->xlim;
    step->u.tf.ylim = st->ylim;
    step->u.tf.hiss = st->hiss;
    {                                                                /* POLARITY, glided and ramped a frame: a
                                                                      * crossfade, no click */
        float p = row[MFX_R_POLARITY], pol = p >= 1.f ? -1.f : 1.f - 2.f * p;
        float a = s->tf_dr == 0.f ? pol : s->tf_pol;
        step->u.tf.pol = a;
        step->u.tf.dpol = (pol - a) * (1.f / (float)step->m);
        s->tf_pol = pol;
    }
    /* the decks' wobbles (one tape: both sides alike) */
    float wob[2];
    s->tf_fl[0] += 6.3f * dt;
    s->tf_fl[1] += 7.1f * dt;
    for (j = 0; j < 2; j++) {
        if (s->tf_fl[j] >= 1.f) s->tf_fl[j] -= 1.f;
        tf_drift(s, j, TF_WOW * tape);
        wob[j] = TF_FLUT * tape * br_sin(s->tf_fl[j]) + s->tf_wow[j];
    }
    const float centre = (-2.f + k[MFX_K_TF_MANUAL] * (8.f / 127.f)) * TF_MS;
    const float half = k[MFX_K_TF_DEPTH] * (4.f / 127.f) * TF_MS, offset = lr * (0.5f / 127.f);
    float dr = TF_REF + wob[0];
    tf_head(s->tf_dr == 0.f ? dr : s->tf_dr, dr, step->m, &step->u.tf.r0, &step->u.tf.rd);
    for (j = 0; j < 2; j++) {
        float d = TF_REF + centre + half * en_slow(j ? s->lfo - offset : s->lfo) + wob[1];
        d = d < 0.f ? 0.f : d > TF_DMAX ? TF_DMAX : d;
        tf_head(s->tf_dr == 0.f ? d : s->tf_dm[j], d, step->m, &step->u.tf.m0[j], &step->u.tf.md[j]);
        s->tf_dm[j] = d;
    }
    s->tf_dr = dr;
    step->lfo = s->lfo;
    s->lfo += st->rate * dt;
    if (s->lfo >= 1.f) s->lfo -= 1.f;
}

/* one interpolated read of the tape at head h (16.16 samples behind the record head at w) */
static inline float tf_read(const int16_t *line, uint32_t w, int32_t h)
{
    uint32_t p = (w - (uint32_t)(h >> 16)) & (TF_LEN - 1u), q = (p - 1u) & (TF_LEN - 1u);
    float a = (float)line[p];
    return a + (float)(h & 0xFFFF) * (1.f / HR_Q) * ((float)line[q] - a);
}

/* the hiss's level follower, a step at a time (2026-11-02, was a sample at a time): from the step's peak, with the
 * per-sample follower's attack (0.02) and release (0.0005) taken over the step's m frames */
static inline float tf_env(float env, float peak, uint32_t m)
{
    float av = peak * TF_K0;
    if (av > env)
        return env + (1.f - amp_exp2f((float)m * -0.029146f)) * (av - env);   /* 1 - 0.98^m */
    return env + (1.f - amp_exp2f((float)m * -0.000722f)) * (av - env);       /* 1 - 0.9995^m */
}

/* TAPE FLANGER on one side (ch) over the planned pass, y in place: its own tape from its own input.
 * Optimized 2026-11-02 (the same sound): the record head's saturation folded into one polynomial (x (a - b x^2), the
 * drive and its make-up in a and b; flat at +-ylim past xlim), the hiss's follower a step at a time (tf_env), the
 * POLARITY at rest an add or a subtract (its multiply only while it crossfades), the output gain 1 (TF_OUT) */
static void tf_side(MfxState *s, int ch, float *y)
{
    MfxSide *sd = &s->side[ch];
    int16_t *line = sd->line.i;
    float lp = sd->flt[0], env = sd->env;
    uint32_t w = sd->w, rng = sd->rng;
    for (uint32_t k = 0; k < s->nstep; k++) {
        const MfxStep *step = &s->plan[k];
        const uint32_t m = step->m;
        const float f = step->u.tf.f, sa = step->u.tf.sa, sb = step->u.tf.sb, xl = step->u.tf.xlim;
        const float yl = step->u.tf.ylim, hs = env * step->u.tf.hiss, dpol = step->u.tf.dpol;
        float pol = step->u.tf.pol, peak = 0.f;
        int32_t hr = step->u.tf.r0, hm = step->u.tf.m0[ch];
        const int32_t er = step->u.tf.rd, em = step->u.tf.md[ch];
        /* one frame: the record head (its saturation folded), the step's peak, both decks, the hiss, the heads */
#define TF_FRAME(MOVING) do { \
            float x = y[i], ax = x < 0.f ? -x : x; \
            peak = ax > peak ? ax : peak; \
            line[w] = (int16_t)(ax < xl ? x * (sa - sb * x * x) : x < 0.f ? -yl : yl); \
            hr += er; hm += em; \
            rng = rng * 1664525u + 1013904223u; \
            float v = tf_read(line, w, hr) MOVING + hs * (float)(int32_t)rng; \
            lp += f * (v - lp); \
            y[i] = lp; \
            w = (w + 1u) & (TF_LEN - 1u); \
        } while (0)
        if (dpol != 0.f || (pol != 1.f && pol != -1.f)) {
            for (uint32_t i = 0; i < m; i++) {                     /* POLARITY crossfading */
                pol += dpol;
                TF_FRAME(+ pol * tf_read(line, w, hm));
            }
        } else if (pol > 0.f) {
            for (uint32_t i = 0; i < m; i++)
                TF_FRAME(+ tf_read(line, w, hm));
        } else {
            for (uint32_t i = 0; i < m; i++)
                TF_FRAME(- tf_read(line, w, hm));
        }
#undef TF_FRAME
        env = tf_env(env, peak, m);
        y += m;
    }
    sd->flt[0] = lp; sd->env = env;
    sd->w = w;
    sd->rng = rng;
}

/* ---- BBD FLANGER ---- */
/* From the block diagram (docs/effect schematics, roland sbf325.png): each channel a delay line with its FEEDBACK
 * (the pot from the line's output back to its input), the DIRECT and the delay summed at the output, the delay's
 * phase switched (NON / INV), one LFO (CENTER FREQ, RATE, DEPTH, a triangle) driving both lines' clocks, channel
 * B's through an inverter (NON / INV). The mode buttons switch the feedback, the crossing (A's delay to B's output,
 * B's to A's) and the inverter; here FLANGER I (alike), II (B inverted), III (II crossed), CHORUS (no feedback, B
 * inverted). The schematic's parts are not legible enough to read the lines' range: taken as a BBD flanger's, the
 * clock swept exponentially (the comb moves evenly in pitch; the delay as 1 / clock), CENTER 0.25 .. 10 ms, DEPTH
 * up to +-1.5 octaves of clock. The lines run at half rate (CHORUS's machinery: the band ~9.5 kHz, the BBD's own),
 * soft at the top of their headroom (the feedback saturates there, it cannot run away), a little hiss riding the
 * level. The direct and the delay alike at the output (BF_MIX each). */
#define BF_DMIN (0.25f * HR_SR * 0.001f)        /* CENTER 0, half-rate samples */
#define BF_DOCT 5.32f                            /* CENTER's range in octaves (to 10 ms) */
#define BF_UN (65536.f / 1.5f)                   /* the line's level back to the bus's (br_sat's slope 1.5) */
#define BF_MIX 0.6f                              /* the direct and the delay each (test_mfx: the level) */

static HrSvf bf_filter(void)                     /* the boards' band (the same each way), worked out once */
{
    static HrSvf f;
    static int ok;
    if (!ok) { f = hr_svf(9500.f, 1.f / 0.9f); ok = 1; }
    return f;
}

static int bf_mode(const float *row)
{
    int m = (int)(row[MFX_R_MODE] + 0.5f);
    return m < 0 ? 0 : m >= MFX_BF_MODES ? MFX_BF_MODES - 1 : m;
}

/* BBD FLANGER's step, for both sides (the plan) */
static void bf_plan(MfxState *s, MfxStep *step, const float *k, const float *row)
{
    BfSet *st = &s->bf_set;
    const float kk[4] = {k[MFX_K_BF_CENTER], k[MFX_K_BF_RATE], k[MFX_K_BF_DEPTH], k[MFX_K_BF_FEEDBACK]};
    const int mode = bf_mode(row);
    const uint32_t mp = step->m >> 1;
    int j;
    if (mfx_moved(st->key, kk, 4)) {
        st->rate = 0.05f * amp_exp2f(kk[1] * (7.64f / 127.f));      /* 0.05 .. 10 Hz */
        st->d0 = BF_DMIN * amp_exp2f(kk[0] * (BF_DOCT / 127.f));
        st->oct = kk[2] * (1.5f / 127.f);
        st->fb = kk[3] * (0.92f / 127.f);
    }
    step->u.bf.f = bf_filter();
    step->u.bf.fb = mode == MFX_BF_CHORUS ? 0.f : st->fb;
    step->u.bf.hiss = 0.002f * (1.f / 2147483648.f);
    step->u.bf.cross = (uint8_t)(mode == MFX_BF_FLANGER3);
    {                                                       /* POLARITY: the delay's gain, ramped */
        float p = row[MFX_R_POLARITY], g = BF_MIX * (p >= 1.f ? -1.f : 1.f - 2.f * p) * (1.f - 0.5f * step->u.bf.fb);
                                                            /* (FEEDBACK's resonance made up, in part) */
        step->u.bf.g0 = s->bf_d[0] == 0.f ? g : s->bf_g;
        step->u.bf.g1 = s->bf_g = g;
    }
    float tri = s->lfo < 0.5f ? 4.f * s->lfo - 1.f : 3.f - 4.f * s->lfo;
    const float im = mp ? 1.f / (float)mp : 0.f;
    for (j = 0; j < 2; j++) {
        float t = j && mode != MFX_BF_FLANGER1 ? -tri : tri;    /* B's sweep through the inverter */
        float d = st->d0 * amp_exp2f(-st->oct * t);
        d = d < 2.f ? 2.f : d > EN_DMAX ? EN_DMAX : d;
        float a = s->bf_d[j] == 0.f ? d : s->bf_d[j];
        step->u.bf.d0[j] = (int32_t)(a * HR_Q);
        step->u.bf.dd[j] = (int32_t)((d - a) * im * HR_Q);
        s->bf_d[j] = d;
    }
    step->lfo = s->lfo;
    s->lfo += st->rate * (float)step->m * (1.f / MFX_SR);
    if (s->lfo >= 1.f) s->lfo -= 1.f;
}

/* BBD FLANGER on one side (ch): its line fed from its own input (MODE III: the other side's, s->bf_in), the direct
 * its own */
static void bf_side(MfxState *s, int ch, float *y)
{
    MfxSide *sd = &s->side[ch];
    float *line = sd->line.f;
    float a1 = sd->flt[0], a2 = sd->flt[1], b1 = sd->flt[2], b2 = sd->flt[3], env = sd->env, fbv = sd->fbv;
    float p = sd->prev;
    uint32_t w = sd->w, rng = sd->rng, xi = 0;
    const float *xin = s->bf_in[ch ^ 1];
    for (uint32_t k = 0; k < s->nstep; k++) {
        const MfxStep *step = &s->plan[k];
        const uint32_t m = step->m, mp = m >> 1, cross = step->u.bf.cross;
        const HrSvf f = step->u.bf.f;
        const float fb = step->u.bf.fb, hiss = step->u.bf.hiss;
        float wet[MFX_SUB / 2];
        int32_t hd = step->u.bf.d0[ch];
        const int32_t dd = step->u.bf.dd[ch];
        for (uint32_t i = 0; i < mp; i++) {
            float x = cross ? xin[xi + i] : y[2u * i] + y[2u * i + 1u];   /* (the pair x 2) */
            float av = (x < 0.f ? -x : x) * 0.5f;              /* the hiss rides the level */
            env += (av > env ? 0.04f : 0.001f) * (av - env);
            float a = hr_lp(&f, 0.5f * x + fb * fbv, &a1, &a2);   /* the input and the FEEDBACK into the line */
            line[w] = br_sat(a * BR_SCALE) * BF_UN;
            hd += dd;
            HrTap t = hr_at(w, hd);
            rng = rng * 1664525u + 1013904223u;
            fbv = hr_lp(&f, hr_read(line, &t) + env * hiss * (float)(int32_t)rng, &b1, &b2);
            wet[i] = fbv;
            w = (w + 1u) & (HR_LEN - 1u);
        }
        xi += mp;
        {                                                      /* back up, the direct beside it */
            float g = step->u.bf.g0, dg = (step->u.bf.g1 - g) * (mp ? 1.f / (float)mp : 0.f);
            for (uint32_t i = 0; i < mp; i++) {
                g += dg;
                float v = g * wet[i];
                y[2u * i] = BF_MIX * y[2u * i] + 0.5f * (p + v);
                y[2u * i + 1u] = BF_MIX * y[2u * i + 1u] + v;
                p = v;
            }
            if (m & 1u)
                y[m - 1u] = BF_MIX * y[m - 1u] + p;
        }
        y += m;
    }
    sd->flt[0] = a1; sd->flt[1] = a2; sd->flt[2] = b1; sd->flt[3] = b2;
    sd->env = env; sd->fbv = fbv; sd->prev = p;
    sd->w = w;
    sd->rng = rng;
}

/* ---- PITCH SHIFT ---- */
/* The late-70s digital pitch changer: the input written into a short memory, a read head moving through it at the
 * pitch ratio's pace (its delay grows by 1 - ratio a sample). When it runs out of memory (up: it catches the writer;
 * down: it falls behind) it splices: it jumps most of the memory's length, to where the waveform there lines up with
 * the one it was reading (the best of the candidate jumps by cross-correlation over a short stretch: the "de-glitched"
 * units' trick; a free splice lands on any phase and leaves sustained notes up to a semitone or two out), and
 * crossfades to it over PS_XF frames. The memory at half rate (22.05 kHz, int16, 512: ~23 ms). The converters: the band
 * (~10 kHz, ~5 kHz at AGE 127), the bits (16, ~10 at AGE 127), a little hiss riding the level. FEEDBACK takes the
 * shifted sound back in, so each pass goes a step further (up or down: the spiral). MIX equal power. */
#define PS_W 352.f                               /* the span the head moves in, half-rate samples (~16 ms) */
#define PS_DM 4.f                                /* the shortest delay a head reads, half-rate samples */
#define PS_JMIN 176                              /* a splice's jump: PS_JMIN .. PS_W (ps_jump: coarse, then fine) */
#ifndef PS_L
#define PS_L 64                                  /* the stretch the candidates are compared over (half-rate samples,
                                                  * every PS_LSTEP-th: ~2.9 ms, a period of a low note; 0.7 ms was too
                                                  * short to find the phase and left some splices clicking) */
#endif
#define PS_LSTEP 2
#define PS_XF 48u                                /* the splice's crossfade, half-rate samples (~2.2 ms) */
#define PS_UN (1.f / (1.5f * 32767.f * BR_SCALE))   /* the line's level back to the bus's (br_sat's slope 1.5) */

/* PITCH SHIFT's step, for both sides (the plan) */
static void ps_plan(MfxState *s, MfxStep *step, const float *k, const float *row, float lr)
{
    PsSet *st = &s->ps_set;
    const float kk[6] = {k[MFX_K_PS_PITCH], k[MFX_K_PS_FINE], k[MFX_K_PS_FEEDBACK], k[MFX_K_PS_MIX], lr, row[MFX_R_AGE]};
    if (mfx_moved(st->key, kk, 6)) {
        const float semi = (kk[0] > 24.f ? 24.f : kk[0]) - 12.f, cents = (kk[1] - 64.f) * (50.f / 63.f);
        const float spread = kk[4] * (1.f / 127.f), mix = kk[3] * (1.f / 127.f), age = kk[5] * (1.f / 127.f);
        const float trim = 1.f / (1.f + 0.25f * br_sin(0.5f * mix));   /* (as CHORUS's MIX) */
        st->ratio[0] = amp_exp2f((semi + cents * 0.01f) * (1.f / 12.f));
        st->ratio[1] = amp_exp2f((semi + cents * (1.f - 2.f * spread) * 0.01f) * (1.f / 12.f));
        st->fb = kk[2] * (0.85f / 127.f);
        st->gw = trim * br_sin(0.25f * mix) / (1.f + st->fb);   /* (the feedback's build-up made up) */
        st->gd = trim * br_sin(0.25f * mix + 0.25f);
        st->f = hr_svf(10000.f * (1.f - 0.5f * age), 1.f / 0.8f);
        st->mask = (int32_t)~((1u << (uint32_t)(6.f * age + 0.5f)) - 1u);   /* (the low bits dropped) */
        st->hiss = (0.001f + 0.006f * age * age) * (1.f / 2147483648.f);
    }
    step->u.ps.f = st->f;
    step->u.ps.fb = st->fb;
    step->u.ps.gd = st->gd;
    step->u.ps.gw = st->gw;
    step->u.ps.hiss = st->hiss;
    step->u.ps.mask = st->mask;
    step->u.ps.slope[0] = 1.f - st->ratio[0];
    step->u.ps.slope[1] = 1.f - st->ratio[1];
    step->lfo = s->lfo;
}

/* the splice's jump from a head at delay d (whole half-rate samples), up (+1: further back) or down (-1): the one
 * whose last PS_L samples best match the head's own */
static int ps_jump(const int16_t *line, uint32_t w, int d, int dir)
{
    /* the jumps that land inside the memory (up: no further back than its end; down: no nearer than PS_DM) */
    int jmax = dir > 0 ? (int)(PS_DM + PS_W) - d : d - (int)PS_DM, jmin = PS_JMIN < jmax ? PS_JMIN : jmax / 2;
    /* coarse to fine (2026-11-02, a third of the work for the same choice): every 4th jump over every 4th sample,
     * then the 7 about the best over every 2nd */
    int best = jmin, j, i, lo, hi, pass;
    float bc = -3.4e38f;
    for (pass = 0; pass < 2; pass++) {
        const int step = pass ? 1 : 4, stride = pass ? PS_LSTEP : 2 * PS_LSTEP;
        lo = pass ? best - 3 : jmin;
        hi = pass ? best + 3 : jmax;
        lo = lo < jmin ? jmin : lo;
        hi = hi > jmax ? jmax : hi;
        bc = -3.4e38f;
        for (j = lo; j <= hi; j += step) {
            int d2 = d + dir * j;
            float c = 0.f;
            for (i = 0; i < PS_L; i += stride)
                c += (float)line[(w - (uint32_t)(d + i)) & (TF_LEN - 1u)] *
                     (float)line[(w - (uint32_t)(d2 + i)) & (TF_LEN - 1u)];
            if (c > bc) { bc = c; best = j; }
        }
    }
    return dir * best;
}

/* PITCH SHIFT on one side (ch) over the planned pass, y in place */
static void ps_side(MfxState *s, int ch, float *y)
{
    MfxSide *sd = &s->side[ch];
    int16_t *line = sd->line.i;
    float a1 = sd->flt[0], a2 = sd->flt[1], b1 = sd->flt[2], b2 = sd->flt[3], env = sd->env, fbv = sd->fbv;
    float d = sd->ps_d, d2 = sd->ps_d2;
    uint32_t w = sd->w, rng = sd->rng, xf = sd->ps_xf;
    if (d == 0.f) d = PS_DM + 0.5f * PS_W;              /* (a fresh start: the middle) */
    for (uint32_t k = 0; k < s->nstep; k++) {
        const MfxStep *step = &s->plan[k];
        const uint32_t m = step->m, mp = m >> 1;
        const HrSvf f = step->u.ps.f;
        const float fb = step->u.ps.fb, hiss = step->u.ps.hiss, slope = step->u.ps.slope[ch];
        const float edge = (float)PS_XF * (slope < 0.f ? -slope : slope) + 1.f;   /* the head's travel over a splice */
        const int32_t mask = step->u.ps.mask;
        float wet[MFX_SUB / 2];
        for (uint32_t i = 0; i < mp; i++) {
            float x = y[2u * i] + y[2u * i + 1u];                    /* (the pair x 2) */
            float av = (x < 0.f ? -x : x) * 0.5f;
            env += (av > env ? 0.04f : 0.001f) * (av - env);
            float a = hr_lp(&f, 0.5f * x + fb * fbv, &a1, &a2);      /* the input filter, the FEEDBACK */
            line[w] = (int16_t)((int32_t)(br_sat(a * BR_SCALE) * 32767.f) & mask);   /* the converter */
            d += slope;
            float v = tf_read(line, w, (int32_t)(d * HR_Q));
            if (xf) {                                                /* splicing: the new place fades in */
                d2 += slope;
                float g = (float)xf * (1.f / (float)PS_XF);
                v = v * g + (1.f - g) * tf_read(line, w, (int32_t)(d2 * HR_Q));
                if (--xf == 0u) d = d2;
            } else if (slope < 0.f ? d < PS_DM + edge : d > PS_DM + PS_W - edge) {   /* nearly out of memory
                                                                      * (the end it moves towards: up, the
                                                                      * writer; down, the memory's end): splice,
                                                                      * early enough that the old head stays in
                                                                      * it through the crossfade */
                int di = (int)d;
                d2 = d + (float)ps_jump(line, w, di, slope < 0.f ? 1 : -1);
                xf = PS_XF;
            }
            rng = rng * 1664525u + 1013904223u;
            fbv = hr_lp(&f, v * PS_UN + env * hiss * (float)(int32_t)rng, &b1, &b2);
            wet[i] = fbv;
            w = (w + 1u) & (TF_LEN - 1u);
        }
        hr_up(y, wet, m, &sd->prev, step->u.ps.gw, step->u.ps.gd);
        y += m;
    }
    sd->flt[0] = a1; sd->flt[1] = a2; sd->flt[2] = b1; sd->flt[3] = b2;
    sd->env = env; sd->fbv = fbv;
    sd->ps_d = d; sd->ps_d2 = d2; sd->ps_xf = xf;
    sd->w = w;
    sd->rng = rng;
}

float mfx_peek(int layer, int i)
{
    const MfxState *s = &mfx_st[layer & 1];
    uint8_t t = mfx_params[layer & 1].type;
    if (t != s->type && i < 3) return -1.f;          /* (not yet run as this type) */
    if (t == MFX_STONE_PHASER && i >= 0 && i < 2) return s->side[i].ctl;
    if (t == MFX_ENS_PHASER && i == 5) return s->ss_ctl;
    if ((t == MFX_ENSEMBLE || t == MFX_ENS_PHASER) && i >= 0 && i < 3) return s->en_d[i] * (1.f / EN_DMAX);
    if (t == MFX_CHORUS && i >= 0 && i < 2) return s->side[i].ch_d * (1.f / EN_DMAX);
    if (t == MFX_BBD_FLANGER && i >= 0 && i < 2) {   /* each side's delay, on CENTER's scale (octaves from its least) */
        union { float f; uint32_t u; } v = {s->bf_d[i] * (1.f / BF_DMIN)};
        float m = (float)(v.u & 0x7FFFFFu) * (1.f / 8388608.f);           /* log2: the exponent, the mantissa's curve */
        float l2 = (float)((int32_t)(v.u >> 23) - 127) + m * (1.3465f - 0.3465f * m);
        return s->bf_d[i] <= 0.f ? -1.f : l2 * (1.f / BF_DOCT);
    }
    if (t == MFX_PITCH_SHIFT && i >= 0 && i < 2)   /* the pitch, -12 .. +12 semitones as 0..1 */
        return (s->kn[MFX_K_PS_PITCH] > 24.f ? 24.f : s->kn[MFX_K_PS_PITCH]) * (1.f / 24.f);
    if (t == MFX_TAPE_FLANGER && i >= 0 && i < 5) {  /* the second deck, the reference, the thumb's sweep's ends */
        const uint8_t *k = mfx_params[layer & 1].knob;
        float d = i < 2 ? s->tf_dm[i] : i == 2 ? TF_REF
                : TF_REF + (-2.f + (float)k[MFX_K_TF_MANUAL] * (8.f / 127.f)
                            + (i == 3 ? -4.f : 4.f) * (float)k[MFX_K_TF_DEPTH] * (1.f / 127.f)) * TF_MS;
        d = d < 0.f ? 0.f : d > TF_DMAX ? TF_DMAX : d;
        return d * (1.f / TF_DMAX);
    }
    if (t == MFX_CHORUS && (i == 3 || i == 4)) {     /* where its delay swings between */
        const uint8_t *k = mfx_params[layer & 1].knob;
        float c = (1.f + (float)k[MFX_K_CH_DELAY] * (7.f / 127.f)) * (HR_SR * 0.001f);
        float d = c + (i == 3 ? -1.f : 1.f) * (float)k[MFX_K_CH_DEPTH] * (2.5f * HR_SR * 0.001f / 127.f);
        d = d < CH_DMIN ? CH_DMIN : d > EN_DMAX ? EN_DMAX : d;
        return d * (1.f / EN_DMAX);
    }
    if ((t == MFX_ENSEMBLE || t == MFX_ENS_PHASER) && (i == 3 || i == 4)) {   /* where its lines swing between: the
                                                                                 * clocks' fastest, slowest */
        const MfxParams *p = &mfx_params[layer & 1];
        float age = t == MFX_ENSEMBLE ? p->knob[MFX_K_AGE] : p->row[MFX_R_AGE];
        float dep = t == MFX_ENSEMBLE ? p->knob[MFX_K_DEPTH] : p->knob[MFX_K_ENS_DEPTH];
        float dc = EN_D0 * (1.f + age * (1.f / 127.f)), sw = EN_SWING * dep * (1.f / 127.f);
        float d = dc / (i == 3 ? 1.f + sw : 1.f - sw);
        return (d > EN_DMAX ? EN_DMAX : d) * (1.f / EN_DMAX);
    }
    return -1.f;
}

/* the knobs' glide a control step (MFX_SUB frames): ~23 ms, as the patch's own knobs (knob smoothing) */
#define MFX_KGLIDE 0.0155f

/* a new type (or back from OFF) starts from silence, at its knobs */
static void mfx_begin(MfxState *s, const MfxParams *p)
{
    int j;
    float lfo = s->lfo;
    memset(s, 0, sizeof *s);
    s->lfo = lfo;
    s->type = p->type;
    for (j = 0; j < 4; j++) s->kn[j] = (float)p->knob[j];
    s->kn[4] = (float)p->lr;
    s->kn[5] = (float)p->row[0];
    s->kn[6] = (float)p->row[1];
    s->ss_ctl = 0.5f;
    s->sp_set.key[0] = s->en_set.key[0] = s->ss_set.key[0] = s->ch_set.key[0] = s->tf_set.key[0] = -1.f;
    s->bf_set.key[0] = s->ps_set.key[0] = -1.f;
                                                /* (none worked out) */
    s->side[1].rng = 0x9E3779B9u;               /* (each side's hiss its own) */
}

/* the knobs a control step on (the last 1/500 of a step taken at once: the knob comes to rest, and the models'
 * settings are not worked out again) */
static void mfx_glide(MfxState *s, const MfxParams *p)
{
    const float want[7] = {p->knob[0], p->knob[1], p->knob[2], p->knob[3], p->lr, p->row[0], p->row[1]};
    for (int j = 0; j < 7; j++) {
        float e = want[j] - s->kn[j];
        s->kn[j] = e < 0.002f && e > -0.002f ? want[j] : s->kn[j] + e * MFX_KGLIDE;
    }
}

int mfx_layer_plan(int layer, float *l, float *r, uint32_t n)
{
    MfxState *s = &mfx_st[layer & 1];
    const MfxParams *p = &mfx_params[layer & 1];
    if (p->type != s->type)
        mfx_begin(s, p);
    s->nstep = 0;
    if (p->type == MFX_OFF || p->type >= MFX_COUNT)
        return 0;
    if (n > MFX_PASS_MAX) n = MFX_PASS_MAX;
    uint32_t xi = 0;                            /* (BBD FLANGER MODE III: where the pass's half-rate input is) */
    /* a control step at a time: each model's settings follow the glided knobs (their set-up is a few dozen
     * operations, against the step's thousands) */
    for (uint32_t f0 = 0; f0 < n; f0 += MFX_SUB) {
        uint32_t m = n - f0 < MFX_SUB ? n - f0 : MFX_SUB;
        mfx_glide(s, p);
        if (p->type == MFX_BULLROARER) {        /* (the prototype: whole, here) */
            bullroarer(s, s->kn, s->kn[4], l + f0, r + f0, m);
            continue;
        }
        MfxStep *step = &s->plan[s->nstep++];
        step->m = m;
        if (p->type == MFX_STONE_PHASER)
            sp_plan(s, step, s->kn, s->kn[4]);
        else if (p->type == MFX_ENS_PHASER)
            ep_plan(s, step, s->kn, s->kn + 5, s->kn[4]);
        else if (p->type == MFX_CHORUS)
            ch_plan(s, step, s->kn, s->kn + 5, s->kn[4]);
        else if (p->type == MFX_TAPE_FLANGER)
            tf_plan(s, step, s->kn, s->kn + 5, s->kn[4]);
        else if (p->type == MFX_BBD_FLANGER) {
            bf_plan(s, step, s->kn, s->kn + 5);
            if (step->u.bf.cross)                    /* MODE III: both sides' input, for the crossed lines */
                for (uint32_t i = 0; i < (m >> 1); i++) {
                    s->bf_in[0][xi + i] = l[f0 + 2u * i] + l[f0 + 2u * i + 1u];
                    s->bf_in[1][xi + i] = r[f0 + 2u * i] + r[f0 + 2u * i + 1u];
                }
            xi += m >> 1;
        } else if (p->type == MFX_PITCH_SHIFT)
            ps_plan(s, step, s->kn, s->kn + 5, s->kn[4]);
        else
            en_plan(s, step, s->kn, s->kn[4], EN_FAST, EN_GAIN);
    }
    return s->nstep != 0;
}

void mfx_layer_side(int layer, int side, float *y)
{
    MfxState *s = &mfx_st[layer & 1];
    if (!s->nstep)
        return;
    if (s->type == MFX_STONE_PHASER)
        sp_side(s, side & 1, y);
    else if (s->type == MFX_CHORUS)
        ch_side(s, side & 1, y);
    else if (s->type == MFX_TAPE_FLANGER)
        tf_side(s, side & 1, y);
    else if (s->type == MFX_BBD_FLANGER)
        bf_side(s, side & 1, y);
    else if (s->type == MFX_PITCH_SHIFT)
        ps_side(s, side & 1, y);
    else
        en_side(s, side & 1, y);
}

void mfx_layer_block(int layer, float *l, float *r, uint32_t n)
{
    while (n) {
        uint32_t k = n < MFX_PASS_MAX ? n : MFX_PASS_MAX;
        if (mfx_layer_plan(layer, l, r, k)) {
            mfx_layer_side(layer, 0, l);
            mfx_layer_side(layer, 1, r);
        }
        l += k; r += k; n -= k;
    }
}
