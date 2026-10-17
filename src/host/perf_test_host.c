/* The FM1's PERF TEST (src/hardware/perf_test.c) run on the host: the same step machine, driven by a simulated clock,
 * each 512-frame half rendered as the FM1 does (two 256-frame blocks) and timed with the PC's clock. A half's load is
 * its time against the FM1's deadline scaled by X0X's calibration: 100 % of one FM1 core ~ NS100 ns/sample on this
 * PC (265..300, docs/fm1-measure-checklist.md, Calibration). So the table predicts the device's; the device decides.
 *
 * Also checks what the device can't easily show: the live patches, the performance, the sequencer, the arp and the
 * patch slots come back unchanged, after a full run and after a stop part-way; and with --guard (an absurdly slow
 * scale) every scenario is cut by the overload guard and the run still finishes.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/hardware/perf_test.c src/host/perf_test_host.c -o perf_test_host
 *   perf_test_host [NS100] [--guard]           (default NS100 280) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
static double now_ns(void)
{
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1e9 / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}
#endif
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/wavetables.h"
#if defined(SYNTH_PROF)                             /* the stage profile in the CPU's time-stamp counter's ticks */
#include <x86intrin.h>
uint32_t synth_prof_now(void) { return (uint32_t)__rdtsc(); }
#endif
#include "hardware/perf_test.h"

#define HALF 512u
#define BLOCK 256u
#define FS 44100.0

static int32_t buf[HALF * 2];
static double ns100 = 280.0;

/* a live state no scenario would make: every byte of the patches and lanes a pattern */
static void scribble(void)
{
    uint8_t *p;
    size_t i;
    p = (uint8_t *)engine_state;
    for (i = 0; i < sizeof engine_state; i++) p[i] = (uint8_t)(i * 7u + 3u) & 0x7Fu;
    engine_state[0].arch = VARCH_STACK;
    engine_state[1].arch = VARCH_SPREAD;
    perf_state.int_tempo = 33;
    perf_state.perf_mode = PERF_SPLIT_SYNTH;
    p = (uint8_t *)seq_lanes;
    for (i = 0; i < sizeof seq_lanes; i++) p[i] = (uint8_t)(i * 5u + 1u) & 0x7Fu;
    arp_state.sequence_length = 5;
    patch_bank[7].filter[0].cutoff = 11;
    patch_bank[100].arch = VARCH_BINAURAL;
}

static EngineState s_eng[NLAYER], s_bank[PATCH_SLOTS];
static MasterPerformancePreset s_perf;
static SequencerLane s_lanes[SEQ_NLANE];
static ArpState s_arp;
static void snapshot(void)
{
    memcpy(s_eng, engine_state, sizeof s_eng);
    memcpy(s_bank, patch_bank, sizeof s_bank);
    s_perf = perf_state;
    memcpy(s_lanes, seq_lanes, sizeof s_lanes);
    s_arp = arp_state;
}
static int same(const char *when)
{
    int ok = !memcmp(s_eng, engine_state, sizeof s_eng) && !memcmp(s_bank, patch_bank, sizeof s_bank) &&
             !memcmp(&s_perf, &perf_state, sizeof s_perf) && !memcmp(s_lanes, seq_lanes, sizeof s_lanes) &&
             !memcmp(&s_arp, &arp_state, sizeof s_arp);
    printf("restore after %s: %s\n", when, ok ? "OK" : "FAILED (live state or patch slots changed)");
    return ok;
}

/* the FM1's audio ISR and main loop, interleaved: one main-loop pass per half (the FM1 runs many) */
static uint8_t run_mode;
static double run(uint32_t stop_after_ms)
{
    double sim_ms = 0, ui_next = 0;
    uint32_t page = PT_PAGE_KEEP;
    pt_start(0, run_mode);
    while (pt_state() == PT_RUN) {
        uint32_t ms = (uint32_t)sim_ms, pm;
        double t0, ns;
        uint8_t p = pt_tick(ms);
        if (p != PT_PAGE_KEEP) page = p;
        if (stop_after_ms && ms >= stop_after_ms) {
            pt_stop();
            break;
        }
        t0 = now_ns();
        if (pt_audio_ok()) {
            synth_render_q15(buf, BLOCK);
            synth_render_q15(buf + 2 * BLOCK, BLOCK);
        } else {
            memset(buf, 0, sizeof buf);
        }
        ns = now_ns() - t0;
        pm = (uint32_t)(ns / (HALF * ns100) * 1000.0 + 0.5);
        pt_audio_half(pm, pm > 1000u);
        sim_ms += HALF * 1000.0 / FS;
        if (sim_ms >= ui_next) {                   /* ~60 frames a second, drawn in no time */
            pt_ui_frame(0);
            ui_next += 16.0;
        }
    }
    (void)page;
    return sim_ms;
}

