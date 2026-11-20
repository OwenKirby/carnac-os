/* The delay > reverb > delay loop as a drone, and the delay alone across LOOP TONE (2026-11-20): for A/B by ear
 * between builds of fx.c (XF_HALF, TAPE_WEAR): build once per variant and give each its own SUFFIX.
 *   drones: a low chord held a few seconds into both sends, both cross-feeds 127, then the loop left alone (45 s);
 *   echo:   the delay alone (cross-feeds 0), a dub stab phrase at a high FEEDBACK, LOOP TONE across its range (20 s).
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/pcm_images_host.c src/host/render_loop_drone.c -lm -o render_loop_drone
 *   render_loop_drone OUTDIR SUFFIX [echo]      writes OUTDIR/<scene>_<SUFFIX>.wav, prints levels */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "PresetStructures.h"
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/fx.h"
#include "dsp/synth_runtime.h"
#include "dsp/midi_clock.h"
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

typedef struct {
    const char *name;
    uint8_t rev, pp, tone, fbk, decay, cutoff;
    uint8_t notes[4];              /* (0: none) */
    uint32_t hold_ms;              /* the chord's length; with stabs, each stab's */
    uint8_t xf;                    /* both cross-feeds (0: the delay alone) */
    uint8_t stabs;                 /* 1: a dub stab phrase instead of one held chord */
    uint32_t secs;
} Scene;
static const Scene drones[] = {
    /* the tape side: worn, dark, lopsided */
    {"1_tumulus_tape24", REVTYPE_TUMULUS, 1, 24, 82, 95, 70, {33, 40, 45, 48}, 3000, 127, 0, 45},
    {"2_mod_tape8", REVTYPE_MOD, 1, 8, 80, 90, 60, {29, 36, 41, 44}, 2500, 127, 0, 45},
    {"3_spring_tape40", REVTYPE_SPRING, 0, 40, 84, 80, 80, {38, 45, 50, 53}, 1500, 127, 0, 45},
    {"4_dolmen_tape16", REVTYPE_DOLMEN, 1, 16, 85, 100, 55, {31, 38, 43, 46}, 4000, 127, 0, 45},
    /* the desk side: thin, bright, pushed */
    {"5_celestial_desk100", REVTYPE_CELESTIAL, 1, 100, 80, 95, 85, {45, 52, 57, 60}, 2000, 127, 0, 45},
    /* LOOP TONE 64: no wear */
    {"6_tumulus_clean64", REVTYPE_TUMULUS, 1, 64, 82, 95, 70, {33, 40, 45, 48}, 3000, 127, 0, 45},
};
static const Scene echoes[] = {
    {"e1_dub_tone64", REVTYPE_TUMULUS, 1, 64, 96, 60, 90, {57, 60, 64, 0}, 120, 0, 1, 20},
    {"e2_dub_tone40", REVTYPE_TUMULUS, 1, 40, 96, 60, 90, {57, 60, 64, 0}, 120, 0, 1, 20},
    {"e3_dub_tone16", REVTYPE_TUMULUS, 1, 16, 96, 60, 90, {57, 60, 64, 0}, 120, 0, 1, 20},
    {"e4_dub_tone0", REVTYPE_TUMULUS, 1, 0, 96, 60, 90, {57, 60, 64, 0}, 120, 0, 1, 20},
    {"e5_dub_tone100", REVTYPE_TUMULUS, 1, 100, 96, 60, 90, {57, 60, 64, 0}, 120, 0, 1, 20},
    {"e6_dub_tone127", REVTYPE_TUMULUS, 1, 127, 96, 60, 90, {57, 60, 64, 0}, 120, 0, 1, 20},
    {"e7_selfosc_tone24", REVTYPE_TUMULUS, 1, 24, 108, 60, 90, {45, 52, 57, 0}, 400, 0, 1, 30},
};

static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

