/* Host test for the UI state machine's voice architecture handling (spec 2.1, 2.2, 4.0, 5.3): hidden pages and
 * section cycling, hidden rows, the per-architecture knob map, HOME's ARCH / UNISON DETUNE / MAX VOICES rows and
 * layer-focus fallback. Prints each page it visits. Exit code 0 = pass. Test scaffolding only.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/test_ui_arch.c -o test_ui_arch */
#include <stdio.h>
#include <string.h>
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/attacks.h"
#include "dsp/fx.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static UIStateMachine sm;
static SequencerLane lanes[SEQ_NLANE];
static ArpState arp;

static void press(uint8_t btn) { ui_sm_handle(&sm, UIEV_BUTTON, (int8_t)btn); }

/* the Preset Encoder's store: slots 3, 7, 30 and 40 of every kind say used (40 is past the 32-slot banks: the walk
 * must not reach it there) */
static uint8_t used_slots(void *ctx, uint8_t target, uint8_t slot, uint8_t name[NAME_LEN])
{
    (void)ctx; (void)target; (void)name;
    return slot == 3u || slot == 7u || slot == 30u || slot == 40u ? (uint8_t)UISLOT_USED : (uint8_t)UISLOT_EMPTY;
}

/* turn the Preset Encoder; the load (if any) is reported done. Returns what it asked for, 255 for nothing */
static uint8_t preset_turn(int8_t detents)
{
    uint8_t slot;
    ui_sm_handle(&sm, UIEV_PRESET_ENC, detents);
    if (!sm.load_pending) return 255u;
    slot = sm.load_slot;
    ui_sm_load_done(&sm, 1u);
    return slot;
}

static int find_row(const char *label)
{
    for (uint8_t i = 0; i < ui_sm_row_count(&sm); i++)
        if (strcmp(ui_sm_row_label(&sm, i), label) == 0) return i;
    return -1;
}

static void dump(const char *title)
{
    printf("%s:", title);
    for (uint8_t i = 0; i < ui_sm_row_count(&sm); i++) {
        const char *t = ui_sm_row_text(&sm, i);
        printf(" [%s=%s%s]", ui_sm_row_label(&sm, i), t ? t : "", ui_sm_row_state(&sm, i) == UIROW_ACTIVE ? "" :
               ui_sm_row_state(&sm, i) == UIROW_INFO ? " info" :
               ui_sm_row_state(&sm, i) == UIROW_HIDDEN ? " hidden" : " grey");
        if (!t) printf("(%u)", ui_sm_row_value(&sm, i));
    }
    printf("\n");
}

static void knobs(uint8_t page, const char *title)
{
    printf("  %s knobs:", title);
    for (uint8_t k = 0; k < 4; k++) {
        UiKnobInfo info;
        uint8_t *p = ui_sm_knob(&sm, page, sm.s.active_synth_layer, k, &info);
        printf(" [%s%s]", info.kind == UIKNOB_NONE ? "--" : info.label, (p || info.kind == UIKNOB_PERF) ? "" : " (no byte)");
    }
    printf("\n");
}

