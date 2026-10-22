#ifndef OSCMIXER_H
#define OSCMIXER_H

#include <stdint.h>
#include "oscillators.h"

/* OSCMIXER page (mvave_fm1_master_specification.txt 4.1). The sound-generation stage between the two
 * wavetable oscillators and the filters: balance, cross-modulation, sub-oscillator and the noise generator.
 * Holds the static knob values only. Each voice resolves its own modulation sources and passes the
 * scaled offsets in, so the mixer never looks at a mod source itself.
 * The pieces are separate so each voice architecture (synth_voice.c) runs only the ones its path uses:
 * osc_mixer_osc2 (cross-modulated OSC2), osc_mixer_extras (sub + noise), osc_mixer_render (the DUAL OSC sum). */

enum { OSCMIX_XMOD_FM, OSCMIX_XMOD_AM, OSCMIX_XMOD_RING, OSCMIX_XMOD_PM, OSCMIX_XMOD_COUNT };

typedef struct {
    int osc_balance;   /* 0..127, 64 = 50/50 (knob 1: 0 = OSC1 only, 127 = OSC2 only) */
    int xmod_amount;   /* 0..127 cross-modulation depth (knob 2) */
    int xmod_type;     /* OSCMIX_XMOD_* */
    int sub_level;     /* 0..127 sub-oscillator level (knob 3) */
    int sub_shift;     /* octaves below OSC1: 1 (-1 oct) or 2 (-2 octaves) */
    int sub_waveform;  /* SUBWAVE_*: sine (bank 0 frame 0), square, or OSC1's current bank and morph frame */
    int noise_level;   /* 0..127 noise generator level (knob 4; knob 2 in SINGLE and SPREAD) */
    int noise_colour;  /* 0..127 colour morph: white 0, pink 64, brown 127 (NOISE_*) */
} OscillatorMixer;

/* One noise generator (per voice; BINAURAL has one per ear, so the ears decorrelate): b0..b2 the pink filter's
 * poles, b3 the brown integrator (separate, so a pink <-> brown morph runs both), and its own white-noise LCG.
 * The seed is per generator, not global: two cores render voices at once on the FM1 (docs/dual-core-plan.md), and a
 * shared generator would race and make the order the voices are rendered in change the sound. */
typedef struct {
    int32_t b0, b1, b2, b3;
    uint32_t seed;
} NoiseState;

void osc_mixer_init(OscillatorMixer *mixer);
void noise_state_init(NoiseState *ns);
void noise_seed_init(NoiseState *ns, uint32_t generator);

/* One voice's mixer settings for the current control tick, worked out by osc_mixer_prepare. */
typedef struct {
    int32_t osc1_gain_q15, osc2_gain_q15;   /* balance */
    int32_t depth;                          /* cross-mod depth 0..127 after its modulation */
    int32_t depth_q15;                      /* the same as a Q15 fraction, 32768 at 127 */
    uint32_t freq2_q16;                     /* OSC2's pitch, for the audio-rate FM path */
    uint32_t sub_step;                      /* sub-oscillator phase step */
    int32_t sub_gain_q15;
    int32_t noise_gain_q15;
    uint8_t xmod_type;                      /* OSCMIX_XMOD_* */
    uint8_t sub_waveform;                   /* SUBWAVE_* */
    uint8_t sub_level;                      /* the sub's mip level */
    uint8_t noise_seg;                      /* colour morph segment: 0 white -> pink, 1 pink -> brown */
    uint16_t noise_t_q8;                    /* .. and the position in it, 0..256 (256 only at brown) */
} OscMixVoice;

/* Control rate: one voice's mixer settings from the layer's knobs and the voice's pitch. The oscillators' own steps
 * are osc_prepare's job (the voice decides each one's spread).
 *   freq1_q16 / freq2_q16 : each oscillator's own pitch, Hz in 16.16 (the sub follows freq1)
 *   balance_offset, xmod_offset, noise_offset, colour_offset : modulation offsets in 0..127 knob units
 *     (noise_offset moves the noise level, colour_offset the colour morph) */
void osc_mixer_prepare(OscMixVoice *mv, const OscillatorMixer *mixer, uint32_t freq1_q16, uint32_t freq2_q16,
                       int balance_offset, int xmod_offset, int noise_offset, int colour_offset);

/* Audio rate: OSC2's sample, cross-modulated by OSC1's sample wave1_q15 (FM / AM / RING / PM by xmod_type). */
/* OSC2 for a block against OSC1's samples w1, into w2: osc_mixer_osc2 sample by sample for a bank; a PCM wave as a
 * block (AM and RING applied after, PM does nothing on it), or sample by sample at its FM pitch while FM moves it */
void osc_mixer_osc2_block(const OscMixVoice *mv, const int32_t *w1, WavetableOsc *osc2, int32_t *w2, int n);
int16_t osc_mixer_osc2(const OscMixVoice *mv, int16_t wave1_q15, WavetableOsc *osc2);

/* Audio rate: the sub-oscillator (following OSC1) and the noise generator at half weight each, Q15, unclamped.
 * sub_phase is the voice's 32-bit sub-oscillator cycle phase, advanced here. */
int32_t osc_mixer_extras(const OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase, NoiseState *ns);

/* Audio rate, at full weight: the sub-oscillator alone (advances sub_phase), and the noise generator alone.
 * BINAURAL uses them separately: one sub for both ears, a noise generator per ear. */
int32_t osc_mixer_sub(const OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase);
int32_t osc_mixer_noise(const OscMixVoice *mv, NoiseState *ns);
/* Blocks: n samples of the sub (osc_mixer_sub >> 1) / the noise (osc_mixer_noise >> 1) added into acc, the same values
 * in the same order; osc_mixer_extras_block is both (the sum osc_mixer_extras gives). */
void osc_mixer_sub_block(const OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase, int32_t *acc, int n);
void osc_mixer_noise_block(const OscMixVoice *mv, NoiseState *ns, int32_t *acc, int n);
void osc_mixer_extras_block(const OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase, NoiseState *ns,
                            int32_t *acc, int n);

/* Audio rate, DUAL OSC: OSC1 + cross-modulated OSC2 by balance, plus sub and noise, to one Q15 mono sample. */
int16_t osc_mixer_render(const OscMixVoice *mv, WavetableOsc *osc1, WavetableOsc *osc2, uint32_t *sub_phase,
                         NoiseState *ns);

#endif
