/* Host test: the effects' cross-feeds (src/dsp/fx.c, 2026-11-02: DLY > REV and REV > DLY, fx_cross). A 0.3 s noise
 * burst at about -12 dBFS into both sends, then silence, through fx_process_block (the render's order), every reverb
 * type. "Its floor": the same settings with both amounts at 0 (the tape's own hiss, the tails), at the same moment.
 *  1. One way alone (each at 127, DELAY FEEDBACK 80, REVERB TIME 90): at 25 s settled (XF_QUIET) (a
 *     one-way send cannot hold itself up).
 *  2. Both at 127, the same settings: the loop blooms and holds, steady (16 s against 20 s within 2 dB) within
 *     XF_LO .. XF_HI dBFS RMS; a hot send (about 4x full scale) settles at the same level (within 2 dB) and bounded.
 *  3. Both at 127 with the longest settings (FEEDBACK 100, TIME 100): bounded, no louder than XF_TOP (SPRING at its
 *     longest stacks three loops: about -7 dBFS, the loudest).
 *  4. Both pulled back to 0 at 10 s mid-bloom: at 30 s settled.
 *  5. (2026-11-03) LOOP TONE's wear and the stereo DLY > REV: at tones 0, 24, 100, 127 with PING PONG, 3. bounded and
 *     4. settled again.
 *  6. (2026-11-03) the stereo BBD (its two lines, REV > DLY in stereo), PING PONG off and on: 3. bounded and 4. settled
 *     again.
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware src/dsp/fx.c src/host/test_fx_cross.c -lm -o test_fx_cross */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "PresetStructures.h"
#include "dsp/fx.h"

#define SR 44100
#define XF_LO -26.0
#define XF_HI -10.0
#define XF_TOP -6.0
/* settled: no more than 3 dB over its floor, or under XF_QUIET (2026-11-03: the floor is the tape's own residue,
 * ~-54 dBFS, and the slowed tape's anti-aliasing took it ~2 dB lower while the loops' tails stayed where they were) */
#define XF_QUIET -50.0
static int fails;
static int g_tone = 64, g_pp, g_bbd;                 /* LOOP TONE, PING PONG, the BBD for run() */
static const char *const TN[REVTYPE_COUNT] = {"SPRING", "TOMB", "MOD", "CELESTIAL", "DOLMEN", "TUMULUS"};

/* the burst (gain g) with the amounts a, b; at `off` s (if > 0) both go to 0. RMS (dBFS) of the wet's left over
 * 0.5 s from each w[k]; the peak */
static void run(int type, int fbk, int time, int a, int b, double g, double off, const double *w, int nw, double *db,
                double *peak)
{
    static MasterPerformancePreset p;
    memset(&p, 0, sizeof p);
    p.reverb_type = (uint8_t)type; p.reverb_decay = (uint8_t)time; p.reverb_damp = 40;
    p.delay_time = 59; p.delay_feedback = (uint8_t)fbk; p.delay_degrade = 36;
    fx_live[FX_L_TONE] = (uint8_t)g_tone;            /* (its thinner half DELAY HP's bits, as fx_tone_sync sets them) */
    p.delay_type = (uint8_t)(g_bbd ? DELAYTYPE_BBD : DELAYTYPE_TAPE);
    p.delay_pingpong = (uint8_t)(g_pp | (g_tone > 64 ? ((g_tone - 64) * 127 / 63) << 1 : 0));
    fx_cross[FX_X_DLY_REV] = (uint8_t)a;
    fx_cross[FX_X_REV_DLY] = (uint8_t)b;
    fx_init();
    unsigned seed = 5;
    int32_t d[128], r[128], wr[128];
    double e[4] = {0}, end = w[nw - 1] + 0.5;
    *peak = 0.0;
    for (int s = 0; s < (int)(SR * end); s += 128) {
        if (off > 0.0 && s >= (int)(SR * off)) fx_cross[0] = fx_cross[1] = 0;
        for (int i = 0; i < 128; i++) {
            seed = seed * 1103515245u + 12345u;
            d[i] = r[i] = s + i < SR * 3 / 10 ? (int32_t)(g * ((int16_t)(seed >> 16) / 3)) : 0;
        }
        fx_process_block(&p, d, r, wr, 128);
        for (int i = 0; i < 128; i++) {
            double t = (double)(s + i) / SR, v = r[i];
            if (!(fabs(v) <= *peak)) *peak = isfinite(v) ? fabs(v) : 1e30;
            for (int k = 0; k < nw; k++) if (t >= w[k] && t < w[k] + 0.5) e[k] += v * v;
        }
    }
    for (int k = 0; k < nw; k++) db[k] = 10.0 * log10(e[k] / (0.5 * SR) + 1e-9) - 90.3;
    fx_cross[0] = fx_cross[1] = 0;
}

