#include "oscmixer.h"
#include "dsp_cold.h"
#include "dsp_clamp.h"
#include "wavetables.h"
#include "PresetStructures.h"
#include <stdint.h>
#include "amplifier.h"                     /* amp_exp2f (the firmware has no libm) */


/* Pink and brown output gains, Q8, measured by simulating exactly this integer path: each colour sits about 4 dB
 * under white noise's RMS, so their higher crest factor only rarely reaches the clamp. */
#define NOISE_PINK_GAIN_Q8 411
#define NOISE_BROWN_GAIN_Q8 857
/* The resonance's band: the voice's white noise (the LCG's top 16 bits, RMS 18919) through two one-poles at the
 * control tick, held at NZ_RMS (~1.5 dB under brown, so it seldom reaches the clamp) whatever its width: 30 Hz from
 * COLOUR 56 to 80, then narrowing to 4 Hz at 127, evenly in octaves. */
#include "synth_voice.h"                  /* SYNTH_CTRL_SHIFT */
#define NZ_TICK_HZ (44100.f / (float)SYNTH_CTRL_PERIOD)   /* the tick rate */
#define NZ_RMS 9500.f
#define NZ_WIDE_HZ 30.f
#define NZ_OCTAVES 2.907f                         /* log2(30 / 4) */

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* 1 / sqrt(x), x > 0: the bit-level first guess and two Newton steps (~1e-5), so no sqrtf joins the firmware */
static float rsqrt(float x)
{
    union { float f; uint32_t i; } u = {x};
    u.i = 0x5F3759DFu - (u.i >> 1);
    float y = u.f;
    y *= 1.5f - 0.5f * x * y * y;
    return y * (1.5f - 0.5f * x * y * y);
}

void osc_mixer_init(OscillatorMixer *mixer) {
    mixer->osc_balance = 64;               /* 50/50 dead centre (spec: rendered 0) */
    mixer->xmod_amount = 0;
    mixer->xmod_type = OSCMIX_XMOD_FM;
    mixer->sub_level = 0;
    mixer->sub_shift = 1;                  /* -1 octave */
    mixer->sub_waveform = SUBWAVE_SINE;
    mixer->noise_level = 0;
    mixer->noise_colour = NOISE_WHITE;
    mixer->noise_harmonic = 0;
    mixer->noise_cluster = 0;
    mixer->noise_tint = 0;
    mixer->noise_inharm = 0;
}

