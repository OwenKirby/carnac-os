/* Host test: the MFX models (src/dsp/mfx.c).
 *  1. STONE PHASER held still (DEPTH 0, at MANUAL) at three sweep points and three FEEDBACK settings: its steady-state gain at 40
 *     frequencies against its own transfer function, worked out here in complex arithmetic from the component values
 *     (the lean model: B2's gain, M's KCL with N = (M + X) / 2 and X a sample late, five swept all-pass stages, B1's
 *     fixed one, the emphasis pair's shelf): within 0.1 dB where the gain is above -40 dB. Beside it, how far the lean model is from the whole
 *     analog circuit (pre / de-emphasis and the delay-free loop included), off the notches, printed only.
 *  2. Its level: pink noise in and out, the full sweep (MANUAL 64, DEPTH 127), at four FEEDBACK settings: LEVEL_DB within 1 dB (SP_OUT, the makeup:
 *     a pad, its energy where the notches sweep, comes out about 1 dB under its dry level).
 *  3. Full FEEDBACK, the fastest RATE, loud noise for 20 s: finite and bounded.
 *  4. OFF leaves the signal alone.
 *  5. L/R PHASE: 0 = both sides the same; 127 = the right side's sweep half a cycle behind (the sides differ).
 *  6. BULLROARER: its level on pink noise at four settings within 1.5 dB of dry, bounded.
 *  7. ENSEMBLE: the same at five settings; WIDTH 0 leaves the sides alike, 127 sets them apart.
 *   gcc -O2 -Isrc -Isrc/dsp src/dsp/mfx.c src/host/test_mfx.c -lm -o test_mfx */
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "mfx.h"

#define SR 44100.0
#define LEVEL_DB 2.0
#define SP_OUT 1.30                         /* mfx.c's */
static int fails;

/* s for a section with its corner at fk Hz: analog, or its prewarped bilinear map (digital) */
static double complex s_at(double f, double fk, int digital)
{
    if (!digital) return I * 2.0 * M_PI * f;
    return I * 2.0 * M_PI * fk * tan(M_PI * f / SR) / tan(M_PI * fk / SR);
}

static double sweep_hz(int manual)
{
    double u = manual / 127.0 - 0.1;
    double on = (u <= -0.05 ? 0.0 : u >= 0.05 ? u : (u + 0.05) * (u + 0.05) * 5.0) / 0.9;
    return 125.4 + (3400.0 - 125.4) * on;
}

static double rpot_k(int fb) { double x = 1.0 - fb / 127.0; return 100.0 * x * x * x; }

/* the lean model's gain (mfx.c), the stages at fs Hz, FEEDBACK knob fb */
static double lean_db(double f, double fs, int fb)
{
    double complex z1 = cexp(-I * 2.0 * M_PI * f / SR);
    double t = tan(M_PI * fs / SR), c = (1.0 - t) / (1.0 + t);
    double complex hs = (c - z1) / (1.0 - c * z1), a = hs * hs * hs * hs * hs;
    double complex s = s_at(f, 1026.0, 1), rc = 1.0 / (2.0 * M_PI * 1026.0);
    double complex hp = s * rc / (1.0 + s * rc);
    double complex g = 510.0 / 5210.0 * a * (20.215 * hp - 10.0);
    double gf = 1.0 / (1.0 + rpot_k(fb)), g9 = 1.0 / 2.2, gl = 1.0 / 5.21, g5h = 0.5 / 4.7;
    double d = g9 + gf + gl + g5h, d0 = g9 + gl + g5h + 1.0 / 101.0;
    double cin = 1.5745 * g9 / d, cfb = (gf + g5h) / d, h = 0.5 * SP_OUT * pow(d / d0, 0.31);
    double complex s2 = s_at(f, 1325.0, 1), rc2 = 1.0 / (2.0 * M_PI * 1325.0);
    double complex shelf = 1.0 + 0.5 * s2 * rc2 / (1.0 + s2 * rc2);
    return 20.0 * log10(cabs(h * cin * (1.0 + g) / (1.0 - cfb * z1 * g) * shelf));
}

