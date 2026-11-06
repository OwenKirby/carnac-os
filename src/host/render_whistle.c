/* The noise whistles (2026-11-05: COLOUR past brown rings a sine at OSC1's pitch): scenes through the real engine as
 * WAVs, and the host cost of 8 held voices at each colour. "noise" scenes are the render with the noise minus the
 * same render without it (everything after the noise is linear at these settings), so the whistle is heard alone.
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/pcm_images_host.c src/host/render_whistle.c -lm -o render_whistle
 *   render_whistle OUTDIR */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/pcm_store.h"

#define RATE 44100u
#define BLOCK 32u

static int32_t buf[BLOCK * 2u];
static const uint8_t melody[8] = {72, 74, 76, 79, 76, 72, 67, 72};
static const uint8_t chord8[8] = {45, 52, 57, 60, 64, 67, 71, 76};

static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

static uint8_t g_harm, g_harm_lfo;                  /* scene 4: HARMONIC's centre, and LFO1 sweeping it */

static void setup(uint8_t level, uint8_t colour)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = VARCH_SINGLE;
    e->amp.reverb_send = 30;
    e->env[0].attack = 30; e->env[0].sustain = 120; e->env[0].release = 50;
    e->filter[0].mod_amt = 0; e->filter[0].cutoff = 127;
    e->oscmix.noise_level = level;
    e->oscmix.noise_colour = colour;
    e->oscmix.noise_col_mod_src = MSRC_MODWHEEL;
    e->oscmix.noise_col_mod_amt = 127;              /* the wheel takes COLOUR up to AIR (scene 3) */
    e->noise_harmonic = g_harm;
    if (g_harm_lfo) {                                /* a slow sine over the whole range: 1 -> 8 -> 1 in ~16 s */
        e->harm_mod_src = MSRC_LFO1; e->harm_mod_amt = 127;
        e->lfo[0].shape = 0; e->lfo[0].rate = 6;
    }
    synth_restart();
    synth_midi_3(0xB0, 1, 0);
}

/* scene 1/2: the melody; 3: a held C4 with the wheel sweeping COLOUR 0 -> AIR -> 0 over 16 s; 4: a held C2;
 * 5: a held chord (each voice its own band, so each note wavers on its own) */
static const uint8_t chord4[4] = {48, 55, 60, 64};
static void play(int scene, int16_t *out, uint32_t len)
{
    for (uint32_t fr = 0; fr < len; fr += BLOCK) {
        if (scene == 4 || scene == 5) {
            if (fr == 0) for (int i = 0; i < (scene == 4 ? 1 : 4); i++) synth_midi_3(0x90, scene == 4 ? 36 : chord4[i], 100);
            if (fr == len - RATE - (len - RATE) % BLOCK)
                for (int i = 0; i < 4; i++) synth_midi_3(0x80, scene == 4 ? 36 : chord4[i], 0);
        } else if (scene != 3) {
            uint32_t t = fr % (RATE * 1u), k = fr / RATE;
            if (k < 8 && t == 0) synth_midi_3(0x90, melody[k], 100);
            if (k < 8 && t == RATE * 9u / 10u - (RATE * 9u / 10u) % BLOCK) synth_midi_3(0x80, melody[k], 0);
        } else {
            if (fr == 0) synth_midi_3(0x90, 60, 100);
            uint32_t half = RATE * 8u;
            if (fr % (RATE / 50u) < BLOCK && fr < 2u * half) {
                uint32_t w = fr < half ? fr * 127u / half : (2u * half - fr) * 127u / half;
                synth_midi_3(0xB0, 1, (uint8_t)w);
            }
            if (fr == 2u * half) synth_midi_3(0x80, 60, 0);
        }
        synth_render_q15(buf, BLOCK);
        for (uint32_t k = 0; k < BLOCK; k++) {
            int32_t v = buf[2 * k];
            out[fr + k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

static void write_wav(const char *dir, const char *name, const int16_t *x, uint32_t len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + len * 2u); fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16u); put_u16(f, 1); put_u16(f, 1); put_u32(f, RATE); put_u32(f, RATE * 2u); put_u16(f, 2); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, len * 2u);
    double peak = 0, sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        put_u16(f, (uint16_t)x[i]);
        double a = fabs(x[i]) / 32768.0; sum += a * a; if (a > peak) peak = a;
    }
    fclose(f);
    printf("  %-34s peak %6.1f dBFS  rms %6.1f dBFS\n", name, 20 * log10(peak + 1e-9), 20 * log10(sqrt(sum / len) + 1e-9));
}