void osc_mixer_voice_init(OscMixVoice *mv, unsigned voice) {
    for (int e = 0; e < 3; e++) {
        mv->nz_a[e] = mv->nz_b[e] = 0.f;
        mv->nz_cur[e] = mv->nz_d[e] = 0;
    }
    mv->cl_amt = mv->cl_for = 0u;
    mv->cl_g[0] = mv->cl_g[1] = mv->cl_g[2] = 0; mv->cl_norm = 32768;
    mv->cl_rkey = 0u; mv->cl_rk = 0.f;
    for (int j = 0; j < 3; j++) {           /* the bands start apart, as the whistle does */
        mv->cl_ph[j] = (voice + 1u) * 0x6F4A7C15u * (uint32_t)(2 * j + 3);
        mv->cl_st[j] = 0u;
    }
    mv->nz_seed = (voice + 1u) * 0x9E3779B9u ^ 0x5BD1E995u;
    mv->nz_col = 0;
    mv->wh_ratio = 0.f;                    /* (0: the first tick takes HARMONIC at once) */
    mv->wh_phase = (voice + 1u) * 0x6F4A7C15u;
    mv->wh_step = 0u;
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

SYNTH_FAST void osc_mixer_prepare(OscMixVoice *mv, const OscillatorMixer *mixer, uint32_t freq1_q16, uint32_t freq2_q16,
                       int balance_offset, int xmod_offset, int noise_offset, int colour_offset, int harm_offset_q8) {
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

    /* 4. Noise generator: level plus its mod, and the colour plus its mod (2026-11-06): white 0 -> pink 16 -> brown 32
     * (brown alone is pink -> brown at 256, no sine) -> brown ringing the sine 56 -> the voice's 30 Hz band ringing it
     * 80 -> that band narrowing to 4 Hz at 127. */
    mv->noise_gain_q15 = clamp_int(mixer->noise_level + noise_offset, 0, 127) * 258;
    /* the split (2026-11-21): COLOUR's plain noise (white -> pink -> brown), and WHISTLE ringing it, played as the old
     * morph's 32..127 (brown there now COLOUR's noise) */
    int tint = clamp_int(mixer->noise_tint, 0, 127);
    mv->tint_seg = (uint8_t)(tint >= 64);
    mv->tint_t_q8 = (uint16_t)(tint < 64 ? tint * 4 : ((tint - 64) * 256 + 31) / 63);
    mv->cl_inh = (uint8_t)clamp_int(mixer->noise_inharm, 0, 127);
    int wh = clamp_int(mixer->noise_colour + colour_offset, 0, 127);
    int c = wh > 0 ? NOISE_MORPH_OF_WHISTLE(wh) : (tint < 64 ? tint / 4 : (int)NOISE_PINK + (tint - 64) / 4);
    if (wh == 0) {                           /* no whistle: COLOUR alone */
        mv->noise_seg = mv->tint_seg;
        mv->noise_t_q8 = mv->tint_t_q8;
    } else if (c < (int)NOISE_BROWN) {
        mv->noise_seg = (uint8_t)(c >> 4);
        mv->noise_t_q8 = (uint16_t)((c & 15) << 4);
    } else if (c == (int)NOISE_BROWN) {
        mv->noise_seg = 1u;
        mv->noise_t_q8 = 256u;
    } else if (c < (int)NOISE_RING) {
        mv->noise_seg = 2u;
        mv->noise_t_q8 = (uint16_t)(((c - (int)NOISE_BROWN) << 8) / ((int)NOISE_RING - (int)NOISE_BROWN));
    } else if (c < (int)NOISE_BAND) {
        mv->noise_seg = 3u;
        mv->noise_t_q8 = (uint16_t)(((c - (int)NOISE_RING) << 8) / ((int)NOISE_BAND - (int)NOISE_RING));
    } else {
        mv->noise_seg = 4u;
        mv->noise_t_q8 = 0u;
    }
    mv->cl_amt = (uint8_t)(mv->noise_seg >= 4u && mv->noise_gain_q15 > 0 ? clamp_int(mixer->noise_cluster, 0, 127) : 0);
    if (mv->cl_amt && mv->cl_amt != mv->cl_for) {   /* the overtones fade in over the first third, the higher ones softer */
        float a = (float)mv->cl_amt * (1.f / 127.f), in = a < 0.35f ? a / 0.35f : 1.f, p = 1.f;
        static const float lv[3] = {0.8f, 0.62f, 0.48f};
        for (int j = 0; j < 3; j++) {
            mv->cl_g[j] = (int32_t)(lv[j] * in * 32768.f);
            p += lv[j] * lv[j] * in * in;
        }
        mv->cl_norm = (int32_t)(32768.f * rsqrt(p));
        mv->cl_for = mv->cl_amt;
    }
    if (mv->noise_seg < 2u || mv->noise_gain_q15 <= 0)
        return;

    /* 5. The resonance: HARMONIC to its ratio, gliding ~20 ms; then the voice's band, when COLOUR reaches it. */
    float f = osc_mixer_harm_ratio((float)mixer->noise_harmonic + (float)harm_offset_q8 * (1.f / 256.f));
    mv->wh_ratio = mv->wh_ratio <= 0.f ? f : mv->wh_ratio + (f - mv->wh_ratio) * (mv->long_tick ? 0.035676f : 0.018f);
    if (mv->noise_seg < 3u)
        return;
    int key = c + 1 + 128 * (mv->cl_amt && mv->cl_inh > 64 ? (mv->cl_inh - 64) >> 2 : 0);
    if (mv->nz_col != key) {                 /* the band's width for this WHISTLE (and INHARM), the gain holding its level */
        float hz = c < (int)NOISE_BAND ? NZ_WIDE_HZ :
                   NZ_WIDE_HZ * amp_exp2f(-NZ_OCTAVES * (float)(c - (int)NOISE_BAND) / (float)(127 - (int)NOISE_BAND));
        if (key > 128) {                     /* INHARM's metal half widens the bands toward ~60 Hz: noisy metal, never ring mod */
            float m = (float)((key - 1) >> 7) * (1.f / 16.f);
            if (hz < 60.f) hz += (60.f - hz) * m;
        }
        float g = 1.f - amp_exp2f(-9.0647203f * hz / NZ_TICK_HZ), p = 1.f - g, p2 = p * p, q = 1.f - p2;   /* (2 pi log2 e) */
        float power = g * g * g * g * (1.f + p2) / (q * q * q);   /* two one-poles: sum of (n+1)^2 p^2n, times g^4 */
        mv->nz_g = g;
        mv->nz_gain = NZ_RMS / 18919.f * rsqrt(power);
        mv->nz_col = (int16_t)key;
    }
    for (int e = 0; e < (mv->cl_amt ? 3 : 2); e++) {   /* each ear one step (+ CLUSTER's second band), a line to it over the tick */
        /* ear 1 rings a band only for BINAURAL (nz_ears 2): elsewhere its seed still steps, so the others draw the same */
        int used = e != 1 || mv->nz_ears > 1u;
        for (int s = 0; s <= (int)mv->long_tick; s++) {
            mv->nz_seed = mv->nz_seed * 1664525u + 1013904223u;
            if (!used) continue;
            float w = (float)(int16_t)(mv->nz_seed >> 16);
            mv->nz_a[e] += mv->nz_g * (w - mv->nz_a[e]);
            mv->nz_b[e] += mv->nz_g * (mv->nz_a[e] - mv->nz_b[e]);
        }
        if (!used) continue;
        float y = mv->nz_b[e] * mv->nz_gain;
        int32_t next = (int32_t)(y > 32767.f ? 32767.f : y < -32767.f ? -32767.f : y) * 16;
        mv->nz_d[e] = (next - mv->nz_cur[e]) >> (SYNTH_CTRL_SHIFT + mv->long_tick);   /* (over a tick) */
    }
}

SYNTH_FAST float osc_mixer_harm_ratio(float h) {
    h = h < 0.f ? 0.f : h > 127.f ? 127.f : h;
    float x = h * (7.f / 127.f), f;
    int i = (int)x;
    if (i > 6) i = 6;
    f = (x - (float)i - 0.17f) * (1.f / 0.66f);   /* a stop holds the outer 17 % of a span at each end */
    f = f < 0.f ? 0.f : f > 1.f ? 1.f : f;
    return 1.f + (float)i + f * f * (3.f - 2.f * f);
}

/* CLUSTER's detuning at full, cents a band (the overtones drift apart as CLUSTER rises past where they faded in) */
static const float CL_DET[3] = {13.f, -17.f, 21.f};
/* INHARM: the overtones stretched sharp to 64 (a stiff string, a bell), then on to JD-800 metal-noise ratios at 127 */
static const float CL_METAL[3] = {1.17f, 1.26f, 1.48f};
static const float CL_STR[3] = {0.035f, 0.099f, 0.182f};    /* 0.035 (j + 1)^1.5 */

SYNTH_FAST void osc_mixer_whistle_step(OscMixVoice *mv, uint32_t osc1_step) {
    float s = (float)osc1_step * mv->wh_ratio;
    mv->wh_step = s > 1879048192.f ? 1879048192u : (uint32_t)s;   /* (under ~19 kHz: past it the sine would alias) */
    if (mv->cl_amt) {                       /* the whistle's next overtones: (k + j) / k of it, k = HARMONIC's ratio */
        uint16_t key = (uint16_t)((mv->cl_amt | (mv->cl_inh << 7)) + 1);
        if (key != mv->cl_rkey || mv->wh_ratio != mv->cl_rk) {   /* the ratios only when HARMONIC / CLUSTER / INHARM move */
            float a = (float)mv->cl_amt * (1.f / 127.f), k = mv->wh_ratio > 0.5f ? mv->wh_ratio : 1.f;
            float d = a > 0.35f ? (a - 0.35f) * (1.f / 0.65f) : 0.f;   /* the detune starts once they are all in */
            float ih = (float)mv->cl_inh * (1.f / 127.f);
            float st = ih < 0.5f ? ih * 2.f : 1.f, mt = ih > 0.5f ? ih * 2.f - 1.f : 0.f;
            for (int j = 0; j < 3; j++) {
                float r = (k + (float)(j + 1)) / k;
                r *= 1.f + CL_STR[j] * st;
                r += (CL_METAL[j] - r) * mt;
                mv->cl_r[j] = r * amp_exp2f(CL_DET[j] * d * (1.f / 1200.f));
            }
            mv->cl_rkey = key;
            mv->cl_rk = mv->wh_ratio;
        }
        for (int j = 0; j < 3; j++) {
            float t = s * mv->cl_r[j];
            mv->cl_st[j] = t > 1879048192.f ? 1879048192u : (uint32_t)t;
        }
    }
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
        return osc_process_fm_tz(osc2, freq2_mod_q16);   /* through zero (2026-11-05; was a 10 Hz floor) */
    }
    }
}

