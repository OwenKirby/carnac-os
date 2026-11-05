/* Host preview of the REC page (src/ui_rec_draw.c) on the running synth, and a check of its partial redraw: after
 * frames of playhead strips and sounding stones, the screen must equal a whole redraw of the same moment. Test
 * scaffolding, as render_ui_screens.c.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/render_rec_screen.c -o render_rec_screen
 *   render_rec_screen OUTDIR          writes OUTDIR/rec_<scene>.bin + .log (seq_screen_png.py png makes the PNGs)
 * Exit code 0 = the redraw checks passed. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/voice_allocator.h"
#include "dsp/synth_voice.h"
#include "dsp/oscillators.h"
#include "dsp/lfo.h"
#include "dsp/midi_clock.h"
#include "dsp/wavetables.h"
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
#include "../ui_page_draw.c"

static UIStateMachine sm;
static int32_t audio[2 * 256];
static int fails;
static uint16_t fb_partial[240 * 240];

static void run(uint32_t frames)
{
    while (frames) {
        uint32_t n = frames > 256u ? 256u : frames;
        synth_render_q15(audio, n);
        frames -= n;
    }
}
/* frames of t recorder ticks at the default 120 BPM (a pulse ~918.75 frames) */
static uint32_t ticks(uint32_t t) { return (uint32_t)((uint64_t)t * 3675u / 16u); }

static void key(uint8_t ch, uint8_t n, uint8_t vel) { synth_midi_3((uint8_t)((vel ? 0x90u : 0x80u) | ch), n, vel); }

static void shot(const char *dir, const char *name)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/rec_%s.log", dir, name);
    tlog = fopen(p, "w");
    ui_screen_draw(&sm, synth_seq(), synth_arp(), 1);
    fclose(tlog);
    tlog = NULL;
    snprintf(p, sizeof p, "%s/rec_%s.bin", dir, name);
    f = fopen(p, "wb");
    if (!f) { fails = 1; return; }
    fwrite(shim_fb, sizeof shim_fb, 1, f);
    fclose(f);
    printf("  rec_%s\n", name);
}

/* UI frames of ~16 ms, drawn partially; then the screen against a whole redraw of the same moment */
static void play_frames(int n, const char *what)
{
    int i;
    blit_px = 0;
    for (i = 0; i < n; i++) {
        run(706);
        ui_sm_tick(&sm, 16);
        ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
    }
    printf("  %-28s %ld px a frame drawn\n", what, blit_px / n);
    memcpy(fb_partial, shim_fb, sizeof fb_partial);
    ui_screen_draw(&sm, synth_seq(), synth_arp(), 1);
    {
        long d = 0;
        int x0 = 240, y0 = 240, x1 = -1, y1 = -1;
        for (i = 0; i < 240 * 240; i++)
            if (fb_partial[i] != shim_fb[i]) {
                d++;
                if (i % 240 < x0) x0 = i % 240;
                if (i % 240 > x1) x1 = i % 240;
                if (i / 240 < y0) y0 = i / 240;
                if (i / 240 > y1) y1 = i / 240;
            }
        printf("  %-28s partial == whole: %ld px differ\n", what, d);
        if (d) {
            printf("    in x %d..%d y %d..%d\n", x0, x1, y0, y1);
            fails = 1;
        }
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    Krec *k;
    int i;
    seq_palette_set(SQPAL_ORANGE);
    synth_init();
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], seq_lanes, &arp_state);
    ui_sm_attach_seq(&sm, synth_seq());
    ui_sm_attach_arp(&sm, synth_arp());
    ui_sm_attach_instrument(&sm, &instrument);
    ui_sm_attach_rec(&sm, synth_rec());
    k = synth_rec();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.midi_ch[0] = 0;
    perf_state.midi_ch[1] = 1;
    ui_sm_enforce(&sm);
    run(5000);

    /* REC from HOME: the page opens, armed */
    ui_sm_goto(&sm, UIPAGE_HOME1);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    if (sm.page != UIPAGE_REC || k->state != KREC_ARMED) { printf("FAIL: REC did not open the page armed\n"); fails = 1; }
    shot(dir, "armed");

    /* the first pass: a 4-bar bass on layer 1, closed by REC */
    {
        static const uint8_t B[8] = {38, 38, 41, 36, 38, 38, 43, 41};
        for (i = 0; i < 4; i++) {
            key(0, B[i], 100); run(ticks(140)); key(0, B[i], 0); run(ticks(100));
            key(0, (uint8_t)(B[i] + 12), 80); run(ticks(60)); key(0, (uint8_t)(B[i] + 12), 0); run(ticks(84));
        }
        shot(dir, "recording");
        ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
        if (k->state != KREC_ODUB || k->len != 4u * KREC_BAR) {
            printf("FAIL: closed state %u len %u\n", k->state, k->len);
            fails = 1;
        }
    }
    /* overdub: chord stabs on layer 1, then the lead on layer 2, the wheel swelling */
    for (i = 0; i < 4; i++) {
        run(ticks(96));
        key(0, 57, 70); key(0, 60, 64); run(ticks(40)); key(0, 57, 0); key(0, 60, 0);
        run(ticks(384 - 136));
    }
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);    /* PLAY */
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);    /* ODUB again: a new pass */
    for (i = 0; i < 10; i++) {
        uint8_t n = (uint8_t)(62 + (i * 5) % 12);
        key(1, n, (uint8_t)(60 + i * 6)); run(ticks(70)); key(1, n, 0); run(ticks(80));
        synth_midi_3(0xB0, 1, (uint8_t)(i * 12));
    }
    play_frames(40, "overdubbing, 40 frames");
    shot(dir, "overdub");

    /* playing, DENSITY down, GATE short: the thinned stones outlined */
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);    /* PLAY */
    k->set.density = 60;
    k->set.gate = 30;
    play_frames(60, "playing, thinned, 60 frames");
    shot(dir, "thinned");
    k->set.density = 127;
    k->set.gate = 64;

    /* the DOUBLE prompt from the footer */
    sm.s.current_param_focus = 2u;
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    if (sm.dialog != UIDLG_CONFIRM || sm.rec_ask != 1u) { printf("FAIL: DOUBLE asked nothing\n"); fails = 1; }
    shot(dir, "double");
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);
    if (k->len != 8u * KREC_BAR) { printf("FAIL: DOUBLE: len %u\n", k->len); fails = 1; }
    play_frames(30, "doubled, 30 frames");
    shot(dir, "doubled");

    /* held REC: UNDO the DOUBLE */
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC_UNDO);
    if (k->len != 4u * KREC_BAR) { printf("FAIL: UNDO DOUBLE: len %u\n", k->len); fails = 1; }
    shot(dir, "undone");
    ui_sm_tick(&sm, 1000);
    play_frames(20, "after the UNDO word, 20 frames");

    /* (user, 2026-11-04) REC from another page with a loop: back to the REC page, the loop as it was */
    {
        uint8_t st = k->state, pl = k->playing;
        ui_sm_goto(&sm, UIPAGE_HOME1);
        ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
        if (sm.page != UIPAGE_REC || k->state != st || k->playing != pl) {
            printf("FAIL: REC from HOME: page %u state %u (was %u)\n", sm.page, k->state, st);
            fails = 1;
        }
        ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);    /* on the page: the cycle */
        if (k->state == st) { printf("FAIL: REC on the page did not act\n"); fails = 1; }
    }

    if (fails) { printf("render_rec_screen: FAILED\n"); return 1; }
    printf("render_rec_screen: OK\n");
    return 0;
}
