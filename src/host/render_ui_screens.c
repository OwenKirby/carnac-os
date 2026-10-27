/* Host preview of every page through the screen's one entry point (src/ui_page_draw.c ui_screen_draw, which hands SEQ
 * and ARP to their own renderers), and a check of the dirty-region redraw. Test scaffolding only, as
 * render_seq_screen.c: the renderers draw on felucca_canvas_shim.h's stand-in canvas, text is logged with its screen
 * position, colour and clip, and composited by seq_screen_png.py. The synth core runs underneath (synth_init, notes,
 * synth_render_q15), so the live visualizers (voices, envelopes, LFOs, the scan) show what a playing synth gives them.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/render_ui_screens.c -o render_ui_screens
 *   render_ui_screens OUTDIR          writes OUTDIR/ui_<scene>.bin + .log, then per scene:
 *   python src/host/seq_screen_png.py png OUTDIR/ui_home.bin OUTDIR/ui_home.log ui_home.png
 * Exit code 0 = every text fitted its place and the redraw checks passed. */
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

static void run(uint32_t frames)
{
    while (frames) {
        uint32_t n = frames > 256u ? 256u : frames;
        synth_render_q15(audio, n);
        frames -= n;
    }
}

static void note(uint8_t ch, uint8_t n, uint8_t vel)
{
    synth_midi_3((uint8_t)((vel ? 0x90u : 0x80u) | ch), n, vel);
}

static void fresh(void)
{
    synth_init();
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], seq_lanes, &arp_state);
    ui_sm_attach_seq(&sm, synth_seq());
    ui_sm_attach_arp(&sm, synth_arp());
    ui_sm_attach_instrument(&sm, &instrument);
}

static void shot(const char *dir, const char *name)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/ui_%s.log", dir, name);
    tlog = fopen(p, "w");
    if (!tlog) { fails = 1; return; }
    blits = 0; blit_px = 0;
    ui_screen_draw(&sm, synth_seq(), synth_arp(), 1);
    fclose(tlog);
    tlog = NULL;
    printf("  %-14s %2d regions, %6ld px\n", name, blits, blit_px);
    snprintf(p, sizeof p, "%s/ui_%s.bin", dir, name);
    f = fopen(p, "wb");
    if (!f) { fails = 1; return; }
    fwrite(shim_fb, sizeof shim_fb, 1, f);
    fclose(f);
}

static void go(uint8_t page) { ui_sm_goto(&sm, page); }

/* ASCII into char_map indices (anything not in the map: a space) */
static void put_name(uint8_t *dst, const char *s)
{
    static const char map[] = UI_CHAR_MAP;
    uint32_t i, k;
    for (i = 0; i < NAME_LEN; i++) {
        dst[i] = 0u;
        for (k = 1; *s && k < CHAR_MAP_LEN; k++)
            if (map[k] == *s) dst[i] = (uint8_t)k;
        if (*s) s++;
    }
}

/* the save scenes' store: slot 24 holds "OLD DUSK" (a patch, a performance) or an unnamed record, the rest are empty */
static uint8_t stub_query(void *ctx, uint8_t target, uint8_t slot, uint8_t name[NAME_LEN])
{
    (void)ctx;
    if (slot != 24u) return UISLOT_EMPTY;
    if (target == UISAVE_PATCH_PRESET || target == UISAVE_WHOLE_PERF) put_name(name, "OLD DUSK");
    return UISLOT_USED;
}

/* the preset list's store: a handful of named patches and performances, SEQ slots 2 and 9, no ARP pattern */
static uint8_t list_query(void *ctx, uint8_t target, uint8_t slot, uint8_t name[NAME_LEN])
{
    static const struct { uint8_t slot; const char *name; } P[] = {
        {3, "CARNAC DRONE"}, {7, "HEEL STONE"}, {12, "BARROW BASS"}, {24, "OLD DUSK"}, {31, "SARSEN PAD"},
        {40, ""}, {55, "MENHIR LEAD"}, {88, "GLASS PAD"}, {90, "HARVEST MOON"}, {101, "AVEBURY BELLS"},
    };
    uint32_t i;
    (void)ctx;
    if (target == UISAVE_ARP_SEQ) return UISLOT_EMPTY;
    if (target == UISAVE_SEQ_SETTINGS) return slot == 2u || slot == 9u ? (uint8_t)UISLOT_USED : (uint8_t)UISLOT_EMPTY;
    for (i = 0; i < sizeof P / sizeof P[0]; i++)
        if (P[i].slot == slot) {
            put_name(name, P[i].name);
            return UISLOT_USED;
        }
    return UISLOT_EMPTY;
}

