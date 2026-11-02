/* MFX listening renders (2026-11-01): the same scenes dry and through each MFX setting, as WAVs to compare by ear,
 * and the host cost of the effect (both layers' worth of its work: one layer, stereo).
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/pcm_images_host.c src/host/foundations_image_host.c src/host/render_mfx_demo.c -lm -o render_mfx_demo
 *   render_mfx_demo OUTDIR          writes OUTDIR/<scene>.wav, prints peak / RMS and the cost
 *
 * Scenes: a slow SPREAD pad playing a minor progression (dry, then STONE PHASER slow / with FEEDBACK / full FEEDBACK,
 * then MANUAL swept by hand as the pedal would be), a plucked bass riff at a fast RATE, white noise (the sweep laid
 * bare), and the pad with the sides' sweeps apart (L/R PHASE 64, 127). */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/mfx.h"
#include "dsp/pcm_store.h"
#include "dsp/wavetables.h"

#define RATE 44100u
#define BLOCK 32u

static int32_t buf[BLOCK * 2u];
static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

/* knob for a STONE PHASER RATE in Hz (mfx.c: 0.08 * 2^(k * 8.29 / 127)) */
static uint8_t rate_knob(double hz) { double k = log2(hz / 0.08) * 127.0 / 8.29; return (uint8_t)(k < 0 ? 0 : k > 127 ? 127 : k + 0.5); }

/* knob for a BULLROARER WHIRL in Hz (mfx.c: 0.1 * 2^(k * 6.32 / 127)) */
static uint8_t whirl_knob(double hz) { double k = log2(hz / 0.1) * 127.0 / 6.32; return (uint8_t)(k < 0 ? 0 : k > 127 ? 127 : k + 0.5); }

/* the pad: SPREAD, slow attack and release, half-open filter, some reverb */
static void pad(void)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = VARCH_SPREAD;
    e->oscmix.spread = 70;
    e->env[0].attack = 70; e->env[0].decay = 80; e->env[0].sustain = 110; e->env[0].release = 75;
    e->filter[0].cutoff = 80; e->filter[0].resonance = 20;
    e->amp.reverb_send = 45; e->amp.delay_send = 0;
    synth_restart();
}

/* the bass: SPREAD 0, short decay, filter lower */
static void bass(void)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = VARCH_SPREAD;
    e->env[0].attack = 0; e->env[0].decay = 55; e->env[0].sustain = 40; e->env[0].release = 30;
    e->filter[0].cutoff = 70; e->filter[0].resonance = 40;
    e->amp.reverb_send = 10; e->amp.delay_send = 0;
    synth_restart();
}

static FILE *wav_open(const char *dir, const char *name, uint32_t len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return NULL; }
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + len * 4u); fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16u); put_u16(f, 1); put_u16(f, 2); put_u32(f, RATE); put_u32(f, RATE * 4u); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, len * 4u);
    return f;
}

/* Cm9 - Abmaj7 - Fm9 - G7sus, four seconds each */
static const uint8_t prog[4][5] = {{48, 55, 63, 70, 74}, {44, 51, 60, 67, 72}, {41, 48, 56, 63, 67}, {43, 50, 60, 65, 69}};
static const uint8_t riff[8] = {36, 36, 48, 36, 39, 36, 46, 43};

enum { SC_PAD, SC_BASS, SC_NOISE };

