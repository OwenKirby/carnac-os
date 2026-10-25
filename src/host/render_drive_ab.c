/* AMP drive A/B (2026-10-25): the same scenes rendered by the build before the drive rework (A) and after (B), as WAVs
 * to compare by ear, and the host cost of 8 held driven voices per model. Build it against each tree and run both
 * into one folder with a different label:
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/pcm_images_host.c src/host/render_drive_ab.c -lm -o render_drive_ab
 *   render_drive_ab OUTDIR LABEL      writes OUTDIR/<scene>_<LABEL>.wav, prints peak / RMS and the cost
 *
 * Scenes: the riff and the chord clean (DRIVE 0), then per model (SOFT HARD FOLD TAPE TUBE OVERDRIVE; an older build plays the last two as SOFT) a SINGLE saw
 * bass riff at DRIVE 50 with mixed velocities, and a 4-note chord at DRIVE 100; then every factory performance with
 * a driven layer, played as render_factory_perfs plays them. */
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
#define NMODEL 6

static SequencerLane syn_seq_bank[BANK_SLOTS][SEQ_NLANE];
static ArpState syn_arp_bank[BANK_SLOTS];
static uint8_t syn_perf_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_seq_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_arp_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_used[UISAVE_COUNT][PATCH_SLOTS / 8u];
#include "factory_presets.c"

static const char *const model_name[NMODEL] = {"soft", "hard", "fold", "tape", "tube", "overdrive"};
static int32_t buf[BLOCK * 2u];
static const uint8_t riff[8] = {33, 33, 45, 33, 36, 33, 43, 40}, riff_vel[8] = {120, 60, 100, 50, 110, 70, 127, 80};
static const uint8_t chord4[4] = {45, 52, 57, 60};

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

static int perf_driven(uint32_t s)
{
    WholePerformance w;
    factory_perf(s, &w);
    return w.engine[0].amp.drive || (w.perf.perf_mode != PERF_SINGL_SYNTH && w.engine[1].amp.drive);
}

/* a bright SINGLE saw through a half-open filter, the model at `drive`; synth_restart after */
static void patch(int model, int drive)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = VARCH_SINGLE;
    e->amp.reverb_send = 20;
    e->amp.drive = (uint8_t)drive;
    e->amp.drive_model = (uint8_t)model;
    e->env[0].attack = 0; e->env[0].decay = 70; e->env[0].sustain = 100; e->env[0].release = 40;
    e->filter[0].cutoff = 90; e->filter[0].resonance = 30;
    synth_restart();
}

static FILE *wav_open(const char *dir, const char *name, const char *label, uint32_t len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s_%s.wav", dir, name, label);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return NULL; }
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + len * 4u); fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16u); put_u16(f, 1); put_u16(f, 2); put_u32(f, RATE); put_u32(f, RATE * 4u); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, len * 4u);
    return f;
}

/* scene < 2 * NMODEL: model scene / 2, riff (even) or chord (odd); else factory performance scene - 2 * NMODEL */
static void render(const char *dir, const char *label, int scene)
{
    char name[48];
    uint32_t len, play = 0, frame = 0;
    double sum = 0.0, peak = 0.0;
    int model = scene < 0 ? 0 : scene / 2, is_riff = !(scene & 1);
    if (scene < 0) {                                  /* -2, -1: the riff and the chord clean, DRIVE 0 */
        snprintf(name, sizeof name, "00_clean_%s", is_riff ? "riff" : "chord");
        patch(0, 0);
        len = RATE * (is_riff ? 5u : 6u);
        if (!is_riff)
            for (int i = 0; i < 4; i++) synth_midi_3(0x90, chord4[i], 100);
    } else if (scene < 2 * NMODEL) {
        snprintf(name, sizeof name, "%02d_%s_%s", scene + 1, model_name[model], is_riff ? "riff_d50" : "chord_d100");
        patch(model, is_riff ? 50 : 100);
        len = RATE * (is_riff ? 5u : 6u);
        if (!is_riff)
            for (int i = 0; i < 4; i++) synth_midi_3(0x90, chord4[i], 100);
    } else {
        uint32_t s = (uint32_t)(scene - 2 * NMODEL), i;
        snprintf(name, sizeof name, "p%02u_", (unsigned)s);
        for (i = 0; FV_TAB[s].name[i] && i < 16; i++)
            name[4 + i] = FV_TAB[s].name[i] == ' ' ? '_' : FV_TAB[s].name[i];
        name[4 + i] = 0;
        load_perf(s);
        synth_restart();
        uint32_t bpm = MIDI_CLOCK_BPM_BASE + perf_state.int_tempo;
        play = 8u * 4u * RATE * 60u / bpm;
        if (play > 20u * RATE) play = 20u * RATE;
        len = play + 3u * RATE;
        maq_set_playing(synth_seq(), 1);
        carp_set_playing(synth_arp(), 1);
    }
    FILE *f = wav_open(dir, name, label, len);
    if (!f) return;
    uint32_t step = RATE / 4u;                        /* the riff: an eighth at 120 BPM */
    while (frame < len) {
        if (scene < 2 * NMODEL && is_riff && frame % step < BLOCK && frame / step < 16u) {
            uint32_t k = frame / step;
            if (k) synth_midi_3(0x80, riff[(k - 1u) & 7u], 0);
            synth_midi_3(0x90, riff[k & 7u], riff_vel[k & 7u]);
        }
        if (scene < 2 * NMODEL && is_riff && frame / step == 16u && frame % step < BLOCK)
            synth_midi_3(0x80, riff[7], 0);
        if (scene < 2 * NMODEL && !is_riff && frame == RATE * 4u)
            for (int i = 0; i < 4; i++) synth_midi_3(0x80, chord4[i], 0);
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
    printf("  %-28s peak %6.1f dBFS  rms %6.1f dBFS\n", name, 20.0 * log10(peak + 1e-9),
           20.0 * log10(sqrt(sum / (2.0 * len)) + 1e-9));
}

/* host ns a sample, 8 held SINGLE voices at `drive` (0: none) through `model`, best of 7 */
static double cost(int model, int drive)
{
    static const uint8_t chord8[8] = {45, 52, 57, 60, 64, 67, 71, 76};
    double best = 1e30;
    for (int r = 0; r < 7; r++) {
        patch(model, drive);
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
    if (argc < 3) { fprintf(stderr, "usage: render_drive_ab OUTDIR LABEL\n"); return 2; }
    synth_init();
    {
        extern const uint8_t pcm_image_host0[], pcm_image_host1[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
    }
    factory_install();
    printf("%s:\n", argv[2]);
    for (int s = -2; s < 2 * NMODEL; s++) render(argv[1], argv[2], s);
    for (uint32_t p = 0; p < FP_N; p++)
        if (perf_driven(p)) render(argv[1], argv[2], 2 * NMODEL + (int)p);
    double c0 = cost(0, 0);
    printf("  host cost, 8 voices: no drive %.0f ns/sample;", c0);
    for (int m = 0; m < NMODEL; m++) printf(" %s %+.1f", model_name[m], cost(m, 80) - c0);
    printf(" ns\n");
    return 0;
}
