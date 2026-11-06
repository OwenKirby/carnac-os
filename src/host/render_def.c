/* Plain audition of the definitive PCM list through the real engine (2026-11-05, proto/pcm-handoff): each wave alone,
 * SPREAD at 0 (= SINGLE), filter open, no effects, its phrase at SCAN 0, 64 and 127. Prototype scaffolding, not a test.
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/wt_areas_host.c src/host/pcm_image_def.c src/host/render_def.c -lm -o render_def
 *   render_def OUTDIR                 the phrase at SCAN 0, 64, 127 per wave
 *   render_def OUTDIR NAME [TAG]      one wave, SCAN swept A -> B: a held chord (sustained) or hits stepping */
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/pcm_store.h"
#include "dsp/wavetables.h"

extern const uint8_t pcm_image_def0[], pcm_image_def1[];
extern const uint32_t pcm_image_def0_len, pcm_image_def1_len;
extern const struct { const char *name; uint8_t perc; uint16_t gate_ms; uint8_t n; uint8_t notes[8]; } DEF_TUNES[];
extern const uint32_t DEF_N;

#define RATE 44100u
static int16_t *out;
static uint32_t frames, cap;

static void run(double sec)
{
    static int32_t buf[2 * 64];
    uint32_t n = (uint32_t)(sec * RATE);
    while (n) {
        uint32_t k = n < 64u ? n : 64u;
        synth_render_q15(buf, k);
        for (uint32_t i = 0; i < 2 * k && frames < cap; i++) {
            int32_t s = buf[i];
            out[2 * frames + (i & 1)] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
            if (i & 1) frames++;
        }
        n -= k;
    }
}
static void put_u32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 255, f); }

