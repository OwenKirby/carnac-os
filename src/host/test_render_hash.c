/* Host test: render hashes, the bit-identical check for changes to the render's structure (docs/dual-core-plan.md).
 * Renders every voice architecture with and without noise and mod slots (S&H, LAG, chained), two layers, and the MAQ +
 * ARP with clock-synced LFOs and ENV2 retriggers on the internal and the MIDI clock, at block sizes 7 / 32 / 128, and
 * hashes the output (FNV-1a over the Q15 samples). Each line is compared with src/host/render_hash_golden.txt.
 * Exit code: the number of lines that differ. Test scaffolding only.
 *
 * A change meant to keep the sound (the block-major render, a second core) must leave every line as it is. A change
 * meant to alter it: check by ear that it is right, then rewrite the golden file with --write and say why.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/test_render_hash.c -o test_render_hash
 *   test_render_hash [--write] [golden file]     (default src/host/render_hash_golden.txt, run from the repo root) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/voice_allocator.h"
#include "dsp/maq_seq.h"

/* --core2: a stand-in second core (synth_set_core2) that runs the job when it is waited for, as the FM1's does after
 * core 0's share; --core2-fail: one that takes the job and fails, so core 0 does it. Same hashes either way. */
static void (*fake_fn)(uint32_t);
static uint32_t fake_arg;
static int fake_fails, fake_jobs;
static int fake_run(void (*fn)(uint32_t), uint32_t arg) { fake_fn = fn; fake_arg = arg; return 1; }
static int fake_wait(void)
{
    fake_jobs++;
    if (fake_fails) return -1;
    fake_fn(fake_arg);
    return 0;
}

static int32_t buf[128 * 2];
static uint32_t h;
static FILE *golden;
static int writing, fails, lines;

/* one scenario's result: write it, or compare it with the golden file's next line */
static void report(const char *line)
{
    char want[128];
    lines++;
    if (writing) {
        fprintf(golden, "%s\n", line);
        return;
    }
    if (!fgets(want, sizeof want, golden)) want[0] = 0;
    want[strcspn(want, "\r\n")] = 0;
    if (strcmp(want, line) != 0) {
        printf("DIFF: got \"%s\", golden \"%s\"\n", line, want);
        fails++;
    }
}

static void hrender(uint32_t frames, uint32_t block)
{
    while (frames) {
        uint32_t n = frames < block ? frames : block;
        synth_render_q15(buf, n);
        for (uint32_t i = 0; i < 2 * n; i++) { h ^= (uint32_t)buf[i]; h *= 16777619u; }
        frames -= n;
    }
}

static void patch(EngineState *e, uint8_t arch, int noise, int slots)
{
    e->arch = arch;
    e->env[0].attack = 0; e->env[0].sustain = 100; e->env[0].release = 30;
    e->env[1].attack = 10; e->env[1].sustain = 60; e->env[1].release = 30;
    e->oscmix.spread = 60; e->oscmix.xmod_amt = 20;
    e->fltmix.stack_width = 127;
    e->osc[1].wavebank = 3;
    e->filter[0].cutoff = 70; e->filter[1].cutoff = 90;
    e->lfo[0].rate = 70; e->lfo[1].rate = 90; e->lfo[2].rate = 40;
    e->amp.delay_send = 50; e->amp.reverb_send = 60;
    if (noise) { e->oscmix.noise_level = 100; e->oscmix.noise_colour = (uint8_t)(noise * 40); }
    if (slots) {
        e->mod[0] = (ModSlotParams){MSRC_LFO2, MFN_SAMPLE_B_ON_A, MSRC_LFO3, 120};
        e->mod[1] = (ModSlotParams){MSRC_MODSLOT1, MFN_LAG, MSRC_ENV2, 100};
        e->mod[2] = (ModSlotParams){MSRC_MODSLOT2, MFN_MUL, MSRC_LFO1, 110};
        e->filter[0].mod_src = MSRC_MODSLOT3; e->filter[0].mod_amt = 120;
    } else {
        e->filter[0].mod_src = MSRC_LFO1; e->filter[0].mod_amt = 120;
    }
}

/* 8 staggered notes, the mod wheel, half of them released, then all */
static void play(uint32_t block)
{
    for (int i = 0; i < 8; i++) { synth_midi_3(0x90, (uint8_t)(48 + i * 3), (uint8_t)(70 + i * 7)); hrender(37, block); }
    synth_midi_3(0xB0, 1, 90);
    hrender(20000, block);
    for (int i = 0; i < 8; i += 2) { synth_midi_3(0x80, (uint8_t)(48 + i * 3), 0); hrender(301, block); }
    hrender(8000, block);
    for (int n = 0; n < 128; n++) synth_midi_3(0x80, (uint8_t)n, 0);
    hrender(30000, block);
}

