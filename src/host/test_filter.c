/* The filter cores on their own (src/dsp/filter.c), what the render hashes do not reach (they play the default patch's
 * MULTI): every flavour's block render is the per-sample one, bit for bit, driven or not, under a sweeping cutoff; and
 * the SH-2 keeps what was tuned by ear on 2026-10-21: self-oscillating it is in tune at DRIVE 0 and 127 (within 10
 * cents), and a resonant zap (cutoff 127 -> 20 in 250 ms) stays out of the output knee at DRIVE 0. Test scaffolding.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware src/host/test_filter.c src/dsp/filter.c -lm -o test_filter */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "filter.h"
#include "PresetStructures.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static float saw(int i) { return (float)(int32_t)(26000.f * (2.f * (float)(i % 400) / 400.f - 1.f)); }

/* self-oscillation from a kick: its frequency (rising zero crossings over the second half second), 0 if it died */
static double self_osc_hz(int cutoff, int drive)
{
    static float y[33075];
    SvfCore k;
    int i, nc = 0;
    double first = 0, last = 0;
    memset(&k, 0, sizeof k);
    k.flavour = 255;
    for (i = 0; i < 33075; i++) {
        if ((i & 31) == 0) filter_core_prepare(&k, cutoff << 8, 115, 0, drive, FLT_SH2, 5, i == 0);
        y[i] = filter_core_render_f(&k, i < 20 ? 8000 : 0);
    }
    for (i = 22051; i < 33075; i++)
        if (y[i - 1] < 0.f && y[i] >= 0.f) {
            double t = i - 1 + (double)(-y[i - 1]) / (y[i] - y[i - 1]);
            if (!nc) first = t;
            last = t;
            nc++;
        }
    return nc < 3 ? 0.0 : 44100.0 * (nc - 1) / (last - first);
}

int main(void)
{
    int fl, d, i, blk;
    for (fl = 0; fl < FLT_COUNT; fl++)
        for (d = 0; d <= 127; d += 127) {
            SvfCore a, b;
            float x[32], ya[32], yb[32];
            int same = 1;
            memset(&a, 0, sizeof a);
            memset(&b, 0, sizeof b);
            a.flavour = b.flavour = 255;
            for (blk = 0; blk < 2000; blk++) {
                int cut = (int)((20.f + 107.f * expf(-(float)(blk % 400) / 60.f)) * 256.f);
                filter_core_prepare(&a, cut, 112, 40, d, fl, 5, blk == 0);
                filter_core_prepare(&b, cut, 112, 40, d, fl, 5, blk == 0);
                for (i = 0; i < 32; i++) x[i] = saw(blk * 32 + i);
                filter_core_render_block(&a, x, ya, 32);
                for (i = 0; i < 32; i++) yb[i] = filter_core_render_f(&b, (int32_t)x[i]);
                same &= memcmp(ya, yb, sizeof ya) == 0;
            }
            CHECK(same, "flavour %d drive %d: the block render is not the per-sample one", fl, d);
        }
    {
        static const int cut[3] = {30, 70, 105};
        double worst = 0;
        for (i = 0; i < 3; i++)
            for (d = 0; d <= 127; d += 127) {
                double fc = 20.0 * pow(700.0, cut[i] / 127.0), f = self_osc_hz(cut[i], d);
                double c = f > 0 ? 1200.0 * log2(f / fc) : 1e9;
                CHECK(fabs(c) < 10.0, "SH-2 cutoff %d drive %d: self-oscillates %.1f cents off", cut[i], d, c);
                if (fabs(c) > worst) worst = fabs(c);
            }
        printf("SH-2 self-oscillation: within %.1f cents of the cutoff at drive 0 and 127\n", worst);
    }
    {
        SvfCore k;
        int over = 0;
        memset(&k, 0, sizeof k);
        k.flavour = 255;
        for (i = 0; i < 22050; i++) {
            float t = (float)(i % 11025) / 11025.f;
            if ((i & 31) == 0)
                filter_core_prepare(&k, (int)((20.f + 107.f * expf(-t * 12.f)) * 256.f), 115, 0, 0, FLT_SH2, 5, i == 0);
            over += fabsf(filter_core_render_f(&k, (int32_t)saw(i))) > 24576.f;
        }
        CHECK(over < 22050 / 100, "SH-2 zap at resonance 115: %.1f %% in the output knee", 100.0 * over / 22050);
        printf("SH-2 zap at resonance 115, drive 0: %.2f %% of the samples in the output knee\n", 100.0 * over / 22050);
    }
    printf(fails ? "%d filter checks failed\n" : "filter checks: all passed\n", fails);
    return fails != 0;
}
