/* Host preview of the SEQ page renderer (src/ui_seq_draw.c), and a check of its dirty-region redraw. Test scaffolding
 * only: the renderer draws on felucca_canvas_shim.h's stand-in canvas; text is logged with its screen position, colour
 * and clip, and composited by seq_screen_png.py (Pillow) with the same advances the shim measures with.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/render_seq_screen.c -o render_seq_screen
 *   render_seq_screen OUTDIR          writes OUTDIR/seq_<scene>.bin + .log, then per scene:
 *   python src/host/seq_screen_png.py png OUTDIR/seq_live.bin OUTDIR/seq_live.log seq_live.png
 * Exit code 0 = every text fitted its place and the redraw checks passed. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/midi_clock.h"
#include "dsp/maq_seq.h"
#include "felucca_canvas_shim.h"

static FILE *tlog;
static int blits;
static long blit_px;

/* every region the renderer sends: count it, and log its text with screen position and clip for seq_screen_png.py */
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

/* ------------------------------------------------------------ scenes --- */
static MaqSeq q;
static SequencerLane lanes[SEQ_NLANE];
static ArpState arp;
static UIStateMachine sm;
static uint32_t now;

static void sink(uint8_t st, uint8_t d1, uint8_t d2) { (void)st; (void)d1; (void)d2; }
static void run(uint32_t frames) { while (frames--) { midi_clock_frame(now); maq_frame(&q, now); now++; } }


static void pattern(void)
{
    static const uint8_t mel[16] = {48, 55, 60, 51, 63, 127, 58, 53, 48, 60, 67, 65, 62, 58, 55, 50};
    int i;
    preset_defaults();
    midi_clock_init();
    seq_defaults(lanes);
    now = 0;
    for (i = 0; i < 16; i++) {
        lanes[0].steps[i].note_buffer[0] = mel[i];
        lanes[1].steps[i].value = (uint8_t)(20 + i * 7);
        lanes[2].steps[i].value = (uint8_t)((i * 53 + 31) % 128);
        lanes[3].steps[i].value = (uint8_t)(BIPOLAR_CENTER + (i % 4 == 3 ? 12 : i % 4 == 1 ? -5 : 0));
    }
    lanes[0].steps[3].note_buffer[1] = 55; lanes[0].steps[3].note_buffer[2] = 58;   /* a chord */
    lanes[0].steps[9].flags = SEQ_FLAG_MUTE;
    lanes[0].steps[12].flags = SEQ_FLAG_SKIP;
    lanes[1].target_mask = SEQ_TGT_LANE1 | SEQ_TGT_LANE3;
    lanes[2].loop_start = 2; lanes[2].loop_end = 13; lanes[2].playback_mode = SEQ_MODE_PENDULUM1;
    lanes[3].event_type = SEQEV_TRANSPOSE; lanes[3].target_mask = 0; lanes[3].prescale = 12;
    lanes[3].playback_mode = SEQ_MODE_RANDOM;
    maq_init(&q, lanes, &perf_state, sink);
    memset(&arp, 0, sizeof arp);
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], lanes, &arp);
    ui_sm_attach_seq(&sm, &q);
    ui_sm_goto(&sm, UIPAGE_SEQ);
}

