/* MS-10 A/B (2026-11-21, proto/ms10): the same scenes through flavour 2 (FLT_MS10) as built from whichever tree, so
 * main's MS-20 (A) and the Korg35-style MS-10 (B) can be compared by ear, each render's peak and RMS, and the host
 * cost of 8 held voices. Build against each tree and run both into one folder:
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c)  *       src/host/pcm_images_host.c src/host/render_ms10_ab.c -lm -o render_ms10_ab
 *   render_ms10_ab OUTDIR LABEL      writes OUTDIR/<scene>_<LABEL>.wav
 * Scenes: an acid riff (MONO LEGATO, accents, slides) near and past the edge, driven; slow sweeps of a held saw
 * below the edge and self-oscillating; the morph's BP and HP; a PCM pad; a driven chord progression. */
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
#include "dsp/pcm_osc.h"

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
static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

static int pcm_code(const char *name)
{
    for (uint32_t i = 0; i < pcm_wave_count(); i++)
        if (!strncmp(pcm_wave(i)->name, name, strlen(name))) return (int)(WAVE_PCM_FIRST + i);
    return -1;
}

static const char *LABEL1 = "X";

typedef struct {
    const char *name;
    int kind;            /* 0 acid riff, 1 slow sweep held saw, 2 PCM pad swell, 3 chord progression */
    int res, drive, cutoff, env_amt;
    const char *pcm;
    int morph;
} Scene;

static const Scene SC[] = {
    {"01_acid_riff_res85", 0, 85, 0, 28, 70, NULL, 0},
    {"02_acid_riff_res100_drive70", 0, 100, 70, 28, 70, NULL, 0},
    {"03_acid_riff_res90_drive127", 0, 90, 127, 32, 60, NULL, 0},
    {"04_saw_slow_sweep_res70", 1, 70, 0, 12, 100, NULL, 0},
    {"05_saw_slow_sweep_res115", 1, 115, 0, 12, 100, NULL, 0},
    {"06_acid_riff_BP_res75", 0, 75, 20, 40, 60, NULL, 64},
    {"07_saw_sweep_HP_res80", 1, 80, 0, 10, 100, NULL, 127},
    {"08_strens_pad_res70", 2, 70, 0, 20, 90, "STR ENS", 0},
    {"09_saw_progression_res80_drive40", 3, 80, 40, 20, 85, NULL, 0},
};
#define NSC (int)(sizeof SC / sizeof SC[0])

static void patch(const Scene *s, int flv)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->filter[0].flavour = (uint8_t)flv;
    e->filter[0].morph = (uint8_t)s->morph;
    e->filter[0].resonance = (uint8_t)s->res;
    e->filter[0].drive = (uint8_t)s->drive;
    e->filter[0].cutoff = (uint8_t)s->cutoff;
    e->filter[0].mod_amt = (uint8_t)s->env_amt;
    e->env[0].attack = 0; e->env[0].decay = 70; e->env[0].sustain = 127; e->env[0].release = 40;
    if (s->kind == 0) {
        e->arch = VARCH_SINGLE;
        perf_state.voice_mode[0] = VMODE_MONO_LEGATO;
        perf_state.porta_mode[0] = PORTA_LEGATO_TIED;
        perf_state.porta_time[0] = 40;
        e->env[0].release = 20;
        e->env[1].attack = 0; e->env[1].decay = 58; e->env[1].sustain = 0; e->env[1].release = 30;
        e->lfo[0].shape = 0; e->lfo[0].rate = 13;
        e->route[0] = (ModRoute){MSRC_VELOCITY, MSRC_NONE, MDST_F1_CUTOFF, BIPOLAR_CENTER + 24, 0};
        e->route[1] = (ModRoute){MSRC_LFO1, MSRC_NONE, MDST_F1_CUTOFF, BIPOLAR_CENTER + 14, 0};
        e->amp.delay_send = 20;
        e->amp.reverb_send = 10;
    } else if (s->kind == 1) {
        e->arch = VARCH_DUAL_OSC;
        e->osc[1].fine = 64 + 8;
        e->env[0].release = 90;
        e->env[1].attack = 108; e->env[1].decay = 110; e->env[1].sustain = 0; e->env[1].release = 100;
        e->amp.reverb_send = 30;
    } else {
        e->arch = s->kind == 2 ? VARCH_SPREAD : VARCH_DUAL_OSC;
        if (s->pcm) {
            int c = pcm_code(s->pcm);
            if (c >= 0) e->osc[0].wavebank = (uint8_t)c;
            e->osc[0].scan = 64;
            e->oscmix.spread = 30;
        }
        e->osc[1].fine = 64 + 9;
        e->oscmix.sub_level = s->kind == 3 ? 40 : 0;
        e->env[0].attack = 40; e->env[0].release = 95;
        if (s->kind == 3) { e->env[1].attack = 96; e->env[1].decay = 99; e->env[1].sustain = 10; e->env[1].release = 90; }
        else { e->env[1].attack = 108; e->env[1].decay = 110; e->env[1].sustain = 0; e->env[1].release = 100; }
        e->amp.reverb_send = 50;
    }
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

