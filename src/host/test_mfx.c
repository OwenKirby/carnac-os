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
 *  8. No zipper: every knob and row turned fast mid-tone (STONE PHASER, ENSEMBLE, ENS+PHASER).
 *  9. ENS+PHASER: its level on pink noise at five settings within 1 dB of LEVEL_DB (as STONE PHASER), bounded.
 * 10. CHORUS: its level on pink noise at five settings, bounded.
 * 11. The stereo image kept: left-only input leaves the right silent on every type.
 * 13. BBD FLANGER: level per MODE, full FEEDBACK bounded, MODE III crossing; PITCH SHIFT in tune on a sustained
 *     tone (+-15 cents), its levels, bounded.
 * 12. TAPE FLANGER: its level on pink noise at five settings within 1 dB of TF_POS_DB / TF_NEG_DB, bounded loud; through zero (TAPE 0,
 *     NEGATIVE, the thumb sweeping through the reference): the sound all but vanishes where the decks meet, and not
 *     quite with the tape's wobbles (TAPE 127); POLARITY flipped mid-tone without a click.
 *   gcc -O2 -Isrc -Isrc/dsp src/dsp/mfx.c src/host/test_mfx.c -lm -o test_mfx */
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "mfx.h"

#define SR 44100.0
#define LEVEL_DB 2.0
#define CH_LEVEL_DB 0.8                     /* CHORUS at MIX 64 */
#define TF_POS_DB 1.5                       /* TAPE FLANGER POSITIVE: the decks' low end adds, as STONE PHASER's */
#define TF_NEG_DB -2.5                      /* ... NEGATIVE: its low end thinned where the decks meet */
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
    MFX_PARAMS(0) = (MfxParams){MFX_STONE_PHASER, {(uint8_t)manual, 0, (uint8_t)fb, 0}, 0};
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
        MFX_PARAMS(0) = (MfxParams){MFX_STONE_PHASER, {64, 64, fbs[fi], 127}, 0};
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
        MFX_PARAMS(0) = (MfxParams){MFX_STONE_PHASER, {64, 127, 127, 127}, 127};
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
        MFX_PARAMS(0).type = MFX_OFF;
        for (int i = 0; i < 64; i++) l[i] = r[i] = (float)i;
        mfx_layer_block(0, l, r, 64);
        for (int i = 0; i < 64; i++) if (l[i] != (float)i || r[i] != (float)i) { printf("OFF changed the signal\n"); fails++; break; }
    }

    /* 5. L/R PHASE: the same pink noise into both sides, 4 s at 1 Hz; how far apart the sides come out */
    for (int st = 0; st < 2; st++) {
        static float l[128], r[128];
        mfx_init();
        MFX_PARAMS(0) = (MfxParams){MFX_STONE_PHASER, {64, (uint8_t)(127.0 * log2(1.0 / 0.08) / 8.29 + 0.5), 60,
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
            MFX_PARAMS(0) = (MfxParams){MFX_BULLROARER, {K[ki][0], K[ki][1], K[ki][2], K[ki][3]}, 0};
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
            MFX_PARAMS(0) = (MfxParams){MFX_ENSEMBLE, {K[ki][0], K[ki][1], K[ki][2], K[ki][3]}, K[ki][4]};
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
        MFX_PARAMS(0) = (MfxParams){MFX_ENSEMBLE, {127, 127, 127, 127}, 127};
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
        static const uint8_t T[8] = {MFX_STONE_PHASER, MFX_ENSEMBLE, MFX_ENS_PHASER, MFX_CHORUS, MFX_TAPE_FLANGER,
                                     MFX_BBD_FLANGER, MFX_PITCH_SHIFT, MFX_BBD_ECHO};
        static const char *const TN[8] = {"STONE PHASER", "ENSEMBLE", "ENS+PHASER", "CHORUS", "TAPE FLANGER",
                                          "BBD FLANGER", "PITCH SHIFT", "BBD ECHO"};
        static float l[128], r[128];
        for (int ti = 0; ti < 8; ti++)
            for (int kk = 0; kk < (ti == 2 ? 7 : ti == 3 ? 6 : 5); kk++) {   /* (TAPE FLANGER's POLARITY: 12) */   /* the knobs, lr, row[] */
                MfxParams p = {T[ti], {64, 64, 64, 64}, 64, {(uint8_t)(ti == 5 ? 0 : 64), (uint8_t)(ti >= 4 ? 0 : 64)}};
                if (T[ti] == MFX_PITCH_SHIFT) p.knob[MFX_K_PS_PITCH] = 19;   /* (+7: 0..24) */
                double ph = 0.0, before = 0.0, after = 0.0, during = 0.0, y1 = 0.0, y2 = 0.0;
                *(kk < 4 ? &p.knob[kk] : kk == 4 ? &p.lr : &p.row[kk - 5]) = 0;
                mfx_init();
                MFX_PARAMS(0) = p;
                for (int b = 0; b < 1000; b++) {                 /* 2.9 s; the turn from block 600 */
                    if (b >= 600 && b < 664) {
                        uint8_t v = (uint8_t)(b - 599 >= 64 ? 127 : 2 * (b - 599));
                        *(kk < 4 ? &MFX_PARAMS(0).knob[kk] : kk == 4 ? &MFX_PARAMS(0).lr : &MFX_PARAMS(0).row[kk - 5]) = v;
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
                           TN[ti], kk + 1, during, before, after);
                    fails++;
                }
            }
    }

    /* 9. ENS+PHASER: pink noise level within 1.5 dB of dry over its corners (rows AGE, COLOUR, WIDTH), bounded loud */
    {
        static const uint8_t K[5][7] = {{74, 64, 25, 110, 64, 30, 40}, {74, 64, 25, 110, 100, 30, 0},
                                        {74, 64, 80, 127, 100, 127, 127}, {127, 127, 127, 127, 127, 127, 127},
                                        {0, 0, 0, 0, 0, 0, 0}};
        static float l[128], r[128];
        for (int ki = 0; ki < 5; ki++) {
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_ENS_PHASER, {K[ki][0], K[ki][1], K[ki][2], K[ki][3]}, K[ki][4],
                                        {K[ki][5], K[ki][6]}};
            srand(5);
            double ein = 0.0, eout = 0.0, peak = 0.0;
            int bad = 0;
            for (int b = 0; b < 3445; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    if (!isfinite(l[i]) || !isfinite(r[i])) bad = 1;
                    eout += (double)l[i] * l[i];
                    if (fabs(l[i]) > peak) peak = fabs(l[i]);
                }
            }
            double db = 10.0 * log10(eout / ein);
            printf("ENS+PHASER %3u %3u %3u %3u WIDTH %3u AGE %3u COLOUR %3u: pink noise %+.2f dB\n", K[ki][0], K[ki][1],
                   K[ki][2], K[ki][3], K[ki][4], K[ki][5], K[ki][6], db);
            if (bad || fabs(db - LEVEL_DB) > 1.0) { printf("ENS+PHASER level off its %+.1f dB or not finite\n", LEVEL_DB); fails++; }
            if (peak > 400000.0) { printf("ENS+PHASER not bounded\n"); fails++; }
        }
        mfx_init();
        MFX_PARAMS(0) = (MfxParams){MFX_ENS_PHASER, {127, 127, 127, 127}, 127, {127, 127}};
        double peak = 0.0;
        for (int b = 0; b < 3445; b++) {
            for (int i = 0; i < 128; i++) { l[i] = (float)((rand() / (double)RAND_MAX - 0.5) * 120000.0); r[i] = -l[i]; }
            mfx_layer_block(0, l, r, 128);
            for (int i = 0; i < 128; i++) if (!(fabs(l[i]) <= peak)) peak = fabs(l[i]);
        }
        printf("ENS+PHASER everything full, loud noise: peak %.0f\n", peak);
        if (!isfinite(peak) || peak > 400000.0) { printf("ENS+PHASER not bounded\n"); fails++; }
    }

    /* 10. CHORUS: pink noise level within 1 dB of CH_LEVEL_DB over its corners (MIX 64: the dry and the wet, equal
     *     power: their low end adds), MIX 0 the dry alone; bounded loud */
    {
        static const uint8_t K[5][6] = {{40, 94, 46, 64, 127, 20}, {60, 60, 46, 64, 0, 20}, {127, 127, 127, 64, 127, 127},
                                        {0, 0, 0, 64, 64, 0}, {40, 94, 46, 0, 127, 20}};
        static float l[128], r[128];
        for (int ki = 0; ki < 5; ki++) {
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_CHORUS, {K[ki][0], K[ki][1], K[ki][2], K[ki][3]}, K[ki][4], {K[ki][5], 0}};
            srand(6);
            double ein = 0.0, eout = 0.0, peak = 0.0;
            int bad = 0;
            for (int b = 0; b < 3445; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    if (!isfinite(l[i]) || !isfinite(r[i])) bad = 1;
                    eout += (double)l[i] * l[i];
                    if (fabs(l[i]) > peak) peak = fabs(l[i]);
                }
            }
            double db = 10.0 * log10(eout / ein), want = K[ki][3] ? CH_LEVEL_DB : 0.0;
            printf("CHORUS %3u %3u %3u %3u WIDTH %3u AGE %3u: pink noise %+.2f dB\n", K[ki][0], K[ki][1], K[ki][2],
                   K[ki][3], K[ki][4], K[ki][5], db);
            if (bad || fabs(db - want) > 1.0) { printf("CHORUS level off its %+.1f dB or not finite\n", want); fails++; }
            if (peak > 400000.0) { printf("CHORUS not bounded\n"); fails++; }
        }
    }

    /* 11. the stereo image kept: every type with sound on the left only leaves the right all but silent (its own hiss,
     *     ducked by the level, at most), at WIDTH 0 and 127 (BBD ECHO not: one brigade, mono, as the pedals) */
    {
        static const uint8_t T[7] = {MFX_STONE_PHASER, MFX_ENSEMBLE, MFX_ENS_PHASER, MFX_CHORUS, MFX_TAPE_FLANGER,
                                     MFX_BBD_FLANGER, MFX_PITCH_SHIFT};
        static float l[128], r[128];
        for (int ti = 0; ti < 7; ti++)
            for (int wi = 0; wi < 2; wi++) {
                mfx_init();
                MFX_PARAMS(0) = (MfxParams){T[ti], {64, 64, 64, 64}, (uint8_t)(wi ? 127 : 0),
                                            {(uint8_t)(T[ti] == MFX_BBD_FLANGER ? 0 : 30),
                                             (uint8_t)(T[ti] == MFX_TAPE_FLANGER ? 1 : T[ti] == MFX_BBD_FLANGER ? 0 : 40)}};
                if (T[ti] == MFX_PITCH_SHIFT) MFX_PARAMS(0).knob[MFX_K_PS_PITCH] = 19;
                srand(7);
                double el = 0.0, er = 0.0;
                for (int b = 0; b < 1378; b++) {
                    for (int i = 0; i < 128; i++) { l[i] = pink(); r[i] = 0.f; }
                    mfx_layer_block(0, l, r, 128);
                    for (int i = 0; i < 128; i++) { el += (double)l[i] * l[i]; er += (double)r[i] * r[i]; }
                }
                double db = 10.0 * log10(er / el + 1e-30);
                if (db > -40.0) {
                    printf("type %u WIDTH %d: the left's sound on the right, %.1f dB under the left\n", T[ti], wi ? 127 : 0,
                           -db);
                    fails++;
                }
            }
        printf("stereo image: left-only input stays left on every type\n");
    }

    /* 12. TAPE FLANGER: pink noise level within 1 dB of TF_POS_DB (NEGATIVE: TF_NEG_DB, its low end thinned where the
     *     decks meet), bounded loud; through zero; POLARITY flipped without a click */
    {
        static const uint8_t K[5][7] = {{40, 40, 64, 40, 0, 0, 0}, {32, 80, 127, 0, 127, 0, 0},
                                        {100, 127, 127, 127, 64, 0, 0}, {40, 40, 64, 40, 0, 0, 1},
                                        {0, 0, 0, 127, 0, 0, 0}};
        static float l[128], r[128];
        for (int ki = 0; ki < 5; ki++) {
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_TAPE_FLANGER, {K[ki][0], K[ki][1], K[ki][2], K[ki][3]}, K[ki][4],
                                        {K[ki][5], K[ki][6]}};
            srand(8);
            double ein = 0.0, eout = 0.0, peak = 0.0;
            int bad = 0;
            for (int b = 0; b < 3445; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    if (!isfinite(l[i]) || !isfinite(r[i])) bad = 1;
                    eout += (double)l[i] * l[i];
                    if (fabs(l[i]) > peak) peak = fabs(l[i]);
                }
            }
            double db = 10.0 * log10(eout / ein), want = K[ki][6] ? TF_NEG_DB : TF_POS_DB;
            printf("TAPE FLANGER %3u %3u %3u %3u L/R %3u %s: pink noise %+.2f dB\n", K[ki][0], K[ki][1], K[ki][2],
                   K[ki][3], K[ki][4], K[ki][6] ? "NEGATIVE" : "POSITIVE", db);
            if (bad || fabs(db - want) > 1.0) { printf("TAPE FLANGER level off its %+.1f dB or not finite\n", want); fails++; }
            if (peak > 400000.0) { printf("TAPE FLANGER not bounded\n"); fails++; }
        }
        mfx_init();
        MFX_PARAMS(0) = (MfxParams){MFX_TAPE_FLANGER, {127, 127, 127, 127}, 127, {0, 1}};
        double peak = 0.0;
        for (int b = 0; b < 3445; b++) {
            for (int i = 0; i < 128; i++) { l[i] = (float)((rand() / (double)RAND_MAX - 0.5) * 120000.0); r[i] = -l[i]; }
            mfx_layer_block(0, l, r, 128);
            for (int i = 0; i < 128; i++) if (!(fabs(l[i]) <= peak)) peak = fabs(l[i]);
        }
        printf("TAPE FLANGER everything full, loud noise: peak %.0f\n", peak);
        if (!isfinite(peak) || peak > 400000.0) { printf("TAPE FLANGER not bounded\n"); fails++; }

        /* through zero: pink noise, the slowest thumb (0.03 Hz) through the reference (MANUAL 32, DEPTH 30: about
         * -0.9 .. +1 ms), NEGATIVE; the quietest block (2.9 ms) against the loudest, clean tape and worn */
        for (int ti = 0; ti < 2; ti++) {
            uint8_t tape = (uint8_t)(ti ? 127 : 0);
            double lo = 1e30, hi = 0.0, acc = 0.0;
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_TAPE_FLANGER, {32, 0, 30, tape}, 0, {0, 1}};
            srand(9);
            for (int b = 0; b < 10336; b++) {               /* 30 s: most of a sweep */
                for (int i = 0; i < 128; i++) l[i] = r[i] = pink();
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) acc += (double)l[i] * l[i];
                if (b > 100) {
                    if (acc < lo) lo = acc;
                    if (acc > hi) hi = acc;
                }
                acc = 0.0;
            }
            double dip = 10.0 * log10(lo / hi);
            printf("TAPE FLANGER through zero, NEGATIVE, TAPE %3u: quietest block %.1f dB under the loudest\n", tape, dip);
            if (ti == 0 && dip > -40.0) { printf("TAPE FLANGER: clean decks do not null where they meet\n"); fails++; }
            if (ti == 1 && (dip > -10.0 || dip < -60.0)) {
                printf("TAPE FLANGER: worn decks' null not deep-but-imperfect\n");
                fails++;
            }
        }

        /* POLARITY flipped mid-tone (220 Hz, the decks apart: MANUAL 100, DEPTH 0): no click */
        {
            double ph = 0.0, before = 0.0, during = 0.0, y1 = 0.0, y2 = 0.0;
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_TAPE_FLANGER, {100, 40, 0, 0}, 0, {0, 0}};
            for (int b = 0; b < 800; b++) {
                if (b == 600) MFX_PARAMS(0).row[MFX_R_POLARITY] = 1;
                for (int i = 0; i < 128; i++) { l[i] = r[i] = (float)(8000.0 * sin(ph)); ph += 2.0 * M_PI * 220.0 / SR; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    double d2 = fabs(l[i] - 2.0 * y1 + y2);
                    y2 = y1; y1 = l[i];
                    if (b >= 500 && b < 600 && d2 > before) before = d2;
                    if (b >= 600 && b < 700 && d2 > during) during = d2;
                }
            }
            printf("TAPE FLANGER POLARITY flip: second difference %.0f (%.0f before)\n", during, before);
            if (during > 1.5 * before + 1.0) { printf("TAPE FLANGER POLARITY flip clicks\n"); fails++; }
        }
    }

    /* 13. BBD FLANGER and PITCH SHIFT (2026-11-02) */
    {
        static float l[128], r[128];
        /* BBD FLANGER: pink noise level per MODE; full FEEDBACK + NEGATIVE bounded; MODE III crosses (the left's
         * delay out of the right), MODE I keeps the image */
        static const char *const MN[4] = {"FLANGER I", "FLANGER II", "FLANGER III", "CHORUS"};
        for (int mo = 0; mo < 5; mo++) {
            MfxParams p = mo < 4 ? (MfxParams){MFX_BBD_FLANGER, {50, 30, 80, 70}, 0, {(uint8_t)mo, 0}}
                                 : (MfxParams){MFX_BBD_FLANGER, {20, 60, 127, 127}, 0, {0, 1}};
            double ein = 0.0, eout = 0.0, peak = 0.0;
            int bad = 0;
            mfx_init();
            MFX_PARAMS(0) = p;
            srand(10);
            for (int b = 0; b < 3445; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    if (!isfinite(l[i]) || !isfinite(r[i])) bad = 1;
                    eout += (double)l[i] * l[i];
                    if (fabs(l[i]) > peak) peak = fabs(l[i]);
                }
            }
            double db = 10.0 * log10(eout / ein), want = mo == 3 ? 0.0 : LEVEL_DB;
            printf("BBD FLANGER %-11s: pink noise %+.2f dB\n", mo < 4 ? MN[mo] : "FB 127 NEG", db);
            if (bad || (mo < 4 && fabs(db - want) > 1.5) || (mo == 4 && db > 5.0) || peak > 400000.0) {
                printf("BBD FLANGER level off, unbounded or not finite\n");
                fails++;
            }
        }
        for (int mo = 0; mo < 3; mo += 2) {
            double el = 0.0, er = 0.0;
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_BBD_FLANGER, {50, 30, 80, 70}, 0, {(uint8_t)mo, 0}};
            srand(11);
            for (int b = 0; b < 1378; b++) {
                for (int i = 0; i < 128; i++) { l[i] = pink(); r[i] = 0.f; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) { el += (double)l[i] * l[i]; er += (double)r[i] * r[i]; }
            }
            double db = 10.0 * log10(er / el + 1e-30);
            printf("BBD FLANGER %s, left-only input: the right %.1f dB under the left\n", MN[mo], -db);
            if ((mo == 0 && db > -40.0) || (mo == 2 && db < -12.0)) {
                printf("BBD FLANGER: MODE I must keep the image, MODE III must cross\n");
                fails++;
            }
        }
        /* PITCH SHIFT: a sustained tone lands in tune (the energy's centre within 15 cents of the interval, 330 Hz in,
         * MIX 127); pink noise level of the detune doubling within 1.5 dB of dry; full FEEDBACK bounded */
        static const int S[6] = {0, 5, 7, 17, 19, 24};
        static float tone[44100];
        for (int si = 0; si < 6; si++) {
            double ph = 0.0, want = 330.0 * pow(2.0, (S[si] - 12) / 12.0), e = 0.0, ef = 0.0;
            int n = 0;
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_PITCH_SHIFT, {(uint8_t)S[si], 64, 0, 127}, 0, {0, 0}};
            for (int b = 0; b < 700; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = (float)(8000.0 * sin(ph)); ph += 2.0 * M_PI * 330.0 / SR; }
                mfx_layer_block(0, l, r, 128);
                if (b >= 300) for (int i = 0; i < 128 && n < 44100; i++) tone[n++] = l[i];
            }
            for (double f = want - 90.0; f <= want + 90.0; f += 1.0) {
                double w = 2.0 * cos(2.0 * M_PI * f / SR), s1 = 0.0, s2 = 0.0;
                for (int i = 0; i < n; i++) { double s0 = tone[i] + w * s1 - s2; s2 = s1; s1 = s0; }
                double pw = s1 * s1 + s2 * s2 - w * s1 * s2;
                e += pw;
                ef += pw * f;
            }
            double cents = 1200.0 * log2(ef / e / want);
            printf("PITCH SHIFT %+3d st: 330 Hz in, %+.1f cents off\n", S[si] - 12, cents);
            if (fabs(cents) > 15.0) { printf("PITCH SHIFT out of tune\n"); fails++; }
        }
        /* no crackle: a 330 Hz sine at each interval (MIX 127, no FEEDBACK): the output's largest second difference
         * within 8x a clean sine's at the shifted pitch and level (the half-rate interpolation alone gives up to ~5.6x
         * an octave down; a splice that reads outside the memory gave 40..150x, 2026-11-02) */
        for (int si = 0; si < 6; si++) {
            double ph = 0.0, rms = 0.0, worst = 0.0, f = 330.0 * pow(2.0, (S[si] - 12) / 12.0);
            int n = 0;
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_PITCH_SHIFT, {(uint8_t)S[si], 64, 0, 127}, 0, {0, 0}};
            for (int b = 0; b < 700; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = (float)(8000.0 * sin(ph)); ph += 2.0 * M_PI * 330.0 / SR; }
                mfx_layer_block(0, l, r, 128);
                if (b >= 100) for (int i = 0; i < 128 && n < 44100; i++) tone[n++] = l[i];
            }
            for (int i = 0; i < n; i++) rms += (double)tone[i] * tone[i];
            double allow = sqrt(2.0 * rms / n) * pow(2.0 * M_PI * f / SR, 2.0);
            for (int i = 2; i < n; i++) {
                double d2 = fabs(tone[i] - 2.0 * tone[i - 1] + tone[i - 2]);
                if (d2 > worst) worst = d2;
            }
            printf("PITCH SHIFT %+3d st: worst second difference %.1fx a clean sine's\n", S[si] - 12, worst / allow);
            if (worst > 8.0 * allow) { printf("PITCH SHIFT crackles\n"); fails++; }
        }
        for (int pi = 0; pi < 2; pi++) {
            MfxParams p = pi ? (MfxParams){MFX_PITCH_SHIFT, {24, 64, 127, 127}, 0, {127, 0}}
                             : (MfxParams){MFX_PITCH_SHIFT, {12, 75, 0, 64}, 127, {30, 0}};
            double ein = 0.0, eout = 0.0, peak = 0.0;
            mfx_init();
            MFX_PARAMS(0) = p;
            srand(12);
            for (int b = 0; b < 3445; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    eout += (double)l[i] * l[i];
                    if (!(fabs(l[i]) <= peak)) peak = isfinite(l[i]) ? fabs(l[i]) : 1e30;
                }
            }
            double db = 10.0 * log10(eout / ein);
            printf("PITCH SHIFT %s: pink noise %+.2f dB, peak %.0f\n", pi ? "+12 FB 127 AGE 127" : "detune doubling", db, peak);
            if ((pi == 0 && fabs(db) > 1.5) || (pi && db > 3.0) || peak > 400000.0) {
                printf("PITCH SHIFT level off or unbounded\n");
                fails++;
            }
        }
    }

    /* 14. BBD ECHO (2026-11-02): the echo lands at TIME; pink noise level at MIX 64 near dry; full FEEDBACK (and
     *     AGE) bounded; SWOOP: played loud with ENV up, the clock runs faster (the echo nearer); DUCK: the echo held
     *     down while played, back when the playing stops (pink: -1..-3.5 dB at MIX 64, its wet dark) */
    {
        static float l[128], r[128];
        {                                       /* a click; the echo's peak (MIX 127, no FEEDBACK, ENV 64: none) */
            int at = -1;
            double best = 0.0;
            mfx_init();
            MFX_PARAMS(0) = (MfxParams){MFX_BBD_ECHO, {99, 0, 64, 127}, 0, {0, 0}};
            for (int b = 0; b < 40; b++) {
                for (int i = 0; i < 128; i++) l[i] = r[i] = (b == 2 && i < 8) ? 20000.f : 0.f;
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++)
                    if (fabs(l[i]) > best) { best = fabs(l[i]); at = (b - 2) * 128 + i; }
            }
            double ms = at * 1000.0 / SR, want = 44.0 * pow(2.0, 99.0 * 5.02 / 127.0) * 4000.0 / SR;
            printf("BBD ECHO TIME 99: the echo at %.1f ms (%.1f wanted)\n", ms, want);
            if (fabs(ms - want) > 3.0) { printf("BBD ECHO: the echo is not at TIME\n"); fails++; }
        }
        for (int pi = 0; pi < 3; pi++) {
            MfxParams p = pi == 0 ? (MfxParams){MFX_BBD_ECHO, {99, 35, 64, 64}, 40, {0, 30}}
                        : pi == 1 ? (MfxParams){MFX_BBD_ECHO, {60, 127, 64, 127}, 0, {0, 127}}
                                  : (MfxParams){MFX_BBD_ECHO, {110, 127, 127, 100}, 127, {0, 60}};
            double ein = 0.0, eout = 0.0, peak = 0.0;
            mfx_init();
            MFX_PARAMS(0) = p;
            srand(14);
            for (int b = 0; b < 3445; b++) {
                for (int i = 0; i < 128; i++) { l[i] = r[i] = pink(); ein += (double)l[i] * l[i]; }
                mfx_layer_block(0, l, r, 128);
                for (int i = 0; i < 128; i++) {
                    eout += (double)l[i] * l[i];
                    if (!(fabs(l[i]) <= peak)) peak = isfinite(l[i]) ? fabs(l[i]) : 1e30;
                    if (!(fabs(r[i]) <= peak)) peak = isfinite(r[i]) ? fabs(r[i]) : 1e30;
                }
            }
            double db = 10.0 * log10(eout / ein);
            printf("BBD ECHO %s: pink noise %+.2f dB, peak %.0f\n",
                   pi == 0 ? "slap, MIX 64" : pi == 1 ? "FB 127 AGE 127, all wet" : "FB 127 SWOOP 127", db, peak);
            if ((pi == 0 && (db < -3.5 || db > 1.0)) || db > 6.0 || peak > 400000.0) {   /* (its wet ~4 kHz: dark) */
                printf("BBD ECHO level off or unbounded\n");
                fails++;
            }
        }
        {                                       /* SWOOP: the clock under loud playing; DUCK: the echo's level */
            float near[2];
            for (int ei = 0; ei < 2; ei++) {
                mfx_init();
                MFX_PARAMS(0) = (MfxParams){MFX_BBD_ECHO, {99, 0, (uint8_t)(ei ? 127 : 64), 127}, 0, {0, 0}};
                srand(15);
                for (int b = 0; b < 400; b++) {
                    for (int i = 0; i < 128; i++) l[i] = r[i] = 3.f * pink();
                    mfx_layer_block(0, l, r, 128);
                }
                near[ei] = mfx_peek(0, 0);
            }
            printf("BBD ECHO SWOOP: played loud, the echo at %.2f of the reach (ENV 127) against %.2f (ENV 64)\n", near[1],
                   near[0]);
            if (!(near[1] < 0.75f * near[0])) { printf("BBD ECHO: SWOOP does not move the clock\n"); fails++; }
            double e[2][2] = {{0.0, 0.0}, {0.0, 0.0}};             /* [ENV 64, 127][while played, after] */
            for (int ei = 0; ei < 2; ei++) {
                mfx_init();
                MFX_PARAMS(0) = (MfxParams){MFX_BBD_ECHO, {99, 60, (uint8_t)(ei ? 127 : 64), 127}, 0, {1, 0}};
                srand(16);
                for (int b = 0; b < 700; b++) {
                    for (int i = 0; i < 128; i++) l[i] = r[i] = b < 400 ? 3.f * pink() : 0.f;
                    mfx_layer_block(0, l, r, 128);
                    if ((b >= 200 && b < 400) || (b >= 450 && b < 500))   /* (after: 0.15 .. 0.33 s on) */
                        for (int i = 0; i < 128; i++) e[ei][b >= 400] += (double)l[i] * l[i];
                }
            }
            double held = 10.0 * log10(e[1][0] / e[0][0]), after = 10.0 * log10(e[1][1] / e[0][1]);
            printf("BBD ECHO DUCK 127: the echo %.1f dB while played, %.1f dB just after (against ENV 64)\n", held, after);
            if (held > -8.0 || after < -4.0) { printf("BBD ECHO: DUCK does not duck and bloom\n"); fails++; }
        }
    }

    printf(fails ? "%d MFX checks failed\n" : "all MFX checks passed\n", fails);
    return fails != 0;
}