int main(int argc, char **argv)
{
    char path[512];
    if (argc < 2) { fprintf(stderr, "usage: render_def OUTDIR\n"); return 2; }
    synth_init();
    if (pcm_store_attach(0, pcm_image_def0, pcm_image_def0_len) <= 0 ||
        pcm_store_attach(1, pcm_image_def1, pcm_image_def1_len) <= 0) { fprintf(stderr, "image rejected\n"); return 1; }
    {
        extern const uint8_t wt_area2_host[];
        extern const uint32_t wt_area2_host_len;
        wavetable_area_attach(0, wt_area2_host, wt_area2_host_len);
    }
    if (pcm_wave_count() != DEF_N) { fprintf(stderr, "%u waves, expected %u\n", pcm_wave_count(), DEF_N); return 1; }
    cap = 60u * RATE;
    out = malloc(cap * 4u);
    for (uint32_t w = 0; w < DEF_N; w++) {
        EngineState *e = &engine_state[0];
        live_defaults();
        engine_defaults(e);
        e->arch = VARCH_SPREAD;
        e->oscmix.spread = 0;
        e->osc[0].wavebank = (uint8_t)(64u + w);
        e->filter[0].cutoff = 127; e->filter[0].resonance = 0; e->filter[0].mod_amt = 0;
        e->env[0].attack = 0; e->env[0].decay = DEF_TUNES[w].perc ? 70 : 64;
        e->env[0].sustain = DEF_TUNES[w].perc && argc < 3 ? 0 : 127; e->env[0].release = DEF_TUNES[w].perc ? 70 : 60;
        e->amp.delay_send = 0; e->amp.reverb_send = 0;
        if (argc >= 4 && strstr(argv[3], "verb")) e->amp.reverb_send = 70;   /* a demo with the reverb */
        perf_state.perf_mode = PERF_SINGL_SYNTH;
        perf_state.voice_mode[0] = VMODE_POLY_REUSE;
        synth_restart();
        frames = 0;
        run(0.2);
        char nm[32]; size_t j = 0;
        for (const char *c = DEF_TUNES[w].name; *c && j < sizeof nm - 1; c++)
            nm[j++] = (*c == ' ' || *c == '>') ? '_' : (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
        nm[j] = 0;
        if (argc >= 4 && !strcmp(argv[3], "probeb")) e->osc[0].scan = 127;   /* the probe at SCAN B */
        if (argc >= 4 && !strncmp(argv[3], "probe", 5)) {    /* one hit on an idle voice, then 3 hits 0.15 s apart */
            if (strcmp(nm, argv[2])) continue;
            uint8_t m = DEF_TUNES[w].notes[0];
            synth_midi_3(0x90, m, 100); run(0.6); synth_midi_3(0x80, m, 0); run(1.4);
            for (int k = 0; k < 3; k++) { synth_midi_3(0x90, m, 100); run(0.15); synth_midi_3(0x80, m, 0); run(0.0); }
            run(1.0);
            snprintf(path, sizeof path, "%s/%s_probe.raw", argv[1], nm);
            FILE *f = fopen(path, "wb"); fwrite(out, 4, frames, f); fclose(f);
            continue;
        }
        if (argc >= 4 && strstr(argv[3], "poly")) {    /* 4-note chords at +0, +12, +24 st, at SCAN A then B */
            if (strcmp(nm, argv[2])) continue;
            for (int sc = 0; sc < 2; sc++) {
                e->osc[0].scan = sc ? 127 : 0;
                for (int o = 0; o < 3; o++) {
                    for (uint32_t i = 0; i < DEF_TUNES[w].n && i < 4; i++) synth_midi_3(0x90, DEF_TUNES[w].notes[i] + 12 * o, 100);
                    run(2.2);
                    for (uint32_t i = 0; i < DEF_TUNES[w].n && i < 4; i++) synth_midi_3(0x80, DEF_TUNES[w].notes[i] + 12 * o, 0);
                    run(0.8);
                }
            }
            run(1.0);
            goto write;
        }
        int sweep = argc >= 3;
        if (sweep && strcmp(nm, argv[2])) continue;
        if (sweep && argc >= 4 && strstr(argv[3], "ab")) {   /* A alone, then B alone: a chord held 3.5 s at each end */
            uint32_t nn = DEF_TUNES[w].n < 3u ? DEF_TUNES[w].n : 3u;
            for (int s = 0; s < 2; s++) {
                e->osc[0].scan = s ? 127 : 0;
                run(0.05);
                for (uint32_t i = 0; i < nn; i++) synth_midi_3(0x90, DEF_TUNES[w].notes[i], 100);
                run(3.5);
                for (uint32_t i = 0; i < nn; i++) synth_midi_3(0x80, DEF_TUNES[w].notes[i], 0);
                run(1.5);
            }
            goto write;
        }
        if (sweep && (!DEF_TUNES[w].perc || (argc >= 4 && strstr(argv[3], "hold")))) {              /* chord 1: 1.5 s at A, 8 s A -> B, 1.5 s at B; chord 2 struck at B */
            uint32_t nn = DEF_TUNES[w].n < 3u ? DEF_TUNES[w].n : 3u;
            e->osc[0].scan = 0;
            for (uint32_t i = 0; i < nn; i++) synth_midi_3(0x90, DEF_TUNES[w].notes[i], 100);
            run(1.5);
            for (int k = 0; k <= 400; k++) { e->osc[0].scan = (uint8_t)(k * 127 / 400); run(0.02); }
            run(1.5);
            for (uint32_t i = 0; i < nn; i++) synth_midi_3(0x80, DEF_TUNES[w].notes[i], 0);
            run(1.5);
            for (uint32_t i = 0; i < nn; i++) synth_midi_3(0x90, DEF_TUNES[w].notes[i], 100);   /* struck at B: B's attack */
            run(4.0);
            for (uint32_t i = 0; i < nn; i++) synth_midi_3(0x80, DEF_TUNES[w].notes[i], 0);
            run(2.0);
        } else if (sweep) {                             /* 16 hits, SCAN stepping 0 -> 127 */
            for (int k = 0; k < 16; k++) {
                e->osc[0].scan = (uint8_t)(k * 127 / 15);
                uint8_t m = DEF_TUNES[w].notes[k % DEF_TUNES[w].n];
                if (argc >= 4 && strstr(argv[3], "repeat"))     /* fast repeats: retaken while it still sounds */
                    for (int r = 0; r < 2; r++) { synth_midi_3(0x90, m, 100); run(0.1); synth_midi_3(0x80, m, 0); run(0.02); }
                synth_midi_3(0x90, m, 100); run(DEF_TUNES[w].gate_ms / 1000.0);
                synth_midi_3(0x80, m, 0); run(0.1);
            }
            run(1.0);
        }
        static const uint8_t SCANS[3] = {0, 64, 127};
        for (int s = 0; s < 3 && !sweep; s++) {
            e->osc[0].scan = SCANS[s];
            for (uint32_t i = 0; i < DEF_TUNES[w].n; i++) {
                synth_midi_3(0x90, DEF_TUNES[w].notes[i], 100);
                run(DEF_TUNES[w].gate_ms / 1000.0);
                synth_midi_3(0x80, DEF_TUNES[w].notes[i], 0);
                run(0.1);
            }
            run(1.0);
        }
    write:;
        int32_t peak = 1;
        for (uint32_t i = 0; i < 2 * frames; i++) if (abs(out[i]) > peak) peak = abs(out[i]);
        double g = 29000.0 / peak;
        int sweep2 = argc >= 3;
        if (sweep2) snprintf(path, sizeof path, "%s/%s_SWEEP%s%s.wav", argv[1], nm, argc >= 4 ? "_" : "", argc >= 4 ? argv[3] : "");
        else snprintf(path, sizeof path, "%s/%s_ENGINE.wav", argv[1], nm);
        FILE *f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
        fwrite("RIFF", 1, 4, f); put_u32(f, 36u + frames * 4u); fwrite("WAVEfmt ", 1, 8, f);
        put_u32(f, 16); put_u32(f, 1u | 2u << 16); put_u32(f, RATE); put_u32(f, RATE * 4u); put_u32(f, 4u | 16u << 16);
        fwrite("data", 1, 4, f); put_u32(f, frames * 4u);
        for (uint32_t i = 0; i < 2 * frames; i++) {
            int32_t s = (int32_t)(out[i] * g);
            fputc(s & 255, f); fputc((s >> 8) & 255, f);
        }
        fclose(f);
        printf("%-14s %5.1f s, peak %5.1f dBFS before gain\n", DEF_TUNES[w].name, frames / (double)RATE,
               20.0 * __builtin_log10(peak / 32768.0));
    }
    return 0;
}
