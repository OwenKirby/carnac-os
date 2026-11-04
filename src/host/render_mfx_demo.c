/* MFX listening renders (2026-11-01): the same scenes dry and through each MFX setting, as WAVs to compare by ear,
 * and the host cost of the effect (both layers' worth of its work: one layer, stereo).
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/pcm_images_host.c src/host/wt_areas_host.c src/host/render_mfx_demo.c -lm -o render_mfx_demo
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
/* BRONZE AGE's scenes (2026-11-03): power chords (root and fifth) chugging in eighths at 120 BPM, and a lead line */
static const uint8_t chug[16] = {40, 40, 40, 43, 40, 40, 46, 45, 40, 40, 40, 43, 48, 47, 46, 45};
static const uint8_t lead[8] = {64, 67, 69, 71, 72, 71, 69, 67};

enum { SC_PAD, SC_BASS, SC_NOISE, SC_RIFF, SC_LEAD, SC_SCREAM, SC_SOLO, SC_ARP, SC_GLIDE };

/* the high, moving scenes (2026-11-04: first for FLINT's aliasing, by ear; FLINT is gone, the scenes stay). Aliases are not
 * harmonics of the note: they move against a bend and beat in a high line, so the scenes are high and moving.
 * SCREAM: a high C held, bent up a tone, vibrato, bent back; GLIDE: a held C5 bent an octave up and down;
 * SOLO: sixteenths at 150 BPM, a minor pentatonic run in the fifth and sixth octaves; ARP: Am - F - G - Am in
 * high sixteenths at 140 BPM */
static const uint8_t solo[32] = {81, 84, 86, 88, 91, 88, 86, 84, 81, 79, 76, 79, 81, 84, 88, 93,
                                 91, 88, 86, 88, 84, 86, 81, 84, 79, 81, 76, 79, 74, 76, 72, 76};
static const uint8_t arpn[4][6] = {{69, 72, 76, 81, 84, 88}, {65, 69, 72, 77, 81, 84}, {67, 71, 74, 79, 83, 86},
                                   {69, 72, 76, 81, 84, 88}};

static void bend_semis(double st)               /* the wheel, at BEND RANGE 12 */
{
    int v = 8192 + (int)(st / 12.0 * 8191.0);
    v = v < 0 ? 0 : v > 16383 ? 16383 : v;
    synth_midi_3(0xE0, (uint8_t)(v & 127), (uint8_t)(v >> 7));
}

/* the riff and the lead: SPREAD 0, open filter, short decay (the riff) or sustained (the lead) */
static void riffy(int sustained)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = VARCH_SPREAD;
    e->env[0].attack = 0; e->env[0].decay = sustained ? 90 : 50; e->env[0].sustain = sustained ? 110 : 30;
    e->env[0].release = sustained ? 50 : 25;
    e->filter[0].cutoff = 105; e->filter[0].resonance = 10;
    e->amp.reverb_send = 20; e->amp.delay_send = 0;
    synth_restart();
}

