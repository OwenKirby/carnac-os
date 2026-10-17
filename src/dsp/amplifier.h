
#ifndef AMPLIFIER_H
#define AMPLIFIER_H
#include <stdint.h>
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

/* AMP drive (spec 4.6): drive 0..127 ahead of the shaper, model = DRIVE_* from PresetStructures.h.
 * Drive 0 is a bypass. Mono Q15 in, Q15 out. */
int32_t amp_drive_q15(int32_t input_q15, int drive, int model);
/* The same in float, Q15 units (the voice's float amp, synth_voice.c finish_mono / finish_pair). */
float amp_drive_f(float x, int drive, int model);

/* Left and right gains for a pan position 0..127 (64 = centre): the constant-power law times the headroom gain.
 * Once per control tick; the voice multiplies its driven signal by them every sample. */
void amp_pan_gains(const StereoAmp *amp, int raw_pan, int32_t *left_q15, int32_t *right_q15);
#endif
