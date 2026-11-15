/* Tape memory A/B (2026-11-14): the tape echo's scenes, from gentle to runaway, rendered by builds of fx.c with
 * different tape memories (TAPE_BYTES) and cells (TAPE_MULAW), as WAVs to compare by ear: whether the longer tape (no
 * slowing, so none of the slowed tape's filters, and none of its aliasing) and the squeezed 8-bit cells land somewhere
 * interesting in high feedback.
 *   gcc -O2 [-DTAPE_BYTES=65536u] [-DTAPE_MULAW=1] -Isrc -Isrc/dsp -Isrc/hardware  *       $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) src/host/pcm_images_host.c  *       src/host/render_tape_ab.c -lm -o render_tape_ab
 *   render_tape_ab OUTDIR LABEL       writes OUTDIR/<scene>_<LABEL>.wav, prints peak / RMS and the side's level */
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
static const uint8_t stab[3] = {57, 60, 64};                     /* A minor */
static const uint32_t stab_at[4] = {0u, RATE * 3u / 4u, RATE * 2u, RATE * 2u + RATE / 2u};

typedef struct {
    const char *name;
    uint8_t pp, time, degrade, feedback;                         /* DELAY TIME 47: ~0.375 s, a dotted eighth at 120 */
    uint32_t secs;
} Scene;
static const Scene scenes[] = {
    {"1_factory_pingpong", 1, 47, 60, 75, 12},                  /* CAIRN SPIRIT's echo */
    {"2_pingpong_fb110", 1, 47, 50, 110, 16},                  /* sustaining */
    {"3_pingpong_howl118", 1, 47, 36, 118, 16},                /* a held howl */
    {"4_pingpong_runaway127", 1, 47, 60, 127, 16},             /* flat out */
    {"5_dub_short_fb115", 1, 30, 80, 115, 16},                 /* ~0.24 s, worn */
    {"6_mono_0s75_worn", 0, 95, 100, 105, 16},                 /* a long mono echo, worn tape */
    {"7_pingpong_1s_fb112", 1, 127, 60, 112, 18},              /* the longest: slowed in every variant */
};

static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

/* a short bright SINGLE saw pluck into the delay (and a little reverb) */
static void patch(const Scene *sc)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = VARCH_SINGLE;
    e->amp.delay_send = 90;
    e->amp.reverb_send = 15;
    e->env[0].attack = 0; e->env[0].decay = 45; e->env[0].sustain = 0; e->env[0].release = 30;
    e->filter[0].cutoff = 80; e->filter[0].resonance = 25;
    perf_state.delay_sync = 0;
    perf_state.delay_time = sc->time;
    perf_state.delay_feedback = sc->feedback;
    perf_state.delay_degrade = sc->degrade;
    perf_state.delay_pingpong = sc->pp;
    perf_state.reverb_type = REVTYPE_MOD;
    perf_state.reverb_decay = 80;
    perf_state.reverb_damp = 40;
    fx_cross[FX_X_DLY_REV] = 0;
    fx_cross[FX_X_REV_DLY] = 0;
    synth_restart();
}

static void render(const char *dir, const char *label, const Scene *sc)
{
    char path[512];
    const uint32_t len = RATE * sc->secs;
    snprintf(path, sizeof path, "%s/%s_%s.wav", dir, sc->name, label);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + len * 4u); fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16u); put_u16(f, 1); put_u16(f, 2); put_u32(f, RATE); put_u32(f, RATE * 4u); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, len * 4u);
    patch(sc);
    double sum = 0.0, side = 0.0, peak = 0.0;
    for (uint32_t frame = 0; frame < len; frame += BLOCK) {
        for (int k = 0; k < 4; k++) {
            if (frame <= stab_at[k] && stab_at[k] < frame + BLOCK)
                for (int i = 0; i < 3; i++) synth_midi_3(0x90, stab[i], 110);
            if (frame <= stab_at[k] + RATE / 8u && stab_at[k] + RATE / 8u < frame + BLOCK)
                for (int i = 0; i < 3; i++) synth_midi_3(0x80, stab[i], 0);
        }
        synth_render_q15(buf, BLOCK);
        for (uint32_t k = 0; k < BLOCK; k++) {
            int32_t l = buf[2u * k], r = buf[2u * k + 1u];
            double a = fabs((double)l) / 32768.0, b = fabs((double)r) / 32768.0, d = (double)(l - r) / 65536.0;
            sum += a * a + b * b;
            side += d * d;
            if (a > peak) peak = a;
            if (b > peak) peak = b;
            put_u16(f, (uint16_t)(int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l));
            put_u16(f, (uint16_t)(int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r));
        }
    }
    fclose(f);
    printf("  %-18s %-6s peak %6.1f dBFS  rms %6.1f  side %6.1f dBFS\n", sc->name, label, 20.0 * log10(peak + 1e-9),
           10.0 * log10(sum / (2.0 * len) + 1e-12), 10.0 * log10(2.0 * side / len + 1e-12));
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: render_tape_ab OUTDIR LABEL\n"); return 2; }
    synth_init();
    {
        extern const uint8_t pcm_image_host0[], pcm_image_host1[], pcm_image_host2[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1, pcm_image_host_len2;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
        pcm_store_attach(PCM_AREA_ST2, pcm_image_host2, pcm_image_host_len2);
    }
    factory_install();
    for (unsigned s = 0; s < sizeof scenes / sizeof scenes[0]; s++) render(argv[1], argv[2], &scenes[s]);
    return 0;
}