int main(void)
{
    synth_init();
    memset(lanes, 0, sizeof lanes);
    memset(&arp, 0, sizeof arp);
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], lanes, &arp);

    /* HOME View 1 rows: ARCH, UNISON DETUNE greyed until UNISON, MAX VOICES read-only */
    dump("HOME1");
    int arch_row = find_row("ARCH"), det_row = find_row("UNISON DETUNE"), vox_row = find_row("MAX VOICES");
    CHECK(arch_row >= 0 && det_row >= 0 && vox_row >= 0, "HOME1 rows missing");
    CHECK(ui_sm_row_state(&sm, (uint8_t)det_row) == UIROW_GREYED, "UNISON DETUNE should be greyed in POLY");
    CHECK(ui_sm_row_state(&sm, (uint8_t)vox_row) == UIROW_INFO, "MAX VOICES should be info");
    CHECK(ui_sm_row_value(&sm, (uint8_t)vox_row) == varch_voice_cap(&engine_state[0]), "MAX VOICES value");

    /* edit ARCH with the encoders: cursor down to it; the list runs SINGLE, DUAL OSC, DUAL FILT, DUAL SPRD, FULL,
     * STACK, BINAURAL (SINGLE = SPREAD; STRIKE retired 2026-11-04); one step at a time to STACK */
    sm.s.current_param_focus = (uint8_t)arch_row;
    {
        static const uint8_t order[] = {VARCH_SPREAD, VARCH_DUAL_OSC, VARCH_DUAL_FILTER, VARCH_FULL,
                                        VARCH_STACK, VARCH_BINAURAL};
        ui_sm_handle(&sm, UIEV_ALGO_ENC, -20);
        CHECK(engine_state[0].arch == VARCH_SPREAD && strcmp(ui_sm_row_text(&sm, (uint8_t)arch_row), "SINGLE") == 0,
              "ARCH list starts on SINGLE (SPREAD): %u", engine_state[0].arch);
        for (unsigned i = 1; i < sizeof order; i++) {
            ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
            CHECK(engine_state[0].arch == order[i], "ARCH step %u: %u", i, engine_state[0].arch);
        }
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
        CHECK(engine_state[0].arch == VARCH_BINAURAL, "ARCH stops at BINAURAL: %u", engine_state[0].arch);
        ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    }
    CHECK(engine_state[0].arch == VARCH_STACK, "ARCH not STACK: %u", engine_state[0].arch);
    CHECK(ui_sm_row_value(&sm, (uint8_t)vox_row) == 8, "STACK max voices %u (2026-11-18 prices)", ui_sm_row_value(&sm, (uint8_t)vox_row));
    dump("HOME1 STACK");
    /* the cursor never lands on MAX VOICES */
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 20);
    CHECK(sm.s.current_param_focus != (uint8_t)vox_row, "cursor on MAX VOICES");

    /* UNISON makes the detune row live */
    perf_state.voice_mode[0] = VMODE_UNISON_STACK;
    CHECK(ui_sm_row_state(&sm, (uint8_t)det_row) == UIROW_ACTIVE, "UNISON DETUNE not active under UNISON");

    /* FX section under STACK: OSC1 -> OSC2 -> OSCMIX -> OSC1 */
    press(BTN_FX); CHECK(sm.page == UIPAGE_OSC1, "FX 1");
    press(BTN_FX); CHECK(sm.page == UIPAGE_OSC2, "FX 2 STACK %u", sm.page);
    press(BTN_FX); CHECK(sm.page == UIPAGE_OSCMIX, "FX 3");
    press(BTN_FX); CHECK(sm.page == UIPAGE_OSC1, "FX 4 wraps to OSC1 (it went to FILTER1 after STRIKE left): %u", sm.page);
    press(BTN_FX); press(BTN_FX); CHECK(sm.page == UIPAGE_OSCMIX, "FX 3 again");
    dump("OSCMIX STACK"); knobs(UIPAGE_OSCMIX, "OSCMIX STACK");
    CHECK(find_row("XMOD TYPE") < 0 && find_row("COL MOD") >= 0 && find_row("LEVEL MOD") >= 0 &&
          find_row("SUB WAVE") >= 0, "STACK OSCMIX rows (XMOD TYPE is knob 1; the mods' sources fixed, 2026-11-11)");
    {
        UiKnobInfo i;
        uint8_t *p = ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 0, &i);
        const char *t;
        engine_state[0].oscmix.xmod_type = XMOD_RING;
        t = ui_sm_knob_text(&sm, UIPAGE_OSCMIX, 0, 0);
        CHECK(p == &engine_state[0].oscmix.xmod_type && i.kind == UIKNOB_LIST && i.n_enum == XMOD_COUNT &&
              t != NULL && strcmp(t, "RING") == 0, "STACK knob 1 = XMOD TYPE list (%s)", t ? t : "NULL");
        CHECK(ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 1, &i) == &engine_state[0].oscmix.xmod_amt &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 2, &i) == &engine_state[0].oscmix.sub_level &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 3, &i) == &engine_state[0].oscmix.noise_level, "STACK knobs 2..4");
        engine_state[0].oscmix.xmod_type = XMOD_FM;
    }
    press(BTN_SEL); press(BTN_SEL); press(BTN_SEL);
    CHECK(sm.page == UIPAGE_FLTMIX, "SEL to FLTMIX under STACK: %u", sm.page);
    dump("FLTMIX STACK"); knobs(UIPAGE_FLTMIX, "FLTMIX STACK");
    CHECK(ui_sm_row_count(&sm) == 0u, "STACK FLTMIX: all on the knobs");

    /* switch to SINGLE while on FILTER MIXER: the page vanishes, fall back to FILTER1 */
    engine_state[0].arch = VARCH_SINGLE;
    ui_sm_enforce(&sm);
    CHECK(sm.page == UIPAGE_FILTER1, "SINGLE fallback from FLTMIX: %u", sm.page);
    press(BTN_SEL); CHECK(sm.page == UIPAGE_FILTER1, "SEL cycles to FILTER1 only under SINGLE: %u", sm.page);
    dump("FILTER1 SINGLE");
    CHECK(find_row("MOD SRC") < 0 && find_row("MORPH MOD SRC") < 0 && ui_sm_row_count(&sm) == 3u,
          "FILTER page: DRIVE, FLAVOUR, KEY TRACK (the pickers gone, 2026-11-11)");
    engine_state[0].filter_key_track[0] = 33u; engine_state[0].filter_key_track[1] = 99u;
    CHECK(find_row("KEY TRACK") == 2 && ui_sm_row_value(&sm, 2) == 33u, "FILTER1 KEY TRACK: the third row, its own byte");
    press(BTN_FX); press(BTN_FX);                     /* 2026-11-06: one oscillator, so OSC2's place is NOISE */
    CHECK(sm.page == UIPAGE_OSC2 && ui_sm_noise_page(&sm, sm.page, 0), "FX 2 under SINGLE: the NOISE page: %u", sm.page);
    dump("NOISE SINGLE"); knobs(UIPAGE_OSC2, "NOISE SINGLE");
    CHECK(ui_sm_row_count(&sm) == 0u, "NOISE: no rows, all on the knobs (2026-11-11): %u", ui_sm_row_count(&sm));
    {
        UiKnobInfo i;
        CHECK(ui_sm_knob(&sm, UIPAGE_OSC2, 0, 0, &i) == &engine_state[0].oscmix.noise_colour && strcmp(i.label, "COLOUR") == 0 &&
              ui_sm_knob(&sm, UIPAGE_OSC2, 0, 1, &i) == &engine_state[0].noise_harmonic && strcmp(i.label, "HARMONIC") == 0 &&
              ui_sm_knob(&sm, UIPAGE_OSC2, 0, 2, &i) == &engine_state[0].oscmix.noise_col_mod_amt && strcmp(i.label, "COL MOD") == 0 &&
              ui_sm_knob(&sm, UIPAGE_OSC2, 0, 3, &i) == &engine_state[0].oscmix.noise_mod_amt && strcmp(i.label, "LEVEL MOD") == 0,
              "NOISE knobs (2026-11-11: the level on the OSCMIXER)");
    }
    press(BTN_FX);
    CHECK(sm.page == UIPAGE_OSCMIX, "FX 3 under SINGLE: OSCMIX: %u", sm.page);
    press(BTN_FX);
    CHECK(sm.page == UIPAGE_OSC1, "FX from OSCMIX back to OSC1, not FILTER1: %u", sm.page);
    press(BTN_FX); press(BTN_FX);
    dump("OSCMIX SINGLE"); knobs(UIPAGE_OSCMIX, "OSCMIX SINGLE");
    CHECK(find_row("NOISE COLOUR") < 0 && find_row("SUB OCTAVE") < 0 && find_row("XMOD TYPE") < 0 &&
          find_row("COL MOD SRC") < 0 && find_row("HARMONIC") < 0 && find_row("SUB WAVE") < 0 && find_row("NOISE LEVEL") < 0,
          "SINGLE OSCMIX rows (the noise colour on its page, SUB WAVE and NOISE LEVEL knobs)");
    {
        UiKnobInfo i;
        const char *t;
        uint8_t *p;
        engine_state[0].oscmix.sub_octave = SUBOCT_DOWN2;
        engine_state[0].oscmix.sub_waveform = SUBWAVE_SQUARE;
        p = ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 1, &i);
        t = ui_sm_knob_text(&sm, UIPAGE_OSCMIX, 0, 1);
        CHECK(p == &sm.sub6_knob && *p == 3u && i.kind == UIKNOB_LIST && i.n_enum == 6u && strcmp(i.label, "SUB WAVE") == 0 &&
              t != NULL && strcmp(t, "SQR -2") == 0, "SINGLE knob 2 = SUB WAVE, both bytes (%s)", t ? t : "NULL");
        *p = 4u;                                     /* a turn, as synth_panel's knob_turn writes it */
        ui_sm_knob_written(&sm, UIPAGE_OSCMIX, 1u);
        CHECK(engine_state[0].oscmix.sub_octave == SUBOCT_DOWN1 && engine_state[0].oscmix.sub_waveform == SUBWAVE_CURRENT,
              "SUB WAVE step 4 = CURRENT -1 OCT: %u %u", engine_state[0].oscmix.sub_octave,
              engine_state[0].oscmix.sub_waveform);
        engine_state[0].oscmix.sub_waveform = SUBWAVE_SINE;
        CHECK(ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 3, &i) == &engine_state[0].oscmix.noise_level &&
              strcmp(i.label, "NOISE LEVEL") == 0, "SINGLE knob 4 = NOISE LEVEL");
        p = ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 2, &i);
        CHECK(p == &engine_state[0].oscmix.sub_level && strcmp(i.label, "SUB LEVEL") == 0, "SINGLE knob 3 = SUB LEVEL");
        CHECK(ui_sm_knob_text(&sm, UIPAGE_OSCMIX, 0, 2) == NULL, "a plain knob has no list text");
        p = ui_sm_knob(&sm, UIPAGE_FLTMIX, 0, 0, &i);
        CHECK(p == NULL, "hidden page knob has no byte");
    }

    /* SPREAD: spread, sub wave, sub, noise level on the knobs (2026-11-11); the spread mod's source and amount rows */
    engine_state[0].arch = VARCH_SPREAD;
    ui_sm_enforce(&sm);
    {                                                /* OSC1 (2026-11-11): SCAN, TUNE, LFO2>PITCH, ENV2>PITCH; FINE OSC2's; KEY TRACK
                                                      * only with two oscillators */
        UiKnobInfo i;
        uint8_t pg = sm.page;
        sm.page = UIPAGE_OSC1;
        CHECK(ui_sm_knob(&sm, UIPAGE_OSC1, 0, 1, &i) == &engine_state[0].osc[0].coarse && i.kind == UIKNOB_TUNE &&
              ui_sm_knob(&sm, UIPAGE_OSC1, 0, 2, &i) == &engine_state[0].osc[0].lfo2_pitch &&
              ui_sm_knob(&sm, UIPAGE_OSC1, 0, 3, &i) == &engine_state[0].osc[0].env2_pitch &&
              find_row("FINE") < 0 && find_row("ENV3>SCAN") >= 0 && find_row("KEY TRACK") < 0,
              "SINGLE OSC1: SCAN, TUNE, LFO2>PITCH, ENV2>PITCH; ENV3>SCAN a row, no FINE, no KEY TRACK");
        {                                            /* TUNE: a semitone a detent, held to +-36 */
            uint8_t *p = ui_sm_knob(&sm, UIPAGE_OSC1, 0, 1, &i);
            char b[12];
            ui_sm_tune_turn(p, 1);
            ui_sm_tune_turn(p, 1);
            CHECK(osc_coarse_semis(*p) == 2 && strcmp(ui_sm_tune_text(p, b), "+2") == 0, "TUNE: two detents (%s)",
                  ui_sm_tune_text(p, b));
            ui_sm_tune_turn(p, -50);
            CHECK(osc_coarse_semis(*p) == -36, "TUNE held at -36 (%d)", (int)osc_coarse_semis(*p));
            *p = OSC_COARSE_ZERO;
        }
        engine_state[0].arch = VARCH_DUAL_OSC;
        ui_sm_enforce(&sm);
        sm.page = UIPAGE_OSC1;
        CHECK(find_row("KEY TRACK") >= 0 && find_row("ENV3>SCAN") >= 0 && find_row("FINE") < 0,
              "DUAL OSC OSC1: KEY TRACK and ENV3>SCAN rows, no FINE");
        sm.page = UIPAGE_OSC2;
        CHECK(find_row("FINE") == 2 && find_row("KEY TRACK") == 3 && find_row("OSC SYNC") >= 0,
              "DUAL OSC OSC2: FINE above KEY TRACK, OSC SYNC");
        sm.page = UIPAGE_OSC1;
        engine_state[0].arch = VARCH_DUAL_FILTER;    /* one oscillator: no KEY TRACK (BINAURAL too) */
        ui_sm_enforce(&sm);
        sm.page = UIPAGE_OSC1;
        CHECK(find_row("KEY TRACK") < 0, "DUAL FILTER OSC1: no KEY TRACK");
        engine_state[0].arch = VARCH_BINAURAL;
        ui_sm_enforce(&sm);
        sm.page = UIPAGE_OSC1;
        CHECK(find_row("KEY TRACK") < 0, "BINAURAL OSC1: no KEY TRACK");
        engine_state[0].arch = VARCH_SPREAD;
        ui_sm_enforce(&sm);
        sm.page = pg;
    }
    dump("OSCMIX SPREAD"); knobs(UIPAGE_OSCMIX, "OSCMIX SPREAD");
    {
        UiKnobInfo i;
        CHECK(ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 0, &i) == &engine_state[0].oscmix.spread &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 1, &i) == &sm.sub6_knob &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 2, &i) == &engine_state[0].oscmix.sub_level &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 3, &i) == &engine_state[0].oscmix.noise_level, "SPREAD knobs");
        CHECK(ui_sm_row_count(&sm) == 0u, "SPREAD OSCMIX: all on the knobs (its mod a route, 2026-11-11)");
    }

    /* BINAURAL: OSC2's place the NOISE page, FILTER2 hidden, FILTER MIXER shown with its phase knobs */
    engine_state[0].arch = VARCH_BINAURAL;
    ui_sm_enforce(&sm);
    CHECK(ui_sm_noise_page(&sm, UIPAGE_OSC2, 0) && !ui_sm_page_visible(&sm, UIPAGE_FILTER2, 0) &&
          ui_sm_page_visible(&sm, UIPAGE_FLTMIX, 0), "BINAURAL pages");
    knobs(UIPAGE_OSCMIX, "OSCMIX BINAURAL");
    {
        UiKnobInfo i;
        uint8_t pg = sm.page, r;
        sm.page = UIPAGE_OSCMIX;
        CHECK(ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 3, &i) == &sm.sub6_knob && find_row("NOISE LEVEL") >= 0 &&
              find_row("SUB WAVE") < 0, "BINAURAL: SUB WAVE knob 4, NOISE LEVEL a row");
        engine_state[0].oscmix.noise_level = 77u;
        CHECK(ui_sm_row_value(&sm, (uint8_t)find_row("NOISE LEVEL")) == 77u, "BINAURAL NOISE LEVEL row: the level's byte");
        engine_state[0].oscmix.noise_level = 0u;
        engine_state[0].arch = VARCH_DUAL_OSC;       /* two oscillators: SUB WAVE a row over both bytes */
        ui_sm_enforce(&sm);
        sm.page = UIPAGE_OSCMIX;
        r = (uint8_t)find_row("SUB WAVE");
        engine_state[0].oscmix.sub_octave = SUBOCT_DOWN2;
        engine_state[0].oscmix.sub_waveform = SUBWAVE_CURRENT;
        CHECK(find_row("SUB WAVE") >= 0 && find_row("SUB OCTAVE") < 0 && ui_sm_row_value(&sm, r) == 5u &&
              strcmp(ui_sm_row_text(&sm, r), "CURR -2") == 0, "DUAL OSC SUB WAVE row: %s", ui_sm_row_text(&sm, r));
        engine_state[0].oscmix.sub_octave = SUBOCT_DOWN1;
        engine_state[0].oscmix.sub_waveform = SUBWAVE_SINE;
        engine_state[0].arch = VARCH_BINAURAL;
        ui_sm_enforce(&sm);
        sm.page = pg;
    }
    press(BTN_SEL); press(BTN_SEL);
    CHECK(sm.page == UIPAGE_FLTMIX, "SEL FILTER1 -> FLTMIX under BINAURAL (skips FILTER2): %u", sm.page);
    dump("FLTMIX BINAURAL"); knobs(UIPAGE_FLTMIX, "FLTMIX BINAURAL");
    {
        UiKnobInfo i;
        CHECK(ui_sm_row_count(&sm) == 0u && ui_sm_knob(&sm, UIPAGE_FLTMIX, 0, 3, &i) == &engine_state[0].fltmix.bin_cutoff_mod_amt,
              "BINAURAL FLTMIX: knob 4 CUT L/R MOD (LFO1), no rows");
    }

    /* DUAL FILTER: FILTER MIXER keeps the spec's balance / routing */
    engine_state[0].arch = VARCH_DUAL_FILTER;
    ui_sm_enforce(&sm);
    dump("FLTMIX DUAL FILTER"); knobs(UIPAGE_FLTMIX, "FLTMIX DUAL FILTER");
    CHECK(ui_sm_row_count(&sm) == 0u, "DUAL FILTER FLTMIX: all on the knobs");
    {   /* 2026-11-11: routes as rows on their pages, EDIT from a modulated parameter onto its route, each page's cursor
         * kept, SOURCE NONE closing the list up when the cursor leaves, DEST over the architecture's destinations */
        EngineState *e = &engine_state[0];
        UiKnobInfo i;
        uint8_t r, *p, d, pg0 = sm.page;
        e->arch = VARCH_DUAL_OSC;
        ui_sm_enforce(&sm);
        for (r = 0; r < NROUTE; r++) e->route[r] = (ModRoute)ROUTE_FREE;
        e->route[0] = (ModRoute){MSRC_LFO3, MSRC_NONE, MDST_PAN, BIPOLAR_CENTER + 15, VAL_MAX};
        press(BTN_EDIT); press(BTN_EDIT);
        CHECK(sm.page == UIPAGE_AMP && find_row("LFO3>PAN") >= 0, "AMP: the PAN route is a row (page %u)", sm.page);
        ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(find_row("LFO3>PAN") - sm.s.current_param_focus));
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 5);
        CHECK(e->route[0].amount == BIPOLAR_CENTER + 20, "AMP: the route row's AMOUNT on the Algorithm Encoder (%d)",
              (int)e->route[0].amount - 64);
        r = sm.s.current_param_focus;
        press(BTN_EDIT);
        CHECK(sm.page == UIPAGE_MOD && ui_sm_route_sel(&sm) == 0u, "EDIT from AMP's PAN row: its route (%u, %u)", sm.page,
              ui_sm_route_sel(&sm));
        press(BTN_EDIT);
        CHECK(sm.page == UIPAGE_AMP && sm.s.current_param_focus == r, "AMP again: the cursor where it was (%u, %u)",
              sm.s.current_param_focus, r);
        press(BTN_SEL);                              /* FILTER1: the cutoff knob turned, then EDIT */
        ui_sm_knob_written(&sm, UIPAGE_FILTER1, 1u);
        press(BTN_EDIT);
        r = ui_sm_route_sel(&sm);
        CHECK(sm.page == UIPAGE_MOD && r == 1u && e->route[1].dest == MDST_F1_CUTOFF && e->route[1].src == MSRC_NONE &&
              strcmp(ui_sm_row_label(&sm, sm.s.current_param_focus), "+ F1 CUTOFF") == 0,
              "EDIT from the cutoff knob: + ROUTE aimed at F1 CUTOFF (route %u, %s)", r,
              ui_sm_row_label(&sm, sm.s.current_param_focus));
        p = ui_sm_knob(&sm, UIPAGE_MOD, 0, 0, &i);
        CHECK(*p == MSRC_LFO3, "+ ROUTE offers the last group's source, LFO3 (%u)", *p);
        *p = MSRC_LFO2; ui_sm_knob_written(&sm, UIPAGE_MOD, 0);
        CHECK(e->route[0].src == MSRC_LFO2 && e->route[0].dest == MDST_F1_CUTOFF && e->route[1].src == MSRC_LFO3 &&
              ui_sm_route_sel(&sm) == 0u && find_row("+ ROUTE") == 2,
              "a SOURCE makes it a route, sorted before LFO3's, the cursor with it; + ROUTE after");
        CHECK(strcmp(ui_sm_row_label(&sm, 0), "F1 CUTOFF") == 0 && strcmp(ui_sm_row_label(&sm, 1), "PAN") == 0,
              "MOD MATRIX rows: the destinations' full names (%s, %s)", ui_sm_row_label(&sm, 0), ui_sm_row_label(&sm, 1));
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);       /* LFO3>PAN: its SOURCE to NONE, then away */
        p = ui_sm_knob(&sm, UIPAGE_MOD, 0, 0, &i); *p = MSRC_NONE; ui_sm_knob_written(&sm, UIPAGE_MOD, 0);
        p = ui_sm_knob(&sm, UIPAGE_MOD, 0, 2, &i); *p = MDST_AMP_LEVEL; ui_sm_knob_written(&sm, UIPAGE_MOD, 2);
        CHECK(e->route[1].src == MSRC_NONE, "a cleared route's DEST does not bring a source back (%u)", e->route[1].src);
        ui_sm_handle(&sm, UIEV_SELECT_ENC, -1);
        CHECK(e->route[0].src == MSRC_LFO2 && e->route[0].dest == MDST_F1_CUTOFF && e->route[1].src == MSRC_NONE &&
              ui_sm_route_sel(&sm) == 0u, "SOURCE NONE: the route goes when the cursor leaves");
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);       /* + ROUTE under the LFO2 group: DEST alone makes an LFO2 route */
        p = ui_sm_knob(&sm, UIPAGE_MOD, 0, 2, &i); *p = MDST_PAN; ui_sm_knob_written(&sm, UIPAGE_MOD, 2);
        CHECK(e->route[1].src == MSRC_LFO2 && e->route[1].dest == MDST_PAN && ui_sm_route_sel(&sm) == 1u,
              "+ ROUTE takes the group's source (%u > %u)", e->route[1].src, e->route[1].dest);
        e->arch = VARCH_SPREAD;                      /* one oscillator: DEST steps over OSC2's */
        ui_sm_enforce(&sm);
        d = MDST_OSC1_PITCH;
        ui_sm_dest_turn(&sm, &d, 1);
        CHECK(d == MDST_PITCH, "DEST in SPREAD: OSC1 PCH -> PITCH, over OSC2's (%u)", d);
        ui_sm_dest_turn(&sm, &d, 1);
        CHECK(d == MDST_SPREAD, "DEST in SPREAD: PITCH -> SPREAD, over OSC MIX and CROSS-MOD (%u)", d);
        for (r = 0; r < NROUTE; r++) e->route[r] = (ModRoute)ROUTE_FREE;
        e->arch = VARCH_DUAL_FILTER;
        sm.page = pg0;
        ui_sm_enforce(&sm);
    }
    {
        uint8_t pg = sm.page;
        sm.page = UIPAGE_FILTER2;
        CHECK(find_row("KEY TRACK") == 2 && ui_sm_row_value(&sm, 2) == 99u, "FILTER2 KEY TRACK: its own byte");
        sm.page = pg;
        engine_state[0].filter_key_track[0] = engine_state[0].filter_key_track[1] = 0u;
    }

    /* layer focus: layer 2 on DUAL OSC while on FILTER MIXER -> falls back to FILTER1 */
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    engine_state[1].arch = VARCH_DUAL_OSC;
    CHECK(ui_sm_set_layer_focus(&sm, 1), "focus L2");
    CHECK(sm.page == UIPAGE_FILTER1, "L2 DUAL OSC fallback from FLTMIX: %u", sm.page);
    /* DUAL: the keys play the layer in focus, on its channel */
    {
        uint8_t c0 = perf_state.midi_ch[0], c1 = perf_state.midi_ch[1];
        perf_state.midi_ch[0] = 0; perf_state.midi_ch[1] = 1;
        CHECK(ui_sm_key_channel(&sm) == 1u, "DUAL, L2 in focus: keys on channel 2");
        perf_state.perf_mode = PERF_SPLIT_SYNTH;
        CHECK(ui_sm_key_channel(&sm) == 0u, "SPLIT: keys on layer 1's channel (the split picks the layer)");
        perf_state.perf_mode = PERF_DUAL_SYNTH;
        perf_state.midi_ch[0] = c0; perf_state.midi_ch[1] = c1;
    }

    /* HOME (2026-10-17 order): LAYER (View 1, or View 2 with layer 2 in focus) -> ZONES -> the performance browser
     * (PERF PRESET, PERF MODE, the links) -> GLOBAL. Layered voices = min cap / 2 */
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME2, "HOME with L2 in focus -> HOME2 (%u)", sm.page);
    CHECK(find_row("PERF MODE") < 0 && find_row("ARCH") == 0, "HOME2 starts with ARCH");
    {
        UiKnobInfo i;
        CHECK(ui_sm_knob(&sm, UIPAGE_HOME2, 1, 0, &i) == &sm.perf_knob[UIPERF_VELOCITY] &&
              ui_sm_knob(&sm, UIPAGE_HOME2, 1, 1, &i) == &sm.perf_knob[UIPERF_MODWHEEL] &&
              ui_sm_knob(&sm, UIPAGE_HOME2, 1, 2, &i) == &sm.perf_knob[UIPERF_CC1] &&
              ui_sm_knob(&sm, UIPAGE_HOME2, 1, 3, &i) == &perf_state.porta_time[1] && i.kind == UIKNOB_PORTA &&
              ui_sm_knob(&sm, UIPAGE_HOME1, 0, 3, &i) == &perf_state.porta_time[0],
              "LAYER knobs: VELOCITY MOD CC1 PORTA TIME (the layer's)");
        CHECK(ui_sm_knob(&sm, UIPAGE_HOME3, 0, 0, &i) == &perf_state.split_note && i.kind == UIKNOB_NOTE &&
              ui_sm_knob(&sm, UIPAGE_HOME3, 0, 3, &i) == &perf_state.vel_fade, "ZONES knobs: its four rows");
    }
    dump("HOME2 DUAL");
    {
        /* the priced pool: the two layers' limits are the pool's, each one's, and sum to no more than the pool */
        uint8_t n0 = ui_sm_layer_voices(&sm, 0), n1 = ui_sm_layer_voices(&sm, 1);
        CHECK(n0 >= 1 && n1 >= 1 && n0 + n1 <= VOICE_POOL && n0 >= n1, "layer voices %u %u of %u", n0, n1, VOICE_POOL);
    }
    /* SPLIT: L2 VOICE MODE and L2 UNISON DETUNE mirror layer 1 */
    perf_state.perf_mode = PERF_SPLIT_SYNTH;
    ui_sm_enforce(&sm);
    CHECK(ui_sm_row_state(&sm, (uint8_t)find_row("UNISON DETUNE")) == UIROW_GREYED, "L2 detune mirrored in SPLIT");
    /* SINGL: back to View 1, layer 2 voices 0 */
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    ui_sm_enforce(&sm);
    CHECK(sm.page == UIPAGE_HOME1 && sm.s.active_synth_layer == 0 && ui_sm_layer_voices(&sm, 1) == 0, "SINGL reset");
    /* the cycle (2026-11-13: GLOBAL and TUNING are HOME held + a key): SINGL LAYER <-> browser; DUAL LAYER -> ZONES
     * -> browser -> LAYER */
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_PERF, "SINGL HOME1 -> browser (%u)", sm.page);
    CHECK(find_row("PERF PRESET") == 0 && find_row("PERF MODE") == 1, "the browser: PERF PRESET, PERF MODE");
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME1, "SINGL browser -> HOME1: a toggle (%u)", sm.page);
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    ui_sm_enforce(&sm);
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME3, "DUAL HOME1 -> ZONES (%u)", sm.page);
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_PERF, "DUAL ZONES -> browser (%u)", sm.page);
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME1, "DUAL browser -> HOME1 (%u)", sm.page);
    /* HOME held + the PIT / GLO keys: TUNING, GLOBAL; HOME from there: LAYER */
    CHECK(ui_sm_home_key(&sm, UIHK_PIT) && sm.page == UIPAGE_TUNING, "HOME + PIT -> TUNING (%u)", sm.page);
    CHECK(ui_sm_home_key_led(&sm, UIHK_PIT) == 2u && ui_sm_home_key_led(&sm, UIHK_GLO) == 1u &&
          ui_sm_home_key_led(&sm, UIHK_NONE) == 0u, "HOME keys: PIT lit on TUNING, GLO dim, the rest dark");
    CHECK(ui_sm_save_kind(&sm) == UISAVE_WHOLE_PERF, "SAVE on TUNING: the performance");
    CHECK(ui_sm_home_key(&sm, UIHK_GLO) && sm.page == UIPAGE_GLOBAL, "HOME + GLO -> GLOBAL (%u)", sm.page);
    CHECK(find_row("MASTER TUNE") < 0 && find_row("TUNING") < 0 && find_row("THEME") >= 0, "GLOBAL: no tuning rows");
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME1, "GLOBAL: HOME -> LAYER (%u)", sm.page);
    {   /* MONO: RETRIG -> LEGATO -> UNISON -> RETRIG; POLY: REUSE <-> RESET; from the other family its first */
        uint8_t *vm = &perf_state.voice_mode[0], keep = *vm;
        *vm = VMODE_POLY_RESET;
        CHECK(ui_sm_home_key_led(&sm, UIHK_POLY) == 2u && ui_sm_home_key_led(&sm, UIHK_MONO) == 1u, "POLY lit");
        ui_sm_home_key(&sm, UIHK_MONO);
        CHECK(*vm == VMODE_MONO_RETRIG && ui_sm_home_key_led(&sm, UIHK_MONO) == 2u, "MONO from POLY: RETRIG");
        ui_sm_home_key(&sm, UIHK_MONO);
        CHECK(*vm == VMODE_MONO_LEGATO, "MONO: LEGATO");
        ui_sm_home_key(&sm, UIHK_MONO);
        CHECK(*vm == VMODE_UNISON_STACK, "MONO: UNISON");
        ui_sm_home_key(&sm, UIHK_MONO);
        CHECK(*vm == VMODE_MONO_RETRIG, "MONO: back to RETRIG");
        ui_sm_home_key(&sm, UIHK_POLY);
        CHECK(*vm == VMODE_POLY_REUSE, "POLY from MONO: REUSE");
        ui_sm_home_key(&sm, UIHK_POLY);
        CHECK(*vm == VMODE_POLY_RESET, "POLY: RESET");
        ui_sm_home_key(&sm, UIHK_POLY);
        CHECK(*vm == VMODE_POLY_REUSE && sm.page == UIPAGE_HOME1, "POLY: REUSE again, the page kept");
        ui_sm_set_layer_focus(&sm, 1);
        ui_sm_home_key(&sm, UIHK_MONO);
        CHECK(perf_state.voice_mode[1] == VMODE_MONO_RETRIG && *vm == VMODE_POLY_REUSE, "L2 focus: L2's voicing");
        ui_sm_set_layer_focus(&sm, 0);
        perf_state.voice_mode[1] = VMODE_POLY_REUSE;
        *vm = keep;
    }
    /* the layer switch swaps HOME1 / HOME2 in place, the cursor on the same row; the cycle never shows both */
    sm.s.current_param_focus = 4u;   /* PORTA MODE (ANALOG is row 1 since 2026-10-21) */
    CHECK(ui_sm_set_layer_focus(&sm, 1) && sm.page == UIPAGE_HOME2 && sm.s.current_param_focus == 4u,
          "L2 focus on HOME1 -> HOME2, row kept (%u, %u)", sm.page, sm.s.current_param_focus);
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME3, "HOME2 -> ZONES (%u)", sm.page);
    press(BTN_HOME); press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME2, "L2: browser -> HOME2 (%u)", sm.page);
    sm.s.current_param_focus = 4u;
    CHECK(ui_sm_set_layer_focus(&sm, 0) && sm.page == UIPAGE_HOME1 && sm.s.current_param_focus == 4u,
          "L1 focus on HOME2 -> HOME1, row kept (%u, %u)", sm.page, sm.s.current_param_focus);
    ui_sm_set_layer_focus(&sm, 1);
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    ui_sm_enforce(&sm);
    CHECK(sm.page == UIPAGE_HOME1 && sm.s.active_synth_layer == 0, "SINGL on HOME2 -> HOME1 (%u)", sm.page);
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    ui_sm_enforce(&sm);
    press(BTN_GLO);
    CHECK(sm.page == UIPAGE_EFFECTS, "GLO from elsewhere -> EFFECTS (%u)", sm.page);
    press(BTN_GLO);
    CHECK(sm.page == UIPAGE_MIX, "GLO -> MIX (%u)", sm.page);
    press(BTN_GLO);
    CHECK(sm.page == UIPAGE_EFFECTS, "GLO toggles MIX -> EFFECTS (2026-11-13: no MFX page) (%u)", sm.page);
    /* EDIT (2026-11-12): MOD -> AMP -> MOD */
    press(BTN_EDIT);
    CHECK(sm.page == UIPAGE_MOD, "EDIT -> MOD (%u)", sm.page);
    press(BTN_EDIT);
    CHECK(sm.page == UIPAGE_AMP, "EDIT -> AMP (%u)", sm.page);
    press(BTN_EDIT);
    CHECK(sm.page == UIPAGE_MOD, "EDIT from AMP -> MOD (%u)", sm.page);
    /* MFX (2026-11-01; AMP's group of rows since 2026-11-13): the MFX row alone at OFF, no knobs; a new type starts at
     * its defaults, shows its own rows and knobs; off the group the knobs are AMP's, and a page button gives them
     * back */
    press(BTN_EDIT);
    CHECK(sm.page == UIPAGE_AMP, "EDIT -> AMP (%u)", sm.page);
    {
        UiKnobInfo info;
        MfxParams *m = ui_sm_mfx(&sm, 0);
        int tr = find_row("MFX"), pr = find_row("L/R PHASE"), wr = find_row("WIDTH"), dr = find_row("DRV MODEL");
        CHECK(dr == 0 && tr > dr && pr > tr && wr > tr && m->type == MFX_OFF, "AMP rows, then MFX's (%d %d %d %d), OFF",
              dr, tr, pr, wr);
        CHECK(ui_sm_row_state(&sm, (uint8_t)pr) == UIROW_HIDDEN && ui_sm_row_state(&sm, (uint8_t)wr) == UIROW_HIDDEN,
              "MFX OFF: no type row");
        CHECK(!ui_sm_mfx_group(&sm) && ui_sm_knob(&sm, UIPAGE_AMP, 0, 0, &info) == &engine_state[0].amp.drive,
              "AMP's first row: knob 1 DRIVE (%s)", info.label);
        ui_sm_handle(&sm, UIEV_SELECT_ENC, (int8_t)(tr - sm.s.current_param_focus));
        CHECK(sm.s.current_param_focus == (uint8_t)tr && ui_sm_mfx_group(&sm), "Select onto MFX: the group (%u)",
              sm.s.current_param_focus);
        CHECK(ui_sm_knob(&sm, UIPAGE_AMP, 0, 0, &info) == 0 && info.kind == UIKNOB_NONE, "MFX OFF: no knobs");
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 2);
        CHECK(m->type == MFX_ENSEMBLE && m->knob[MFX_K_AGE] == 40u && m->knob[MFX_K_RATE] == 74u && m->lr == 100u,
              "TYPE -> ENSEMBLE at its defaults (%u: %u %u, %u)", m->type, m->knob[0], m->knob[1], m->lr);
        CHECK(ui_sm_row_state(&sm, (uint8_t)wr) == UIROW_ACTIVE && ui_sm_row_state(&sm, (uint8_t)pr) == UIROW_HIDDEN,
              "ENSEMBLE: WIDTH, not L/R PHASE");
        CHECK(ui_sm_knob(&sm, UIPAGE_AMP, 0, 2, &info) == &m->knob[MFX_K_SHIMMER] && info.label[0] == 'S',
              "ENSEMBLE knob 3 is SHIMMER (%s)", info.label);
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
        CHECK(ui_sm_mfx_group(&sm) && ui_sm_knob(&sm, UIPAGE_AMP, 0, 2, &info) == &m->knob[MFX_K_SHIMMER],
              "ENSEMBLE's own row: still the MFX's knobs");
        ui_sm_handle(&sm, UIEV_SELECT_ENC, -100);
        CHECK(!ui_sm_mfx_group(&sm) && ui_sm_knob(&sm, UIPAGE_AMP, 0, 0, &info) == &engine_state[0].amp.drive,
              "back up to DRV MODEL: AMP's knobs");
        sm.s.current_param_focus = (uint8_t)tr;
        press(BTN_EDIT); press(BTN_EDIT);
        CHECK(sm.page == UIPAGE_AMP && sm.s.current_param_focus == 0u && !ui_sm_mfx_group(&sm),
              "EDIT, EDIT back to AMP: its own knobs, not the MFX's (%u, %u)", sm.page, sm.s.current_param_focus);
        sm.s.current_param_focus = (uint8_t)tr;
        ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
        CHECK(m->type == MFX_STONE_PHASER && m->knob[MFX_K_DEPTH] == 127u &&
              ui_sm_row_state(&sm, (uint8_t)pr) == UIROW_ACTIVE, "TYPE -> STONE PHASER: L/R PHASE, DEPTH 127");
        ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
        CHECK(m->type == MFX_OFF, "TYPE -> OFF");
    }
    press(BTN_GLO);
    CHECK(sm.page == UIPAGE_EFFECTS, "GLO -> EFFECTS (%u)", sm.page);
    CHECK(find_row("DELAY HP") < 0 && find_row("DLY > REV") < 0, "EFFECTS: DELAY HP and the cross-feeds moved to MIX");
    /* MIX (2026-11-02): knob 1 BALANCE in DUAL, DRY/WET in SINGL (one byte); LOOP TONE's thinner half is DELAY HP
     * (fx_tone_sync), beside PING PONG in delay_pingpong */
    press(BTN_GLO);
    CHECK(sm.page == UIPAGE_MIX, "GLO -> MIX (%u)", sm.page);
    {
        UiKnobInfo info;
        int br = find_row("BALANCE"), dw = find_row("DRY/WET"), tr = find_row("LOOP TONE");
        CHECK(br >= 0 && dw >= 0 && tr >= 0, "MIX rows (%d %d %d)", br, dw, tr);
        CHECK(ui_sm_row_state(&sm, (uint8_t)br) == UIROW_ACTIVE && ui_sm_row_state(&sm, (uint8_t)dw) == UIROW_HIDDEN,
              "DUAL: BALANCE, not DRY/WET");
        CHECK(ui_sm_knob(&sm, UIPAGE_MIX, 0, 0, &info) == &fx_live[FX_L_BALANCE] && info.label[0] == 'B',
              "DUAL: knob 1 BALANCE (%s)", info.label);
        CHECK(ui_sm_knob(&sm, UIPAGE_MIX, 0, 2, &info) == &fx_cross[FX_X_DLY_REV], "knob 3 DLY > REV");
        perf_state.delay_pingpong = 1u;
        fx_tone_sync(&perf_state);
        sm.s.current_param_focus = (uint8_t)tr;
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 127);
        fx_tone_sync(&perf_state);
        CHECK(fx_live[FX_L_TONE] == 127u && DELAY_HP(&perf_state) == 127u && DELAY_PINGPONG(&perf_state) == 1u,
              "LOOP TONE 127 = DELAY HP 127, PING PONG kept (%u, %u)", fx_live[FX_L_TONE], perf_state.delay_pingpong);
        ui_sm_handle(&sm, UIEV_ALGO_ENC, -127);
        fx_tone_sync(&perf_state);
        CHECK(fx_live[FX_L_TONE] == 0u && DELAY_HP(&perf_state) == 0u && DELAY_PINGPONG(&perf_state) == 1u,
              "LOOP TONE 0 (darkest): DELAY HP off (%u, %u)", fx_live[FX_L_TONE], perf_state.delay_pingpong);
        perf_state.delay_pingpong = (uint8_t)(20u << 1);   /* a performance loaded with DELAY HP 20 */
        fx_tone_sync(&perf_state);
        CHECK(fx_live[FX_L_TONE] == 74u && DELAY_HP(&perf_state) == 20u,
              "a loaded DELAY HP 20 sets LOOP TONE 74 (%u, %u)", fx_live[FX_L_TONE], DELAY_HP(&perf_state));
        perf_state.perf_mode = PERF_SINGL_SYNTH;
        CHECK(ui_sm_row_state(&sm, (uint8_t)br) == UIROW_HIDDEN && ui_sm_row_state(&sm, (uint8_t)dw) == UIROW_ACTIVE &&
              ui_sm_knob(&sm, UIPAGE_MIX, 0, 0, &info) == &fx_live[FX_L_BALANCE] && info.label[0] == 'D',
              "SINGL: knob 1 DRY/WET (%s)", info.label);
        perf_state.perf_mode = PERF_DUAL_SYNTH;
        perf_state.delay_pingpong = 0u;
        fx_tone_sync(&perf_state);
        fx_live[FX_L_TONE] = 64u;
        fx_tone_sync(&perf_state);
    }
    press(BTN_GLO);
    press(BTN_SAVE);
    CHECK(sm.page == UIPAGE_SAVE_COMMIT, "SAVE opens the save page (%u)", sm.page);
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    press(BTN_HOME);
    press(BTN_HOME);                                 /* (LAYER -> the browser) */
    ui_sm_enforce(&sm);

    /* Preset Encoder (5.2): the page picks the kind, each detent the next used slot, the link follows the load */
    ui_sm_attach_store(&sm, used_slots, NULL);
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.layer_patch[0] = 0; perf_state.layer_patch[1] = 1;
    perf_state.seq_link = 0; perf_state.arp_link = 0; sm.perf_slot = 0;
    ui_sm_enforce(&sm);
    CHECK(sm.page == UIPAGE_PERF && ui_sm_preset_target(&sm) == UISAVE_WHOLE_PERF, "HOME loads performances");
    CHECK(preset_turn(1) == 3u && sm.perf_slot == 3u, "HOME +1: perf 3 (%u)", sm.perf_slot);
    CHECK(preset_turn(2) == 30u && sm.perf_slot == 30u, "HOME +2: perf 30 (%u)", sm.perf_slot);
    CHECK(preset_turn(5) == 255u && sm.perf_slot == 30u, "HOME +5 past the bank's last used slot: stays (not 40)");
    CHECK(preset_turn(-1) == 7u, "HOME -1: perf 7");
    press(BTN_SEQ);
    CHECK(ui_sm_preset_target(&sm) == UISAVE_SEQ_SETTINGS && preset_turn(1) == 3u && perf_state.seq_link == 3u,
          "SEQ loads sequencer slots (%u)", perf_state.seq_link);
    press(BTN_ARP);
    CHECK(ui_sm_preset_target(&sm) == UISAVE_ARP_SEQ && preset_turn(3) == 30u && perf_state.arp_link == 30u,
          "ARP loads arp patterns, not past slot 31 (%u)", perf_state.arp_link);
    press(BTN_ENV);
    ui_sm_set_layer_focus(&sm, 1);
    CHECK(ui_sm_preset_target(&sm) == UISAVE_PATCH_PRESET && preset_turn(1) == 3u && perf_state.layer_patch[1] == 3u &&
          perf_state.layer_patch[0] == 0u, "ENV loads the focused layer's patch (L1 %u L2 %u)",
          perf_state.layer_patch[0], perf_state.layer_patch[1]);
    CHECK(preset_turn(3) == 40u && preset_turn(-3) == 3u, "patches walk the 128-slot bank (to 40 and back)");
    ui_sm_handle(&sm, UIEV_PRESET_ENC, 1);
    ui_sm_handle(&sm, UIEV_PRESET_ENC, 1);   /* refused while the first load is pending */
    CHECK(sm.load_pending && sm.load_slot == 7u, "one load at a time");
    ui_sm_load_done(&sm, 0u);
    CHECK(!sm.load_pending && perf_state.layer_patch[1] == 3u, "a failed load leaves the link");

    /* the PERF page's links and PERF PRESET load what they point at (2026-10-27), rows and knobs alike */
    ui_sm_goto(&sm, UIPAGE_PERF);
    perf_state.layer_patch[0] = 0; perf_state.layer_patch[1] = 3; perf_state.seq_link = 3; perf_state.arp_link = 30;
    sm.perf_slot = 7;
    sm.s.current_param_focus = (uint8_t)find_row("LAYER1 PATCH");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    CHECK(sm.load_pending && sm.load_target == UISAVE_PATCH_PRESET && sm.load_layer == 0u && sm.load_slot == 3u &&
          perf_state.layer_patch[0] == 0u, "LAYER1 PATCH row: loads patch 3 into layer 1 (slot %u)", sm.load_slot);
    ui_sm_load_done(&sm, 1u);
    CHECK(perf_state.layer_patch[0] == 3u, "LAYER1 PATCH follows the load (%u)", perf_state.layer_patch[0]);
    ui_sm_perf_link_turn(&sm, 1u, 1);
    CHECK(sm.load_pending && sm.load_layer == 1u && sm.load_slot == 7u, "L2 PATCH knob: patch 7 into layer 2");
    ui_sm_load_done(&sm, 1u);
    sm.s.current_param_focus = (uint8_t)find_row("SEQ LINK");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    CHECK(sm.load_pending && sm.load_target == UISAVE_SEQ_SETTINGS && sm.load_slot == 7u, "SEQ LINK row loads seq 7");
    ui_sm_load_done(&sm, 1u);
    ui_sm_perf_link_turn(&sm, 3u, -1);
    CHECK(sm.load_pending && sm.load_target == UISAVE_ARP_SEQ && sm.load_slot == 7u, "ARP LINK knob loads arp 7");
    ui_sm_load_done(&sm, 1u);
    CHECK(perf_state.layer_patch[1] == 7u && perf_state.seq_link == 7u && perf_state.arp_link == 7u, "links followed");
    sm.s.current_param_focus = (uint8_t)find_row("PERF PRESET");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    CHECK(sm.load_pending && sm.load_target == UISAVE_WHOLE_PERF && sm.load_slot == 30u, "PERF PRESET row loads perf 30");
    ui_sm_load_done(&sm, 1u);
    perf_state.layer_patch[0] = 5;                   /* an empty slot: the popup says so */
    {
        const char *t = ui_sm_row_slot_name(&sm, (uint8_t)find_row("LAYER1 PATCH"));
        CHECK(t != NULL && strcmp(t, "EMPTY") == 0, "slot name of an empty slot: %s", t ? t : "(null)");
        CHECK(ui_sm_row_slot_name(&sm, (uint8_t)find_row("PERF MODE")) == NULL, "PERF MODE has no slot name");
    }
    perf_state.layer_patch[0] = 0;

    printf("\nGLOBAL knob 4: INT TEMPO on the internal clock; TUNING knob 1 MASTER TUNE (2026-11-13)\n");
    {
        UiKnobInfo i;
        uint8_t *p;
        perf_state.clock_source = CLKSRC_INTERNAL;
        p = ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 3, &i);
        CHECK(p == &perf_state.int_tempo && i.kind == UIKNOB_BPM && strcmp(i.label, "TEMPO") == 0,
              "INTERNAL: knob 4 edits INT TEMPO");
        ui_sm_attach_instrument(&sm, &instrument);
        p = ui_sm_knob(&sm, UIPAGE_TUNING, 0, 0, &i);
        CHECK(p == &instrument.master_tune && i.kind == UIKNOB_BIP, "TUNING knob 1: MASTER TUNE");
        CHECK(ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 0, &i) == &instrument.vel_curve && i.n_enum == VELCURVE_COUNT &&
              ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 1, &i) == &instrument.clock_out && i.n_enum == CLKOUT_COUNT,
              "GLOBAL knobs 1, 2: VEL CURVE, CLOCK OUT");
        CHECK(ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 2, &i) == &perf_state.base_velocity, "GLOBAL knob 3: BASE VELOCITY");
        instrument.master_tune = 127u;
        ui_sm_knob_written(&sm, UIPAGE_TUNING, 0u);
        CHECK(instrument.master_tune == MASTER_TUNE_MAX && instrument_tune_cents() == 50, "MASTER TUNE held to +50 cents");
        instrument.master_tune = 64u;
        instrument.vel_curve = VELCURVE_HARD;
        CHECK(instrument_velocity(64) < 64 && instrument_velocity(1) == 1 && instrument_velocity(127) == 127, "HARD curve");
        instrument.vel_curve = VELCURVE_SOFT;
        CHECK(instrument_velocity(64) > 64 && instrument_velocity(127) == 127, "SOFT curve");
        instrument.vel_curve = VELCURVE_LINEAR;
        CHECK(instrument_velocity(64) == 64, "LINEAR curve");
        perf_state.clock_source = CLKSRC_MIDI;
        p = ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 3, &i);
        CHECK(p == NULL && i.kind == UIKNOB_NONE, "MIDI clock: knob 4 greyed");
        perf_state.clock_source = CLKSRC_INTERNAL;
    }

    printf("\nTUNING and ROOT (2026-10-29; their own page 2026-11-13): beside DELAY SYNC / DELAY TYPE's bit 0, ROOT only for a built-in\n");
    {
        uint8_t was = sm.page, r;
        int k;
        sm.page = UIPAGE_TUNING;
        perf_state.delay_sync = 1u;                     /* DELAY SYNC on, TUNING EQUAL */
        perf_state.delay_type = 1u;                     /* ROOT C; bit 0 set (once DELAY TYPE: kept as it is) */
        k = find_row("ROOT");
        CHECK(k < 0 || ui_sm_row_state(&sm, (uint8_t)k) == UIROW_HIDDEN, "EQUAL: no ROOT");
        perf_state.delay_sync = (uint8_t)(TUNING_JUST << 1 | 1u);
        perf_state.delay_type = (uint8_t)(2u << 1 | 1u);
        k = find_row("TUNING");
        CHECK(k >= 0 && strcmp(ui_sm_row_text(&sm, (uint8_t)k), "JUST") == 0, "TUNING reads JUST");
        k = find_row("ROOT");
        r = k >= 0 ? ui_sm_row_state(&sm, (uint8_t)k) : UIROW_HIDDEN;
        CHECK(r == UIROW_ACTIVE && strcmp(ui_sm_row_text(&sm, (uint8_t)k), "D") == 0, "JUST: ROOT D");
        CHECK(DELAY_SYNC(&perf_state) == 1u && (perf_state.delay_type & 1u) == 1u, "the delay's bits kept");
        {   /* the knobs: TUNING and ROOT through their high-7 shadows, the delay's bits kept; ROOT greyed with no root */
            UiKnobInfo i;
            uint8_t *p = ui_sm_knob(&sm, UIPAGE_TUNING, 0, 1, &i);
            CHECK(p != NULL && *p == TUNING_JUST && i.n_enum == TUNING_COUNT, "knob 2 reads JUST");
            *p = TUNING_RAST;
            ui_sm_knob_written(&sm, UIPAGE_TUNING, 1u);
            p = ui_sm_knob(&sm, UIPAGE_TUNING, 0, 2, &i);
            CHECK(PERF_TUNING(&perf_state) == TUNING_RAST && DELAY_SYNC(&perf_state) == 1u && p != NULL && *p == 2u,
                  "knob 2 -> RAST, SYNC kept; knob 3 reads D");
            *p = 9u;
            ui_sm_knob_written(&sm, UIPAGE_TUNING, 2u);
            CHECK(PERF_TUNING_ROOT(&perf_state) == 9u && (perf_state.delay_type & 1u) == 1u, "knob 3 -> A, bit 0 kept");
            p = ui_sm_knob(&sm, UIPAGE_TUNING, 0, 3, &i);
            CHECK(p == &perf_state.bend_range && i.n_enum == 25u, "knob 4: BEND RANGE 0..24");
            perf_state.delay_sync = 1u;
            CHECK(ui_sm_knob(&sm, UIPAGE_TUNING, 0, 2, &i) == NULL && i.kind == UIKNOB_NONE, "EQUAL: knob 3 greyed");
        }
        perf_state.delay_sync = (uint8_t)((TUNING_USER1 + 1) << 1);
        k = find_row("ROOT");
        CHECK(k < 0 || ui_sm_row_state(&sm, (uint8_t)k) == UIROW_HIDDEN, "USER 2: no ROOT");
        perf_state.delay_sync = 0u;
        perf_state.delay_type = 0;
        sm.page = was;
    }


    /* the save page (2026-10-17): SAVE opens it on what the page saves, at the slot it plays from; SAVE stores at once
     * into an empty slot and asks first over a used one; SAVED returns to the page SAVE was pressed on. SAVE held +
     * OCT- / OCT+ copy and paste (the paste opens the save page and asks) */
    printf("\nSAVE page\n");
    {
    UiKnobInfo i;
    ui_sm_attach_store(&sm, used_slots, NULL);
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    ui_sm_enforce(&sm);
    ui_sm_set_layer_focus(&sm, 0);
    perf_state.layer_patch[0] = 7;                    /* a used slot */
    press(BTN_ENV);
    press(BTN_SAVE);
    CHECK(sm.page == UIPAGE_SAVE_COMMIT && sm.save_target == UISAVE_WHOLE_PERF && sm.save_slot == sm.perf_slot,
          "SAVE on ENV1: the save page, PERF, its slot (2026-11-13) (%u, %u, %u)", sm.page, sm.save_target,
          sm.save_slot);
    sm.save_target = UISAVE_PATCH_PRESET;             /* TARGET turned to PATCH */
    ui_sm_knob_written(&sm, UIPAGE_SAVE_COMMIT, 0);
    CHECK(sm.save_slot == 7u, "TARGET to PATCH: the layer's patch slot 7 (%u)", sm.save_slot);
    CHECK(ui_sm_save_armed(&sm), "SAVE flashes on the save page");
    press(BTN_SAVE);
    CHECK(sm.dialog == UIDLG_CONFIRM && !sm.commit_pending, "a used slot asks first");
    press(BTN_SAVE);                                  /* NO (the popup opens on NO) */
    CHECK(sm.dialog == UIDLG_NONE && sm.page == UIPAGE_SAVE_COMMIT, "NO: back on the save page");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    CHECK(sm.save_slot == 8u, "the Algorithm Encoder walks the slots (%u)", sm.save_slot);
    press(BTN_SAVE);
    CHECK(sm.dialog == UIDLG_WRITING && sm.commit_pending, "an empty slot saves at once");
    ui_sm_commit_done(&sm, 1);
    CHECK(sm.dialog == UIDLG_SAVED, "SAVED");
    press(BTN_FX);
    CHECK(sm.dialog == UIDLG_NONE && sm.page == UIPAGE_ENV1, "SAVED: back to ENV1 (%u)", sm.page);
    sm.commit_pending = 0u;
    press(BTN_SEQ);
    press(BTN_SAVE);
    CHECK(sm.save_target == UISAVE_SEQ_SETTINGS, "SAVE on SEQ saves the sequence (%u)", sm.save_target);
    CHECK(ui_sm_knob(&sm, UIPAGE_SAVE_COMMIT, 0, 3, &i) == &sm.seq_name[sm.name_cursor] &&
          ui_sm_rename_name(&sm) == sm.seq_name, "a sequence is named: knob 4 edits sm.seq_name (2026-10-25)");
    sm.save_target = UISAVE_ARP_SEQ;
    CHECK(ui_sm_knob(&sm, UIPAGE_SAVE_COMMIT, 0, 3, &i) == &sm.arp_name[sm.name_cursor], ".. an arp: sm.arp_name");
    sm.save_target = UISAVE_SEQ_SETTINGS;
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 100);
    CHECK(sm.save_slot == BANK_SLOTS - 1u, "a sequence's slots end at 31 (%u)", sm.save_slot);
    sm.save_target = UISAVE_PATCH_PRESET;
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 100);
    CHECK(sm.save_slot == PATCH_SLOTS - 1u, "a patch's at 127 (%u)", sm.save_slot);
    sm.save_target = UISAVE_ARP_SEQ;
    ui_sm_enforce(&sm);
    CHECK(sm.save_slot == BANK_SLOTS - 1u, "TARGET to ARP from patch slot 127: slot 31 (%u)", sm.save_slot);
    sm.save_target = UISAVE_SEQ_SETTINGS;
    press(BTN_HOME);
    press(BTN_SAVE);
    CHECK(sm.save_target == UISAVE_WHOLE_PERF, "SAVE on HOME saves the performance (%u)", sm.save_target);
    CHECK(!ui_sm_keyboard_visible(&sm), "the keyboard waits for the name knobs");
    ui_sm_knob_written(&sm, UIPAGE_SAVE_COMMIT, 3);
    CHECK(ui_sm_keyboard_visible(&sm), "a name knob brings the keyboard up");
    ui_sm_tick(&sm, UI_KB_MS);
    CHECK(!ui_sm_keyboard_visible(&sm), ".. and it goes after UI_KB_MS");
    ui_sm_knob_written(&sm, UIPAGE_SAVE_COMMIT, 3);
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    CHECK(!ui_sm_keyboard_visible(&sm), ".. or at once on an encoder or button");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    /* COPY / PASTE on SAVE + OCT */
    press(BTN_FX);
    ui_sm_save_oct(&sm, 0);
    CHECK(sm.clip_kind == UISAVE_PATCH_PRESET && sm.page == UIPAGE_OSC1, "SAVE + OCT- on OSC1 copies the patch, stays");
    press(BTN_SEQ);
    ui_sm_save_oct(&sm, 1);
    CHECK(sm.page == UIPAGE_SEQ && sm.dialog == UIDLG_NONE, "PASTE refused: the clipboard holds a patch, not a sequence");
    press(BTN_LFO);
    ui_sm_save_oct(&sm, 1);
    CHECK(sm.page == UIPAGE_SAVE_COMMIT && sm.dialog == UIDLG_CONFIRM && sm.save_action == UIACT_PASTE,
          "SAVE + OCT+ on LFO1: the save page asks PASTE?");
    CHECK(sm.save_target == UISAVE_PATCH_PRESET, ".. into a patch (a paste keeps the clipboard's kind)");
    press(BTN_SAVE);                                  /* NO */
    CHECK(sm.dialog == UIDLG_NONE && sm.save_action == UIACT_SAVE, "NO: nothing pasted, SAVE armed again");
    ui_sm_set_hold(&sm, BTN_SAVE);
    CHECK(sm.hold == BTN_SAVE, "the hold strip: SAVE");
    ui_sm_set_hold(&sm, BTN_COUNT);
    CHECK(sm.hold == BTN_COUNT, "the hold strip: gone");
    }
    {   /* THE VOICES POPUP (2026-11-20): an edit that changes the voice budget shows it; a load does not; any control
         * puts it away; it goes by itself after UI_VBPOP_MS */
        uint8_t n0[NLAYER], n1[NLAYER];
        press(BTN_HOME);
        sm.perf->perf_mode = PERF_SINGL_SYNTH;
        sm.perf->reverb_type = REVTYPE_TOMB;
        sm.perf->mix[2] = sm.perf->mix[3] = 0u;
        sm.engine[0]->arch = VARCH_STACK;
        sm.engine[0]->mfx.type = MFX_OFF;
        ui_sm_tick(&sm, 10);
        press(BTN_HOME);                              /* (any control: a clean start) */
        ui_sm_tick(&sm, 10);
        CHECK(sm.vbpop_ms == 0u, "no VOICES popup while nothing changes");
        perf_voice_limits(sm.perf, sm.engine[0], sm.engine[1], n0);
        sm.engine[0]->mfx.type = MFX_ENS_PHASER;      /* an edit that costs voices */
        sm.perf->reverb_type = REVTYPE_CELESTIAL;
        sm.perf->mix[2] = 40u;
        perf_voice_limits(sm.perf, sm.engine[0], sm.engine[1], n1);
        ui_sm_tick(&sm, 10);
        CHECK(n1[0] < n0[0] && sm.vbpop_ms > 0u && sm.vb_from[0] == n0[0] && sm.vb_to[0] == n1[0],
              "an edit that costs voices shows them: %u -> %u (popup %u, %u -> %u)", n0[0], n1[0], sm.vbpop_ms,
              sm.vb_from[0], sm.vb_to[0]);
        press(BTN_HOME);
        CHECK(sm.vbpop_ms == 0u, ".. any control puts it away");
        sm.engine[0]->mfx.type = MFX_OFF;
        ui_sm_tick(&sm, 10);
        CHECK(sm.vbpop_ms > 0u && sm.vb_to[0] > n1[0], ".. and giving them back shows that too");
        ui_sm_tick(&sm, UI_VBPOP_MS);
        CHECK(sm.vbpop_ms == 0u, ".. it goes after UI_VBPOP_MS");
        sm.perf->layer_patch[0] = (uint8_t)(sm.perf->layer_patch[0] + 1u);   /* a load: another patch on layer 1 */
        sm.engine[0]->mfx.type = MFX_METAL;
        ui_sm_tick(&sm, 10);
        CHECK(sm.vbpop_ms == 0u, "a load changes the voices silently");
        sm.perf->mix[2] = 0u;
        sm.perf->reverb_type = REVTYPE_TOMB;
        sm.engine[0]->mfx.type = MFX_OFF;
        sm.engine[0]->arch = VARCH_DUAL_OSC;
        sm.perf->layer_patch[0] = (uint8_t)(sm.perf->layer_patch[0] - 1u);
        ui_sm_tick(&sm, 10);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall UI checks passed\n", fails);
    return fails != 0;
}