/* the riff: note (0 rest), accent, slide into the next */
static const uint8_t R_NOTE[16] = {36, 36, 48, 36, 0, 39, 36, 46, 36, 0, 43, 36, 48, 47, 36, 34};
static const uint8_t R_ACC[16] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1};
static const uint8_t R_SLD[16] = {0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0};
static const uint8_t PAD[4] = {45, 52, 57, 64}, HELD[2] = {33, 45};
static const uint8_t PROG[4][4] = {{45, 52, 57, 60}, {41, 48, 57, 64}, {43, 50, 55, 62}, {40, 47, 55, 59}};

static void render(const char *dir, const Scene *s)
{
    uint32_t step = RATE * 60u / (125u * 4u), bars = 6u;
    uint32_t len = s->kind == 0 ? step * 16u * bars + RATE * 2u : RATE * 22u, hold = RATE * 16u, frame = 0;
    double sum = 0.0, peak = 0.0;
    int sounding = -1, cur = -1;
    patch(s, FLT_MS10);
    FILE *f = wav_open(dir, s->name, LABEL1, len);
    if (!f) return;
    if (s->kind == 1) for (int i = 0; i < 2; i++) synth_midi_3(0x90, HELD[i], 100);
    if (s->kind == 2) for (int i = 0; i < 4; i++) synth_midi_3(0x90, PAD[i], 90);
    while (frame < len) {
        if (s->kind == 0 && frame < step * 16u * bars + BLOCK) {
            uint32_t n = frame / step, ph = frame % step;
            int st = (int)(n & 15u);
            if (ph < BLOCK) {                             /* the step's start: its note on, then the last one off */
                int prev = sounding;
                sounding = -1;
                if (n < 16u * bars && R_NOTE[st]) {
                    synth_midi_3(0x90, R_NOTE[st], R_ACC[st] ? 127 : 80);
                    sounding = R_NOTE[st];
                }
                if (prev >= 0 && prev != sounding) synth_midi_3(0x80, (uint8_t)prev, 0);
            }
            if (ph >= step / 2u && ph < step / 2u + BLOCK && sounding >= 0 && !R_SLD[st]) {   /* gate 50 % */
                synth_midi_3(0x80, (uint8_t)sounding, 0);
                sounding = -1;
            }
        }
        if (s->kind == 3 && frame % (RATE * 4u) < BLOCK && frame < hold + BLOCK) {
            int n = (int)(frame / (RATE * 4u));
            if (cur >= 0) for (int i = 0; i < 4; i++) synth_midi_3(0x80, PROG[cur][i], 0);
            cur = n < 4 ? n : -1;
            if (cur >= 0) for (int i = 0; i < 4; i++) synth_midi_3(0x90, PROG[cur][i], 90);
        }
        if (frame >= hold && frame < hold + BLOCK) {
            if (s->kind == 1) for (int i = 0; i < 2; i++) synth_midi_3(0x80, HELD[i], 0);
            if (s->kind == 2) for (int i = 0; i < 4; i++) synth_midi_3(0x80, PAD[i], 0);
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
    printf("  %-30s %-8s peak %6.1f dBFS  rms %6.1f dBFS\n", s->name, LABEL1, 20.0 * log10(peak + 1e-9),
           20.0 * log10(sqrt(sum / (2.0 * len)) + 1e-9));
}

/* host ns a sample, 8 held saw voices through `flv` (LP, res 80, cutoff 70), best of 7 */
static double cost(int flv)
{
    static const uint8_t chord8[8] = {45, 52, 57, 60, 64, 67, 71, 76};
    static const Scene c = {"cost", 1, 80, 0, 70, 0, NULL, 0};
    double best = 1e30;
    for (int r = 0; r < 7; r++) {
        patch(&c, flv);
        engine_state[0].arch = VARCH_SINGLE;
        synth_restart();
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
    if (argc < 3) { fprintf(stderr, "usage: render_ms10_ab OUTDIR LABEL\n"); return 2; }
    LABEL1 = argv[2];
    synth_init();
    {
        extern const uint8_t pcm_image_host0[], pcm_image_host1[], pcm_image_host2[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1, pcm_image_host_len2;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
        pcm_store_attach(PCM_AREA_ST2, pcm_image_host2, pcm_image_host_len2);
    }
    factory_install();
    for (int i = 0; i < NSC; i++) render(argv[1], &SC[i]);
    printf("  host cost, 8 voices LP res 80: flavour 2 %.0f, MULTI %.0f ns/sample\n", cost(FLT_MS10), cost(FLT_MULTI));
    return 0;
}