static void patch(const Scene *sc)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = VARCH_SINGLE;
    e->amp.delay_send = 100;
    e->amp.reverb_send = sc->stabs ? 30 : 70;
    if (sc->stabs) {                                  /* a short bright pluck */
        e->env[0].attack = 0; e->env[0].decay = 45; e->env[0].sustain = 0; e->env[0].release = 30;
    } else {                                          /* a soft saw pad */
        e->env[0].attack = 50; e->env[0].decay = 60; e->env[0].sustain = 100; e->env[0].release = 60;
    }
    e->filter[0].cutoff = sc->cutoff; e->filter[0].resonance = 30;
    perf_state.delay_sync = 0;
    perf_state.delay_time = sc->stabs ? 46 : 70;
    perf_state.delay_feedback = sc->fbk;
    perf_state.delay_degrade = 60;
    perf_state.delay_type = 0;
    fx_live[FX_L_TONE] = sc->tone;                    /* (its thinner half DELAY HP's bits, as fx_tone_sync sets them) */
    perf_state.delay_pingpong = (uint8_t)(sc->pp | (sc->tone > 64 ? ((sc->tone - 64) * 127 / 63) << 1 : 0));
    perf_state.reverb_type = sc->rev;
    perf_state.reverb_decay = sc->decay;
    perf_state.reverb_damp = 45;
    fx_cross[FX_X_DLY_REV] = sc->xf;
    fx_cross[FX_X_REV_DLY] = sc->xf;
    synth_restart();
}

static void notes(const Scene *sc, int on)
{
    for (int i = 0; i < 4; i++)
        if (sc->notes[i]) synth_midi_3(on ? 0x90 : 0x80, sc->notes[i], on ? 105 : 0);
}

static void render(const char *dir, const char *suffix, const Scene *sc)
{
    static const uint32_t stab_at[4] = {0u, RATE * 3u / 4u, RATE * 2u, RATE * 2u + RATE / 2u};
    char path[512];
    const uint32_t len = RATE * sc->secs, hold = RATE / 1000u * sc->hold_ms, tail = RATE * (sc->secs / 3u);
    snprintf(path, sizeof path, "%s/%s_%s.wav", dir, sc->name, suffix);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + len * 4u); fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16u); put_u16(f, 1); put_u16(f, 2); put_u32(f, RATE); put_u32(f, RATE * 4u); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, len * 4u);
    patch(sc);
    if (!sc->stabs) notes(sc, 1);
    double late = 0.0, peak = 0.0;
    for (uint32_t frame = 0; frame < len; frame += BLOCK) {
        if (!sc->stabs && frame <= hold && hold < frame + BLOCK) notes(sc, 0);
        for (int k = 0; sc->stabs && k < 4; k++) {
            if (frame <= stab_at[k] && stab_at[k] < frame + BLOCK) notes(sc, 1);
            if (frame <= stab_at[k] + hold && stab_at[k] + hold < frame + BLOCK) notes(sc, 0);
        }
        synth_render_q15(buf, BLOCK);
        for (uint32_t k = 0; k < BLOCK; k++) {
            int32_t l = buf[2u * k], r = buf[2u * k + 1u];
            double a = fabs((double)l) / 32768.0, b = fabs((double)r) / 32768.0;
            if (frame >= tail) late += a * a + b * b;
            if (a > peak) peak = a;
            if (b > peak) peak = b;
            put_u16(f, (uint16_t)(int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l));
            put_u16(f, (uint16_t)(int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r));
        }
    }
    fclose(f);
    printf("  %-22s %-4s peak %6.1f dBFS  last two thirds %6.1f dBFS RMS\n", sc->name, suffix,
           20.0 * log10(peak + 1e-9), 10.0 * log10(late / (2.0 * (double)(len - tail)) + 1e-12));
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: render_loop_drone OUTDIR SUFFIX [echo]\n"); return 2; }
    const int echo = argc > 3 && !strcmp(argv[3], "echo");
    const Scene *sc = echo ? echoes : drones;
    const unsigned ns = echo ? sizeof echoes / sizeof echoes[0] : sizeof drones / sizeof drones[0];
    synth_init();
    {
        extern const uint8_t pcm_image_host0[], pcm_image_host1[], pcm_image_host2[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1, pcm_image_host_len2;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
        pcm_store_attach(PCM_AREA_ST2, pcm_image_host2, pcm_image_host_len2);
    }
    factory_install();
    for (unsigned s = 0; s < ns; s++) render(argv[1], argv[2], &sc[s]);
    return 0;
}
