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
    {                                                /* NOISE (2026-11-06): OSC2's place with one oscillator */
        e->oscmix.noise_level = 100; e->oscmix.noise_colour = 112; e->noise_harmonic = 38;   /* ringing, 3rd stop */
        run(3000);
        go(UIPAGE_OSC2);
        shot(dir, "noise");
        e->oscmix.noise_colour = 24; e->noise_harmonic = 0;       /* pink -> brown: no resonance yet */
        run(3000);
        shot(dir, "noise_pink");
        e->oscmix.noise_colour = 70; e->noise_harmonic = 90;      /* the band at ~30 Hz, between stops 5 and 6 */
        run(3000);
        shot(dir, "noise_glide");
        e->oscmix.noise_level = 20; e->oscmix.noise_colour = 90; e->noise_harmonic = 0;
        go(UIPAGE_OSCMIX);
    }
    e->arch = VARCH_SINGLE; e->oscmix.sub_octave = SUBOCT_DOWN2; e->oscmix.sub_waveform = SUBWAVE_SQUARE; e->oscmix.sub_level = 70;
    ui_sm_enforce(&sm);
    shot(dir, "oscmix_one");

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

    /* MOD (2026-11-03, one page): LFO1 * ENV1 on MOD1, a few frames of history. Its rows are every destination, each
     * the SLOT's send there: the Algorithm Encoder sends MOD1 to F1 CUTOFF on top of the filter's own ENV2 */
    e->mod[0].src_a = MSRC_LFO1; e->mod[0].func = MFN_MUL; e->mod[0].src_b = MSRC_ENV1; e->mod[0].amount = 100;
    e->mod[1] = (ModSlotParams){MSRC_LFO2, MFN_MUL, MSRC_MODWHEEL, 110};
    e->mod[2] = (ModSlotParams){MSRC_ENV3, MFN_ADD, MSRC_NONE, 100};
    e->mod[3] = (ModSlotParams){MSRC_RANDOM, MFN_MUL, MSRC_POLY_AT, 90};
    e->filter[0].mod_src = MSRC_ENV2; e->filter[0].mod_amt = 20;
    go(UIPAGE_MOD);
    {
        static const struct { uint8_t slot; const char *dest; int8_t amt; } SENDS[] = {
            {0, "F1 CUTOFF", 40}, {0, "PAN", 30}, {0, "OSC1 SCAN", -24}, {1, "OSC1 PITCH", 50}, {1, "OSC2 PITCH", 50},
            {2, "F1 CUTOFF", -12}, {2, "NOISE LEVEL", 25}, {3, "F1 CUTOFF", 20},
        };
        uint8_t n = ui_sm_row_count(&sm), r, k, used = 0;
        if (n < 10u) { printf("FAIL: MOD should ring every destination (%u)\n", n); fails = 1; }
        for (k = 0; k < sizeof SENDS / sizeof SENDS[0]; k++) {
            sm.mod_slot = SENDS[k].slot;             /* (knob 4, SLOT) */
            for (r = 0; r < n && strcmp(ui_sm_row_label(&sm, r), SENDS[k].dest) != 0; r++) {}
            ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(r - sm.s.current_param_focus));
            ui_sm_handle(&sm, UIEV_ALGO_ENC, SENDS[k].amt);
            if (ui_sm_send(e, SENDS[k].slot, ui_sm_row_dest(&sm, r)) != SENDS[k].amt) {
                printf("FAIL: MOD%u -> %s: send %d\n", SENDS[k].slot + 1u, SENDS[k].dest,
                       ui_sm_send(e, SENDS[k].slot, ui_sm_row_dest(&sm, r)));
                fails = 1;
            }
        }
        for (k = 0; k < NMODSEND; k++) used += e->send[k].dest != MDST_NONE;
        if (used != 8u || e->filter[0].mod_src != MSRC_ENV2 || e->filter[0].mod_amt != 20) {
            printf("FAIL: the sends took %u entries / changed the filter's own source (%u, %u)\n", used,
                   e->filter[0].mod_src, e->filter[0].mod_amt);
            fails = 1;
        }
        sm.mod_slot = 0;
        for (r = 0; r < n && strcmp(ui_sm_row_label(&sm, r), "F1 CUTOFF") != 0; r++) {}
        ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(r - sm.s.current_param_focus));
        if (ui_sm_mod_route(&sm, r) != UIMOD_HERE) { printf("FAIL: F1 CUTOFF should be MOD1's\n"); fails = 1; }
        sm.mod_slot = 1;
        if (ui_sm_mod_route(&sm, r) != UIMOD_OTHER) { printf("FAIL: F1 CUTOFF should be another slot's for MOD2\n"); fails = 1; }
        for (r = 0; r < n && strcmp(ui_sm_row_label(&sm, r), "MFX 1") != 0; r++) {}
        if (r >= n || ui_sm_row_state(&sm, r) != UIROW_GREYED) { printf("FAIL: MFX 1 should be dark at OFF\n"); fails = 1; }
        sm.mod_slot = 0;
    }
    for (i = 0; i < 100; i++) {
        run(441);
        ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
    }
    shot(dir, "mod1");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 5);             /* F1 CUTOFF +45, the popup up */
    if (!sm.rowpop || ui_sm_send(e, 0, MDST_F1_CUTOFF) != 45) { printf("FAIL: the Algorithm Encoder did not set MOD1's send\n"); fails = 1; }
    shot(dir, "mod1_rowpop");
    ui_sm_tick(&sm, UI_ROWPOP_MS);
    for (uint8_t sl = 1; sl < 4; sl++) {             /* SLOT MOD2 .. MOD4: the cove's light moves */
        static const char *const NM[4] = {"mod1", "mod2", "mod3", "mod4"};
        sm.mod_slot = sl;
        for (i = 0; i < 100; i++) { run(441); ui_screen_draw(&sm, synth_seq(), synth_arp(), 0); }
        shot(dir, NM[sl]);
    }
    sm.mod_slot = 0;
    /* .. the MFX knobs: named after the type's, live once there is one */
    e->mfx.type = MFX_STONE_PHASER;
    {
        uint8_t n = ui_sm_row_count(&sm), r;
        for (r = 0; r < n && strcmp(ui_sm_row_label(&sm, r), "MFX MANUAL") != 0; r++) {}
        if (r >= n || ui_sm_row_state(&sm, r) != UIROW_ACTIVE) { printf("FAIL: MFX MANUAL should be a live destination\n"); fails = 1; }
        e->mfx.type = MFX_PITCH_SHIFT;
        if (r >= n || ui_sm_row_state(&sm, r) != UIROW_GREYED) { printf("FAIL: PITCH SHIFT's PITCH takes no send\n"); fails = 1; }
        e->mfx.type = MFX_OFF;
    }
    /* .. LEVEL 0: the slot does nothing, the cove dark */
    e->mod[0].amount = BIPOLAR_CENTER;
    ui_screen_draw(&sm, synth_seq(), synth_arp(), 0);
    shot(dir, "mod1_sealed");
    e->mod[0].amount = 100;
    /* .. a full pool: the thirteenth send is refused, SENDS FULL in the popup */
    {
        uint8_t n = ui_sm_row_count(&sm), r, k, filled = 0;
        for (r = 0; r < n && filled < 5; r++) {
            sm.mod_slot = 1;
            if (ui_sm_row_state(&sm, r) != UIROW_ACTIVE || ui_sm_mod_route(&sm, r) != UIMOD_NONE) continue;
            ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(r - sm.s.current_param_focus));
            ui_sm_handle(&sm, UIEV_ALGO_ENC, 10);
            filled++;
        }
        for (k = 0, filled = 0; k < NMODSEND; k++) filled += e->send[k].dest != MDST_NONE;
        for (r = 0; r < n && ui_sm_mod_route(&sm, r) != UIMOD_NONE; r++) {}
        ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(r - sm.s.current_param_focus));
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 10);
        if (filled != NMODSEND || !sm.sends_full || ui_sm_send(e, 1, ui_sm_row_dest(&sm, r)) != 0) {
            printf("FAIL: the full pool (%u) took a 13th send / no SENDS FULL\n", filled);
            fails = 1;
        }
        shot(dir, "mod_full");
        for (k = 0; k < NMODSEND; k++)               /* back to the seven */
            if (MSEND_SLOT(e->send[k].dest) == 1u && e->send[k].amount == 64 + 10) e->send[k].dest = MDST_NONE;
        ui_sm_tick(&sm, UI_ROWPOP_MS);
        sm.mod_slot = 0;
    }
    /* .. FILTER1 shows the sends: stones before MOD SRC's value, the cutoff knob's reach and ghost; the popup lists
     * them; a destination's own source list has no MOD slots */
    go(UIPAGE_FILTER1);
    note(0, 48, 110);
    for (i = 0; i < 40; i++) { run(441); ui_screen_draw(&sm, synth_seq(), synth_arp(), 0); }
    {
        uint8_t n = ui_sm_row_count(&sm), r;
        for (r = 0; r < n && strcmp(ui_sm_row_label(&sm, r), "MOD SRC") != 0; r++) {}
        ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(r - sm.s.current_param_focus));
        shot(dir, "filter1_sends");
        e->filter[0].mod_src = MSRC_LFO3;
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
        if (e->filter[0].mod_src != MSRC_LFO3) { printf("FAIL: MOD SRC stepped to %u, onto a slot\n", e->filter[0].mod_src); fails = 1; }   /* (v13: the slots are last) */
        e->filter[0].mod_src = MSRC_ENV2;
        shot(dir, "filter1_sends_pop");
        ui_sm_tick(&sm, UI_ROWPOP_MS);
    }
    note(0, 48, 0);
    run(20000);

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

    /* MFX (2026-11-01): OFF, then STONE PHASER and ENSEMBLE with a chord held and the audio run, so the gusts stand
     * where the effect is in its sweep */
    fresh();
    go(UIPAGE_MFX);
    shot(dir, "mfx_off");
    note(0, 48, 100); note(0, 55, 100); note(0, 63, 100);
    MFX_PARAMS(0) = (MfxParams){MFX_STONE_PHASER, {64, 41, 100, 127}, 64};
    run(44100u * 2u / 3u);
    shot(dir, "mfx_phaser");
    MFX_PARAMS(0) = (MfxParams){MFX_ENSEMBLE, {90, 74, 60, 80}, 110};
    run(44100u / 2u);
    shot(dir, "mfx_ensemble");
    MFX_PARAMS(0) = (MfxParams){MFX_ENSEMBLE, {0, 74, 0, 64}, 0};
    run(44100u / 3u);
    shot(dir, "mfx_ensemble_mono_new");
    MFX_PARAMS(0) = (MfxParams){MFX_ENS_PHASER, {74, 64, 26, 120}, 100, {60, 90}};
    run(44100u / 2u);
    shot(dir, "mfx_ens_phaser");
    MFX_PARAMS(0) = (MfxParams){MFX_CHORUS, {45, 94, 46, 64}, 127, {80, 0}};
    run(44100u / 2u);
    shot(dir, "mfx_chorus");
    MFX_PARAMS(0) = (MfxParams){MFX_TAPE_FLANGER, {40, 63, 64, 60}, 64, {0, 1}};
    run(44100u / 2u);
    shot(dir, "mfx_tape_flanger");
    MFX_PARAMS(0) = (MfxParams){MFX_BBD_FLANGER, {50, 30, 80, 100}, 0, {1, 0}};
    run(44100u / 2u);
    shot(dir, "mfx_bbd_flanger");
    MFX_PARAMS(0) = (MfxParams){MFX_PITCH_SHIFT, {19, 75, 90, 64}, 127, {80, 0}};
    run(44100u / 2u);
    shot(dir, "mfx_pitch_shift");
    MFX_PARAMS(0) = (MfxParams){MFX_BBD_ECHO, {99, 70, 110, 60}, 60, {0, 40}};
    run(44100u / 2u);
    shot(dir, "mfx_bbd_echo");
    MFX_PARAMS(0).type = MFX_OFF;
    note(0, 48, 0); note(0, 55, 0); note(0, 63, 0);

    /* EFFECTS, GLOBAL */
    fresh();
    perf_state.delay_time = 50; perf_state.delay_feedback = 100; perf_state.reverb_decay = 90;
    perf_state.reverb_type = REVTYPE_TOMB;
    go(UIPAGE_EFFECTS);
    shot(dir, "effects");
    perf_state.delay_type = DELAYTYPE_BBD; perf_state.delay_sync = 1; perf_state.reverb_type = REVTYPE_SPRING;
    shot(dir, "effects_bbd");                     /* the delay type, SYNC and the reverb's name together */
    perf_state.delay_type = DELAYTYPE_TAPE; perf_state.delay_sync = 0; perf_state.reverb_type = REVTYPE_TOMB;
    /* MIX (2026-11-02): DUAL, layer 2 back, LOOP TONE dark, the loop up and running (its dashes lit); then SINGL's
     * DRY/WET, thinner repeats */
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.reverb_type = REVTYPE_MOD;
    fx_live[FX_L_BALANCE] = 40u; fx_live[FX_L_TONE] = 30u; fx_live[FX_L_DLY_REV] = 120u; fx_live[FX_L_REV_DLY] = 115u;
    engine_state[0].amp.delay_send = 90; engine_state[0].amp.reverb_send = 60;   /* the ley lines: each its own */
    engine_state[1].amp.delay_send = 30; engine_state[1].amp.reverb_send = 120;
    go(UIPAGE_MIX);
    shot(dir, "mix_dual_rest");                   /* nothing sounding: dormant ley lines, the stones barely shimmering */
    note(0, 48, 100); note(1, 60, 100);
    run(44100u * 4u);
    go(UIPAGE_MIX);
    shot(dir, "mix_dual_loop");
    note(0, 48, 0); note(1, 60, 0);
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    fx_live[FX_L_BALANCE] = 100u; fx_live[FX_L_TONE] = 90u; fx_live[FX_L_DLY_REV] = 0u; fx_live[FX_L_REV_DLY] = 0u;
    run(44100u);
    go(UIPAGE_MIX);
    shot(dir, "mix_single_drywet");
    fx_live[FX_L_BALANCE] = 64u; fx_live[FX_L_TONE] = 64u;
    perf_state.delay_pingpong = 0u;
    run(30000);
    go(UIPAGE_GLOBAL);
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 7);
    shot(dir, "global");
    go(UIPAGE_GLOBAL);                            /* TUNING EQUAL: ROOT hidden, BEND RANGE right under it (2026-10-30) */
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 3);
    shot(dir, "global_tuning");

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
    go(UIPAGE_MOD);
    sm.mod_slot = 1;
    shot(dir, "red_mod2");
    sm.mod_slot = 0;

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