/* the Preset Encoder turned: the load the caller would run, done */
static void preset_turn(int8_t d)
{
    ui_sm_handle(&sm, UIEV_PRESET_ENC, d);
    if (sm.load_pending) ui_sm_load_done(&sm, 1u);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int i;
    EngineState *e;
    seq_palette_set(SQPAL_ORANGE);

    /* HOME: DUAL, a chord on each layer */
    fresh();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    ui_sm_enforce(&sm);
    note(0, 48, 100); note(0, 55, 90); note(1, 67, 70); note(1, 72, 110);
    run(9000);
    go(UIPAGE_HOME1);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
    shot(dir, "home1");
    blits = 0;
    ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
    printf("  unchanged frame: %d regions\n", blits);
    if (blits != 0) {
        /* the voices are still moving (envelopes): a quiet synth must send nothing */
        note(0, 48, 0); note(0, 55, 0); note(1, 67, 0); note(1, 72, 0);
        run(44100 * 4);
        ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
        blits = 0;
        ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
        printf("  unchanged frame, voices quiet: %d regions\n", blits);
        if (blits != 0) { printf("FAIL: a still screen redrew %d regions\n", blits); fails = 1; }
        note(0, 48, 100); note(0, 55, 90); note(1, 67, 70); note(1, 72, 110);
        run(9000);
    }
    blits = 0; blit_px = 0;
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
    ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
    printf("  cursor moved: %d regions, %ld px\n", blits, blit_px);
    go(UIPAGE_HOME3);
    perf_state.split_note = 60; perf_state.split_fade = 24; perf_state.vel_split = 80; perf_state.vel_fade = 30;
    shot(dir, "home3");

    /* OSC: DUAL OSC, a note held, the scan moving with ENV3 */
    fresh();
    e = &engine_state[0];
    e->arch = VARCH_DUAL_OSC;
    e->osc[0].scan = 70; e->osc[0].scan_mod_amt = 40; e->osc[0].wavebank = 3;
    e->osc[1].wavebank = 6; e->osc[1].scan = 30;
    note(0, 57, 100);
    run(5000);
    go(UIPAGE_OSC1);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 2);
    shot(dir, "osc1");
    go(UIPAGE_OSC2);
    shot(dir, "osc2");
    {                                                /* FINE TUNE (2026-10-17): +-7 semitones; SINGLE has SCAN AMT on knob 3 */
        uint8_t keep = e->arch;
        e->osc[0].fine = 100;                        /* +229 cents */
        ui_sm_tick(&sm, UI_ROWPOP_MS);
        go(UIPAGE_OSC1);
        shot(dir, "osc1_fine");
        e->arch = VARCH_SINGLE;
        ui_sm_enforce(&sm);
        go(UIPAGE_OSC1);
        shot(dir, "osc1_single");
        e->arch = keep;
        e->osc[0].fine = 64;
        ui_sm_enforce(&sm);
    }
    e->oscmix.balance = 40; e->oscmix.sub_level = 60; e->oscmix.noise_level = 20; e->oscmix.noise_colour = 90;
    go(UIPAGE_OSCMIX);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 4);
    shot(dir, "oscmix");
    e->arch = VARCH_SPREAD; e->oscmix.spread = 70; e->oscmix.sub_level = 0;
    ui_sm_enforce(&sm);
    shot(dir, "oscmix_spr");
    e->arch = VARCH_SINGLE; e->oscmix.sub_octave = SUBOCT_DOWN2; e->oscmix.sub_waveform = SUBWAVE_SQUARE; e->oscmix.sub_level = 70;
    ui_sm_enforce(&sm);
    shot(dir, "oscmix_one");

    /* ATTACK (STRIKE): BOW PLUCK whole, then LITHOPHONE from a quarter in, fading (DECAY 50) */
    e->arch = VARCH_STRIKE; e->oscmix.atk_sample = 16; e->oscmix.atk_level = 110;
    ui_sm_enforce(&sm);
    go(UIPAGE_ATTACK);
    shot(dir, "attack");
    e->oscmix.atk_sample = 6; e->oscmix.atk_start = 64; e->oscmix.atk_decay = 50; e->oscmix.atk_filter = 90;
    e->oscmix.atk_bend = 64 + 24; e->oscmix.atk_bend_mod_amt = 64 - 20;
    shot(dir, "attack_lith");
    go(UIPAGE_OSCMIX);
    shot(dir, "oscmix_strike");
    e->oscmix.atk_start = 0; e->oscmix.atk_decay = 127; e->oscmix.atk_filter = 0; e->oscmix.atk_bend = 64;
    e->oscmix.atk_bend_mod_amt = 64;

    /* FILTER: a resonant bandpass-ish SVF, a ladder; FILTER MIXER in each architecture */
    e->arch = VARCH_DUAL_FILTER;
    e->filter[0].cutoff = 70; e->filter[0].resonance = 90; e->filter[0].morph = 100;
    e->filter[1].cutoff = 50; e->filter[1].resonance = 40; e->filter[1].flavour = FLT_MS20;
    ui_sm_enforce(&sm);
    go(UIPAGE_FILTER1);
    shot(dir, "filter1");
    e->filter[0].flavour = FLT_MULTI;
    shot(dir, "filter1_lad");
    e->filter[0].flavour = FLT_FORMANT; e->filter[0].resonance = 100;
    shot(dir, "filter1_frm");
    e->filter[0].morph = 0;
    shot(dir, "filter1_frm0");
    e->filter[0].flavour = FLT_MULTI; e->filter[0].morph = 0;
    shot(dir, "filter1_lad0");
    e->filter[0].flavour = FLT_MS20; e->filter[0].morph = 0; e->filter[0].drive = 110; e->filter[0].resonance = 110;
    shot(dir, "filter1_ms20");
    e->filter[0].flavour = FLT_SH2; e->filter[0].morph = 100; e->filter[0].resonance = 90;
    go(UIPAGE_FILTER2);
    shot(dir, "filter2");
    e->fltmix.routing = 90; e->fltmix.balance = 70;
    go(UIPAGE_FLTMIX);
    shot(dir, "fltmix_dual");
    e->arch = VARCH_STACK; e->fltmix.stack_width = 100; e->fltmix.balance = 80;
    ui_sm_enforce(&sm);
    shot(dir, "fltmix_stack");
    e->arch = VARCH_BINAURAL; e->fltmix.bin_cutoff_offset = 100; e->fltmix.bin_mod_phase = 40;
    ui_sm_enforce(&sm);
    shot(dir, "fltmix_bin");

    /* ENV and LFO, a note held */
    fresh();
    e = &engine_state[0];
    e->env[0].attack = 40; e->env[0].decay = 70; e->env[0].sustain = 80; e->env[0].release = 90;
    e->lfo[0].shape = 30; e->lfo[0].fade = 100; e->lfo[0].delay = 30;
    e->lfo[1].shape = 76; e->lfo[2].shape = 90;
    note(0, 60, 110);
    run(3000);
    go(UIPAGE_ENV1);
    shot(dir, "env1");
    go(UIPAGE_ENV2);
    shot(dir, "env2");
    e->env[1].invert = 1;                            /* drawn upside down */
    ui_sm_enforce(&sm);
    shot(dir, "env2_inv");
    e->env[1].invert = 0;
    run(20000);
    go(UIPAGE_LFO1);
    shot(dir, "lfo1");
    go(UIPAGE_LFO2);
    shot(dir, "lfo2");
    go(UIPAGE_LFO3);
    shot(dir, "lfo3");

    /* MOD1: lfo1 * env1, a few frames of history */
    e->mod[0].src_a = MSRC_LFO1; e->mod[0].func = MFN_MUL; e->mod[0].src_b = MSRC_ENV1; e->mod[0].amount = 100;
    e->filter[0].mod_src = MSRC_MODSLOT1; e->filter[0].mod_amt = 90;
    e->osc[0].pitch_mod_src = MSRC_MODSLOT1; e->osc[0].pitch_mod_amt = 20;
    e->lfo[1].speed_mod_src = MSRC_MODSLOT1; e->lfo[1].speed_mod_amt = 64;
    e->amp.pan_mod_src = MSRC_MODSLOT1; e->amp.pan_mod_depth = 90;
    e->oscmix.noise_mod_src = MSRC_MODSLOT1; e->oscmix.noise_mod_amt = 64 - 30;
    e->mod[1].src_b = MSRC_MODSLOT1;
    go(UIPAGE_MOD1);
    for (i = 0; i < 100; i++) {
        run(441);
        ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
    }
    shot(dir, "mod1");
    /* .. its destinations are its rows (2026-10-17: every one the architecture has, as the ring of stones): the Select
     * Encoder walks them, the Algorithm Encoder sets the focused one's amount, and routes an unrouted one here */
    {
        uint8_t n = ui_sm_row_count(&sm), r, pre;
        if (n < 10u) { printf("FAIL: MOD1 should ring every destination (%u)\n", n); fails = 1; }
        for (r = 0; r < n && strcmp(ui_sm_row_label(&sm, r), "F1 CUTOFF") != 0; r++) {}
        ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(r - sm.s.current_param_focus));
        if (sm.rowpop) { printf("FAIL: the Select Encoder opened the parameter popup\n"); fails = 1; }
        pre = e->filter[0].mod_amt;
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 5);
        if (!sm.rowpop) { printf("FAIL: the Algorithm Encoder did not open MOD1's destination\n"); fails = 1; }
        if (e->filter[0].mod_amt != pre + 5) { printf("FAIL: the Algorithm Encoder did not set F1 CUTOFF's amount\n"); fails = 1; }
        shot(dir, "mod1_rowpop");
        for (r = 0; r < n && strcmp(ui_sm_row_label(&sm, r), "OSC1 SCAN") != 0; r++) {}
        ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(r - sm.s.current_param_focus));
        if (ui_sm_mod_route(&sm, r) != UIMOD_OTHER) { printf("FAIL: OSC1 SCAN should be fed by ENV3\n"); fails = 1; }
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 30);
        if (e->osc[0].scan_mod_src != MSRC_MODSLOT1 || e->osc[0].scan_mod_amt != 30) {
            printf("FAIL: the amount did not route OSC1 SCAN to MOD1 (%u, %u)\n", e->osc[0].scan_mod_src,
                   e->osc[0].scan_mod_amt);
            fails = 1;
        }
    }
    ui_sm_tick(&sm, UI_ROWPOP_MS);
    shot(dir, "mod1_cursor");
    /* .. at amount 0 the portal is sealed */
    e->mod[0].amount = BIPOLAR_CENTER;
    ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
    shot(dir, "mod1_sealed");
    /* .. the rings bent by LFO1 alone (A + nothing), a sine, then slewed random */
    e->mod[0].src_a = MSRC_LFO1; e->mod[0].func = MFN_ADD; e->mod[0].src_b = MSRC_NONE; e->mod[0].amount = 64 + 60;
    e->lfo[0].rate = 80; e->lfo[0].shape = 0;
    for (i = 0; i < 100; i++) { run(441); ui_screen_draw(&sm, synth_seq(), synth_arp(), 0); }
    shot(dir, "mod1_lfo");
    e->lfo[0].shape = 120;
    for (i = 0; i < 100; i++) { run(441); ui_screen_draw(&sm, synth_seq(), synth_arp(), 0); }
    shot(dir, "mod1_random");
    /* .. ENV1 on a held note, then let go */
    e->mod[0].src_a = MSRC_ENV1; e->env[0].attack = 50; e->env[0].decay = 60; e->env[0].sustain = 60; e->env[0].release = 70;
    note(0, 48, 110);
    for (i = 0; i < 70; i++) { run(441); ui_screen_draw(&sm, synth_seq(), synth_arp(), 0); }
    note(0, 48, 0);
    for (i = 0; i < 30; i++) { run(441); ui_screen_draw(&sm, synth_seq(), synth_arp(), 0); }
    shot(dir, "mod1_env");
    /* .. more than ten destinations: the stones page */
    e->osc[0].scan_mod_src = MSRC_MODSLOT1; e->filter[0].morph_mod_src = MSRC_MODSLOT1;
    e->oscmix.noise_col_mod_src = MSRC_MODSLOT1; e->lfo[0].speed_mod_src = MSRC_NONE;
    e->lfo[2].speed_mod_src = MSRC_MODSLOT1; e->amp.amp_mod_src = MSRC_MODSLOT1; e->oscmix.xmod_mod_src = MSRC_MODSLOT1;
    e->osc[1].scan_mod_src = MSRC_MODSLOT1;
    ui_sm_enforce(&sm);
    if (ui_sm_row_count(&sm) <= 10u) { printf("FAIL: the paging scene needs more than 10 destinations\n"); fails = 1; }
    ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
    shot(dir, "mod1_many");

    /* AMP: DUAL, voices panned by the keyboard */
    fresh();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    engine_state[0].amp.pan_mod_depth = 127; engine_state[1].amp.pan_mod_depth = 127;
    ui_sm_enforce(&sm);
    note(0, 36, 120); note(0, 60, 90); note(1, 72, 80); note(1, 96, 127);
    run(6000);
    go(UIPAGE_AMP);
    shot(dir, "amp");
    engine_state[0].amp.drive = 80; engine_state[0].amp.delay_send = 127; engine_state[0].amp.reverb_send = 100;
    shot(dir, "amp_fx");
    engine_state[0].amp.drive = 0; engine_state[0].amp.delay_send = 0; engine_state[0].amp.reverb_send = 0;
    go(UIPAGE_HOME1);
    shot(dir, "home1_play");

    /* EFFECTS, GLOBAL */
    fresh();
    perf_state.delay_time = 50; perf_state.delay_feedback = 100; perf_state.reverb_decay = 90;
    perf_state.reverb_predelay = 30; perf_state.reverb_type = REVTYPE_TOMB;
    go(UIPAGE_EFFECTS);
    shot(dir, "effects");
    perf_state.delay_type = DELAYTYPE_BBD; perf_state.delay_sync = 1; perf_state.reverb_type = REVTYPE_SPRING;
    shot(dir, "effects_bbd");                     /* the delay type, SYNC and the reverb's name together */
    perf_state.delay_type = DELAYTYPE_TAPE; perf_state.delay_sync = 0; perf_state.reverb_type = REVTYPE_TOMB;
    run(30000);
    go(UIPAGE_GLOBAL);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 7);
    shot(dir, "global");

    /* SAVE: the preset links, the name and the keyboard, the dialogs */
    fresh();
    ui_sm_attach_store(&sm, stub_query, NULL);
    put_name(perf_state.name, "AMBIENT DUSK");
    put_name(engine_state[0].name, "GLASS PAD");
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.layer_patch[0] = 88; perf_state.layer_patch[1] = 12; perf_state.seq_link = 4; perf_state.arp_link = 6;
    ui_sm_enforce(&sm);
    sm.perf_slot = 24;
    go(UIPAGE_PERF);
    shot(dir, "save1");
    {                                                /* the dolmen with something in it: a programmed L1 patch, a lightly
                                                      * edited L2, a written sequence, 5 arp steps; then SINGL */
        EngineState e0 = engine_state[0], e1 = engine_state[1];
        SequencerLane s0 = seq_lanes[0], s1 = seq_lanes[1], s2 = seq_lanes[2];
        ArpState a0 = arp_state;
        uint32_t i;
        for (i = 0; i < 40; i++) ((uint8_t *)&engine_state[0])[i * 3] ^= 0x15u;
        for (i = 0; i < 12; i++) ((uint8_t *)&engine_state[1])[i * 5] ^= 0x15u;
        for (i = 0; i < SEQ_NSTEP; i++) {
            seq_lanes[0].steps[i].note_buffer[0] = (uint8_t)(48 + (i * 5) % 12);
            seq_lanes[1].steps[i].value = (uint8_t)(20 + i * 6);
            seq_lanes[2].steps[i].value = (uint8_t)(60 + (i * 37) % 60);
        }
        for (i = 0; i < 5; i++) arp_state.steps[i].chord_root = (uint8_t)(48 + i);
        shot(dir, "perf_full");
        perf_state.perf_mode = PERF_SINGL_SYNTH;
        ui_sm_enforce(&sm);
        shot(dir, "perf_singl");
        perf_state.perf_mode = PERF_DUAL_SYNTH;
        engine_state[0] = e0; engine_state[1] = e1; seq_lanes[0] = s0; seq_lanes[1] = s1; seq_lanes[2] = s2; arp_state = a0;
        ui_sm_enforce(&sm);
    }
    /* the save page (2026-10-17): SAVE on HOME's browser saves the performance at its slot, 024 (used: OLD DUSK) */
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);
    if (sm.page != UIPAGE_SAVE_COMMIT || sm.save_target != UISAVE_WHOLE_PERF || sm.save_slot != 24u) {
        printf("FAIL: SAVE on the browser did not open the save page on the performance at 024\n");
        fails = 1;
    }
    shot(dir, "save2");
    sm.name_cursor = 3;
    ui_sm_knob_written(&sm, UIPAGE_SAVE_COMMIT, 3u);   /* a name knob: the keyboard */
    shot(dir, "save2_kb");
    ui_sm_tick(&sm, UI_KB_MS);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);             /* 025: empty */
    shot(dir, "save2_empty");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);        /* 024 is used: it asks */
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    shot(dir, "save_over");                          /* OVERWRITE? SLOT 024  OLD DUSK */
    ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);        /* NO */
    if (sm.dialog != UIDLG_NONE) { printf("FAIL: NO left the popup up\n"); fails = 1; }
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);        /* 025 is empty: it saves at once */
    if (sm.dialog != UIDLG_WRITING || !sm.commit_pending) { printf("FAIL: an empty slot asked first\n"); fails = 1; }
    ui_sm_commit_done(&sm, 1);
    shot(dir, "save_done");
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);        /* clears SAVED: back to the browser */
    if (sm.page != UIPAGE_PERF) { printf("FAIL: SAVED did not return to the browser (%u)\n", sm.page); fails = 1; }
    sm.commit_pending = 0u;

    /* the hold strips, and 6.2.3 COPY layer 1's patch, PASTE it into layer 2 (SAVE + OCT- / OCT+) */
    ui_sm_set_hold(&sm, BTN_HOME);
    shot(dir, "hold_home");
    ui_sm_set_hold(&sm, BTN_COUNT);
    go(UIPAGE_FILTER1);
    ui_sm_set_hold(&sm, BTN_SAVE);
    shot(dir, "hold_save_empty");                    /* nothing to paste: PASTE dim */
    ui_sm_save_oct(&sm, 0u);
    ui_sm_set_layer_focus(&sm, 1u);
    shot(dir, "hold_save");                          /* a patch on the clipboard: PASTE lit */
    ui_sm_save_oct(&sm, 1u);
    ui_sm_set_hold(&sm, BTN_COUNT);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    shot(dir, "save_paste");
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);
    if (memcmp(&engine_state[1], &engine_state[0], sizeof(EngineState)) != 0) {
        printf("FAIL: PASTE did not copy layer 1's patch into layer 2\n");
        fails = 1;
    }
    go(UIPAGE_SEQ);                                  /* a sequence: no name, knobs 3 / 4 off */
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);
    if (sm.save_target != UISAVE_SEQ_SETTINGS || ui_sm_keyboard_visible(&sm)) {
        printf("FAIL: SAVE on SEQ did not open the save page on the sequence\n");
        fails = 1;
    }
    shot(dir, "save2_seq");

    /* HOME in its 2026-10-17 order, the parameter list away: LAYER, ZONES, the browser, GLOBAL */
    fresh();
    ui_sm_attach_store(&sm, stub_query, NULL);
    put_name(perf_state.name, "AMBIENT DUSK");
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.split_note = 60;
    sm.perf_knob[UIPERF_VELOCITY] = 100;
    sm.perf_knob[UIPERF_MODWHEEL] = 40;
    sm.perf_knob[UIPERF_CC1] = 90;
    instrument.master_tune = 61u;                    /* -3 cents */
    ui_sm_enforce(&sm);
    go(UIPAGE_HOME1);
    shot(dir, "home_layer");
    go(UIPAGE_HOME3);
    shot(dir, "home_zones");
    go(UIPAGE_PERF);
    shot(dir, "home_perf");
    go(UIPAGE_GLOBAL);
    shot(dir, "home_global");
    instrument.master_tune = 64u;

    /* the preset list: over a patch page, over SEQ, with nothing saved; it closes on its own and on another control */
    fresh();
    ui_sm_attach_store(&sm, list_query, NULL);
    perf_state.layer_patch[0] = 12;
    go(UIPAGE_FILTER1);
    preset_turn(2);                                  /* 12 -> 24 -> 31 */
    shot(dir, "browse_patch");
    if (!sm.browse || sm.perf->layer_patch[0] != 31u) { printf("FAIL: the preset list did not open on 031\n"); fails = 1; }
    ui_sm_tick(&sm, UI_BROWSE_MS - 1u);
    if (!sm.browse) { printf("FAIL: the preset list closed early\n"); fails = 1; }
    ui_sm_tick(&sm, 1u);
    if (sm.browse) { printf("FAIL: the preset list did not time out\n"); fails = 1; }
    shot(dir, "browse_closed");
    preset_turn(-1);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
    if (sm.browse) { printf("FAIL: the Select Encoder left the preset list up\n"); fails = 1; }
    go(UIPAGE_SEQ);
    preset_turn(1);
    shot(dir, "browse_seq");
    go(UIPAGE_ARP);
    preset_turn(1);
    shot(dir, "browse_empty");

    /* the parameter popup (2026-10-17): only the Algorithm Encoder opens it, the Select Encoder moves in the list and
     * puts it away, it times out, a button puts it away */
    fresh();
    go(UIPAGE_FILTER1);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 2);
    if (sm.rowpop) { printf("FAIL: the Select Encoder opened the parameter popup\n"); fails = 1; }
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    if (!sm.rowpop) { printf("FAIL: the Algorithm Encoder did not open the parameter popup\n"); fails = 1; }
    shot(dir, "rowpop_filter");
    ui_sm_tick(&sm, UI_ROWPOP_MS - 10u);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 3);
    ui_sm_tick(&sm, UI_ROWPOP_MS - 10u);
    if (!sm.rowpop) { printf("FAIL: the Algorithm Encoder let the parameter list close\n"); fails = 1; }
    shot(dir, "rowpop_filter_val");
    ui_sm_tick(&sm, 10u);
    if (sm.rowpop) { printf("FAIL: the parameter list did not time out\n"); fails = 1; }
    shot(dir, "rowpop_closed");
    go(UIPAGE_HOME1);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 30);          /* to the last rows: the longest labels */
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    shot(dir, "rowpop_home");
    ui_sm_handle(&sm, UIEV_SELECT_ENC, -1);
    if (sm.rowpop) { printf("FAIL: the Select Encoder left the parameter popup up\n"); fails = 1; }
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_ENV);
    if (sm.rowpop) { printf("FAIL: a button left the parameter list up\n"); fails = 1; }
    go(UIPAGE_ARP);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    shot(dir, "rowpop_arp");

    /* ARP and SEQ through the same entry point */
    fresh();
    go(UIPAGE_ARP);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 2);
    ui_sm_tick(&sm, UI_ROWPOP_MS);                   /* (the parameter list away) */
    shot(dir, "arp");
    go(UIPAGE_SEQ);
    shot(dir, "seq");
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 5);           /* STEPS 1..4 .. 13..16, g, then EVENT TYPE */
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    shot(dir, "rowpop_seq");
    ui_sm_tick(&sm, UI_ROWPOP_MS);

    /* PERF: turning LAYER1 PATCH loads the next stored patch; the popup names it under its number */
    fresh();
    ui_sm_attach_store(&sm, list_query, NULL);
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.layer_patch[0] = 3;
    ui_sm_enforce(&sm);
    go(UIPAGE_PERF);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 2);           /* PERF PRESET, PERF MODE, LAYER1 PATCH */
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    if (!sm.load_pending || sm.load_slot != 7u) { printf("FAIL: LAYER1 PATCH did not ask for patch 7\n"); fails = 1; }
    if (sm.load_pending) ui_sm_load_done(&sm, 1u);
    shot(dir, "rowpop_perf_patch");
    ui_sm_tick(&sm, UI_ROWPOP_MS);

    /* every page in RED, for the text checks: nothing cut, nothing past its canvas */
    seq_palette_set(SQPAL_RED);
    fresh();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    for (i = 0; i < UIPAGE_COUNT; i++) {
        uint8_t a;
        for (a = 0; a < VARCH_COUNT; a++) {
            engine_state[0].arch = a;
            ui_sm_enforce(&sm);
            go((uint8_t)i);
            tlog = NULL;
            ui_screen_draw(&sm, synth_seq(), synth_arp(), 1);
        }
    }
    go(UIPAGE_MOD2);
    shot(dir, "red_mod2");

    /* one palette for both layers: the same text checks with LAYER 2 in focus, every page still in RED; then a shot of
     * each layer's FILTER 1 (the header's L1 / L2), SEQ and HOME in the default ORANGE */
    ui_sm_set_layer_focus(&sm, 1u);
    for (i = 0; i < UIPAGE_COUNT; i++) {
        uint8_t a;
        for (a = 0; a < VARCH_COUNT; a++) {
            engine_state[1].arch = a;
            ui_sm_enforce(&sm);
            go((uint8_t)i);
            tlog = NULL;
            ui_screen_draw(&sm, synth_seq(), synth_arp(), 1);
            if (sq_pal != SQPAL_RED) {
                printf("FAIL: layer 2's page %d not in the one palette (%u)\n", i, sq_pal);
                fails = 1;
            }
        }
    }
    seq_palette_set(SQPAL_ORANGE);
    engine_state[1].arch = VARCH_DUAL_FILTER;
    ui_sm_enforce(&sm);
    go(UIPAGE_FILTER1);
    shot(dir, "l2_filter1");
    ui_sm_set_layer_focus(&sm, 0u);
    shot(dir, "l1_filter1");
    if (sq_pal != SQPAL_ORANGE) { printf("FAIL: layer 1's FILTER 1 not in ORANGE\n"); fails = 1; }
    go(UIPAGE_SEQ);
    ui_sm_set_layer_focus(&sm, 1u);                  /* SEQ is both layers': the focus changes nothing */
    shot(dir, "seq_l2");
    if (sq_pal != SQPAL_ORANGE) { printf("FAIL: SEQ changed colour with the layer\n"); fails = 1; }
    go(UIPAGE_HOME1);
    shot(dir, "home1_l2");
    if (sq_pal != SQPAL_ORANGE) { printf("FAIL: HOME changed colour with the layer\n"); fails = 1; }

    /* GLOBAL > THEME: the row picks the palette for every page; a shot of FILTER 1 and the parameter popup in each */
    {
        static const char *const TN[UITHEME_COUNT] = {"samhain", "bloodmoon", "fire", "solstice", "lichen", "heather",
                                                      "ritual", "granite", "oak"};
        uint8_t t, r, n;
        char nm[40];
        fresh();
        go(UIPAGE_GLOBAL);
        n = ui_sm_row_count(&sm);
        for (r = 0; r < n && strcmp(ui_sm_row_label(&sm, r), "THEME") != 0; r++) {}
        if (r == n) { printf("FAIL: no THEME row on GLOBAL\n"); fails = 1; }
        ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(r - sm.s.current_param_focus));
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 2);
        if (sm.theme != UITHEME_FIRE) { printf("FAIL: THEME row did not reach FIRE (%u)\n", sm.theme); fails = 1; }
        shot(dir, "theme_global");
        for (t = 0; t < UITHEME_COUNT; t++) {
            sm.theme = t;
            for (i = 0; i < UIPAGE_COUNT; i++) {
                go((uint8_t)i);
                tlog = NULL;
                ui_screen_draw(&sm, synth_seq(), synth_arp(), 1);
            }
            go(UIPAGE_FILTER1);
            snprintf(nm, sizeof nm, "theme_%s", TN[t]);
            shot(dir, nm);
            ui_sm_handle(&sm, UIEV_SELECT_ENC, 2);
            ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
            ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
            snprintf(nm, sizeof nm, "theme_%s_pop", TN[t]);
            shot(dir, nm);
            ui_sm_tick(&sm, UI_ROWPOP_MS);
        }
        sm.theme = UITHEME_HEATHER;                  /* a preset load leaves it alone */
        ui_sm_attach_store(&sm, list_query, NULL);
        preset_turn(1);
        if (sm.theme != UITHEME_HEATHER) { printf("FAIL: a preset load changed THEME\n"); fails = 1; }
    }

    printf("text cut: %d, text past its canvas: %d\n", shim_cuts, shim_overflows);
    fails |= shim_cuts != 0 || shim_overflows != 0;
    printf(fails ? "render checks FAILED\n" : "render checks passed\n");
    return fails;
}
