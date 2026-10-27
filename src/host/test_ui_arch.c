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

    /* edit ARCH with the encoders: cursor down to it, step it to STACK */
    sm.s.current_param_focus = (uint8_t)arch_row;
    ui_sm_handle(&sm, UIEV_ALGO_ENC, (int8_t)(VARCH_STACK - engine_state[0].arch));
    CHECK(engine_state[0].arch == VARCH_STACK, "ARCH not STACK: %u", engine_state[0].arch);
    CHECK(ui_sm_row_value(&sm, (uint8_t)vox_row) == 6, "STACK max voices %u", ui_sm_row_value(&sm, (uint8_t)vox_row));
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
    dump("OSCMIX STACK"); knobs(UIPAGE_OSCMIX, "OSCMIX STACK");
    CHECK(find_row("XMOD TYPE") < 0 && find_row("XMOD MOD SRC") >= 0 && find_row("MIX MOD SRC") < 0 &&
          find_row("SPR MOD SRC") < 0, "STACK OSCMIX rows (XMOD TYPE is knob 1)");
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
    CHECK(find_row("BLEND MOD SRC") >= 0 && find_row("RTG MOD SRC") < 0, "STACK FLTMIX rows");

    /* switch to SINGLE while on FILTER MIXER: the page vanishes, fall back to FILTER1 */
    engine_state[0].arch = VARCH_SINGLE;
    ui_sm_enforce(&sm);
    CHECK(sm.page == UIPAGE_FILTER1, "SINGLE fallback from FLTMIX: %u", sm.page);
    press(BTN_SEL); CHECK(sm.page == UIPAGE_FILTER1, "SEL cycles to FILTER1 only under SINGLE: %u", sm.page);
    dump("FILTER1 SINGLE");
    CHECK(find_row("MORPH MOD SRC") >= 0 && find_row("MORPH MOD AMT") >= 0, "FILTER page: the type morph's mod rows");
    CHECK(ui_sm_row_text(&sm, (uint8_t)find_row("MORPH MOD AMT")) != NULL, "MORPH MOD AMT is bipolar text");
    engine_state[0].filter_key_track[0] = 33u; engine_state[0].filter_key_track[1] = 99u;
    CHECK(find_row("KEY TRACK") == 2 && ui_sm_row_value(&sm, 2) == 33u, "FILTER1 KEY TRACK: the third row, its own byte");
    press(BTN_FX); press(BTN_FX);
    CHECK(sm.page == UIPAGE_OSCMIX, "FX skips OSC2 under SINGLE: %u", sm.page);
    dump("OSCMIX SINGLE"); knobs(UIPAGE_OSCMIX, "OSCMIX SINGLE");
    CHECK(find_row("NOISE COLOUR") < 0 && find_row("SUB OCTAVE") < 0 && find_row("XMOD TYPE") < 0 &&
          find_row("COL MOD SRC") >= 0 && find_row("COL MOD AMT") >= 0 && find_row("SUB WAVE") >= 0,
          "SINGLE OSCMIX rows (colour and sub octave are knobs; the colour mod rows)");
    {
        UiKnobInfo i;
        const char *t;
        uint8_t *p = ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 0, &i);
        CHECK(p == &engine_state[0].oscmix.noise_colour && strcmp(i.label, "NOISE COLOUR") == 0, "SINGLE knob 1 = NOISE COLOUR");
        p = ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 1, &i);
        CHECK(p == &engine_state[0].oscmix.noise_level && strcmp(i.label, "NOISE") == 0, "SINGLE knob 2 = NOISE");
        p = ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 2, &i);
        engine_state[0].oscmix.sub_octave = SUBOCT_DOWN2;
        t = ui_sm_knob_text(&sm, UIPAGE_OSCMIX, 0, 2);
        CHECK(p == &engine_state[0].oscmix.sub_octave && i.kind == UIKNOB_LIST && i.n_enum == SUBOCT_COUNT &&
              t != NULL && strcmp(t, "-2 OCT") == 0, "SINGLE knob 3 = SUB OCTAVE list (%s)", t ? t : "NULL");
        engine_state[0].oscmix.sub_octave = SUBOCT_DOWN1;
        p = ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 3, &i);
        CHECK(p == &engine_state[0].oscmix.sub_level && strcmp(i.label, "SUB LEVEL") == 0, "SINGLE knob 4 = SUB LEVEL");
        CHECK(ui_sm_knob_text(&sm, UIPAGE_OSCMIX, 0, 3) == NULL, "a plain knob has no list text");
        p = ui_sm_knob(&sm, UIPAGE_FLTMIX, 0, 0, &i);
        CHECK(p == NULL, "hidden page knob has no byte");
    }

    /* SPREAD: spread, noise, sub, spread mod on the knobs; the spread source stays a row */
    engine_state[0].arch = VARCH_SPREAD;
    ui_sm_enforce(&sm);
    dump("OSCMIX SPREAD"); knobs(UIPAGE_OSCMIX, "OSCMIX SPREAD");
    {
        UiKnobInfo i;
        CHECK(ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 0, &i) == &engine_state[0].oscmix.spread &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 1, &i) == &engine_state[0].oscmix.noise_level &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 2, &i) == &engine_state[0].oscmix.sub_level &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 3, &i) == &engine_state[0].oscmix.spread_mod_amt, "SPREAD knobs");
        CHECK(find_row("NOISE LEVEL") < 0 && find_row("SPREAD MOD AMT") < 0 && find_row("SPR MOD SRC") >= 0 &&
              find_row("NOISE COLOUR") >= 0 && find_row("SUB OCTAVE") >= 0, "SPREAD rows");
    }

    /* BINAURAL: OSC2 and FILTER2 hidden, FILTER MIXER shown with its phase knobs */
    engine_state[0].arch = VARCH_BINAURAL;
    ui_sm_enforce(&sm);
    CHECK(!ui_sm_page_visible(&sm, UIPAGE_OSC2, 0) && !ui_sm_page_visible(&sm, UIPAGE_FILTER2, 0) &&
          ui_sm_page_visible(&sm, UIPAGE_FLTMIX, 0), "BINAURAL pages");
    knobs(UIPAGE_OSCMIX, "OSCMIX BINAURAL");
    press(BTN_SEL); press(BTN_SEL);
    CHECK(sm.page == UIPAGE_FLTMIX, "SEL FILTER1 -> FLTMIX under BINAURAL (skips FILTER2): %u", sm.page);
    dump("FLTMIX BINAURAL"); knobs(UIPAGE_FLTMIX, "FLTMIX BINAURAL");
    CHECK(find_row("CUT L/R SRC") >= 0 && find_row("CUT L/R AMT") >= 0 && find_row("PHASE MOD SRC") >= 0,
          "BINAURAL FLTMIX: CUTOFF L/R's mod rows");

    /* DUAL FILTER: FILTER MIXER keeps the spec's balance / routing */
    engine_state[0].arch = VARCH_DUAL_FILTER;
    ui_sm_enforce(&sm);
    dump("FLTMIX DUAL FILTER"); knobs(UIPAGE_FLTMIX, "FLTMIX DUAL FILTER");
    CHECK(find_row("RTG MOD SRC") >= 0 && find_row("CUT L/R SRC") < 0, "DUAL FILTER routing row, no L/R mod");
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
        uint8_t lo = (uint8_t)(varch_voice_cap(&engine_state[0]) < varch_voice_cap(&engine_state[1])
                                  ? varch_voice_cap(&engine_state[0]) : varch_voice_cap(&engine_state[1]));
        CHECK(ui_sm_layer_voices(&sm, 0) == (lo + 1) / 2 && ui_sm_layer_voices(&sm, 1) == lo / 2, "layer voices %u %u of %u",
              ui_sm_layer_voices(&sm, 0), ui_sm_layer_voices(&sm, 1), lo);
    }
    /* SPLIT: L2 VOICE MODE and L2 UNISON DETUNE mirror layer 1 */
    perf_state.perf_mode = PERF_SPLIT_SYNTH;
    ui_sm_enforce(&sm);
    CHECK(ui_sm_row_state(&sm, (uint8_t)find_row("UNISON DETUNE")) == UIROW_GREYED, "L2 detune mirrored in SPLIT");
    /* SINGL: back to View 1, layer 2 voices 0 */
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    ui_sm_enforce(&sm);
    CHECK(sm.page == UIPAGE_HOME1 && sm.s.active_synth_layer == 0 && ui_sm_layer_voices(&sm, 1) == 0, "SINGL reset");
    /* the cycle: SINGL LAYER -> browser -> GLOBAL -> LAYER; DUAL LAYER -> ZONES -> browser -> GLOBAL -> LAYER */
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_PERF, "SINGL HOME1 -> browser (%u)", sm.page);
    CHECK(find_row("PERF PRESET") == 0 && find_row("PERF MODE") == 1, "the browser: PERF PRESET, PERF MODE");
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_GLOBAL, "SINGL browser -> GLOBAL (%u)", sm.page);
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME1, "SINGL GLOBAL -> HOME1 (%u)", sm.page);
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    ui_sm_enforce(&sm);
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME3, "DUAL HOME1 -> ZONES (%u)", sm.page);
    press(BTN_HOME); press(BTN_HOME);
    CHECK(sm.page == UIPAGE_GLOBAL, "DUAL ZONES -> browser -> GLOBAL (%u)", sm.page);
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME1, "DUAL GLOBAL -> HOME1 (%u)", sm.page);
    /* the layer switch swaps HOME1 / HOME2 in place, the cursor on the same row; the cycle never shows both */
    sm.s.current_param_focus = 4u;   /* PORTA MODE (ANALOG is row 1 since 2026-10-21) */
    CHECK(ui_sm_set_layer_focus(&sm, 1) && sm.page == UIPAGE_HOME2 && sm.s.current_param_focus == 4u,
          "L2 focus on HOME1 -> HOME2, row kept (%u, %u)", sm.page, sm.s.current_param_focus);
    press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME3, "HOME2 -> ZONES (%u)", sm.page);
    press(BTN_HOME); press(BTN_HOME); press(BTN_HOME);
    CHECK(sm.page == UIPAGE_HOME2, "L2: browser -> GLOBAL -> HOME2 (%u)", sm.page);
    sm.s.current_param_focus = 4u;
    CHECK(ui_sm_set_layer_focus(&sm, 0) && sm.page == UIPAGE_HOME1 && sm.s.current_param_focus == 4u,
          "L1 focus on HOME2 -> HOME1, row kept (%u, %u)", sm.page, sm.s.current_param_focus);
    ui_sm_set_layer_focus(&sm, 1);
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    ui_sm_enforce(&sm);
    CHECK(sm.page == UIPAGE_HOME1 && sm.s.active_synth_layer == 0, "SINGL on HOME2 -> HOME1 (%u)", sm.page);
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    ui_sm_enforce(&sm);
    press(BTN_GLO); press(BTN_GLO); press(BTN_GLO);
    CHECK(sm.page == UIPAGE_AMP, "GLO cycles AMP -> EFFECTS -> AMP (%u)", sm.page);
    /* DELAY HP and PING PONG share delay_pingpong (bits 1..7, bit 0): each row turns its own bits */
    press(BTN_GLO);
    CHECK(sm.page == UIPAGE_EFFECTS, "GLO -> EFFECTS (%u)", sm.page);
    perf_state.delay_pingpong = 1u;
    sm.s.current_param_focus = (uint8_t)find_row("DELAY HP");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 127);
    CHECK(perf_state.delay_pingpong == 255u && DELAY_HP(&perf_state) == 127u && DELAY_PINGPONG(&perf_state) == 1u,
          "DELAY HP to 127 keeps PING PONG (%u)", perf_state.delay_pingpong);
    sm.s.current_param_focus = (uint8_t)find_row("PING PONG");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, -1);
    CHECK(perf_state.delay_pingpong == 254u && ui_sm_row_value(&sm, (uint8_t)find_row("DELAY HP")) == 127u,
          "PING PONG off keeps DELAY HP (%u)", perf_state.delay_pingpong);
    perf_state.delay_pingpong = 0u;
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

    printf("\nGLOBAL knob 4: INT TEMPO on the internal clock\n");
    {
        UiKnobInfo i;
        uint8_t *p;
        perf_state.clock_source = CLKSRC_INTERNAL;
        p = ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 3, &i);
        CHECK(p == &perf_state.int_tempo && i.kind == UIKNOB_BPM && strcmp(i.label, "TEMPO") == 0,
              "INTERNAL: knob 4 edits INT TEMPO");
        ui_sm_attach_instrument(&sm, &instrument);
        p = ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 0, &i);
        CHECK(p == &instrument.master_tune && i.kind == UIKNOB_BIP, "knob 1: MASTER TUNE");
        CHECK(ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 1, &i) == &instrument.vel_curve && i.n_enum == VELCURVE_COUNT &&
              ui_sm_knob(&sm, UIPAGE_GLOBAL, 0, 2, &i) == &instrument.clock_out && i.n_enum == CLKOUT_COUNT,
              "knobs 2, 3: VEL CURVE, CLOCK OUT");
        instrument.master_tune = 127u;
        ui_sm_knob_written(&sm, UIPAGE_GLOBAL, 0u);
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

    /* STRIKE (2026-10-16): FX cycles OSC1 -> ATTACK -> OSCMIX; ATTACK's knobs are DECAY, TO FILTER, BEND, BEND MOD
     * (2026-10-17), its rows sound, start, tune, velocity, key track, bend mod source; OSCMIX has the attack level;
     * no other architecture shows ATTACK */
    {
        UiKnobInfo i;
        ui_sm_set_layer_focus(&sm, 0u);                  /* layer 1 (an earlier check may have left layer 2) */
        engine_state[0].arch = VARCH_STRIKE;
        ui_sm_enforce(&sm);
        press(BTN_FX);
        if (sm.page != UIPAGE_OSC1) press(BTN_FX);
        while (sm.page != UIPAGE_OSC1) press(BTN_FX);
        press(BTN_FX); CHECK(sm.page == UIPAGE_ATTACK, "STRIKE: FX 2 is ATTACK, not %u", sm.page);
        dump("ATTACK STRIKE"); knobs(UIPAGE_ATTACK, "ATTACK STRIKE");
        CHECK(ui_sm_knob(&sm, UIPAGE_ATTACK, 0, 0, &i) == &engine_state[0].oscmix.atk_decay &&
              ui_sm_knob(&sm, UIPAGE_ATTACK, 0, 1, &i) == &engine_state[0].oscmix.atk_filter, "ATTACK knobs 1, 2");
        CHECK(ui_sm_knob(&sm, UIPAGE_ATTACK, 0, 2, &i) == &engine_state[0].oscmix.atk_bend && i.kind == UIKNOB_BIP,
              "ATTACK knob 3: BEND, bipolar");
        CHECK(ui_sm_knob(&sm, UIPAGE_ATTACK, 0, 3, &i) == &engine_state[0].oscmix.atk_bend_mod_amt && i.kind == UIKNOB_BIP,
              "ATTACK knob 4: BEND MOD, bipolar (as OSC1's PITCH MOD)");
        CHECK(find_row("SOUND") == 0 && find_row("START") == 1 && find_row("TUNE") >= 0 && find_row("VELOCITY") >= 0 &&
              find_row("KEY TRACK") >= 0 && find_row("BEND MOD SRC") >= 0, "ATTACK rows: SOUND, START first, BEND MOD SRC");
        press(BTN_FX); CHECK(sm.page == UIPAGE_OSCMIX, "STRIKE: FX 3 is OSCMIX, not %u", sm.page);
        CHECK(ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 0, &i) == &engine_state[0].oscmix.atk_level &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 1, &i) == &engine_state[0].oscmix.noise_colour &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 2, &i) == &engine_state[0].oscmix.sub_level &&
              ui_sm_knob(&sm, UIPAGE_OSCMIX, 0, 3, &i) == &engine_state[0].oscmix.noise_level, "STRIKE OSCMIX knobs");
        CHECK(find_row("NOISE COLOUR") < 0 && find_row("SUB OCTAVE") >= 0, "STRIKE OSCMIX: colour a knob, octave a row");
        press(BTN_FX); CHECK(sm.page == UIPAGE_OSC1, "STRIKE: FX wraps to OSC1 (no OSC2), not %u", sm.page);
        for (uint8_t a = 0; a < VARCH_COUNT; a++) {
            engine_state[0].arch = a;
            CHECK(ui_sm_page_visible(&sm, UIPAGE_ATTACK, 0) == (a == VARCH_STRIKE), "ATTACK shown under arch %u", a);
        }
        engine_state[0].arch = VARCH_SINGLE;
        ui_sm_enforce(&sm);
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
    CHECK(sm.page == UIPAGE_SAVE_COMMIT && sm.save_target == UISAVE_PATCH_PRESET && sm.save_slot == 7u,
          "SAVE on ENV1: the save page, PATCH, slot 7 (%u, %u, %u)", sm.page, sm.save_target, sm.save_slot);
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
    press(BTN_SAVE);                                  /* NO */
    CHECK(sm.dialog == UIDLG_NONE && sm.save_action == UIACT_SAVE, "NO: nothing pasted, SAVE armed again");
    ui_sm_set_hold(&sm, BTN_SAVE);
    CHECK(sm.hold == BTN_SAVE, "the hold strip: SAVE");
    ui_sm_set_hold(&sm, BTN_COUNT);
    CHECK(sm.hold == BTN_COUNT, "the hold strip: gone");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall UI checks passed\n", fails);
    return fails != 0;
}