/* one scene: kind, its MFX settings, and MANUAL swept (0: as set) */
static void render(const char *dir, const char *name, int kind, MfxParams m, int sweep)
{
    uint32_t len = kind == SC_PAD ? RATE * 19u : kind == SC_BASS ? RATE * 8u : RATE * 12u, frame = 0;
    double sum = 0.0, peak = 0.0;
    if (kind == SC_BASS) bass(); else pad();
    if (kind == SC_NOISE) {                      /* white noise at full level, no filter, no reverb */
        engine_state[0].oscmix.noise_level = 127;
        engine_state[0].osc[0].pitch_mod_amt = 0;
        engine_state[0].filter[0].cutoff = 127;
        engine_state[0].amp.reverb_send = 0;
        engine_state[0].env[0].attack = 0;
        synth_restart();
    }
    mfx_params[0] = m;
    FILE *f = wav_open(dir, name, len);
    if (!f) return;
    uint32_t seed = 1;
    while (frame < len) {
        if (kind == SC_PAD && frame % (4u * RATE) < BLOCK && frame / (4u * RATE) < 4u) {
            uint32_t c = frame / (4u * RATE);
            if (c) for (int i = 0; i < 5; i++) synth_midi_3(0x80, prog[c - 1u][i], 0);
            for (int i = 0; i < 5; i++) synth_midi_3(0x90, prog[c][i], 90);
        }
        if (kind == SC_PAD && frame >= 16u * RATE && frame < 16u * RATE + BLOCK)
            for (int i = 0; i < 5; i++) synth_midi_3(0x80, prog[3][i], 0);
        if (kind == SC_BASS && frame % (RATE / 4u) < BLOCK && frame < 7u * RATE) {
            uint32_t k = frame / (RATE / 4u);
            if (k) synth_midi_3(0x80, riff[(k - 1u) & 7u], 0);
            synth_midi_3(0x90, riff[k & 7u], (uint8_t)(k & 1u ? 80 : 115));
        }
        if (kind == SC_NOISE && frame == 0) synth_midi_3(0x90, 60, 100);
        if (kind == SC_NOISE && frame >= 11u * RATE && frame < 11u * RATE + BLOCK) synth_midi_3(0x80, 60, 0);
        if (sweep) {                             /* the pedal: heel to toe and back over 8 s, twice */
            double t = fmod((double)frame / RATE, 8.0) / 8.0;
            mfx_params[0].knob[MFX_K_MANUAL] = (uint8_t)(127.0 * (t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t) + 0.5);
        }
        (void)seed;
        synth_render_q15(buf, BLOCK);
        for (uint32_t k = 0; k < BLOCK * 2u; k++) {
            int32_t v = buf[k];
            double a = fabs((double)v) / 32768.0;
            sum += a * a;
            if (a > peak) peak = a;
            put_u16(f, (uint16_t)(int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v));
        }
        frame += BLOCK;
    }
    fclose(f);
    mfx_params[0].type = MFX_OFF;
    printf("  %-34s peak %6.1f dBFS  rms %6.1f dBFS\n", name, 20.0 * log10(peak + 1e-9),
           20.0 * log10(sqrt(sum / (2.0 * len)) + 1e-9));
}