/* the whole analog circuit's gain from the input to N (its emphasis, the delay-free loop) */
static double circuit_db(double f, double fs, int fb)
{
    double complex s = I * 2.0 * M_PI * f;
    double complex hp_pre = s * 150.0 * 100e-9 / (1.0 + s * 150.0 * 100e-9);
    double complex b2 = 1.0 + 2700.0 / 4700.0 + (2700.0 / 150.0) * hp_pre;
    double rc = 1.0 / (2.0 * M_PI * fs);
    double complex ap = (s * rc - 1.0) / (s * rc + 1.0);
    double complex chain = 510.0 / 5210.0 * ap * ap * ap * ap * ap;
    double zth = 470.0 * 510.0 / 980.0;
    double complex hpc = s * 3300.0 * 47e-9 / (1.0 + s * 3300.0 * 47e-9);
    double complex gx = chain * ((1.0 + 4700.0 / zth) * hpc - 4700.0 / zth * 510.0 / 980.0);
    double g9 = 1.0 / 2200.0, gf = 1.0 / (1000.0 + 1000.0 * rpot_k(fb)), gl = 1.0 / 5210.0, g5 = 1.0 / 4700.0;
    double g28 = 1.0 / 4700.0;
    double complex ysh = 1.0 / (330.0 + 1.0 / (s * 47e-9));
    double complex nm = (g5 + g28 * gx) / (g5 + g28 + ysh);
    double complex m = g9 * b2 / (g9 + gf + gl + g5 - gf * gx - g5 * nm);
    return 20.0 * log10(cabs(nm * m));
}

/* the model's steady-state gain at f Hz (a sine, after it settles), held at MANUAL `manual` */
static double model_db(double f, int manual, int fb)
{
    static float l[128], r[128];
    mfx_init();
    mfx_params[0] = (MfxParams){MFX_STONE_PHASER, {(uint8_t)manual, 0, (uint8_t)fb, 0}, 0};
    double ph = 0.0, w = 2.0 * M_PI * f / SR, amp = 100.0, acc_in = 0.0, acc_out = 0.0;
    for (int b = 0; b < 1034; b++) {            /* the MANUAL glide settles in the first 0.75 s */
        for (int i = 0; i < 128; i++) { l[i] = r[i] = (float)(amp * sin(ph)); ph += w; }
        double in = 0.0;
        for (int i = 0; i < 128; i++) in += (double)l[i] * l[i];
        mfx_layer_block(0, l, r, 128);
        if (b >= 517) {
            acc_in += in;
            for (int i = 0; i < 128; i++) acc_out += (double)l[i] * l[i];
        }
    }
    return 10.0 * log10(acc_out / acc_in);
}

/* pink noise (Paul Kellet's filter) into both sides */
static double pk[7];
static float pink(void)
{
    double w = (rand() / (double)RAND_MAX - 0.5) * 2000.0;
    pk[0] = 0.99886 * pk[0] + w * 0.0555179; pk[1] = 0.99332 * pk[1] + w * 0.0750759;
    pk[2] = 0.96900 * pk[2] + w * 0.1538520; pk[3] = 0.86650 * pk[3] + w * 0.3104856;
    pk[4] = 0.55000 * pk[4] + w * 0.5329522; pk[5] = -0.7616 * pk[5] - w * 0.0168980;
    float y = (float)(pk[0] + pk[1] + pk[2] + pk[3] + pk[4] + pk[5] + pk[6] + w * 0.5362);
    pk[6] = w * 0.115926;
    return y;
}