int main(void)
{
    perf_defaults(&perf_state);                      /* the MIX page's bytes (fx_live) at their defaults */
    static const double W1[1] = {25.0}, W2[2] = {16.0, 20.0}, W4[1] = {30.0};
    double db[2], hot[2], floor_db[2], pk;
    for (int t = 0; t < REVTYPE_COUNT; t++) {
        /* 1. */
        run(t, 80, 90, 0, 0, 1.0, 0.0, W1, 1, floor_db, &pk);
        for (int way = 0; way < 2; way++) {
            run(t, 80, 90, way ? 0 : 127, way ? 127 : 0, 1.0, 0.0, W1, 1, db, &pk);
            if (db[0] > floor_db[0] + 3.0 && db[0] > XF_QUIET) {
                printf("%s: %s alone holds itself up (%.1f dBFS at 25 s, floor %.1f)\n", TN[t],
                       way ? "REV > DLY" : "DLY > REV", db[0], floor_db[0]);
                fails++;
            }
        }
        /* 2. */
        run(t, 80, 90, 127, 127, 1.0, 0.0, W2, 2, db, &pk);
        run(t, 80, 90, 127, 127, 12.0, 0.0, W2, 2, hot, &pk);
        printf("%-9s both at 127: %6.1f dBFS at 16 s, %6.1f at 20 s; hot send %6.1f, peak %.0f", TN[t], db[0], db[1],
               hot[1], pk);
        if (db[0] < XF_LO || db[0] > XF_HI || fabs(db[1] - db[0]) > 2.0 || fabs(hot[1] - db[1]) > 2.0 || !(pk < 70000.0)) {
            printf("  << not a held bloom in %.0f .. %.0f dBFS", XF_LO, XF_HI);
            fails++;
        }
        /* 3. */
        run(t, 100, 100, 127, 127, 1.0, 0.0, W2, 2, db, &pk);
        printf("; longest %6.1f dBFS, peak %.0f\n", db[1], pk);
        if (db[1] > XF_TOP || !(pk < 70000.0)) { printf("%s: the longest settings' loop not held\n", TN[t]); fails++; }
        /* 4. */
        run(t, 80, 90, 0, 0, 1.0, 0.0, W4, 1, floor_db, &pk);
        run(t, 80, 90, 127, 127, 1.0, 10.0, W4, 1, db, &pk);
        if (db[0] > floor_db[0] + 3.0 && db[0] > XF_QUIET) {
            printf("%s: pulled back to 0, the loop holds on (%.1f dBFS at 30 s, floor %.1f)\n", TN[t], db[0], floor_db[0]);
            fails++;
        }
    }
    /* 5. */
    static const int tones[4] = {0, 24, 100, 127};
    g_pp = 1;
    for (int t = 0; t < REVTYPE_COUNT; t++)
        for (int k = 0; k < 4; k++) {
            g_tone = tones[k];
            run(t, 100, 100, 127, 127, 12.0, 0.0, W2, 2, db, &pk);
            if (db[1] > XF_TOP || !(pk < 70000.0)) {
                printf("%s tone %d: the worn loop not held (%.1f dBFS, peak %.0f)\n", TN[t], g_tone, db[1], pk);
                fails++;
            }
            run(t, 80, 90, 0, 0, 1.0, 0.0, W4, 1, floor_db, &pk);
            run(t, 80, 90, 127, 127, 1.0, 10.0, W4, 1, db, &pk);
            if (db[0] > floor_db[0] + 3.0 && db[0] > XF_QUIET) {
                printf("%s tone %d: pulled back to 0, the worn loop holds on (%.1f dBFS, floor %.1f)\n", TN[t], g_tone,
                       db[0], floor_db[0]);
                fails++;
            }
        }
    g_tone = 64; g_pp = 0;
    /* 6. */
    g_bbd = 1;
    for (int t = 0; t < REVTYPE_COUNT; t++)
        for (g_pp = 0; g_pp < 2; g_pp++) {
            run(t, 100, 100, 127, 127, 12.0, 0.0, W2, 2, db, &pk);
            if (db[1] > XF_TOP || !(pk < 70000.0)) {
                printf("%s BBD pp %d: the loop not held (%.1f dBFS, peak %.0f)\n", TN[t], g_pp, db[1], pk);
                fails++;
            }
            run(t, 80, 90, 0, 0, 1.0, 0.0, W4, 1, floor_db, &pk);
            run(t, 80, 90, 127, 127, 1.0, 10.0, W4, 1, db, &pk);
            if (db[0] > floor_db[0] + 3.0 && db[0] > XF_QUIET) {
                printf("%s BBD pp %d: pulled back to 0, the loop holds on (%.1f dBFS, floor %.1f)\n", TN[t], g_pp,
                       db[0], floor_db[0]);
                fails++;
            }
        }
    g_bbd = 0; g_pp = 0;
    printf("worn (LOOP TONE 0, 24, 100, 127, PING PONG): bounded, and settles when pulled back\n");
    printf("the stereo BBD (PING PONG off, on): bounded, and settles when pulled back\n");
    printf("one way alone settles to its floor; pulled back to 0, the loop does too\n");
    printf(fails ? "%d cross-feed checks failed\n" : "all cross-feed checks passed\n", fails);
    return fails != 0;
}
