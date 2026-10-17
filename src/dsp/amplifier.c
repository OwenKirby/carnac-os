
#include "amplifier.h"
#include "dsp_clamp.h"
#include "PresetStructures.h"   /* DRIVE_* */
#include <stdint.h>

// CONSTANT-POWER PANNING LOOK-UP TABLE (Q15 FORMAT)
static const int16_t pan_trig_lut[128] = {
    32767, 32763, 32753, 32740, 32720, 32696, 32666, 32631, 32591, 32546, 32497, 32442, 32382, 32300, 32247, 32169,
    32086, 31998, 31906, 31808, 31706, 31599, 31488, 31371, 31250, 31124, 30994, 30859, 30720, 30576, 30427, 30274,
    30117, 29955, 29789, 29618, 29443, 29264, 29080, 28892, 28700, 28504, 28304, 28100, 27892, 27679, 27463, 27242,
    27018, 26789, 26557, 26321, 26081, 26081, 25591, 25340, 25085, 24827, 24565, 24300, 24031, 23484, 23205, 22923,
    22637, 22460, 22348, 22056, 21761, 21463, 21161, 20857, 20549, 20239, 19925, 19609, 19289, 18967, 18642, 18314,
    17983, 17650, 17314, 17314, 16632, 16287, 15939, 15589, 15636, 14881, 14524, 14164, 13803, 13439, 13073, 12705,
    12335, 12335, 11590, 11214, 10836, 10457, 10076, 9694, 9310, 8926, 8540, 8153, 7766, 7378, 7378, 6600,
    6209, 5818, 5427, 5035, 4643, 4251, 3858, 3466, 3073, 2680, 2288, 1895, 1502, 1109, 717, 324
};

void amp_init(StereoAmp *amp) {
    amp->master_gain_q15 = 22938; // ~0.7 safety scaling headroom limit (Q15)
    amp->pan_knob = 64;           // Balanced dead center
}

int32_t amp_const_power_q15(int idx) {
    if (idx < 0) idx = 0;
    if (idx > 127) idx = 127;
    return (int32_t)pan_trig_lut[idx];
}

static int32_t clamp_q15_range(int32_t v, int32_t lo, int32_t hi) { return v > hi ? hi : (v < lo ? lo : v); }

/* Cubic soft clip, Q15: x - x^3/3. Unity slope at zero, flat at |x| = 1 (output 2/3). Input must be in -32767..32767. */
static int32_t soft_cubic_q15(int32_t x) {
    int32_t x3 = (((x * x) >> 15) * x) >> 15;
    return x - div3_s32(x3);
}

int32_t amp_drive_q15(int32_t input_q15, int drive, int model) {
    if (drive <= 0) return input_q15;

    /* Pre-gain: 1x at drive 0, about 5x at 127. */
    int32_t pre_q8 = 256 + drive * 8;
    if (model == DRIVE_FOLD) {               /* wave-folder: the peaks the pre-gain pushes past full scale reflect
                                              * back inside it (up to ~5x: at most 3 folds). Not clamped first: the
                                              * clamp it had made it a plain clipper (fixed 2026-10-15). */
        int32_t y = (input_q15 * pre_q8) >> 8;
        for (int i = 0; i < 4 && (y > 32767 || y < -32768); i++) {
            if (y > 32767)  y = 2 * 32767 - y;
            if (y < -32768) y = -2 * 32768 - y;
        }
        return clamp_q15(y);
    }
    int32_t x = clamp_q15((input_q15 * pre_q8) >> 8);

    switch (model) {
    case DRIVE_HARD:                         /* hard overdrive, clipped at 3/4 scale */
        return clamp_q15_range(x, -24576, 24576);
    case DRIVE_TAPE: {                       /* soft saturation with a bias for even harmonics */
        const int32_t bias = 2048;           /* 1/16 full scale */
        return clamp_q15(soft_cubic_q15(clamp_q15(x + bias)) - soft_cubic_q15(bias));
    }
    default:                                 /* DRIVE_SOFT */
        return soft_cubic_q15(x);
    }
}

static inline float clampf_(float v, float lo, float hi) { return v > hi ? hi : (v < lo ? lo : v); }

/* soft_cubic_q15 in float, Q15 units: x - x^3 / (3 * 32768^2) */
static inline float soft_cubic_f(float x) { return x - x * x * x * (1.f / (3.f * 32768.f * 32768.f)); }

float amp_drive_f(float x, int drive, int model) {
    if (drive <= 0) return x;
    x *= 1.f + (float)drive * (1.f / 32.f);                                /* 256 + drive * 8, Q8 */
    if (model == DRIVE_FOLD) {                                             /* reflected, not clamped first */
        for (int i = 0; i < 4 && (x > 32767.f || x < -32768.f); i++) {
            if (x > 32767.f)  x = 2.f * 32767.f - x;
            if (x < -32768.f) x = -2.f * 32768.f - x;
        }
        return clampf_(x, -32768.f, 32767.f);
    }
    x = clampf_(x, -32768.f, 32767.f);
    switch (model) {
    case DRIVE_HARD:
        return clampf_(x, -24576.f, 24576.f);
    case DRIVE_TAPE:
        return clampf_(soft_cubic_f(clampf_(x + 2048.f, -32768.f, 32767.f)) - soft_cubic_f(2048.f), -32768.f, 32767.f);
    default:
        return soft_cubic_f(x);
    }
}

void amp_pan_gains(const StereoAmp *amp, int raw_pan, int32_t *left_q15, int32_t *right_q15) {
    int pan = raw_pan > 127 ? 127 : (raw_pan < 0 ? 0 : raw_pan);
    /* the constant-power pan law, scaled by the headroom gain */
    *left_q15 = (amp->master_gain_q15 * (int32_t)pan_trig_lut[pan]) >> 15;
    *right_q15 = (amp->master_gain_q15 * (int32_t)pan_trig_lut[127 - pan]) >> 15;
}