static void scene(const char *dir, const char *name, int sc, uint8_t level, uint8_t colour, int noise_only)
{
    uint32_t len = (sc == 3 || sc == 4 ? 18u : 10u) * RATE / BLOCK * BLOCK;   /* whole blocks: play writes BLOCK at a time */
    int16_t *a = malloc(len * 2u), *b = malloc(len * 2u);
    setup(level, colour);
    play(sc, a, len);
    {
        int32_t pk = 0;
        for (uint32_t i = 0; i < len; i++) if (abs(a[i]) > pk) pk = abs(a[i]);
        if (pk >= 32767) printf("  (%s: the engine's own output reaches full scale)\n", name);
    }
    if (noise_only) {                          /* minus the same render without noise: the noise path alone (x2 for one
                                                * note, x1 for a chord) */
        setup(0, colour);
        play(sc, b, len);
        for (uint32_t i = 0; i < len; i++) {
            int32_t d = (sc == 5 ? 1 : 2) * (a[i] - b[i]);
            a[i] = (int16_t)(d > 32767 ? 32767 : d < -32768 ? -32768 : d);
        }
    }
    write_wav(dir, name, a, len);
    free(a); free(b);
}

/* host ns a sample, 8 held voices at noise 100 and this colour, best of 7 */
static double cost(uint8_t level, uint8_t colour)
{
    double best = 1e30;
    for (int r = 0; r < 7; r++) {
        setup(level, colour);
        for (int i = 0; i < 8; i++) synth_midi_3(0x90, chord8[i], 100);
        clock_t t0 = clock();
        for (uint32_t fr = 0; fr < RATE * 4u; fr += BLOCK) synth_render_q15(buf, BLOCK);
        double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / (RATE * 4.0);
        if (ns < best) best = ns;
    }
    return best;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: render_whistle OUTDIR\n"); return 2; }
    synth_init();
    {
        extern const uint8_t pcm_image_host0[], pcm_image_host1[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
    }
    scene(argv[1], "1_air_melody_noise_only", 1, 127, NOISE_AIR, 1);
    scene(argv[1], "2_air_melody_with_osc", 1, 90, NOISE_AIR, 0);
    scene(argv[1], "3_band_80_melody_noise_only", 1, 127, NOISE_BAND, 1);
    scene(argv[1], "4_wheel_colour_sweep_noise_only", 3, 127, NOISE_WHITE, 1);
    g_harm = 64; g_harm_lfo = 1;
    scene(argv[1], "5_lfo_on_harmonic_C2_colour_112", 4, 127, 112, 1);
    g_harm = 0; g_harm_lfo = 0;
    scene(argv[1], "6_chord_colour_120_each_note_its_own", 5, 127, 120, 1);
    g_harm = 38;
    scene(argv[1], "7_chord_colour_120_harmonic_3", 5, 127, 120, 1);
    g_harm = 0;
    double c0 = cost(0, 0), cw = cost(100, NOISE_WHITE), cp = cost(100, 24), cb = cost(100, NOISE_BROWN),
           cwh = cost(100, NOISE_BAND), cwm = cost(100, 44), ca = cost(100, NOISE_AIR), cam = cost(100, 68);
    printf("  host cost, 8 voices: no noise %.0f ns/sample; white %+.1f, pink->brown %+.1f, brown %+.1f, "
           "brown->whistle %+.1f, whistle %+.1f, whistle->air %+.1f, air %+.1f ns\n",
           c0, cw - c0, cp - c0, cb - c0, cwm - c0, cwh - c0, cam - c0, ca - c0);
    return 0;
}
