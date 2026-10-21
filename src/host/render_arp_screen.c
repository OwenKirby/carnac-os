/* Host preview of the ARP page renderer (src/ui_arp_draw.c), and a check of its dirty-region redraw. Test scaffolding
 * only, as render_seq_screen.c: the renderer draws on felucca_canvas_shim.h's stand-in canvas, text is logged with its
 * screen position, colour and clip, and composited by seq_screen_png.py.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/render_arp_screen.c -o render_arp_screen
 *   render_arp_screen OUTDIR          writes OUTDIR/arp_<scene>.bin + .log, then per scene:
 *   python src/host/seq_screen_png.py png OUTDIR/arp_play.bin OUTDIR/arp_play.log arp_play.png
 * Exit code 0 = every text fitted its place and the redraw checks passed. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/midi_clock.h"
#include "dsp/maq_seq.h"
#include "dsp/arp_seq.h"
#include "felucca_canvas_shim.h"

static FILE *tlog;
static int blits;
static long blit_px;

static void shim_on_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const ShimText *t, int n)
{
    int i;
    blits++;
    blit_px += (long)w * (long)h;
    for (i = 0; i < n; i++)
        if (tlog)
            fprintf(tlog, "%d %d %c %u %u %u %u %u\t%s\n", t[i].x + (int)x, t[i].y + (int)y, t[i].face, t[i].col,
                    x, y, x + w, y + h, t[i].s);
}

#include "../ui_seq_draw.c"
#include "../ui_arp_draw.c"

static MaqSeq q;
static CarpSeq a;
static SequencerLane lanes[SEQ_NLANE];
static UIStateMachine sm;
static uint32_t now;

static void sink(uint8_t st, uint8_t d1, uint8_t d2) { (void)st; (void)d1; (void)d2; }
static void run(uint32_t frames)
{
    while (frames--) {
        midi_clock_frame(now);
        maq_frame(&q, now);
        carp_frame(&a, now, &q);
        now++;
    }
}

/* a captured pattern: 6 of 8 steps (one a rest, one a TIE, two accented), mixed styles (a HOLD strummed down),
 * rhythms and rates, Neo_Soul_1 */
static void pattern(void)
{
    static const uint8_t root[8] = {53, 58, 55, ARP_CHORD_EMPTY, 60, ARP_CHORD_TIE, 62, 65};
    static const uint8_t style[8] = {ARPSTYLE_UP, ARPSTYLE_BLOCK, ARPSTYLE_HOLD, 0, ARPSTYLE_STRUM_UP, ARPSTYLE_RANDOM,
                                     ARPSTYLE_DOWN, ARPSTYLE_STRUM_DN};
    static const uint8_t var[8] = {1, 8, 13, 0, 3, 19, 10, 22};
    static const uint8_t rate[8] = {ARPDIV_1_4, ARPDIV_1_8, ARPDIV_1_4T, ARPDIV_1_4, ARPDIV_1_2, ARPDIV_1_4,
                                    ARPDIV_1_16, ARPDIV_1_8T};
    int i;
    preset_defaults();
    midi_clock_init();
    seq_defaults(lanes);
    now = 0;
    for (i = 0; i < 8; i++) {
        arp_state.steps[i].chord_root = root[i];
        arp_state.steps[i].style = style[i];
        arp_state.steps[i].variation = var[i];
        arp_state.steps[i].rate = rate[i];
        arp_state.steps[i].octave_range = (uint8_t)(i % 3);
        arp_state.steps[i].accent = (uint8_t)(i == 0 || i == 4);
    }
    arp_state.sequence_length = 6;
    arp_state.gate_length = 80;
    arp_state.current_genre_bank = 66;               /* Neo_Soul_1 */
    maq_init(&q, lanes, &perf_state, sink);
    carp_init(&a, &arp_state, &perf_state, sink);
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], lanes, &arp_state);
    ui_sm_attach_seq(&sm, &q);
    ui_sm_attach_arp(&sm, &a);
    ui_sm_goto(&sm, UIPAGE_ARP);
}

static int shot(const char *dir, const char *name)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/arp_%s.log", dir, name);
    tlog = fopen(p, "w");
    if (!tlog) return 1;
    printf("%s:\n", name);
    blits = 0; blit_px = 0;
    arp_draw(&sm, &q, &a, 1);
    fclose(tlog);
    tlog = NULL;
    printf("  forced redraw: %d regions, %ld px\n", blits, blit_px);
    snprintf(p, sizeof p, "%s/arp_%s.bin", dir, name);
    f = fopen(p, "wb");
    if (!f) return 1;
    fwrite(shim_fb, sizeof shim_fb, 1, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int fails = 0;

    /* playing, ORANGE, transposed +3 with TRANSPOSE MAQ, step 5's key held (its card inverted), the cursor on GENRE */
    seq_palette_set(SQPAL_ORANGE);
    pattern();
    arp_state.transpose_maq_option = 1;
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 4);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    run(22050u * 2u + 100u);
    ui_sm_arp_key(&sm, 3, 56, 1, 100);               /* G#3: +3 */
    run(10);
    ui_sm_arp_key(&sm, 3, 56, 0, 0);
    ui_sm_arp_key(&sm, 20, 74, 1, 100);              /* (from F3: D5 is the 5th step key) */
    run(10);
    fails |= shot(dir, "play");

    blits = 0;
    arp_draw(&sm, &q, &a, 0);
    printf("  unchanged frame: %d regions\n", blits);
    if (blits != 0) { printf("FAIL: an unchanged frame redrew %d regions\n", blits); fails = 1; }
    run(22050u);
    blits = 0; blit_px = 0;
    arp_draw(&sm, &q, &a, 0);
    printf("  one step later: %d regions, %ld px (%ld%% of the screen)\n", blits, blit_px, blit_px * 100 / (240 * 240));
    if (blits == 0 || blits > 4) { printf("FAIL: a step redrew %d regions\n", blits); fails = 1; }

    /* stopped, RED, DUAL with LAYER BOTH, the cursor on GATE */
    seq_palette_set(SQPAL_RED);
    pattern();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    arp_state.target_layer = ARPLAYER_BOTH;
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
    fails |= shot(dir, "stopped");

    /* stopped, ORANGE: REC armed (step entry, two chords in: the cursor on step 3), LATCH on, the cursor on ACCENT VEL,
     * the knobs RATE 1/16T and HOLD strummed UP 5 */
    seq_palette_set(SQPAL_ORANGE);
    pattern();
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 5);
    sm.arp_knob[0] = ARPDIV_1_16T;                   /* the knobs: RATE 1/16T, STYLE HOLD (knob 4 then STRUM) */
    sm.arp_knob[2] = ARPSTYLE_HOLD;
    sm.arp_knob[3] = 5;
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    ui_sm_arp_key(&sm, 15, 68, 1, 100);              /* G#4: LATCH */
    ui_sm_arp_key(&sm, 15, 68, 0, 0);
    ui_sm_arp_key(&sm, 0, 53, 1, 100); run(10); ui_sm_arp_key(&sm, 0, 53, 0, 0);
    ui_sm_arp_key(&sm, 7, 60, 1, 100); run(10); ui_sm_arp_key(&sm, 7, 60, 0, 0);
    run(10);
    fails |= shot(dir, "rec");

    printf("text cut: %d, text past its canvas: %d\n", shim_cuts, shim_overflows);
    fails |= shim_cuts != 0 || shim_overflows != 0;
    printf(fails ? "render checks FAILED\n" : "render checks passed\n");
    return fails;
}
