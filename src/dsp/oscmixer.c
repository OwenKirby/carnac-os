#include "oscmixer.h"
#include "dsp_clamp.h"
#include "wavetables.h"
#include "PresetStructures.h"
#include <stdint.h>


/* Pink and brown output gains, Q8, measured by simulating exactly this integer path: each colour sits about 4 dB
 * under white noise's RMS, so their higher crest factor only rarely reaches the clamp. */
#define NOISE_PINK_GAIN_Q8 411
#define NOISE_BROWN_GAIN_Q8 857

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void osc_mixer_init(OscillatorMixer *mixer) {
    mixer->osc_balance = 64;               /* 50/50 dead centre (spec: rendered 0) */
    mixer->xmod_amount = 0;
    mixer->xmod_type = OSCMIX_XMOD_FM;
    mixer->sub_level = 0;
    mixer->sub_shift = 1;                  /* -1 octave */
    mixer->sub_waveform = SUBWAVE_SINE;
    mixer->noise_level = 0;
    mixer->noise_colour = NOISE_WHITE;
}

NoiseBuf noise_buf[2];
static NoiseState noise_gen[2];

void noise_shared_init(void) {
    for (int e = 0; e < 2; e++) {
        noise_gen[e].b0 = noise_gen[e].b1 = noise_gen[e].b2 = noise_gen[e].b3 = 0;
        noise_gen[e].seed = 12345u + (uint32_t)e * 0x9E3779B9u;
        noise_shared_fill(e, NOISE_PASS_FRAMES, 1, 1);   /* every colour holds noise from the start */
    }
}

void osc_mixer_prepare(OscMixVoice *mv, const OscillatorMixer *mixer, uint32_t freq1_q16, uint32_t freq2_q16,
                       int balance_offset, int xmod_offset, int noise_offset, int colour_offset) {
    /* 1. Balance: 0 = OSC1 only, 127 = OSC2 only. Gains in Q15. */
    int balance = clamp_int(mixer->osc_balance + balance_offset, 0, 127);
    mv->osc2_gain_q15 = balance * 258;
    mv->osc1_gain_q15 = 32768 - mv->osc2_gain_q15;

    /* 2. Cross-modulation depth. depth 0 leaves OSC2 untouched for every type. */
    mv->xmod_type = (uint8_t)mixer->xmod_type;
    mv->depth = clamp_int(mixer->xmod_amount + xmod_offset, 0, 127);
    mv->depth_q15 = (mv->depth << 15) / 127;                      /* depth / 127: 32768 at full depth */
    mv->freq2_q16 = freq2_q16;

    /* 3. Sub-oscillator: one or two octaves below OSC1 (SUBOCT_*). Its rate is always at most half of OSC1's,
     * so every waveform reads its table at half speed or slower. */
    mv->sub_waveform = (uint8_t)mixer->sub_waveform;
    mv->sub_gain_q15 = mixer->sub_level * 258;
    if (mv->sub_gain_q15 > 0) {
        uint32_t sub_q16 = freq1_q16 >> mixer->sub_shift;
        /* Same 32-bit cycle phase step as the main oscillators (oscillators.c phase_step). */
        mv->sub_step = ((sub_q16 >> 8) * 380u) + (((sub_q16 >> 8) * 3u) >> 3);
        mv->sub_level = wt_level_for_freq(sub_q16);
    }

    /* 4. Noise generator: level plus its mod, and the colour morph plus its mod: 0..63 white -> pink, 64..127
     * pink -> brown, in steps of 4/256 (127 lands on 256: brown alone). */
    mv->noise_gain_q15 = clamp_int(mixer->noise_level + noise_offset, 0, 127) * 258;
    int colour = clamp_int(mixer->noise_colour + colour_offset, 0, 127);
    mv->noise_seg = (uint8_t)(colour >= (int)NOISE_PINK);
    mv->noise_t_q8 = colour >= (int)NOISE_BROWN ? 256u : (uint16_t)((colour & 63) << 2);
}