static int shot(const char *dir, const char *name)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/seq_%s.log", dir, name);
    tlog = fopen(p, "w");
    if (!tlog) return 1;
    printf("%s:\n", name);
    blits = 0; blit_px = 0;
    seq_draw(&sm, &q, 1);
    fclose(tlog);
    tlog = NULL;
    printf("  forced redraw: %d regions, %ld px\n", blits, blit_px);
    snprintf(p, sizeof p, "%s/seq_%s.bin", dir, name);
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

    /* live play, ORANGE: lane 1 active, knobs on steps 5..8, the cursor on EV:, playing. Lane 4 transposes the only
     * NOTE lane, so its TARGET is hidden */
    seq_palette_set(SQPAL_ORANGE);
    pattern();
    sm.seq_group = 1;
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);              /* LANE -> EV */
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    run(5512u * 6u + 100u);
    fails |= shot(dir, "live");

    /* the dirty-region redraw: nothing changed -> nothing drawn; one step on -> only what moved */
    blits = 0;
    seq_draw(&sm, &q, 0);
    printf("  unchanged frame: %d regions\n", blits);
    if (blits != 0) { printf("FAIL: an unchanged frame redrew %d regions\n", blits); fails = 1; }
    run(5513u);
    blits = 0; blit_px = 0;
    seq_draw(&sm, &q, 0);
    printf("  one 1/16 step later: %d regions, %ld px (%ld%% of the screen)\n", blits, blit_px, blit_px * 100 / (240 * 240));
    if (blits == 0 || blits > 2 * (int)SEQ_NLANE) { printf("FAIL: a step redrew %d regions\n", blits); fails = 1; }
    ui_sm_seq_knob_turn(&sm, 2, 3);
    blits = 0;
    seq_draw(&sm, &q, 0);
    printf("  knob 3 turned: %d regions\n", blits);
    if (blits != 2) { printf("FAIL: a knob turn should redraw the knob cards and lane 1's grid, got %d\n", blits); fails = 1; }

    /* step record on step 7: knobs = lanes 1..4, the frame on one column, REC in the header; the cursor on LANE */
    seq_palette_set(SQPAL_ORANGE);
    pattern();
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    run(1);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 6);
    run(1);
    fails |= shot(dir, "record");

    /* RED palette, stopped, DUAL with lane 1 on [BOTH], MIDI clock. Lane 3 is a second NOTE lane, so lane 4's TRANSPOSE
     * shows its 4 slots (lanes 2 and 4 ruins: not pickable); lane 2 active with the cursor on its TARGET slots */
    seq_palette_set(SQPAL_RED);
    pattern();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.clock_source = CLKSRC_MIDI;
    lanes[0].target_mask = SEQ_TGT_BOTH;
    lanes[2].event_type = SEQEV_NOTE; lanes[2].target_mask = SEQ_TGT_L2;
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);                /* LANE row: 1 -> 2 */
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 2);              /* -> TARGET */
    sm.seq_group = 3;
    fails |= shot(dir, "stopped");
    if (ui_sm_row_state(&sm, 2) != UIROW_ACTIVE || sm.s.current_param_focus != 2) {
        printf("FAIL: the cursor should sit on lane 2's TARGET\n"); fails = 1;
    }

    /* live play, ORANGE, the cursor on LANE: lane 2 on its foggy ground, its knob group (steps 9..12) on the textured
     * block; lane 2 loops steps 3..13 so the grass runs short of both ends */
    seq_palette_set(SQPAL_ORANGE);
    pattern();
    lanes[1].loop_start = 2; lanes[1].loop_end = 12;
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);                /* LANE row: 1 -> 2 */
    sm.seq_group = 2;
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    run(5512u * 3u + 100u);
    fails |= shot(dir, "lane");

    /* SCALE (2026-10-16): the widest footer, DUAL with lane 1 on [BOTH] in HMin from C#, the cursor on its SCALE;
     * lane 3 a second NOTE lane on Genr (no ROOT) */
    seq_palette_set(SQPAL_ORANGE);
    pattern();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    lanes[0].target_mask = SEQ_TGT_BOTH; lanes[0].scale = SEQSCALE_HARM_MINOR; lanes[0].scale_root = 1;
    lanes[2].event_type = SEQEV_NOTE; lanes[2].target_mask = SEQ_TGT_L2; lanes[2].scale = SEQSCALE_GENRE;
    sm.s.active_seq_lane = 0;
    sm.s.current_param_focus = 3;
    fails |= shot(dir, "scale");
    lanes[0].scale = SEQSCALE_OFF; lanes[2].scale = SEQSCALE_OFF;

    printf("text cut: %d, text past its canvas: %d\n", shim_cuts, shim_overflows);
    fails |= shim_cuts != 0 || shim_overflows != 0;
    printf(fails ? "render checks FAILED\n" : "render checks passed\n");
    return fails;
}
