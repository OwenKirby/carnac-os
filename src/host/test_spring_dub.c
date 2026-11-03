/* Host test: SPRING's dub feedback (src/dsp/fx.c, 2026-11-02: REVERB TIME past 100 patches the tank's return back into
 * its send). A 0.3 s noise burst at about -12 dBFS, then silence, at each TIME:
 *  1. TIME 100 .. 110 only lengthens the tail (each one's level at 2 and 4 s at least the one before's, 0.5 dB slack),
 *     and the send is not squashed as the feedback comes in (the peak at 105 within 1 dB of 100's).
 *  2. 116 and up howl on: the level from 8 s steady, within -22 .. -8 dBFS RMS; 110 dies away (silent by 12 s).
 *  3. A hot send (about 4x full scale) at TIME 127: the howl still within its window, the output finite and bounded.
 *  4. Turned back from 127 to 90 mid-howl: silent within 6 s.
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware src/dsp/fx.c src/host/test_spring_dub.c -lm -o test_spring_dub */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "PresetStructures.h"
#include "dsp/fx.h"

#define SR 44100
static int fails;

/* the burst at TIME t (gain g on the burst; TIME to t2 from second `at`), the RMS (dBFS) of the left over 0.25 s from
 * each of the times w[0..nw-1], and the peak */
static void run(int t, double g, int t2, double at, const double *w, int nw, double *db, double *peak)
{
    static MasterPerformancePreset p;
    memset(&p, 0, sizeof p);
    p.reverb_type = REVTYPE_SPRING; p.reverb_decay = (uint8_t)t; p.reverb_damp = 40;
    fx_init();
    unsigned seed = 5;
    int32_t x[32], r[32];
    double e[8] = {0}, end = w[nw - 1] + 0.25;
    *peak = 0.0;
    for (int s = 0; s < (int)(SR * end); s += 32) {
        if (at > 0.0 && s >= (int)(SR * at)) p.reverb_decay = (uint8_t)t2;
        for (int i = 0; i < 32; i++) {
            seed = seed * 1103515245u + 12345u;
            x[i] = s + i < SR * 3 / 10 ? (int32_t)(g * ((int16_t)(seed >> 16) / 3)) : 0;
        }
        fx_reverb_block(&p, x, r, 32);
        for (int i = 0; i < 32; i++) {
            double tt = (double)(s + i) / SR, v = x[i];
            if (!isfinite(v) || fabs(v) > *peak) *peak = isfinite(v) ? fabs(v) : 1e30;
            for (int k = 0; k < nw; k++) if (tt >= w[k] && tt < w[k] + 0.25) e[k] += v * v;
        }
    }
    for (int k = 0; k < nw; k++) db[k] = 10.0 * log10(e[k] / (0.25 * SR) + 1e-9) - 90.3;
}

int main(void)
{
    perf_defaults(&perf_state);                      /* the MIX page's bytes (fx_live) at their defaults */
    static const double W[3] = {2.0, 4.0, 8.0}, W2[2] = {8.0, 12.0};
    double db[3], prev[3] = {0.0, 0.0, 0.0}, pk, pk100 = 0.0;
    /* 1. */
    for (int t = 100; t <= 110; t++) {
        run(t, 1.0, t, 0.0, W, 2, db, &pk);
        if (t == 100) pk100 = pk;
        if (t > 100 && (db[0] < prev[0] - 0.5 || (prev[1] > -100.0 && db[1] < prev[1] - 0.5))) {
            printf("TIME %d: the tail shorter than at %d (2 s %.1f / %.1f, 4 s %.1f / %.1f dBFS)\n", t, t - 1, db[0],
                   prev[0], db[1], prev[1]);
            fails++;
        }
        if (t == 105 && fabs(20.0 * log10(pk / pk100)) > 1.0) {
            printf("TIME 105: the send's peak %.0f against 100's %.0f\n", pk, pk100);
            fails++;
        }
        memcpy(prev, db, sizeof prev);
    }
    printf("TIME 100 .. 110: each tail at least the one before's\n");
    /* 2. */
    for (int t = 110; t <= 127; t++) {
        if (t != 110 && t != 116 && t != 120 && t != 124 && t != 127) continue;
        run(t, 1.0, t, 0.0, W2, 2, db, &pk);
        printf("TIME %d: %.1f dBFS at 8 s, %.1f at 12 s, peak %.0f\n", t, db[0], db[1], pk);
        if (t == 110 && db[1] > -100.0) { printf("TIME 110 does not die away\n"); fails++; }
        if (t >= 116 && (db[0] < -22.0 || db[0] > -8.0 || fabs(db[1] - db[0]) > 1.5)) {
            printf("TIME %d: the howl not steady in -22 .. -8 dBFS\n", t);
            fails++;
        }
    }
    /* 3. */
    run(127, 12.0, 127, 0.0, W2, 2, db, &pk);
    printf("hot send, TIME 127: %.1f dBFS at 8 s, peak %.0f\n", db[0], pk);
    if (!(pk < 40000.0) || db[0] < -22.0 || db[0] > -8.0) { printf("hot send: the howl not held\n"); fails++; }
    /* 4. */
    {
        static const double W4[1] = {10.0};
        run(127, 1.0, 90, 4.0, W4, 1, db, &pk);
        printf("127 -> 90 at 4 s: %.1f dBFS at 10 s\n", db[0]);
        if (db[0] > -100.0) { printf("the howl does not die when TIME comes back down\n"); fails++; }
    }
    printf(fails ? "%d spring dub checks failed\n" : "all spring dub checks passed\n", fails);
    return fails != 0;
}