int16_t osc_mixer_osc2(const OscMixVoice *mv, int16_t wave1_q15, WavetableOsc *osc2) {
    switch (mv->xmod_type) {
    case OSCMIX_XMOD_AM: {
        /* Carrier scaled by the modulator's unipolar value; depth 127 = full AM. */
        int16_t carrier_q15 = osc_render(osc2);
        int32_t mod_uni_q15 = ((int32_t)wave1_q15 + 32768) >> 1;     /* 0..32767 */
        int32_t gain_q15 = 32767 - (((32767 - mod_uni_q15) * mv->depth_q15) >> 15);
        return (int16_t)(((int32_t)carrier_q15 * gain_q15) >> 15);
    }
    case OSCMIX_XMOD_RING: {
        /* Blend between the clean carrier (depth 0) and the full ring product (depth 127). */
        int16_t carrier_q15 = osc_render(osc2);
        int32_t ring_q15 = ((int32_t)carrier_q15 * wave1_q15) >> 15;
        return (int16_t)(carrier_q15 + (((ring_q15 - carrier_q15) * mv->depth_q15) >> 15));
    }
    case OSCMIX_XMOD_PM: {
        /* Phase offset for this sample only: up to a quarter cycle at full depth (2^30 of 2^32).
         * Added before the render and removed after, so the accumulator keeps its own pitch. */
        uint32_t off = (uint32_t)((int64_t)wave1_q15 * mv->depth * 258);
        int16_t out;
        for (int i = 0; i < 4; i++) osc2->phases_q16[i] += off;
        osc2->center_phase += off;
        out = osc_render(osc2);
        for (int i = 0; i < 4; i++) osc2->phases_q16[i] -= off;
        osc2->center_phase -= off;
        return out;
    }
    default: {
        if (mv->depth == 0)
            return osc_render(osc2);
        /* FM: OSC1 moves OSC2's frequency every sample, 1.5 Hz per unit of (wave1 * depth) >> 12, in 16.16.
         * The pitch changes at audio rate, so OSC2 works out its own step and mip level per sample. No spread:
         * only SPREAD runs detuned playheads, and it has no OSC2. */
        int32_t fm_hz = ((int32_t)wave1_q15 * mv->depth) >> 12;
        int32_t freq2_mod_q16 = (int32_t)mv->freq2_q16 + fm_hz * 98304;
        if (freq2_mod_q16 < 655360) freq2_mod_q16 = 655360;          /* 10 Hz floor, no fold-back */
        return osc_process_skulpt_spread_fixed(osc2, (uint32_t)freq2_mod_q16, 0);
    }
    }
}

void osc_mixer_osc2_block(const OscMixVoice *mv, const int32_t *w1, WavetableOsc *osc2, int32_t *w2, int n) {
    if (!osc2->pcm.on) {
        for (int i = 0; i < n; i++)
            w2[i] = osc_mixer_osc2(mv, (int16_t)w1[i], osc2);
        return;
    }
    if (mv->xmod_type != OSCMIX_XMOD_AM && mv->xmod_type != OSCMIX_XMOD_RING && mv->xmod_type != OSCMIX_XMOD_PM &&
        mv->depth != 0) {                       /* FM: osc_mixer_osc2's pitch, per sample */
        for (int i = 0; i < n; i++) {
            int32_t fm_hz = ((int32_t)(int16_t)w1[i] * mv->depth) >> 12;
            int32_t f = (int32_t)mv->freq2_q16 + fm_hz * 98304;
            w2[i] = pcm_render_one_fm(&osc2->pcm, (uint32_t)(f < 655360 ? 655360 : f));
        }
        osc2->center_phase += osc2->center_step * (uint32_t)n;
        return;
    }
    osc_render_block(osc2, w2, n);
    if (mv->xmod_type == OSCMIX_XMOD_AM) {
        for (int i = 0; i < n; i++) {
            int32_t mod_uni_q15 = ((int32_t)(int16_t)w1[i] + 32768) >> 1;
            int32_t gain_q15 = 32767 - (((32767 - mod_uni_q15) * mv->depth_q15) >> 15);
            w2[i] = (int16_t)((w2[i] * gain_q15) >> 15);
        }
    } else if (mv->xmod_type == OSCMIX_XMOD_RING) {
        for (int i = 0; i < n; i++) {
            int32_t ring_q15 = (w2[i] * (int32_t)(int16_t)w1[i]) >> 15;
            w2[i] = (int16_t)(w2[i] + (((ring_q15 - w2[i]) * mv->depth_q15) >> 15));
        }
    }
}

