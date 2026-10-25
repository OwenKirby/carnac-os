/* Shared noise A/B (2026-10-25): the same scenes rendered by a build with one noise generator per voice (A) and one
 * with the shared generator (B), as WAVs to compare by ear, and the host cost of 8 held noisy voices. Build it against
 * each tree and run both into one folder with a different label:
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/pcm_images_host.c src/host/render_noise_ab.c -lm -o render_noise_ab
 *   render_noise_ab OUTDIR LABEL      writes OUTDIR/<scene>_<LABEL>.wav, prints peak / RMS and the cost
 *
 * Scenes: 1 an 8-note SINGLE pad with noise in the mix (white -> pink), 2 a wind chord, mostly noise through a
 * resonant filter, 3 a BINAURAL chord with noise, 4 NEWGRANGE DAWN as a 6-note chord with its noise at 8 (taken out
 * for CPU), 5-8 the factory performances that use noise, played as render_factory_perfs plays them. */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "PresetStructures.h"
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/midi_clock.h"
#include "dsp/maq_seq.h"
#include "dsp/pcm_store.h"

#define RATE 44100u
#define BLOCK 32u

static SequencerLane syn_seq_bank[BANK_SLOTS][SEQ_NLANE];
static ArpState syn_arp_bank[BANK_SLOTS];
static uint8_t syn_perf_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_seq_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_arp_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_used[UISAVE_COUNT][PATCH_SLOTS / 8u];
#include "factory_presets.c"

static int32_t buf[BLOCK * 2u];
static const uint8_t chord8[8] = {45, 52, 57, 60, 64, 67, 71, 76};

static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

static void load_perf(uint32_t s)
{
    WholePerformance w;
    factory_perf(s, &w);
    perf_state = w.perf;
    engine_state[0] = w.engine[0];
    engine_state[1] = w.engine[1];
    memcpy(seq_lanes, w.seq, sizeof seq_lanes);
    arp_state = w.arp;
}

/* the hand-made scenes' patches; synth_restart after */
static void patch(int scene)
{
    EngineState *e = &engine_state[0];
    if (scene == 4) {
        load_perf(0);
        engine_state[0].oscmix.noise_level = 8;
        synth_restart();
        return;
    }
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->amp.reverb_send = 30;
    e->env[0].attack = 40; e->env[0].sustain = 120; e->env[0].release = 60;
    e->filter[0].mod_amt = BIPOLAR_CENTER;
    if (scene == 1) {
        e->arch = VARCH_SINGLE;
        e->filter[0].cutoff = 80; e->filter[0].resonance = 20;
        e->oscmix.noise_level = 64; e->oscmix.noise_colour = 40;
    } else if (scene == 2) {
        e->arch = VARCH_SINGLE;
        e->filter[0].cutoff = 70; e->filter[0].resonance = 110;
        e->oscmix.noise_level = 127; e->oscmix.noise_colour = 20;
    } else {
        e->arch = VARCH_BINAURAL;
        e->filter[0].cutoff = 76; e->filter[0].resonance = 30;
        e->oscmix.noise_level = 80; e->oscmix.noise_colour = 64;
    }
    synth_restart();
}

static void render(const char *dir, const char *label, int scene)
{
    static const uint32_t perf_of[9] = {0, 0, 0, 0, 0, 3, 4, 9, 15};   /* HYPOGEUM, SKARA BRAE, CURSUS, BONE FLUTE */
    char path[512], name[40];
    uint32_t len, play = 0, frame = 0;
    double sum = 0.0, peak = 0.0;
    if (scene <= 4) {
        snprintf(name, sizeof name, "%d_%s", scene,
                 scene == 1 ? "pad_chord" : scene == 2 ? "wind_chord" : scene == 3 ? "binaural_chord" : "newgrange_noise8");
        patch(scene);
        int nv = scene == 4 ? 6 : 8;
        for (int i = 0; i < nv; i++) synth_midi_3(0x90, chord8[i], 100);
        len = RATE * 10u;
    } else {
        uint32_t s = perf_of[scene], i;
        snprintf(name, sizeof name, "%d_", scene);
        for (i = 0; FV_TAB[s].name[i] && i < 16; i++)
            name[2 + i] = FV_TAB[s].name[i] == ' ' ? '_' : FV_TAB[s].name[i];
        name[2 + i] = 0;
        load_perf(s);
        synth_restart();
        uint32_t bpm = MIDI_CLOCK_BPM_BASE + perf_state.int_tempo;
        play = 8u * 4u * RATE * 60u / bpm;
        if (play > 30u * RATE) play = 30u * RATE;
        len = play + 3u * RATE;
        maq_set_playing(synth_seq(), 1);
        carp_set_playing(synth_arp(), 1);
    }
    snprintf(path, sizeof path, "%s/%s_%s.wav", dir, name, label);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + len * 4u); fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16u); put_u16(f, 1); put_u16(f, 2); put_u32(f, RATE); put_u32(f, RATE * 4u); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, len * 4u);
    while (frame < len) {
        if (scene <= 4 && frame == RATE * 7u)
            for (int i = 0; i < 8; i++) synth_midi_3(0x80, chord8[i], 0);
        if (play && frame < play && frame + BLOCK >= play) {
            maq_set_playing(synth_seq(), 0);
            carp_set_playing(synth_arp(), 0);
        }
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
    printf("  %-24s peak %6.1f dBFS  rms %6.1f dBFS\n", name, 20.0 * log10(peak + 1e-9),
           20.0 * log10(sqrt(sum / (2.0 * len)) + 1e-9));
}

/* host ns a sample, 8 held SINGLE voices, noise level `level` at colour `colour`, best of 7 */
static double cost(uint8_t level, uint8_t colour)
{
    double best = 1e30;
    for (int r = 0; r < 7; r++) {
        patch(1);
        engine_state[0].oscmix.noise_level = level;
        engine_state[0].oscmix.noise_colour = colour;
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
    if (argc < 3) { fprintf(stderr, "usage: render_noise_ab OUTDIR LABEL\n"); return 2; }
    synth_init();
    {
        extern const uint8_t pcm_image_host0[], pcm_image_host1[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
    }
    factory_install();
    printf("%s:\n", argv[2]);
    for (int s = 1; s <= 8; s++) render(argv[1], argv[2], s);
    double c0 = cost(0, 0), cw = cost(100, 0), cp = cost(100, 40), cb = cost(100, 100);
    printf("  host cost, 8 voices: no noise %.0f ns/sample; noise white %+.1f, white->pink %+.1f, pink->brown %+.1f ns"
           " (%+.1f / %+.1f / %+.1f %%)\n", c0, cw - c0, cp - c0, cb - c0,
           (cw - c0) * 100.0 / c0, (cp - c0) * 100.0 / c0, (cb - c0) * 100.0 / c0);
    return 0;
}
