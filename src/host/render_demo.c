/* Desktop listening prototype: drives the real synth core with a scripted note pattern and a MIDI clock, and
 * writes the result to a 16-bit stereo WAV.
 *
 *   render_demo <out.wav> <internal|midi> [bpm] [bars]
 *
 *   internal : no MIDI clock is sent; the synth runs its own clock at the BPM you give (INT TEMPO)
 *   midi     : the synth follows 0xF8 pulses at the BPM you give (CLOCK SOURCE = MIDI)
 *
 * The demo patch: LFO1 grid-synced to 1/8 pitch-modulating OSC1, and ENV2 auto-retriggering on a 1/16 grid
 * sweeping the filter. The synth itself is integer-only; this host file is test scaffolding. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "PresetStructures.h"
#include "preset.h"
#include "hardware/audio_hal.h"
#include "dsp/synth_runtime.h"
#include "dsp/midi_clock.h"

#define RATE 44100u
#define BLOCK 32u

/* the synth's own sample buffer is interleaved Q15 stereo */
static int32_t block_buf[BLOCK * 2u];

static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

static void write_wav_header(FILE *f, uint32_t frames)
{
    uint32_t data = frames * 4u;
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + data); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put_u32(f, 16u); put_u16(f, 1); put_u16(f, 2);
    put_u32(f, RATE); put_u32(f, RATE * 4u); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, data);
}

/* a short scripted pattern: C3 E3 G3 B3 on eighth notes, held for 3/4 of each step so ENV2 retriggers
 * inside the held note */
static const uint8_t pattern[8] = { 48, 52, 55, 59, 48, 55, 52, 60 };

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: render_demo <out.wav> <internal|midi> [bpm] [bars]\n");
        return 1;
    }
    int use_midi = strcmp(argv[2], "midi") == 0;
    int bpm = argc > 3 ? atoi(argv[3]) : 120;
    int bars = argc > 4 ? atoi(argv[4]) : 8;
    if (bpm < 40 || bpm > 167) { fprintf(stderr, "bpm must be 40..167\n"); return 1; }

    synth_init();

    /* ---- the patch ---- */
    EngineState *e = &engine_state[0];
    e->osc[0].pitch_mod_src = MSRC_LFO1;
    e->osc[0].pitch_mod_amt = 48;                 /* LFO1 pitch depth */
    e->lfo[0].clock_sync = 1;                     /* synced to the clock */
    e->lfo[0].rate = 56;                          /* division 3 = 1/8 */
    e->lfo[0].shape = 0;                          /* sine */
    e->lfo[0].speed_mod_amt = BIPOLAR_CENTER;     /* no speed modulation */
    e->lfo[0].key_reset = 0;                      /* grid-locked: cycles on the bar grid */
    e->filter[0].mod_src = MSRC_ENV2;
    e->filter[0].mod_amt = 72;
    e->filter[0].cutoff = 60;
    e->env[1].attack = 0;
    e->env[1].decay = 60;
    e->env[1].sustain = 0;
    e->env[1].retrig_rate = 80;                   /* division 5 = 1/16 */
    e->env[1].retrig_sync = 1;
    e->env[0].attack = 0; e->env[0].decay = 40; e->env[0].sustain = 90; e->env[0].release = 40;

    perf_state.clock_source = use_midi ? CLKSRC_MIDI : CLKSRC_INTERNAL;
    perf_state.int_tempo = (uint8_t)(bpm - 40);   /* INT TEMPO: BPM = 40 + value */

    /* ---- the timeline, in frames ---- */
    uint32_t frames_per_beat = RATE * 60u / (uint32_t)bpm;
    uint32_t frames_per_tick_num = RATE * 60u, frames_per_tick_den = (uint32_t)bpm * 24u;
    uint32_t total = bars * 4u * frames_per_beat;
    uint32_t step = frames_per_beat / 2u;          /* eighth notes */
    uint32_t n_steps = bars * 8u;

    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror("fopen"); return 1; }
    write_wav_header(f, total);

    if (use_midi) synth_midi_3(0xFA, 0, 0);       /* Start */
    uint32_t tick_k = 0, next_step = 0, held_note = 0xFFFFFFFFu;
    uint32_t frame = 0;
    while (frame < total) {
        /* MIDI clock pulses that fall inside this block (sent before it renders) */
        if (use_midi) {
            while ((uint64_t)tick_k * frames_per_tick_num / frames_per_tick_den <= frame + BLOCK) {
                synth_midi_3(0xF8, 0, 0);
                tick_k++;
            }
        }
        /* note events on the eighth-note grid, note off at 3/4 of the step */
        if (next_step < n_steps && next_step * step <= frame) {
            if (held_note != 0xFFFFFFFFu) synth_midi_3(0x80, (uint8_t)held_note, 0);
            uint8_t n = pattern[next_step % 8];
            synth_midi_3(0x90, n, 96);
            held_note = n;
            next_step++;
        }
        if (held_note != 0xFFFFFFFFu && next_step > 0 && (next_step - 1) * step + (step * 3u) / 4u <= frame) {
            synth_midi_3(0x80, (uint8_t)held_note, 0);
            held_note = 0xFFFFFFFFu;
        }

        synth_render_q15(block_buf, BLOCK);
        for (uint32_t i = 0; i < BLOCK * 2u; i++) {
            put_u16(f, (uint16_t)(int16_t)block_buf[i]);   /* the core's own level: limited to -1 dBFS */
        }
        frame += BLOCK;
    }
    fclose(f);
    printf("wrote %s: %u frames (%.1f s), clock=%s, %d BPM, %d bars\n", argv[1], total,
           (double)total / RATE, use_midi ? "midi" : "internal", bpm, bars);
    return 0;
}