/* host ns a sample of the MFX alone: one layer, stereo, over 10 s of noise */
static double cost(MfxParams m)
{
    static float l[128], r[128];
    double best = 1e30;
    uint32_t seed = 7;
    for (int rep = 0; rep < 5; rep++) {
        mfx_init();
        mfx_params[0] = m;
        clock_t t0 = clock();
        for (uint32_t fr = 0; fr < RATE * 10u; fr += 128u) {
            for (int i = 0; i < 128; i++) { seed = seed * 1664525u + 1013904223u; l[i] = r[i] = (float)((int32_t)seed >> 17); }
            mfx_layer_block(0, l, r, 128);
        }
        double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / (RATE * 10.0);
        if (ns < best) best = ns;
    }
    mfx_params[0].type = MFX_OFF;
    return best;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: render_mfx_demo OUTDIR\n"); return 2; }
    synth_init();
    {
        extern const uint8_t pcm_image_host0[], pcm_image_host1[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;
        extern const uint8_t foundations_image_host[];
        extern const uint32_t foundations_image_host_len;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
        wavetable_bank_attach(foundations_image_host, foundations_image_host_len);
    }
    const uint8_t slow = rate_knob(0.12), mid = rate_knob(0.5), fast = rate_knob(4.0);
    printf("STONE PHASER (RATE knobs: 0.12 Hz = %u, 0.5 Hz = %u, 4 Hz = %u):\n", slow, mid, fast);
    render(argv[1], "01_pad_dry", SC_PAD, (MfxParams){MFX_OFF, {0}}, 0);
    render(argv[1], "02_pad_stone_slow", SC_PAD, (MfxParams){MFX_STONE_PHASER, {64, slow, 0, 127}, 0}, 0);
    render(argv[1], "03_pad_stone_slow_fb80", SC_PAD, (MfxParams){MFX_STONE_PHASER, {64, slow, 80, 127}, 0}, 0);
    render(argv[1], "04_pad_stone_slow_fb127", SC_PAD, (MfxParams){MFX_STONE_PHASER, {64, slow, 127, 127}, 0}, 0);
    render(argv[1], "05_pad_stone_mid_fb100", SC_PAD, (MfxParams){MFX_STONE_PHASER, {64, mid, 100, 127}, 0}, 0);
    render(argv[1], "06_pad_stone_manual_pedal_fb100", SC_PAD, (MfxParams){MFX_STONE_PHASER, {0, 0, 100, 0}, 0}, 1);
    render(argv[1], "07_bass_dry", SC_BASS, (MfxParams){MFX_OFF, {0}}, 0);
    render(argv[1], "08_bass_stone_fast_fb64", SC_BASS, (MfxParams){MFX_STONE_PHASER, {64, fast, 64, 127}, 0}, 0);
    render(argv[1], "09_noise_stone_slow_fb110", SC_NOISE, (MfxParams){MFX_STONE_PHASER, {64, mid, 110, 127}, 0}, 0);
    render(argv[1], "10_pad_stone_slow_fb80_stereo64", SC_PAD, (MfxParams){MFX_STONE_PHASER, {64, slow, 80, 127}, 64}, 0);
    render(argv[1], "11_pad_stone_slow_fb80_stereo127", SC_PAD, (MfxParams){MFX_STONE_PHASER, {64, slow, 80, 127}, 127}, 0);
    {
        const uint8_t w03 = whirl_knob(0.3), w05 = whirl_knob(0.5), w08 = whirl_knob(0.8), w025 = whirl_knob(0.25),
                      w5 = whirl_knob(5.0);
        printf("BULLROARER (WHIRL knobs: 0.25 Hz = %u, 0.5 Hz = %u, 5 Hz = %u):\n", w025, w05, w5);
        render(argv[1], "21_pad_roar_steady", SC_PAD, (MfxParams){MFX_BULLROARER, {w03, 70, 0, 30}, 64}, 0);
        render(argv[1], "22_pad_roar_stall", SC_PAD, (MfxParams){MFX_BULLROARER, {w05, 90, 64, 50}, 64}, 0);
        render(argv[1], "23_pad_roar_swingback", SC_PAD, (MfxParams){MFX_BULLROARER, {w08, 100, 127, 60}, 96}, 0);
        render(argv[1], "24_pad_roar_ancient_dust", SC_PAD, (MfxParams){MFX_BULLROARER, {w025, 60, 80, 127}, 127}, 0);
        render(argv[1], "25_bass_roar_fast", SC_BASS, (MfxParams){MFX_BULLROARER, {w5, 40, 40, 40}, 32}, 0);
        render(argv[1], "26_noise_roar", SC_NOISE, (MfxParams){MFX_BULLROARER, {w05, 100, 64, 64}, 64}, 0);
        printf("  host cost, one layer stereo: BULLROARER %.1f ns/sample\n",
               cost((MfxParams){MFX_BULLROARER, {w05, 90, 64, 127}, 64}));
    }
    {   /* ENSEMBLE: RATE 74 = 0.78 Hz (the unit's), knobs AGE / RATE / SHIMMER / DEPTH, row WIDTH */
        printf("ENSEMBLE:\n");
        render(argv[1], "31_pad_ens_new", SC_PAD, (MfxParams){MFX_ENSEMBLE, {0, 74, 45, 64}, 100}, 0);
        render(argv[1], "32_pad_ens_dusty", SC_PAD, (MfxParams){MFX_ENSEMBLE, {80, 74, 45, 64}, 100}, 0);
        render(argv[1], "33_pad_ens_ancient", SC_PAD, (MfxParams){MFX_ENSEMBLE, {127, 66, 70, 85}, 127}, 0);
        render(argv[1], "34_pad_ens_slow_no_shimmer", SC_PAD, (MfxParams){MFX_ENSEMBLE, {40, 35, 0, 110}, 127}, 0);
        render(argv[1], "35_pad_ens_all_shimmer", SC_PAD, (MfxParams){MFX_ENSEMBLE, {40, 74, 127, 70}, 110}, 0);
        render(argv[1], "36_pad_ens_mono_like_unit", SC_PAD, (MfxParams){MFX_ENSEMBLE, {30, 74, 45, 64}, 0}, 0);
        render(argv[1], "37_bass_ens_dusty", SC_BASS, (MfxParams){MFX_ENSEMBLE, {90, 74, 30, 50}, 64}, 0);
        render(argv[1], "38_noise_ens_dusty", SC_NOISE, (MfxParams){MFX_ENSEMBLE, {80, 74, 45, 64}, 100}, 0);
        printf("  host cost, one layer stereo: ENSEMBLE %.1f ns/sample\n",
               cost((MfxParams){MFX_ENSEMBLE, {80, 74, 45, 64}, 100}));
    }
    {   /* ENS+PHASER: knobs ENS RATE / ENS DEPTH / PHASE RATE / PHASE DEPTH, lr WIDTH, rows AGE / COLOUR. PHASE RATE
         * knob for Hz: 0.04 * 2^(k * 7.966 / 127): 0.1 Hz = 15, 0.2 Hz = 26, 0.5 Hz = 44, 3 Hz = 74 */
        printf("ENS+PHASER:\n");
        render(argv[1], "41_pad_ensph_oxygene", SC_PAD, (MfxParams){MFX_ENS_PHASER, {74, 64, 15, 110}, 100, {30, 40}}, 0);
        render(argv[1], "42_pad_ensph_colour_on", SC_PAD, (MfxParams){MFX_ENS_PHASER, {74, 64, 26, 120}, 100, {30, 76}}, 0);
        render(argv[1], "43_pad_ensph_dusty_slow", SC_PAD, (MfxParams){MFX_ENS_PHASER, {60, 80, 10, 127}, 110, {100, 50}}, 0);
        render(argv[1], "44_pad_ensph_fast_swirl", SC_PAD, (MfxParams){MFX_ENS_PHASER, {90, 70, 44, 90}, 127, {40, 100}}, 0);
        render(argv[1], "45_noise_ensph", SC_NOISE, (MfxParams){MFX_ENS_PHASER, {74, 64, 26, 127}, 100, {30, 76}}, 0);
        render(argv[1], "46_bass_ensph", SC_BASS, (MfxParams){MFX_ENS_PHASER, {74, 50, 74, 80}, 64, {40, 60}}, 0);
        printf("  host cost, one layer stereo: ENS+PHASER %.1f ns/sample\n",
               cost((MfxParams){MFX_ENS_PHASER, {74, 64, 26, 120}, 100, {30, 76}}));
    }
    {   /* CHORUS: knobs RATE / DEPTH / DELAY / MIX, lr WIDTH, row AGE. RATE knob for Hz: 0.1 * 2^(k * 6.644 / 127):
         * 0.51 Hz = 45 (the classic I), 0.86 Hz = 59 (II), 9.75 Hz = 126 (I+II, with a small DEPTH) */
        printf("CHORUS:\n");
        render(argv[1], "51_pad_chorus_I", SC_PAD, (MfxParams){MFX_CHORUS, {45, 94, 46, 64}, 127, {20, 0}}, 0);
        render(argv[1], "52_pad_chorus_II", SC_PAD, (MfxParams){MFX_CHORUS, {59, 110, 46, 64}, 127, {20, 0}}, 0);
        render(argv[1], "53_pad_chorus_I+II", SC_PAD, (MfxParams){MFX_CHORUS, {126, 12, 46, 64}, 127, {20, 0}}, 0);
        render(argv[1], "54_pad_chorus_dusty_wide", SC_PAD, (MfxParams){MFX_CHORUS, {25, 110, 70, 72}, 127, {100, 0}}, 0);
        render(argv[1], "55_bass_chorus_I", SC_BASS, (MfxParams){MFX_CHORUS, {45, 94, 46, 64}, 127, {20, 0}}, 0);
        render(argv[1], "56_noise_chorus_II", SC_NOISE, (MfxParams){MFX_CHORUS, {59, 110, 46, 64}, 127, {20, 0}}, 0);
        printf("  host cost, one layer stereo: CHORUS %.1f ns/sample\n",
               cost((MfxParams){MFX_CHORUS, {45, 94, 46, 64}, 127, {20, 0}}));
    }
    printf("  host cost, one layer stereo: STONE PHASER %.1f ns/sample\n",
           cost((MfxParams){MFX_STONE_PHASER, {64, mid, 100, 127}, 0}));
    {   /* beside the voices: 8 held pad notes, the whole render, without and with it (best of 5) */
        static const uint8_t chord8[8] = {48, 55, 60, 63, 67, 70, 74, 79};
        double t[2];
        for (int with = 0; with < 2; with++) {
            t[with] = 1e30;
            for (int rep = 0; rep < 5; rep++) {
                pad();
                mfx_params[0] = (MfxParams){with ? MFX_STONE_PHASER : MFX_OFF, {64, mid, 100, 127}};
                for (int i = 0; i < 8; i++) synth_midi_3(0x90, chord8[i], 100);
                for (uint32_t fr = 0; fr < RATE; fr += BLOCK) synth_render_q15(buf, BLOCK);   /* the attack */
                clock_t t0 = clock();
                for (uint32_t fr = 0; fr < RATE * 4u; fr += BLOCK) synth_render_q15(buf, BLOCK);
                double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / (RATE * 4.0);
                if (ns < t[with]) t[with] = ns;
            }
        }
        printf("  8 SPREAD voices: %.0f ns/sample, with STONE PHASER %.0f (+%.1f %%)\n", t[0], t[1],
               100.0 * (t[1] - t[0]) / t[0]);
    }
    return 0;
}
