
#include "amplifier.h"
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

#include "drive_tables.h"

__attribute__((noinline)) void amp_drive_prepare(AmpDrive *d, int drive, int model) {   /* once a tick: one copy */
    if (drive < 0) drive = 0;
    if (drive > 127) drive = 127;
    if (model < 0 || model >= DRIVE_COUNT) model = DRIVE_SOFT;
    const int16_t *mk = drive_makeup_cdb[model] + (drive >> 3);     /* centi-dB every 8 steps, straight between */
    int cdb = mk[0] + (((mk[1] - mk[0]) * (drive & 7)) >> 3);
    d->g = amp_drive_gain(model, drive);
    d->m = amp_exp2f((float)cdb * (1.f / 602.06f));           /* (the table holds the Q15 scale) */
    d->a = amp_drive_tape_a(drive);
    d->model = (uint8_t)model;
}

void amp_drive_block(const AmpDrive *d, float *s, float *x, int n) {
    float st[2] = {s[0], s[1]};
#define DRIVE_LOOP(m)                                                                                                  \
    for (int i = 0; i < n; i++) x[i] = amp_drive_step(d, m, st, x[i]);                                                 \
    break;
    switch (d->model) {
    case DRIVE_HARD: DRIVE_LOOP(DRIVE_HARD)
    case DRIVE_FOLD: DRIVE_LOOP(DRIVE_FOLD)
    case DRIVE_TAPE: DRIVE_LOOP(DRIVE_TAPE)
    case DRIVE_TUBE: DRIVE_LOOP(DRIVE_TUBE)
    case DRIVE_OVERDRIVE: DRIVE_LOOP(DRIVE_OVERDRIVE)
    default: DRIVE_LOOP(DRIVE_SOFT)
    }
#undef DRIVE_LOOP
    s[0] = st[0];
    s[1] = st[1];
}

void amp_pan_gains(const StereoAmp *amp, int raw_pan, int32_t *left_q15, int32_t *right_q15) {
    int pan = raw_pan > 127 ? 127 : (raw_pan < 0 ? 0 : raw_pan);
    /* the constant-power pan law, scaled by the headroom gain */
    *left_q15 = (amp->master_gain_q15 * (int32_t)pan_trig_lut[pan]) >> 15;
    *right_q15 = (amp->master_gain_q15 * (int32_t)pan_trig_lut[127 - pan]) >> 15;
}
