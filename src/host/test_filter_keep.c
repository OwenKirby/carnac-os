/* Host test: the filters' kept values and lowpass loops (filter.c, 2026-11-12) against the plain code. filter.c is
 * compiled in twice: as it ships, and (included below under ref_ names with FILTER_NO_KEEP) preparing every tick in
 * full and running the general loops. Two cores in step over random settings and input, every flavour, the cutoff
 * moving most ticks, the rest now and then: the outputs and the filters' states must be the same numbers.
 *   gcc -O2 -Isrc -Isrc/dsp src/host/test_filter_keep.c src/dsp/filter.c -lm */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "filter.h"
#include "PresetStructures.h"
#define dual_filter_init ref_dual_filter_init
#define filter_core_prepare ref_core_prepare
#define dual_filter_prepare ref_dual_filter_prepare
#define filter_core_render_f ref_core_render_f
#define filter_core_render_block ref_core_render_block
#define dual_filter_render_block ref_dual_filter_render_block
#define filter_core_render ref_core_render
#define dual_filter_render_f ref_dual_filter_render_f
#define dual_filter_render ref_dual_filter_render
#define FILTER_NO_KEEP
#include "../dsp/filter.c"
#undef dual_filter_init
#undef filter_core_prepare
#undef dual_filter_prepare
#undef filter_core_render_f
#undef filter_core_render_block
#undef dual_filter_render_block
#undef filter_core_render
#undef dual_filter_render_f
#undef dual_filter_render
static uint32_t rs = 12345u;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
int main(void)
{
    long bad = 0, n_same = 0, n_lp = 0, blocks = 0, zs = 0;
    for (int trial = 0; trial < 4000; trial++) {
        DualFilterMatrix a, b;
        ref_dual_filter_init(&a);
        dual_filter_init(&b);
        int flav = rnd() % FLT_COUNT, cut = rnd() % (128 << 8), res = rnd() % (128 << 8), morph = rnd() % (128 << 8), drive = rnd() % 128;
        if (trial % 3 == 0) morph = 0;                       /* the plain lowpass, often */
        if (trial % 7 == 0) drive = 0;
        float amp = (float)(rnd() % 40000);
        for (int tick = 0; tick < 60; tick++) {
            uint32_t r = rnd();
            int what = r % 10;
            if (what < 4) cut = (cut + (int)(rnd() % 2001) - 1000) & 0x7FFF;   /* the cutoff moves (an envelope, an LFO) */
            else if (what == 4) res = rnd() % (128 << 8);
            else if (what == 5) morph = (rnd() & 1) ? 0 : (int)(rnd() % (128 << 8));
            else if (what == 6) drive = rnd() % 128;
            else if (what == 7 && (rnd() % 8) == 0) flav = rnd() % FLT_COUNT;   /* (rarely: the flavour) */
            /* else: nothing moves */
            int shift = 4 + (int)(rnd() & 1), snap = tick == 0 || (rnd() % 23) == 0;
            int cq = cut > (127 << 8) ? (127 << 8) : cut;
            float x[32], ya[32], yb[32];
            int n = 1 << shift;
            for (int i = 0; i < n; i++) x[i] = ((float)(int32_t)(rnd() % 65536) - 32768.f) * amp * (1.f / 32768.f);
            ref_core_prepare(&a.core[0], cq, res, morph, drive, flav, (uint8_t)shift, snap);
            ref_core_render_block(&a.core[0], x, ya, n);
            n_same += b.core[0].ck_ok && b.core[0].ck_res == (res > (115 << 8) ? (115 << 8) : res) && b.core[0].ck_morph == morph && b.core[0].ck_drive == drive && b.core[0].flavour == flav;
            filter_core_prepare(&b.core[0], cq, res, morph, drive, flav, (uint8_t)shift, snap);
            filter_core_render_block(&b.core[0], x, yb, n);
            n_lp += b.core[0].lp && (flav == FLT_MULTI || flav == FLT_SH2);
            blocks++;
            for (int i = 0; i < n; i++) {
                if (ya[i] != yb[i]) { bad++; if (bad < 5) printf("DIFF trial %d tick %d i %d flav %d: %.9g %.9g\n", trial, tick, i, flav, ya[i], yb[i]); }
                else if (memcmp(&ya[i], &yb[i], 4)) zs++;       /* equal, another zero's sign */
            }
            /* the state the next tick starts from, the kept fields apart */
            if (memcmp(&a.core[0], &b.core[0], offsetof(SvfCore, flavour)) && bad < 5) {
                const float *fa = (const float *)&a.core[0], *fb = (const float *)&b.core[0];
                for (unsigned q = 0; q < offsetof(SvfCore, flavour) / 4; q++)
                    if (fa[q] != fb[q]) { bad++; printf("STATE trial %d tick %d word %u flav %d: %.9g %.9g\n", trial, tick, q, flav, fa[q], fb[q]); break; }
            }
        }
    }
    printf("%ld blocks, %ld with kept values, %ld through a lowpass loop, %ld zero-sign differences, %ld DIFFERENCES\n", blocks, n_same, n_lp, zs, bad);
    return bad != 0;
}