int main(int argc, char **argv)
{
    static const uint32_t blocks[3] = {7, 32, 128};
    const char *path = "src/host/render_hash_golden.txt";
    char line[128];
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--write") == 0) writing = 1;
        else if (strcmp(argv[i], "--core2") == 0) synth_set_core2(fake_run, fake_wait);
        else if (strcmp(argv[i], "--core2-fail") == 0) { fake_fails = 1; synth_set_core2(fake_run, fake_wait); }
        else path = argv[i];
    }
    golden = fopen(path, writing ? "w" : "r");
    if (!golden) { printf("cannot open %s\n", path); return 1; }

    for (uint8_t a = 0; a < VARCH_COUNT; a++)
        for (int noise = 0; noise < 3; noise++)
            for (int slots = 0; slots < 2; slots++) {
                h = 2166136261u;
                synth_init();
                patch(&engine_state[0], a, noise, slots);
                play(blocks[(a + noise + slots) % 3]);
                snprintf(line, sizeof line, "arch %u noise %d slots %d  %08x", a, noise, slots, h);
                report(line);
            }

    /* two layers, different architectures, both with noise and slots */
    h = 2166136261u;
    synth_init();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    patch(&engine_state[0], VARCH_DUAL_OSC, 1, 1);
    patch(&engine_state[1], VARCH_BINAURAL, 2, 1);
    play(32);
    snprintf(line, sizeof line, "dual layer  %08x", h);
    report(line);

    /* MAQ + ARP + clock-synced LFOs / ENV2 retrigger: events land mid-block; internal then MIDI clock */
    for (int ext = 0; ext < 2; ext++)
        for (int bi = 0; bi < 3; bi++) {
            h = 2166136261u;
            synth_init();
            EngineState *e = &engine_state[0];
            patch(e, bi == 2 ? VARCH_BINAURAL : VARCH_SINGLE, 1, 1);
            e->lfo[1].clock_sync = 1; e->lfo[1].key_reset = 0; e->lfo[1].rate = 3 << 4;
            e->lfo[2].clock_sync = 1; e->lfo[2].key_reset = 1; e->lfo[2].rate = 5 << 4;
            e->lfo[0].clock_sync = 1; e->lfo[0].rate = 4 << 4;
            e->env[1].retrig_sync = 1; e->env[1].retrig_rate = 5 << 4;
            perf_state.int_tempo = 97;
            if (ext) perf_state.clock_source = CLKSRC_MIDI;
            seq_lane_set_event(&seq_lanes[0], SEQEV_NOTE);
            seq_lane_set_event(&seq_lanes[1], SEQEV_MODWHEEL);
            seq_lane_set_event(&seq_lanes[3], SEQEV_RATCHET);
            for (int s = 0; s < (int)SEQ_NSTEP; s++) {
                seq_lanes[0].steps[s].note_buffer[0] = (uint8_t)(48 + (s * 5) % 17);
                seq_lanes[1].steps[s].value = (uint8_t)(s * 8);
                seq_lanes[3].steps[s].value = (uint8_t)((s % 4 == 3) ? 40 : 0);
            }
            for (int s = 0; s < 3; s++) {
                ArpChordStep *st = &arp_state.steps[s];
                st->chord_root = (uint8_t)(55 + s * 2); st->octave_range = 1;
                st->style = (uint8_t)(ARPSTYLE_UP + s); st->variation = (uint8_t)(s * 3);
            }
            arp_state.sequence_length = 3;
            arp_state.gate_length = 90;              /* (each step's gate before 2026-10-17's RATE; 1/4 steps) */
            arp_state.current_genre_bank = 0;        /* (Pop_1, the default before Phrygian) */
            carp_set_playing(synth_arp(), 1);        /* (its own transport, started with the MAQ) */
            maq_set_playing(synth_seq(), 1);
            uint32_t block = bi == 0 ? 32u : (bi == 1 ? 7u : 128u);
            uint32_t acc = 0;
            for (uint32_t done = 0; done < 44100u * 6u; done += block) {
                if (ext) {                                  /* 0xF8 at 100 BPM: 1102.5 frames per pulse */
                    acc += block * 2u;
                    while (acc >= 2205u) { acc -= 2205u; synth_midi_3(0xF8, 0, 0); }
                    if (done == 0) synth_midi_3(0xFA, 0, 0);
                }
                hrender(block, block);
            }
            maq_set_playing(synth_seq(), 0);
            carp_set_playing(synth_arp(), 0);
            hrender(44100, 32);
            snprintf(line, sizeof line, "seq ext %d block %u  %08x", ext, block, h);
            report(line);
        }

    fclose(golden);
    if (writing)
        printf("wrote %d lines to %s\n", lines, path);
    else if (fails)
        printf("%d of %d render hashes differ\n", fails, lines);
    else
        printf("all %d render hashes match%s\n", lines, fake_jobs ? " (second core stand-in)" : "");
    return fails;
}
