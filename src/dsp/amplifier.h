
#ifndef AMPLIFIER_H
#define AMPLIFIER_H
#include <stdint.h>
#include "PresetStructures.h"   /* DRIVE_* */
typedef struct {
    int32_t left;   /* Q15 */
    int32_t right;  /* Q15 */
} StereoFrame;
typedef struct {
    int32_t master_gain_q15;
    int pan_knob;
} StereoAmp;
void amp_init(StereoAmp *amp);

/* Constant-power gain for an index 0..127: 0 = full, 127 = near silent. The split crossfades use it. */
int32_t amp_const_power_q15(int idx);

/* AMP drive (spec 4.6; reworked 2026-10-25): DRIVE 0..127 into the shaper, after the amp level (so the envelope and
 * velocity play it), model = DRIVE_* from PresetStructures.h; drive 0 is a bypass. The gain rises exponentially (each
 * model its own range) and a makeup (drive_tables.h) per model and drive holds the level of a -6 dBFS 110 Hz saw
 * where it was, so DRIVE adds grit, not volume. Every curve is smooth (no corner: less aliasing than a hard clamp):
 *   SOFT       cubic, flat at full scale (0 .. +30 dB)
 *   HARD       7th-order: straight to ~3/4, then a short knee (0 .. +30 dB)
 *   FOLD       folded back at full scale, the fold points rounded by the cubic (0 .. +16 dB)
 *   TAPE       the cubic with a bias (even harmonics), the highs rolled off as it drives harder (0 .. +24 dB)
 *   TUBE       asymmetric, one side soft and the other half as high: mostly the 2nd harmonic; the bias sags with
 *              the level (the DC blocker's state fed back), so it blooms on a loud note (0 .. +30 dB)
 *   OVERDRIVE  the Tube Screamer's plan: only the part above ~720 Hz clipped (diode-like x / (1 + |x|)), the lows
 *              added back clean, then a ~3.4 kHz tone roll-off (0 .. +40 dB)
 * The constants hold still over a control tick (amp_drive_prepare); a path's state is two floats. */
typedef struct {
    float g, m;          /* the gain in (Q15 units -> 1 = full scale) and the makeup out (-> Q15 units) */
    float a;             /* TAPE: the roll-off's one-pole coefficient */
    uint8_t model;
} AmpDrive;
void amp_drive_prepare(AmpDrive *d, int drive, int model);
/* n samples in place through d's model, s the path's state: one loop per model, the only copy in the firmware */
void amp_drive_block(const AmpDrive *d, float *s, float *x, int n);

#define DRIVE_DC_A 0.0028f       /* the DC blocker's one-pole, ~20 Hz */
#define DRIVE_TUBE_SAG 1.f       /* how far the blocked DC moves TUBE's bias */
#define DRIVE_OD_HP_A 0.0975f    /* OVERDRIVE: the split into the clipper, ~720 Hz */
#define DRIVE_OD_TONE_A 0.384f   /* OVERDRIVE: the tone roll-off, ~3.4 kHz */

/* 2^x for the control tick (no libm on the FM1): a cubic on the fraction, within 0.01 %, times 2^floor(x) */
static inline float amp_exp2f(float x) {
    int n = (int)x - (x < (float)(int)x);
    float f = x - (float)n;
    union { uint32_t u; float v; } e = {(uint32_t)(n + 127 > 0 ? n + 127 : 1) << 23};
    return e.v * (1.f + f * (0.6957f + f * (0.2259f + f * 0.0784f)));
}

/* each model's gain at DRIVE 127, dB, and the gain for a drive, Q15 units -> 1 = full scale */
static const uint8_t drive_top_db[DRIVE_COUNT] = {30, 30, 16, 24, 30, 40};   /* DRIVE_* order */
static inline float amp_drive_gain(int model, int drive) {
    return amp_exp2f((float)(drive_top_db[model] * drive) * (1.f / (127.f * 6.0206f))) * (1.f / 32768.f);
}

/* TAPE's roll-off: ~16 kHz at DRIVE 1 down to ~6 kHz at 127 (the one-pole's coefficient, straight between them) */
static inline float amp_drive_tape_a(int drive) { return 0.898f - (0.898f - 0.575f) * (float)drive * (1.f / 127.f); }

static inline float drive_soft_(float c) {                 /* the cubic, |c| <= 1 -> |y| <= 1 */
    c = c > 1.f ? 1.f : (c < -1.f ? -1.f : c);
    return c * (1.5f - 0.5f * c * c);
}

/* One sample, Q15 units in and out; s is the path's two floats of state. model is d->model, as a constant in
 * amp_drive_block's loops (the switch folds away, the state stays in registers). */
static inline __attribute__((always_inline)) float amp_drive_step(const AmpDrive *d, int model, float *s, float x) {
    float u = x * d->g, y;
    switch (model) {
    case DRIVE_HARD: {
        float c = u > 1.f ? 1.f : (u < -1.f ? -1.f : u), c2 = c * c;
        y = c * ((7.f / 6.f) - (1.f / 6.f) * c2 * c2 * c2);
        break;
    }
    case DRIVE_FOLD:                                         /* reflected into -1 .. 1 (<= 4 folds) */
        for (int i = 0; i < 4 && (u > 1.f || u < -1.f); i++)
            u = u > 1.f ? 2.f - u : -2.f - u;
        y = drive_soft_(u);
        break;
    case DRIVE_TAPE: {
        float t = drive_soft_(u + 0.1f) - 0.1f * 1.495f;     /* drive_soft_(0.1): no offset at rest */
        s[1] += DRIVE_DC_A * (t - s[1]);
        s[0] += d->a * (t - s[1] - s[0]);
        y = s[0];
        break;
    }
    case DRIVE_TUBE: {
        float v = u - DRIVE_TUBE_SAG * s[1];
        v = v > 2.f ? 2.f : (v < -1.f ? -1.f : v);
        float t = v + v * (0.125f * v - 0.375f * (v < 0.f ? -v : v));   /* v - v^2/4 above 0, v + v^2/2 below */
        s[1] += DRIVE_DC_A * (t - s[1]);
        y = t - s[1];
        break;
    }
    case DRIVE_OVERDRIVE: {                                  /* s[0] the split's low part, in Q15 units */
        s[0] += DRIVE_OD_HP_A * (x - s[0]);
        float h = (x - s[0]) * d->g;
        h = h / (1.f + (h < 0.f ? -h : h));
        s[1] += DRIVE_OD_TONE_A * (x * (1.f / 32768.f) + h - s[1]);
        y = s[1];
        break;
    }
    default:                                                 /* DRIVE_SOFT */
        y = drive_soft_(u);
        break;
    }
    return y * d->m;
}

/* Left and right gains for a pan position 0..127 (64 = centre): the constant-power law times the headroom gain.
 * Once per control tick; the voice multiplies its driven signal by them every sample. */
void amp_pan_gains(const StereoAmp *amp, int raw_pan, int32_t *left_q15, int32_t *right_q15);
#endif