/* Kellet's economy pink filter: three one-poles summed with a little of the white. The white is taken at 1/8 scale
 * (+-4096) so every state times its Q15 coefficient stays inside 32 bits. */
static int32_t noise_pink(NoiseState *ns, int32_t w) {
    int32_t w8 = w >> 3;
    ns->b0 = ((ns->b0 * 32690) >> 15) + ((w8 * 3245) >> 15);
    ns->b1 = ((ns->b1 * 31556) >> 15) + ((w8 * 9716) >> 15);
    ns->b2 = ((ns->b2 * 18678) >> 15) + ((w8 * 34494) >> 15);
    return clamp_q15(((ns->b0 + ns->b1 + ns->b2 + ((w8 * 6056) >> 15)) * NOISE_PINK_GAIN_Q8) >> 8);
}

/* Leaky integrator: -6 dB per octave above about 110 Hz, the leak keeps it from wandering off centre. */
static int32_t noise_brown(NoiseState *ns, int32_t w) {
    ns->b3 += ((w >> 3) >> 2) - (ns->b3 >> 6);
    return clamp_q15((ns->b3 * NOISE_BROWN_GAIN_Q8) >> 8);
}

/* The generator runs the filters asked for this pass; a filter that joins starts from where it stopped. */
void noise_shared_fill(int ear, uint32_t frames, int pink, int brown) {
    NoiseState g = noise_gen[ear];             /* in registers over the pass */
    NoiseBuf *nb = &noise_buf[ear];
    for (uint32_t i = 0; i < frames; i++) {
        g.seed = g.seed * 1103515245u + 12345u;
        int32_t w = (int16_t)(g.seed >> 16);
        nb->w[i] = (int16_t)w;
        if (pink) nb->p[i] = (int16_t)noise_pink(&g, w);
        if (brown) nb->b[i] = (int16_t)noise_brown(&g, w);
    }
    noise_gen[ear] = g;
}

/* one voice's noise sample at pass frame i: the colour morph between the pass's colours, white alone, white + pink,
 * pink alone, pink + brown or brown alone (the gain checked by the caller) */
static inline int32_t noise_sample(const OscMixVoice *mv, const NoiseBuf *nb, int i) {
    int32_t n, t = mv->noise_t_q8;
    if (mv->noise_seg == 0) {
        n = nb->w[i];
        if (t != 0)
            n += ((nb->p[i] - n) * t) >> 8;
    } else if (t >= 256) {
        n = nb->b[i];
    } else {
        n = nb->p[i];
        if (t != 0)
            n += ((nb->b[i] - n) * t) >> 8;
    }
    return (n * mv->noise_gain_q15) >> 15;
}

int32_t osc_mixer_noise(const OscMixVoice *mv, int ear, int at) {
    if (mv->noise_gain_q15 <= 0)
        return 0;
    return noise_sample(mv, &noise_buf[ear], at);
}

/* the block picks its two colours once: x alone, or x + (y - x) * t; (n * gain) >> 16 is noise_sample's value >> 1 */
void osc_mixer_noise_block(const OscMixVoice *mv, int ear, int at, int32_t *acc, int n) {
    int32_t g = mv->noise_gain_q15, t = mv->noise_t_q8;
    if (g <= 0)
        return;
    const NoiseBuf *nb = &noise_buf[ear];
    const int16_t *x, *y;
    if (mv->noise_seg == 0) {
        x = nb->w + at; y = nb->p + at;
    } else if (t >= 256) {
        x = y = nb->b + at; t = 0;
    } else {
        x = nb->p + at; y = nb->b + at;
    }
    if (t == 0) {
        for (int i = 0; i < n; i++)
            acc[i] += (x[i] * g) >> 16;
    } else {
        for (int i = 0; i < n; i++)
            acc[i] += ((x[i] + (((y[i] - x[i]) * t) >> 8)) * g) >> 16;
    }
}