int main(void)
{
    /* 1. against its own transfer function; and the lean model against the whole analog circuit */
    static const int man[3] = {0, 64, 127}, fbk[3] = {0, 90, 127};
    double worst = 0.0, lean_off = 0.0;
    for (int mi = 0; mi < 3; mi++)
        for (int fi = 0; fi < 3; fi++) {
            double fs = sweep_hz(man[mi]), dsum = 0.0, dd[40];
            int nd = 0, ok[40];
            for (int k = 0; k < 40; k++) {
                double f = 40.0 * pow(2.0, k * 0.22);          /* 40 Hz .. ~16 kHz */
                double want = lean_db(f, fs, fbk[fi]), ana = circuit_db(f, fs, fbk[fi]);
                double d = model_db(f, man[mi], fbk[fi]) - want;
                if (want > -40.0 && fabs(d) > worst) worst = fabs(d);
                if (want > -40.0 && fabs(d) > 0.1) {
                    printf("MANUAL %d FEEDBACK %d, %.0f Hz: model %+.3f dB off its transfer function\n", man[mi],
                           fbk[fi], f, d);
                    fails++;
                }
                ok[k] = want > -20.0 && ana > -20.0 && f < 8000.0;   /* away from the notches, below the emphasis */
                dd[k] = want - ana;
                if (ok[k]) { dsum += dd[k]; nd++; }
            }
            for (int k = 0; k < 40; k++)                       /* the level offset (the trims) taken out */
                if (ok[k] && fabs(dd[k] - dsum / nd) > lean_off) lean_off = fabs(dd[k] - dsum / nd);
        }
    printf("vs its transfer function: worst %.3f dB; lean vs the whole circuit (off the notches, < 8 kHz): %.2f dB\n",
           worst, lean_off);

    /* 2. level: pink noise, the full sweep at a middle rate, FEEDBACK 0 / 64 / 100 / 127 */
    for (int fi = 0; fi < 4; fi++) {
        static const uint8_t fbs[4] = {0, 64, 100, 127};
        static float l[128], r[128];
        mfx_init();
        mfx_params[0] = (MfxParams){MFX_STONE_PHASER, {64, 64, fbs[fi], 127}, 0};
        srand(1);
        double ein = 0.0, eout = 0.0;
        for (int b = 0; b < 3445; b++) {                   /* 10 s */
            for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
            mfx_layer_block(0, l, r, 128);
            for (int i = 0; i < 128; i++) eout += (double)l[i] * l[i];
        }
        double db = 10.0 * log10(eout / ein);
        printf("pink noise through it, FEEDBACK %3u: %+.2f dB\n", fbs[fi], db);
        if (fabs(db - LEVEL_DB) > 1.0) { printf("level off its %+.1f dB by more than 1 dB\n", LEVEL_DB); fails++; }
    }

    /* 3. full feedback, fastest rate, loud: bounded */
    {
        static float l[128], r[128];
        mfx_init();
        mfx_params[0] = (MfxParams){MFX_STONE_PHASER, {64, 127, 127, 127}, 127};
        double peak = 0.0;
        int bad = 0;
        for (int b = 0; b < 6890; b++) {
            for (int i = 0; i < 128; i++) { l[i] = (float)((rand() / (double)RAND_MAX - 0.5) * 120000.0); r[i] = -l[i]; }
            mfx_layer_block(0, l, r, 128);
            for (int i = 0; i < 128; i++) {
                if (!isfinite(l[i]) || !isfinite(r[i])) bad = 1;
                if (fabs(l[i]) > peak) peak = fabs(l[i]);
            }
        }
        printf("full feedback, loud noise: peak %.0f (Q15 units)\n", peak);
        if (bad || peak > 400000.0) { printf("not bounded\n"); fails++; }
    }

    /* 4. OFF */
    {
        float l[64], r[64];
        mfx_params[0].type = MFX_OFF;
        for (int i = 0; i < 64; i++) l[i] = r[i] = (float)i;
        mfx_layer_block(0, l, r, 64);
        for (int i = 0; i < 64; i++) if (l[i] != (float)i || r[i] != (float)i) { printf("OFF changed the signal\n"); fails++; break; }
    }

    /* 5. L/R PHASE: the same pink noise into both sides, 4 s at 1 Hz; how far apart the sides come out */
    for (int st = 0; st < 2; st++) {
        static float l[128], r[128];
        mfx_init();
        mfx_params[0] = (MfxParams){MFX_STONE_PHASER, {64, (uint8_t)(127.0 * log2(1.0 / 0.08) / 8.29 + 0.5), 60,
                                                       127}, (uint8_t)(st ? 127 : 0)};
        srand(2);
        double diff = 0.0, sum = 0.0;
        for (int b = 0; b < 1378; b++) {
            for (int i = 0; i < 128; i++) l[i] = r[i] = pink();
            mfx_layer_block(0, l, r, 128);
            for (int i = 0; i < 128; i++) { diff += (double)(l[i] - r[i]) * (l[i] - r[i]); sum += (double)l[i] * l[i]; }
        }
        double db = 10.0 * log10(diff / sum + 1e-30);
        printf("L/R PHASE %3d: left minus right %.1f dB under the left\n", st ? 127 : 0, -db);
        if (st == 0 && diff != 0.0) { printf("L/R PHASE 0: the sides differ\n"); fails++; }
        if (st == 1 && db < -10.0) { printf("L/R PHASE 127: the sides barely differ\n"); fails++; }
    }

    /* 6. BULLROARER: pink noise level within 1.5 dB of dry over its corners; full everything, loud: bounded (each
     *    side's brigade hisses on its own, so the sides always differ a little) */
    {
        static const uint8_t K[4][4] = {{47, 90, 64, 0}, {47, 90, 64, 127}, {127, 127, 127, 127}, {0, 0, 0, 0}};
        static float l[128], r[128];
        for (int ki = 0; ki < 4; ki++) {
            mfx_init();
            mfx_params[0] = (MfxParams){MFX_BULLROARER, {K[ki][0], K[ki][1], K[ki][2], K[ki][3]}, 0};
            srand(3);
            double ein = 0.0, eout = 0.0, peak = 0.0, diff = 0.0;
            int bad = 0;
            for (int b = 0; b < 3445; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    if (!isfinite(l[i]) || !isfinite(r[i])) bad = 1;
                    eout += (double)l[i] * l[i];
                    diff += (double)(l[i] - r[i]) * (l[i] - r[i]);
                    if (fabs(l[i]) > peak) peak = fabs(l[i]);
                }
            }
            double db = 10.0 * log10(eout / ein);
            printf("BULLROARER %3u %3u %3u %3u: pink noise %+.2f dB, sides %s\n", K[ki][0], K[ki][1], K[ki][2], K[ki][3],
                   db, diff == 0.0 ? "equal" : "differ");
            if (bad || fabs(db) > 1.5) { printf("BULLROARER level off or not finite\n"); fails++; }
            if (peak > 400000.0) { printf("BULLROARER not bounded\n"); fails++; }
        }
    }

    /* 7. ENSEMBLE: pink noise level within 1.5 dB of dry over its corners, bounded loud; WIDTH 0 the sides alike (but
     *    for each side's own hiss), WIDTH 127 apart */
    {
        static const uint8_t K[5][5] = {{0, 74, 0, 64, 0}, {0, 74, 64, 64, 127}, {127, 74, 64, 64, 127},
                                        {127, 127, 127, 127, 127}, {0, 0, 0, 0, 0}};
        static float l[128], r[128];
        for (int ki = 0; ki < 5; ki++) {
            mfx_init();
            mfx_params[0] = (MfxParams){MFX_ENSEMBLE, {K[ki][0], K[ki][1], K[ki][2], K[ki][3]}, K[ki][4]};
            srand(4);
            double ein = 0.0, eout = 0.0, peak = 0.0, diff = 0.0;
            int bad = 0;
            for (int b = 0; b < 3445; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    if (!isfinite(l[i]) || !isfinite(r[i])) bad = 1;
                    eout += (double)l[i] * l[i];
                    diff += (double)(l[i] - r[i]) * (l[i] - r[i]);
                    if (fabs(l[i]) > peak) peak = fabs(l[i]);
                }
            }
            double db = 10.0 * log10(eout / ein), sd = 10.0 * log10(diff / eout + 1e-30);
            printf("ENSEMBLE %3u %3u %3u %3u WIDTH %3u: pink noise %+.2f dB, left minus right %.1f dB under\n", K[ki][0],
                   K[ki][1], K[ki][2], K[ki][3], K[ki][4], db, -sd);
            if (bad || fabs(db) > 1.5) { printf("ENSEMBLE level off or not finite\n"); fails++; }
            if (peak > 400000.0) { printf("ENSEMBLE not bounded\n"); fails++; }
            if (K[ki][4] == 0 && sd > -40.0) { printf("ENSEMBLE WIDTH 0: the sides differ\n"); fails++; }
            if (K[ki][4] == 127 && K[ki][3] && sd < -15.0) { printf("ENSEMBLE WIDTH 127: the sides barely differ\n"); fails++; }
        }
        mfx_init();
        mfx_params[0] = (MfxParams){MFX_ENSEMBLE, {127, 127, 127, 127}, 127};
        double peak = 0.0;
        for (int b = 0; b < 3445; b++) {
            for (int i = 0; i < 128; i++) { l[i] = (float)((rand() / (double)RAND_MAX - 0.5) * 120000.0); r[i] = -l[i]; }
            mfx_layer_block(0, l, r, 128);
            for (int i = 0; i < 128; i++) if (!(fabs(l[i]) <= peak)) peak = fabs(l[i]);
        }
        printf("ENSEMBLE everything full, loud noise: peak %.0f\n", peak);
        if (!isfinite(peak) || peak > 400000.0) { printf("ENSEMBLE not bounded\n"); fails++; }
    }

    /* 8. no zipper: each knob and the row turned fast mid-tone (a 220 Hz sine), 0 -> 127 in steps of 2 a block (0.19 s,
     *    as a quick turn's detents); the output's second difference while it turns stays within 1.5x of the larger of
     *    before and after, settled (unglided, each step steps the output: the zipper) */
    {
        static const uint8_t T[2] = {MFX_STONE_PHASER, MFX_ENSEMBLE};
        static float l[128], r[128];
        for (int ti = 0; ti < 2; ti++)
            for (int kk = 0; kk < 5; kk++) {
                MfxParams p = {T[ti], {64, 64, 64, 64}, 64};
                double ph = 0.0, before = 0.0, after = 0.0, during = 0.0, y1 = 0.0, y2 = 0.0;
                if (kk < 4) p.knob[kk] = 0; else p.lr = 0;
                mfx_init();
                mfx_params[0] = p;
                for (int b = 0; b < 1000; b++) {                 /* 2.9 s; the turn from block 600 */
                    if (b >= 600 && b < 664) {
                        uint8_t v = (uint8_t)(b - 599 >= 64 ? 127 : 2 * (b - 599));
                        if (kk < 4) mfx_params[0].knob[kk] = v; else mfx_params[0].lr = v;
                    }
                    for (int i = 0; i < 128; i++) { l[i] = r[i] = (float)(8000.0 * sin(ph)); ph += 2.0 * M_PI * 220.0 / SR; }
                    mfx_layer_block(0, l, r, 128);
                    for (int i = 0; i < 128; i++) {
                        double d2 = fabs(l[i] - 2.0 * y1 + y2);
                        y2 = y1; y1 = l[i];
                        if (b >= 500 && b < 600 && d2 > before) before = d2;
                        if (b >= 600 && b < 700 && d2 > during) during = d2;
                        if (b >= 900 && d2 > after) after = d2;
                    }
                }
                double calm = before > after ? before : after;
                if (during > 1.5 * calm + 1.0) {
                    printf("%s knob %d turned: zipper (second difference %.0f while turning, %.0f before, %.0f after)\n",
                           ti ? "ENSEMBLE" : "STONE PHASER", kk + 1, during, before, after);
                    fails++;
                }
            }
    }

    printf(fails ? "%d MFX checks failed\n" : "all MFX checks passed\n", fails);
    return fails != 0;
}
