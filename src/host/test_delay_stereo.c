/* Host test: the stereo delay keeps the sides apart (2026-11-24). Each side of the send feeds its own line, so a note on
 * one side echoes only there, a centred note echoes on both, and PING PONG carries one side's echoes across to the other. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "PresetStructures.h"
#include "dsp/fx.h"

#define N (44100 * 2)
#define BLK 128
static int32_t tone[N], silent[N], ol[N], orr[N];

static double peak(const int32_t *x, int from)
{
    double p = 0.0;
    for (int i = from; i < N; i++) {
        double a = fabs((double)x[i]);
        if (a > p) p = a;
    }
    return p;
}

static void run(int pp, int fb, const int32_t *a, const int32_t *b)
{
    perf_defaults(&perf_state);
    static MasterPerformancePreset p;
    memset(&p, 0, sizeof p);
    p.delay_time = 64;
    p.delay_type = 1;                 /* STEREO */
    p.delay_pingpong = (uint8_t)pp;
    p.delay_feedback = (uint8_t)fb;
    fx_init();
    for (int s = 0; s < N; s += BLK) {
        int n = N - s < BLK ? N - s : BLK;
        static int32_t l[BLK], r[BLK];
        fx_delay_block(&p, a + s, b + s, l, r, (uint32_t)n);
        memcpy(ol + s, l, (size_t)n * sizeof l[0]);
        memcpy(orr + s, r, (size_t)n * sizeof r[0]);
    }
}

int main(void)
{
    int fails = 0;
    for (int i = 0; i < N; i++) tone[i] = (int32_t)lround(8000.0 * sin(2.0 * M_PI * 1000.0 * i / 44100.0));
    memset(silent, 0, sizeof silent);

    run(0, 0, tone, silent);          /* left only */
    double pl = peak(ol, 0), pr = peak(orr, 0);
    printf("left only:   left %8.0f  right %8.0f\n", pl, pr);
    if (!(pl > 1000.0 && pr == 0.0)) { printf("  FAIL: a left-only note must echo on the left alone\n"); fails++; }

    run(0, 0, silent, tone);          /* right only */
    pl = peak(ol, 0); pr = peak(orr, 0);
    printf("right only:  left %8.0f  right %8.0f\n", pl, pr);
    if (!(pr > 1000.0 && pl == 0.0)) { printf("  FAIL: a right-only note must echo on the right alone\n"); fails++; }

    run(0, 0, tone, tone);            /* centred */
    pl = peak(ol, 0); pr = peak(orr, 0);
    printf("centred:     left %8.0f  right %8.0f\n", pl, pr);
    if (!(pl > 1000.0 && pr > 1000.0)) { printf("  FAIL: a centred note must echo on both sides\n"); fails++; }

    run(1, 60, tone, silent);         /* left only, PING PONG: the left's echoes must reach the right, later */
    double late_r = peak(orr, N / 2), late_l = peak(ol, N / 2);
    printf("ping pong:   after 1 s left %8.0f  right %8.0f\n", late_l, late_r);
    if (!(late_r > 1000.0)) { printf("  FAIL: PING PONG must carry the left echoes to the right\n"); fails++; }

    printf(fails ? "%d stereo delay checks failed\n" : "all stereo delay checks passed\n", fails);
    return fails != 0;
}
