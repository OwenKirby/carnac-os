/* Incremental host cost of one voice per architecture (spec 4.0 Voice Caps): time with 1 and 4 held voices, slope =
 * per-voice cost, and the cap that would hold each architecture to SINGLE x 8 voices. Best of several runs to cut
 * scheduler noise. A cap table check on the host only; the FM1 numbers must come from profiling on the FM1.
 * Each filter flavour is timed in turn (both filters set to it, resonance 90).
 * Note: the rows of STACK and BINAURAL read low whenever their cap is under 4 (fewer voices sound).
 * Test scaffolding only.
 *
 *   gcc -Os -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/bench_voice_arch.c -o bench_voice_arch */
#include <stdio.h>
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

static int32_t buf[64 * 2];
static const char *ARCH[] = {"SINGLE", "DUAL_OSC", "SPREAD", "DUAL_FILTER", "STACK", "BINAURAL", "STRIKE"};

static double time_voices(uint8_t arch, int n, int flavour)
{
    double best = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        synth_init();
        EngineState *e = &engine_state[0];
        e->arch = arch;
        e->env[0].sustain = 100;
        e->env[1].sustain = 100;
        e->oscmix.spread = 60;
        e->oscmix.xmod_amt = 20;
        e->filter[0].mod_src = MSRC_LFO1;
        e->filter[0].mod_amt = 100;
        e->filter[0].flavour = (uint8_t)flavour;
        e->filter[1].flavour = (uint8_t)flavour;
        e->filter[0].resonance = 90;
        e->filter[1].resonance = 90;
        for (int i = 0; i < n; i++) synth_midi_3(0x90, (uint8_t)(48 + i * 3), 100);
        for (int i = 0; i < 64; i++) synth_render_q15(buf, 64);
        double t0 = now_ns();
        for (int i = 0; i < 44100 * 6 / 64; i++) synth_render_q15(buf, 64);
        double ns = (now_ns() - t0) / (44100.0 * 6);
        if (ns < best) best = ns;
    }
    return best;
}

int main(void)
{
    double ref = 0;
    printf("%-12s %8s %8s %8s %10s %12s\n", "arch", "1v ns", "4v ns", "ns/voice", "x SINGLE", "fair cap");
    static const char *const FLV[FLT_COUNT] = {" (sh-2)", " (multi)", " (ms-20)", " (formant)"};
    for (int flv = 0; flv < FLT_COUNT; flv++) {
        for (uint8_t a = 0; a < VARCH_COUNT; a++) {
            double t1 = time_voices(a, 1, flv), t4 = time_voices(a, 4, flv);
            double per = (t4 - t1) / 3.0;
            if (a == 0 && !flv) ref = per;
            printf("%-12s %8.1f %8.1f %8.1f %10.2f %12.1f%s\n", ARCH[a], t1, t4, per, per / ref, 8.0 * ref / per,
                   FLV[flv]);
        }
    }
    return 0;
}