int32_t osc_mixer_sub(const OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase) {
    /* Every sub waveform is a band-limited table read at the mip level of the sub's own pitch, with linear
     * interpolation, as the main oscillators read theirs. */
    int32_t sub_q15 = 0;
    if (mv->sub_gain_q15 > 0) {
        *sub_phase += mv->sub_step;
        uint8_t level = mv->sub_level;
        uint32_t off = wavetable_level_start[level], shift = WT_LEVEL_SHIFT[level], mask = WT_LEVEL_SAMPLES[level] - 1u;
        if (mv->sub_waveform == SUBWAVE_CURRENT) {          /* follows OSC1's scan: its frames for this tick */
            sub_q15 = wt_read_frames(osc1->frame_a, osc1->frame_b, osc1->morph_frac_q15, off, shift, mask, *sub_phase);
        } else {
            const int16_t *table = mv->sub_waveform == SUBWAVE_SQUARE ? wavetable_sub_square : wavetable_sine;
            sub_q15 = wt_read_frames(table, table, 0, off, shift, mask, *sub_phase);
        }
        sub_q15 = (sub_q15 * mv->sub_gain_q15) >> 15;
    }
    return sub_q15;
}

void osc_mixer_sub_block(const OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase, int32_t *acc, int n) {
    if (mv->sub_gain_q15 <= 0)
        return;                                /* osc_mixer_sub: 0, the phase held */
    uint8_t level = mv->sub_level;
    uint32_t off = wavetable_level_start[level], shift = WT_LEVEL_SHIFT[level], mask = WT_LEVEL_SAMPLES[level] - 1u;
    const int16_t *fa, *fb;
    int32_t mf;
    if (mv->sub_waveform == SUBWAVE_CURRENT) {
        fa = osc1->frame_a; fb = osc1->frame_b; mf = osc1->morph_frac_q15;
    } else {
        fa = fb = mv->sub_waveform == SUBWAVE_SQUARE ? wavetable_sub_square : wavetable_sine;
        mf = 0;
    }
    uint32_t phase = *sub_phase, step = mv->sub_step;
    int32_t gain = mv->sub_gain_q15;
    for (int i = 0; i < n; i++) {
        phase += step;
        int32_t s = wt_read_frames(fa, fb, mf, off, shift, mask, phase);
        acc[i] += ((s * gain) >> 15) >> 1;
    }
    *sub_phase = phase;
}

void osc_mixer_extras_block(const OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase, int at,
                            int32_t *acc, int n) {
    osc_mixer_sub_block(mv, osc1, sub_phase, acc, n);   /* (o + sub/2) + noise/2: o + (sub/2 + noise/2), in int */
    osc_mixer_noise_block(mv, 0, at, acc, n);
}

int32_t osc_mixer_extras(const OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase, int at) {
    /* the sub and the noise, each balanced in at half weight */
    return (osc_mixer_sub(mv, osc1, sub_phase) >> 1) + (osc_mixer_noise(mv, 0, at) >> 1);
}

int16_t osc_mixer_render(const OscMixVoice *mv, WavetableOsc *osc1, WavetableOsc *osc2, uint32_t *sub_phase,
                         int at) {
    /* OSC1 is always the modulator / first source. */
    int16_t wave1_q15 = osc_render(osc1);
    int16_t wave2_q15 = osc_mixer_osc2(mv, wave1_q15, osc2);
    int32_t mixed_q15 = (((int32_t)wave1_q15 * mv->osc1_gain_q15) >> 15) +
                        (((int32_t)wave2_q15 * mv->osc2_gain_q15) >> 15);
    return (int16_t)clamp_q15(mixed_q15 + osc_mixer_extras(mv, osc1, sub_phase, at));
}