/* one scene: kind, its MFX settings, and a knob swept (sweep: the knob's index + 1; 0: as set) */
static void render(const char *dir, const char *name, int kind, MfxParams m, int sweep)
{
    uint32_t len = kind == SC_PAD ? RATE * 19u : kind == SC_BASS || kind == SC_RIFF || kind == SC_LEAD ? RATE * 8u
                                                                                                      : RATE * 12u;
    uint32_t frame = 0;
    double sum = 0.0, peak = 0.0;
    if (kind == SC_BASS) bass(); else if (kind == SC_RIFF || kind >= SC_LEAD) riffy(kind != SC_RIFF); else pad();
    if (kind >= SC_SCREAM) {
        perf_state.bend_range = 12;
        if (kind == SC_SOLO || kind == SC_ARP) {    /* (shorter notes, a touch of decay) */
            engine_state[0].env[0].decay = 60; engine_state[0].env[0].sustain = 90; engine_state[0].env[0].release = 30;
        }
        synth_restart();
        bend_semis(0.0);
    }
    if (kind == SC_NOISE) {                      /* white noise at full level, no filter, no reverb */
        engine_state[0].oscmix.noise_level = 127;
        engine_state[0].osc[0].pitch_mod_amt = 0;
        engine_state[0].filter[0].cutoff = 127;
        engine_state[0].amp.reverb_send = 0;
        engine_state[0].env[0].attack = 0;
        synth_restart();
    }
    MFX_PARAMS(0) = m;
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
        if (kind == SC_RIFF && frame % (RATE / 4u) < BLOCK && frame < 7u * RATE) {   /* eighths at 120 BPM */
            uint32_t k = frame / (RATE / 4u);
            if (k) { synth_midi_3(0x80, chug[(k - 1u) & 15u], 0); synth_midi_3(0x80, chug[(k - 1u) & 15u] + 7, 0); }
            synth_midi_3(0x90, chug[k & 15u], 110);
            synth_midi_3(0x90, chug[k & 15u] + 7, 110);
        }
        if (kind == SC_LEAD && frame % (RATE * 3u / 4u) < BLOCK && frame < 6u * RATE) {   /* dotted quarters */
            uint32_t k = frame / (RATE * 3u / 4u);
            if (k) synth_midi_3(0x80, lead[(k - 1u) & 7u], 0);
            synth_midi_3(0x90, lead[k & 7u], 105);
        }
        if (kind == SC_LEAD && frame >= 6u * RATE && frame < 6u * RATE + BLOCK) synth_midi_3(0x80, lead[7], 0);
        if (kind >= SC_SCREAM) {
            const double t = (double)frame / RATE;
            if (kind == SC_SCREAM || kind == SC_GLIDE) {
                const uint8_t note = kind == SC_SCREAM ? 84 : 72;
                if (frame == 0) synth_midi_3(0x90, note, 110);
                if (frame >= 11u * RATE && frame < 11u * RATE + BLOCK) synth_midi_3(0x80, note, 0);
                double st;
                if (kind == SC_SCREAM)
                    st = (t < 2.0 ? 0.0 : t < 3.0 ? 2.0 * (t - 2.0) : 2.0) + (t > 4.0 ? 0.35 * sin(2.0 * M_PI * 5.5 * t) : 0.0)
                         - (t > 8.0 ? 2.0 * (t - 8.0 < 1.0 ? t - 8.0 : 1.0) : 0.0);
                else
                    st = t < 1.0 ? 0.0 : t < 5.0 ? 3.0 * (t - 1.0) : t < 9.0 ? 12.0 - 4.5 * (t - 5.0) : t < 10.0 ? -6.0 + 6.0 * (t - 9.0) : 0.0;
                bend_semis(st);
            } else if (frame < 10u * RATE) {
                const uint32_t step = kind == SC_SOLO ? RATE / 10u : (uint32_t)(RATE * 60u / 140u / 4u);
                if (frame % step < BLOCK) {
                    const uint32_t k = frame / step;
                    uint8_t prev = 0, now;
                    if (kind == SC_SOLO) { now = solo[k & 31u]; if (k) prev = solo[(k - 1u) & 31u]; }
                    else {
                        const uint32_t bar = (k / 16u) & 3u;
                        static const uint8_t pat[8] = {0, 1, 2, 3, 4, 5, 4, 2};
                        now = arpn[bar][pat[k & 7u]];
                        if (k) prev = arpn[((k - 1u) / 16u) & 3u][pat[(k - 1u) & 7u]];
                    }
                    if (k) synth_midi_3(0x80, prev, 0);
                    synth_midi_3(0x90, now, (uint8_t)(k & 3u ? 95 : 118));
                }
            } else if (frame < 10u * RATE + BLOCK)
                for (int nn = 60; nn < 100; nn++) synth_midi_3(0x80, (uint8_t)nn, 0);
        }
        if (kind == SC_NOISE && frame == 0) synth_midi_3(0x90, 60, 100);
        if (kind == SC_NOISE && frame >= 11u * RATE && frame < 11u * RATE + BLOCK) synth_midi_3(0x80, 60, 0);
        if (sweep) {                             /* the pedal: heel to toe and back over 8 s, twice */
            double t = fmod((double)frame / RATE, 8.0) / 8.0;
            MFX_PARAMS(0).knob[sweep - 1] = (uint8_t)(127.0 * (t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t) + 0.5);
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
    MFX_PARAMS(0).type = MFX_OFF;
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
        MFX_PARAMS(0) = m;
        clock_t t0 = clock();
        for (uint32_t fr = 0; fr < RATE * 10u; fr += 128u) {
            for (int i = 0; i < 128; i++) { seed = seed * 1664525u + 1013904223u; l[i] = r[i] = (float)((int32_t)seed >> 17); }
            mfx_layer_block(0, l, r, 128);
        }
        double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / (RATE * 10.0);
        if (ns < best) best = ns;
    }
    MFX_PARAMS(0).type = MFX_OFF;
    return best;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: render_mfx_demo OUTDIR [m|r]   (m: BRONZE AGE's scenes alone; r: the resonators')\n");
        return 2;
    }
    synth_init();
    {
        extern const uint8_t pcm_image_host0[], pcm_image_host1[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;
        extern const uint8_t wt_area2_host[];
        extern const uint32_t wt_area2_host_len;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
        wavetable_area_attach(0, wt_area2_host, wt_area2_host_len);
    }
    if (argc >= 3) {
        if (argv[2][0] == 'r') goto resonators;
        if (argv[2][0] == 'p') goto phase_flange;
        goto bronze;
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
    {   /* TAPE FLANGER: knobs MANUAL / RATE / DEPTH / TAPE, lr L/R PHASE, row[1] POLARITY. RATE knob for Hz: 0.03 *
         * 2^(k * 7.38 / 127): 0.08 Hz = 24, 0.15 Hz = 40, 0.5 Hz = 63, 2 Hz = 105. MANUAL ~32: the decks in step */
        printf("TAPE FLANGER:\n");
        render(argv[1], "61_pad_tape_positive", SC_PAD, (MfxParams){MFX_TAPE_FLANGER, {40, 40, 64, 40}, 0, {0, 0}}, 0);
        render(argv[1], "62_pad_tape_negative", SC_PAD, (MfxParams){MFX_TAPE_FLANGER, {40, 40, 64, 40}, 0, {0, 1}}, 0);
        render(argv[1], "63_pad_tape_slow_deep_worn", SC_PAD, (MfxParams){MFX_TAPE_FLANGER, {56, 24, 127, 110}, 0, {0, 0}}, 0);
        render(argv[1], "64_pad_tape_negative_wide", SC_PAD, (MfxParams){MFX_TAPE_FLANGER, {40, 40, 64, 40}, 64, {0, 1}}, 0);
        render(argv[1], "65_pad_tape_clean_tape", SC_PAD, (MfxParams){MFX_TAPE_FLANGER, {40, 40, 64, 0}, 0, {0, 0}}, 0);
        render(argv[1], "66_bass_tape_fast", SC_BASS, (MfxParams){MFX_TAPE_FLANGER, {36, 105, 30, 40}, 0, {0, 0}}, 0);
        render(argv[1], "67_noise_tape_positive", SC_NOISE, (MfxParams){MFX_TAPE_FLANGER, {40, 63, 64, 40}, 0, {0, 0}}, 0);
        render(argv[1], "68_noise_tape_negative", SC_NOISE, (MfxParams){MFX_TAPE_FLANGER, {40, 63, 64, 40}, 0, {0, 1}}, 0);
        printf("  host cost, one layer stereo: TAPE FLANGER %.1f ns/sample\n",
               cost((MfxParams){MFX_TAPE_FLANGER, {40, 40, 64, 40}, 0, {0, 0}}));
    }
    {   /* BBD FLANGER: knobs CENTER / RATE / DEPTH / FEEDBACK, row[0] MODE (I, II, III, CHORUS), row[1] POLARITY */
        printf("BBD FLANGER:\n");
        render(argv[1], "71_pad_bbdf_flanger1", SC_PAD, (MfxParams){MFX_BBD_FLANGER, {50, 30, 80, 70}, 0, {0, 0}}, 0);
        render(argv[1], "72_pad_bbdf_flanger2_wide", SC_PAD, (MfxParams){MFX_BBD_FLANGER, {50, 30, 80, 70}, 0, {1, 0}}, 0);
        render(argv[1], "73_pad_bbdf_flanger3_crossed", SC_PAD, (MfxParams){MFX_BBD_FLANGER, {50, 30, 80, 90}, 0, {2, 0}}, 0);
        render(argv[1], "74_pad_bbdf_chorus", SC_PAD, (MfxParams){MFX_BBD_FLANGER, {95, 45, 60, 0}, 0, {3, 0}}, 0);
        render(argv[1], "75_noise_bbdf_jet_negative", SC_NOISE, (MfxParams){MFX_BBD_FLANGER, {30, 25, 110, 120}, 0, {1, 1}}, 0);
        render(argv[1], "76_bass_bbdf_fast", SC_BASS, (MfxParams){MFX_BBD_FLANGER, {40, 90, 50, 80}, 0, {1, 0}}, 0);
        printf("  host cost, one layer stereo: BBD FLANGER %.1f ns/sample\n",
               cost((MfxParams){MFX_BBD_FLANGER, {50, 30, 80, 70}, 0, {1, 0}}));
    }
    {   /* PITCH SHIFT: knobs PITCH (0..24, 12 none) / FINE / FEEDBACK / MIX, lr SPREAD, row[0] AGE */
        printf("PITCH SHIFT:\n");
        render(argv[1], "81_pad_pitch_doubling", SC_PAD, (MfxParams){MFX_PITCH_SHIFT, {12, 75, 0, 64}, 127, {30, 0}}, 0);
        render(argv[1], "82_pad_pitch_octave_up", SC_PAD, (MfxParams){MFX_PITCH_SHIFT, {24, 64, 0, 50}, 0, {20, 0}}, 0);
        render(argv[1], "83_bass_pitch_octave_down", SC_BASS, (MfxParams){MFX_PITCH_SHIFT, {0, 64, 0, 64}, 0, {20, 0}}, 0);
        render(argv[1], "84_pad_pitch_fifth_spiral", SC_PAD, (MfxParams){MFX_PITCH_SHIFT, {19, 64, 100, 70}, 40, {40, 0}}, 0);
        render(argv[1], "85_pad_pitch_down_spiral_aged", SC_PAD, (MfxParams){MFX_PITCH_SHIFT, {7, 64, 105, 75}, 60, {110, 0}}, 0);
        render(argv[1], "86_pad_pitch_shimmer_octave_fb", SC_PAD, (MfxParams){MFX_PITCH_SHIFT, {24, 64, 90, 55}, 90, {30, 0}}, 0);
        printf("  host cost, one layer stereo: PITCH SHIFT %.1f ns/sample\n",
               cost((MfxParams){MFX_PITCH_SHIFT, {19, 64, 60, 64}, 127, {30, 0}}));
    }
    {   /* BBD ECHO: knobs TIME / FEEDBACK / ENV (64 none) / MIX, lr WIDTH, row[0] ENV MODE (SWOOP, DUCK), row[1] AGE */
        printf("BBD ECHO:\n");
        render(argv[1], "91_bass_echo_slap", SC_BASS, (MfxParams){MFX_BBD_ECHO, {99, 35, 64, 55}, 40, {0, 30}}, 0);
        render(argv[1], "92_bass_echo_swoop_up", SC_BASS, (MfxParams){MFX_BBD_ECHO, {99, 50, 115, 60}, 40, {0, 30}}, 0);
        render(argv[1], "93_bass_echo_swoop_down", SC_BASS, (MfxParams){MFX_BBD_ECHO, {104, 55, 10, 60}, 40, {0, 50}}, 0);
        render(argv[1], "94_pad_echo_duck_bloom", SC_PAD, (MfxParams){MFX_BBD_ECHO, {115, 85, 120, 70}, 90, {1, 40}}, 0);
        render(argv[1], "95_pad_echo_dub_worn", SC_PAD, (MfxParams){MFX_BBD_ECHO, {120, 118, 80, 60}, 127, {0, 110}}, 0);
        render(argv[1], "96_pad_echo_short_talking", SC_PAD, (MfxParams){MFX_BBD_ECHO, {25, 100, 120, 64}, 60, {0, 20}}, 0);
        printf("  host cost, one layer stereo: BBD ECHO %.1f ns/sample\n",
               cost((MfxParams){MFX_BBD_ECHO, {99, 50, 115, 60}, 40, {0, 30}}));
    }
bronze:
    {   /* BRONZE AGE: knobs DIST / HIGH / MIDDLE / MID FREQ, rows LEVEL / LOW */
        printf("BRONZE AGE:\n");
        render(argv[1], "101_riff_dry", SC_RIFF, (MfxParams){MFX_OFF, {0}}, 0);
        render(argv[1], "102_riff_bronze_flat", SC_RIFF, (MfxParams){MFX_METAL, {100, 64, 64, 64}, 0, {64, 64}}, 0);
        render(argv[1], "103_riff_bronze_scooped", SC_RIFF, (MfxParams){MFX_METAL, {120, 76, 20, 50}, 0, {64, 92}}, 0);
        render(argv[1], "104_riff_bronze_max_mids", SC_RIFF, (MfxParams){MFX_METAL, {127, 60, 110, 72}, 0, {58, 70}}, 0);
        render(argv[1], "105_riff_bronze_midfreq_swept", SC_RIFF, (MfxParams){MFX_METAL, {120, 64, 120, 64}, 0, {54, 64}},
               MFX_K_MT_FREQ + 1);
        render(argv[1], "106_bass_dry", SC_BASS, (MfxParams){MFX_OFF, {0}}, 0);
        render(argv[1], "107_bass_bronze_crunch", SC_BASS, (MfxParams){MFX_METAL, {30, 64, 80, 40}, 0, {80, 80}}, 0);
        render(argv[1], "108_lead_dry", SC_LEAD, (MfxParams){MFX_OFF, {0}}, 0);
        render(argv[1], "109_lead_bronze", SC_LEAD, (MfxParams){MFX_METAL, {127, 58, 96, 84}, 0, {60, 64}}, 0);
        render(argv[1], "110_pad_bronze_chord_mud", SC_PAD, (MfxParams){MFX_METAL, {90, 64, 64, 64}, 0, {60, 64}}, 0);
        printf("  host cost, one layer stereo: BRONZE AGE %.1f ns/sample\n",
               cost((MfxParams){MFX_METAL, {100, 64, 64, 64}, 0, {64, 64}}));
        if (argc >= 3) return 0;
    }
phase_flange:
    {   /* PHASE+FLANGE (2026-11-04): knobs PHASE RATE / FLANGE RATE / CENTER / FEEDBACK, lr WIDTH, row[1] COLOUR */
        printf("PHASE+FLANGE:\n");
        render(argv[1], "401_pad_dry", SC_PAD, (MfxParams){MFX_OFF, {0}}, 0);
        render(argv[1], "402_pad_phase_flange", SC_PAD, (MfxParams){MFX_PHASE_FLANGE, {35, 25, 50, 70}, 64, {0, 60}}, 0);
        render(argv[1], "403_pad_phase_flange_fast_phaser", SC_PAD, (MfxParams){MFX_PHASE_FLANGE, {75, 20, 45, 60}, 90, {0, 40}}, 0);
        render(argv[1], "404_pad_slow_jet_deep", SC_PAD, (MfxParams){MFX_PHASE_FLANGE, {20, 12, 70, 110}, 100, {0, 90}}, 0);
        render(argv[1], "405_riff_dry", SC_RIFF, (MfxParams){MFX_OFF, {0}}, 0);
        render(argv[1], "406_riff_phase_flange", SC_RIFF, (MfxParams){MFX_PHASE_FLANGE, {60, 40, 40, 80}, 64, {0, 50}}, 0);
        render(argv[1], "407_lead_phase_flange", SC_LEAD, (MfxParams){MFX_PHASE_FLANGE, {50, 30, 55, 90}, 80, {0, 70}}, 0);
        render(argv[1], "408_noise_phase_flange", SC_NOISE, (MfxParams){MFX_PHASE_FLANGE, {40, 30, 50, 90}, 64, {0, 80}}, 0);
        printf("  host cost, one layer stereo: PHASE+FLANGE %.1f ns/sample\n",
               cost((MfxParams){MFX_PHASE_FLANGE, {35, 25, 50, 70}, 64, {0, 60}}));
        if (argc >= 3) return 0;
    }
resonators:
    {   /* WIND HARP / RINGING STONES (2026-11-04): knobs DECAY / TONE / AGE / MIX, lr WIDTH, rows ROOT (0 = C2) / CHORD
         * (FIFTHS MINOR MAJOR SUS4 MINOR 7 PHRYGIAN OCTAVES); the pad plays Cm9 - Abmaj7 - Fm9 - G7sus */
        printf("resonators:\n");
        render(argv[1], "301_pad_dry", SC_PAD, (MfxParams){MFX_OFF, {0}}, 0);
        render(argv[1], "302_pad_harp_c_minor", SC_PAD, (MfxParams){MFX_WIND_HARP, {90, 70, 20, 55}, 100, {0, 1}}, 0);
        render(argv[1], "303_pad_harp_c_minor_worn", SC_PAD, (MfxParams){MFX_WIND_HARP, {100, 55, 110, 60}, 100, {0, 1}}, 0);
        render(argv[1], "304_pad_harp_fifths_long_drone", SC_PAD, (MfxParams){MFX_WIND_HARP, {127, 90, 50, 75}, 127, {0, 0}},
               0);
        render(argv[1], "315_pad_harp_c_minor_38s", SC_PAD, (MfxParams){MFX_WIND_HARP, {123, 75, 30, 70}, 100, {0, 1}}, 0);
        render(argv[1], "316_pad_stones_c_minor_long", SC_PAD, (MfxParams){MFX_RINGING_STONES, {120, 70, 30, 70}, 100, {0, 1}},
               0);
        render(argv[1], "305_pad_stones_c_fifths", SC_PAD, (MfxParams){MFX_RINGING_STONES, {85, 60, 20, 55}, 100, {0, 0}}, 0);
        render(argv[1], "306_pad_stones_c_minor7_worn", SC_PAD, (MfxParams){MFX_RINGING_STONES, {100, 90, 110, 60}, 100,
                                                                             {0, 4}}, 0);
        render(argv[1], "307_bass_dry", SC_BASS, (MfxParams){MFX_OFF, {0}}, 0);
        render(argv[1], "308_bass_harp_c_minor", SC_BASS, (MfxParams){MFX_WIND_HARP, {80, 60, 40, 55}, 90, {0, 1}}, 0);
        render(argv[1], "309_bass_stones_phrygian", SC_BASS, (MfxParams){MFX_RINGING_STONES, {75, 70, 40, 60}, 90, {0, 5}}, 0);
        render(argv[1], "310_lead_dry", SC_LEAD, (MfxParams){MFX_OFF, {0}}, 0);
        render(argv[1], "311_lead_harp_e_minor", SC_LEAD, (MfxParams){MFX_WIND_HARP, {95, 80, 30, 55}, 110, {4, 1}}, 0);
        render(argv[1], "312_lead_stones_e_minor", SC_LEAD, (MfxParams){MFX_RINGING_STONES, {95, 80, 30, 55}, 110, {4, 1}}, 0);
        render(argv[1], "313_solo_harp_a_minor_octaves", SC_SOLO, (MfxParams){MFX_WIND_HARP, {100, 85, 30, 55}, 120,
                                                                               {9, 6}}, 0);
        render(argv[1], "314_solo_stones_a_minor", SC_SOLO, (MfxParams){MFX_RINGING_STONES, {100, 85, 30, 55}, 120, {9, 1}},
               0);
        printf("  host cost, one layer stereo: WIND HARP %.1f, RINGING STONES %.1f ns/sample\n",
               cost((MfxParams){MFX_WIND_HARP, {90, 70, 40, 50}, 100, {9, 1}}),
               cost((MfxParams){MFX_RINGING_STONES, {85, 60, 30, 50}, 100, {0, 0}}));
        if (argc >= 3) return 0;
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
                MFX_PARAMS(0) = (MfxParams){with ? MFX_STONE_PHASER : MFX_OFF, {64, mid, 100, 127}};
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