int main(int argc, char **argv)
{
    int guard = 0, fails = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--guard")) guard = 1;
        else ns100 = atof(argv[i]);
    }
    if (guard) ns100 = 1.0;                         /* every half "late": the guard must cut each scenario */
    synth_init();
    scribble();
    snapshot();

    double ms = run(0);
    printf("PERF TEST, host, 100 %% = %.0f ns/sample: %u scenarios, %.1f s simulated\n\n", ns100, pt_count(), ms / 1000);
    printf("%-18s %7s %7s %5s %6s %4s  %s\n", "scenario", "avg %", "peak %", "late", "ns/smp", "vox", "");
    for (uint8_t i = 0; i < pt_count(); i++) {
        const PtResult *r = pt_result(i);
        printf("%-18s %7.1f %7.1f %5u %6.0f %4u  %s%s\n", pt_name(i), r->avg_pm / 10.0, r->peak_pm / 10.0, r->late,
               r->avg_pm * ns100 / 1000.0, r->voices, r->over ? "CUT " : "",
               !r->over && r->avg_pm <= 700 && r->peak_pm < 900 && !r->late ? "pass" : "FAIL");
        if (guard && !r->over) { printf("  ^ not cut by the guard\n"); fails++; }
        if (!guard && i > 0 && !r->voices) { printf("  ^ no voice sounded\n"); fails++; }
    }
    printf("\n");
#if defined(SYNTH_PROF)
    {
        static const char *const ST[SPROF_N] = {"TOTAL", "CTRL", "VOICE", "OSC", "EXTRAS", "FILTER", "AMP", "MIX", "FX", "CLOCK", "SEQ", "LTICK",
                                                  "c.LFO", "c.ENV", "c.OSC", "c.FILT", "c.AMP"};
        printf("one counter read: %u ticks (each stage pays one a lap)\n", (unsigned)pt_lap_cost());
        printf("%-18s", "ticks/sample");
        for (int k = 0; k < SPROF_N; k++) printf(" %6s", ST[k]);
        printf("\n");
        for (uint8_t i = 0; i < pt_count(); i++) {
            const PtResult *r = pt_result(i);
            printf("%-18s", pt_name(i));
            for (int k = 0; k < SPROF_N; k++) printf(" %6u", r->cyc[k]);
            printf("\n");
        }
        printf("\n");
    }
#endif
    fails += !same("a full run");
    fails += pt_state() != PT_DONE;

    run(guard ? 1000u : 9000u);                     /* stopped part-way (third scenario; guard: each is ~0.1 s) */
    fails += !same("a stop part-way");
    fails += pt_state() != PT_STOPPED;

    {   /* PT_WT_RAM: the frames come from the RAM copy during the run, and the pointer table comes back exactly */
        static const int16_t *before[NUM_BANKS * NUM_MORPH_STEPS];
        memcpy(before, wavetable_frames, sizeof before);
        run_mode = PT_WT_RAM;
        run(guard ? 1000u : 9000u);
        fails += pt_mode() != PT_WT_RAM;
        run(0);
        run_mode = 0;
        if (memcmp(before, wavetable_frames, sizeof before)) {
            printf("restore after a RAM-wavetable run: FAILED (wavetable_frames changed)\n");
            fails++;
        } else {
            printf("restore after a RAM-wavetable run (stopped, then full): OK\n");
        }
        fails += !same("a RAM-wavetable run");
    }
    printf("%s\n", fails ? "perf_test_host: FAILED" : "perf_test_host: OK");
    return fails != 0;
}