void osc_mixer_osc2_block(const OscMixVoice *mv, const int32_t *w1, WavetableOsc *osc2, int32_t *w2, int n) {
    int fm = mv->xmod_type != OSCMIX_XMOD_AM && mv->xmod_type != OSCMIX_XMOD_RING &&
             mv->xmod_type != OSCMIX_XMOD_PM && mv->depth != 0;
    /* A bank or the VCO (oscillators.c): sample by sample only for FM and PM, which move its pitch or phase per sample;
     * plain, AM and RING take the block below, the same values (2026-11-02: the per-sample call cost DUAL OSC 7 points
     * on the VCO; 2026-11-06 the wavetable banks too) */
    if (!osc2->pcm.on && fm) {
        osc_render_fm_block(osc2, w1, mv->depth, mv->freq2_q16, w2, n);
        return;
    }
    if (!osc2->pcm.on && mv->xmod_type == OSCMIX_XMOD_PM) {
        if (osc2->xfade_q15 == 0) {
            osc_render_pm_block(osc2, w1, mv->depth, w2, n);
        } else {                                /* (a spread on OSC2: the playheads move with it) */
            for (int i = 0; i < n; i++)
                w2[i] = osc_mixer_osc2(mv, (int16_t)w1[i], osc2);
        }
        return;
    }
    if (fm) {                                   /* FM: osc_mixer_osc2's pitch, per sample, through zero */
        pcm_render_fm_block(&osc2->pcm, w1, mv->depth, mv->freq2_q16, w2, n);
        osc2->center_phase += osc2->center_step * (uint32_t)n;
        return;
    }
    if (osc2->pcm.on && mv->xmod_type == OSCMIX_XMOD_PM && mv->depth != 0) {   /* PM on a PCM wave (2026-11-05) */
        for (int i = 0; i < n; i++)
            w2[i] = pcm_render_one_pm(&osc2->pcm, (int32_t)((int64_t)(int16_t)w1[i] * mv->depth * 258));
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

/* The resonance's sine at a 32-bit cycle phase, x sqrt 2 (so it keeps its noise's power): a parabola with one
 * correction step, harmonics ~-56 dB, no table and no state (2026-11-05). Range +-46341. */
static inline int32_t whistle_sine(uint32_t ph) {
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x;              /* -pi..pi as -32768..32767 */
    int32_t y = (x * (32768 - ax)) >> 13, ay = y < 0 ? -y : y;       /* the parabola, +-32768 */
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;                      /* + 0.225 (y|y| - y) */
    return (y * 46341) >> 15;
}

/* COLOUR's plain noise at pass frame i (what WHISTLE rings; brown before the split) */
static inline int32_t tint_sample(const OscMixVoice *mv, const NoiseBuf *nb, int i) {
    int32_t t = mv->tint_t_q8, n;
    if (mv->tint_seg == 0) {
        n = nb->w[i];
        if (t != 0) n += ((nb->p[i] - n) * t) >> 8;
    } else if (t >= 256) {
        n = nb->b[i];
    } else {
        n = nb->p[i];
        if (t != 0) n += ((nb->b[i] - n) * t) >> 8;
    }
    return n;
}

/* the resonance at pass frame i: brown fading into brown x sine (2), brown x sine into the voice's band x sine (3),
 * the band x sine (4); nz the band's value now */
static inline int32_t whistle_sample(int seg, int32_t t, int32_t b, int32_t nz, uint32_t ph) {
    int32_t s = whistle_sine(ph);
    if (seg == 2)
        return (b * ((((256 - t) << 15) + t * s) >> 8)) >> 15;
    if (seg == 3)
        return ((b + (((nz - b) * t) >> 8)) * s) >> 15;
    return (nz * s) >> 15;
}

/* CLUSTER: the whistle and its next three overtones; 1 + 3 ring the voice's band noise, 2 + 4 its second one (BINAURAL's
 * right ear rings all four on its own), the sum held at one whistle's power */
static inline int32_t cluster_sample(const OscMixVoice *mv, int32_t nzA, int32_t nzB, uint32_t p0, uint32_t p1,
                                     uint32_t p2, uint32_t p3) {
    int32_t sa = whistle_sine(p0) + ((whistle_sine(p2) * mv->cl_g[1]) >> 15);
    int32_t sb = ((whistle_sine(p1) * mv->cl_g[0]) >> 15) + ((whistle_sine(p3) * mv->cl_g[2]) >> 15);
    return (int32_t)((((((int64_t)nzA * sa) >> 15) + (((int64_t)nzB * sb) >> 15)) * mv->cl_norm) >> 15);
}

/* one voice's noise sample at pass frame i: the colour morph between the pass's colours, white alone, white + pink,
 * pink alone, pink + brown or the resonance (the gain checked by the caller) */
static inline int32_t noise_sample(OscMixVoice *mv, int ear, const NoiseBuf *nb, int i) {
    int32_t n, t = mv->noise_t_q8;
    if (mv->cl_amt) {                       /* (a narrow band: seg 4) */
        mv->nz_cur[ear] += mv->nz_d[ear];
        int32_t b2 = mv->nz_cur[ear];
        if (ear == 0) {
            mv->nz_cur[2] += mv->nz_d[2];
            b2 = mv->nz_cur[2];
        }
        n = cluster_sample(mv, mv->nz_cur[ear] >> 4, b2 >> 4, mv->wh_phase, mv->cl_ph[0], mv->cl_ph[1], mv->cl_ph[2]);
    } else if (mv->noise_seg >= 2) {
        mv->nz_cur[ear] += mv->nz_d[ear];
        n = whistle_sample(mv->noise_seg, t, tint_sample(mv, nb, i), mv->nz_cur[ear] >> 4, mv->wh_phase);
    } else if (mv->noise_seg == 0) {
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

int32_t osc_mixer_noise(OscMixVoice *mv, int ear, int at) {
    if (mv->noise_gain_q15 <= 0)
        return 0;
    return noise_sample(mv, ear, &noise_buf[ear], at);
}

/* the block picks its two colours once: x alone, or x + (y - x) * t; (n * gain) >> 16 is noise_sample's value >> 1.
 * The resonance walks the whistle's phase from where it stands (the caller moves it on) and the ear's band. */
void osc_mixer_noise_block(OscMixVoice *mv, int ear, int at, int32_t *acc, int n) {
    int32_t g = mv->noise_gain_q15, t = mv->noise_t_q8;
    if (g <= 0)
        return;
    const NoiseBuf *nb = &noise_buf[ear];
    if (mv->cl_amt) {                       /* CLUSTER: the phases walk here, osc_mixer_whistle_advance moves them on */
        uint32_t p0 = mv->wh_phase, p1 = mv->cl_ph[0], p2 = mv->cl_ph[1], p3 = mv->cl_ph[2];
        uint32_t s0 = mv->wh_step, s1 = mv->cl_st[0], s2 = mv->cl_st[1], s3 = mv->cl_st[2];
        int32_t ca = mv->nz_cur[ear], da = mv->nz_d[ear], cb = mv->nz_cur[2], db = mv->nz_d[2];
        for (int i = 0; i < n; i++, p0 += s0, p1 += s1, p2 += s2, p3 += s3) {
            ca += da;
            if (ear == 0) cb += db;
            acc[i] += (cluster_sample(mv, ca >> 4, (ear == 0 ? cb : ca) >> 4, p0, p1, p2, p3) * g) >> 16;
        }
        mv->nz_cur[ear] = ca;
        if (ear == 0) mv->nz_cur[2] = cb;
        return;
    }
    if (mv->noise_seg >= 2) {
        uint32_t step = mv->wh_step, ph = mv->wh_phase;
        int32_t cur = mv->nz_cur[ear], d = mv->nz_d[ear];
        int seg = mv->noise_seg;
        for (int i = 0; i < n; i++, ph += step) {
            cur += d;
            acc[i] += (whistle_sample(seg, t, seg < 4 ? tint_sample(mv, nb, at + i) : 0, cur >> 4, ph) * g) >> 16;
        }
        mv->nz_cur[ear] = cur;
        return;
    }
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
            sub_q15 = osc1->vco ? osc_vco_at(osc1, *sub_phase, mv->sub_step)    /* the VCO: its own shape */
                                : wt_read_frames(osc1->frame_a, osc1->frame_b, osc1->morph_frac_q15, off, shift, mask,
                                                 *sub_phase);
        } else {
            const int16_t *table = mv->sub_waveform == SUBWAVE_SQUARE ? wavetable_sub_square : wavetable_sine;
            sub_q15 = wt_read_table16(table, off, shift, mask, *sub_phase);
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
    uint32_t phase = *sub_phase, step = mv->sub_step;
    int32_t gain = mv->sub_gain_q15;
    if (mv->sub_waveform == SUBWAVE_CURRENT && osc1->vco) {   /* the VCO's own shape (osc_mixer_sub) */
        for (int i = 0; i < n; i++) {
            phase += step;
            acc[i] += ((osc_vco_at(osc1, phase, step) * gain) >> 15) >> 1;
        }
        *sub_phase = phase;
        return;
    }
    if (mv->sub_waveform == SUBWAVE_CURRENT) {               /* OSC1's frames (8-bit) for this tick */
        const int8_t *fa = osc1->frame_a, *fb = osc1->frame_b;
        const int32_t mf = osc1->morph_frac_q15;
        for (int i = 0; i < n; i++) {
            phase += step;
            acc[i] += ((wt_read_frames(fa, fb, mf, off, shift, mask, phase) * gain) >> 15) >> 1;
        }
    } else {                                                  /* SINE / SQUARE: the int16 tables */
        const int16_t *t = mv->sub_waveform == SUBWAVE_SQUARE ? wavetable_sub_square : wavetable_sine;
        for (int i = 0; i < n; i++) {
            phase += step;
            acc[i] += ((wt_read_table16(t, off, shift, mask, phase) * gain) >> 15) >> 1;
        }
    }
    *sub_phase = phase;
}

void osc_mixer_extras_block(OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase, int at,
                            int32_t *acc, int n) {
    osc_mixer_sub_block(mv, osc1, sub_phase, acc, n);   /* (o + sub/2) + noise/2: o + (sub/2 + noise/2), in int */
    osc_mixer_noise_block(mv, 0, at, acc, n);
    osc_mixer_whistle_advance(mv, n);
}

int32_t osc_mixer_extras(OscMixVoice *mv, const WavetableOsc *osc1, uint32_t *sub_phase, int at) {
    /* the sub and the noise, each balanced in at half weight */
    int32_t s = (osc_mixer_sub(mv, osc1, sub_phase) >> 1) + (osc_mixer_noise(mv, 0, at) >> 1);
    osc_mixer_whistle_advance(mv, 1);
    return s;
}

int16_t osc_mixer_render(OscMixVoice *mv, WavetableOsc *osc1, WavetableOsc *osc2, uint32_t *sub_phase, int at) {
    /* OSC1 is always the modulator / first source. */
    int16_t wave1_q15 = osc_render(osc1);
    int16_t wave2_q15 = osc_mixer_osc2(mv, wave1_q15, osc2);
    int32_t mixed_q15 = (((int32_t)wave1_q15 * mv->osc1_gain_q15) >> 15) +
                        (((int32_t)wave2_q15 * mv->osc2_gain_q15) >> 15);
    return (int16_t)clamp_q15(mixed_q15 + osc_mixer_extras(mv, osc1, sub_phase, at));
}
