/* Browser entry point for the synth core. Same setup and render path as render_demo.c (synth_init, then
 * synth_render_q15 in blocks, the make-up gain on the Q15 output), exported to JavaScript.
 *
 * web/emu/build.sh compiles this with the src/dsp C files into web/emu/synth.js and synth.wasm.
 *
 *   web_init()                 once, before anything else (synth_init: the default patch)
 *   web_midi(status, d1, d2)   a channel message or a realtime byte, as synth_midi_3 takes it
 *   web_render(frames)         up to SYNTH_WEB_MAX_FRAMES frames into web_out_ptr()
 *   web_out_ptr()              interleaved stereo Q15 (int32 per sample), the core's own level (limited to -1 dBFS)
 *
 * The core is integer-only and stays that way: the gain is an integer multiply here, and the worklet only
 * scales Q15 to the float range Web Audio wants. The core is written for 44.1 kHz (LFO and oscillator phase
 * steps, MIDI clock frames per tick), so the page must run its AudioContext at web_sample_rate(). */
#include <stdint.h>
#include <string.h>
#include "dsp/synth_runtime.h"
#include "dsp/preset.h"
#include "PresetStructures.h"
#include "UIStateMachine.h"

#define WEB_SAMPLE_RATE 44100u
/* knob pages: the spec 2.1 submenus the core has parameters for, in 2.2 knob order. HOME's knobs 1..3 are
 * performance controllers the page sends as MIDI (velocity, pitch bend, mod wheel), so only knob 4 is in the core. */
#define WEB_KNOB_NONE 255u

#define EXPORT __attribute__((used, visibility("default")))

/* page ids: web/emu/index.html carries the same order */
enum {
    WPAGE_OSC1, WPAGE_OSC2, WPAGE_OSCMIX,
    WPAGE_FILTER1, WPAGE_FILTER2, WPAGE_FLTMIX,
    WPAGE_ENV1, WPAGE_ENV2, WPAGE_ENV3,
    WPAGE_LFO1, WPAGE_LFO2, WPAGE_LFO3,
    WPAGE_MOD1, WPAGE_MOD2, WPAGE_MOD3,
    WPAGE_AMP, WPAGE_EFFECTS, WPAGE_HOME,
    WPAGE_SEQ,                       /* the MAQ sequencer page (spec 7): knobs, keys and the screen (web_seq_screen.c) */
    WPAGE_ARP,                       /* the ARP chord page (spec 8): knobs, the keybed split and the screen */
    WPAGE_GLOBAL,                    /* HOME's last page (4.6; was GLO submenu 3) */
    WPAGE_HOME3,                     /* HOME View 3, the split (5.3) */
    WPAGE_SAVE1, WPAGE_SAVE2,        /* SAVE submenus 1 and 2 (6.1, 6.2) */
    WPAGE_COUNT
};

/* The browser's page ids are the UIPAGE_* ids of UIStateMachine.h, except HOME, which the browser lists once
 * (its View 1). These asserts keep the two orders in step. */
/* Since 2026-10-16 the core has STRIKE's ATTACK page after OSC 2, which the browser does not show yet: its ids from
 * OSC MIX on are one below the core's. */
PS_STATIC_ASSERT((int)WPAGE_OSC1 == UIPAGE_OSC1 && (int)WPAGE_OSC2 == UIPAGE_OSC2 && UIPAGE_ATTACK == UIPAGE_OSC2 + 1 &&
                 (int)WPAGE_LFO3 + 1 == UIPAGE_LFO3 && (int)WPAGE_MOD3 + 1 == UIPAGE_MOD3, "UI page order");
PS_STATIC_ASSERT((int)WPAGE_AMP + 1 == UIPAGE_AMP && (int)WPAGE_EFFECTS + 1 == UIPAGE_EFFECTS, "UI page order");
PS_STATIC_ASSERT(UIPAGE_HOME1 == UIPAGE_EFFECTS + 1, "HOME View 1 follows EFFECTS in UIPAGE order");

/* the Select and Algorithm encoders: the core's UI state machine, which owns the page's selectable rows */
static UIStateMachine web_ui;

static uint32_t web_ready;

/* The browser page id as a UIPAGE_* id: the same up to EFFECTS, HOME is View 1 (its knobs are the same in every
 * view). UIPAGE_COUNT for an id the browser does not have. */
static uint8_t web_uipage(uint32_t page)
{
    static const uint8_t TAIL[WPAGE_COUNT - WPAGE_HOME] = {
        UIPAGE_HOME1, UIPAGE_SEQ, UIPAGE_ARP, UIPAGE_GLOBAL, UIPAGE_HOME3, UIPAGE_PERF, UIPAGE_SAVE_COMMIT,
    };
    if (page <= WPAGE_OSC2) return (uint8_t)page;
    if (page <= WPAGE_EFFECTS) return (uint8_t)(page + 1u);           /* past ATTACK */
    return page < WPAGE_COUNT ? TAIL[page - WPAGE_HOME] : (uint8_t)UIPAGE_COUNT;
}

/* the reverse: the browser's id for the core's page (HOME View 1 and 2 are both WPAGE_HOME) */
static uint32_t web_wpage(uint8_t uipage)
{
    uint32_t w;
    if (uipage <= UIPAGE_OSC2) return uipage;
    if (uipage == UIPAGE_ATTACK) return WPAGE_OSC1;                   /* (the browser has no ATTACK page) */
    if (uipage <= UIPAGE_EFFECTS) return (uint32_t)uipage - 1u;
    if (uipage == UIPAGE_HOME2) return WPAGE_HOME;
    for (w = WPAGE_HOME; w < WPAGE_COUNT; w++)
        if (web_uipage(w) == uipage) return w;
    return WPAGE_HOME;
}

/* The stored byte behind one knob (0..3) of a page for one layer, from the core's knob map (UIStateMachine.c,
 * spec 2.2), which follows that layer's voice architecture. NULL when the page has no parameter on that knob.
 * *n_enum is set to the list length for a knob that picks from a list (a mod source or a transform function),
 * else 0: those knobs take 0..127 and store the list index. */
static uint8_t *knob_slot(uint32_t page, uint32_t layer, uint32_t knob, uint32_t *n_enum, UiKnobInfo *info)
{
    *n_enum = 0;
    info->label = "";
    info->kind = UIKNOB_NONE;
    info->n_enum = 0;
    if (!web_ready || layer >= NLAYER || knob >= 4u || web_uipage(page) >= UIPAGE_COUNT)
        return 0;
    uint8_t *p = ui_sm_knob(&web_ui, web_uipage(page), (uint8_t)layer, (uint8_t)knob, info);
    *n_enum = info->n_enum;
    return p;
}


/* The emulator's store (6.2.3): no flash, so a confirmed save lands in RAM for the page's lifetime. Patches go to
 * preset.c's patch_bank (128), sequences, arps and whole performances to the banks below (32 each, as on the FM1 since
 * 2026-10-17); web_used marks the slots written, so a never-written slot reads EMPTY as an erased flash record would,
 * and the overwrite warning has something to warn about. */
static WholePerformance web_perf_bank[BANK_SLOTS];
static SequencerLane web_seq_bank[BANK_SLOTS][SEQ_NLANE];
static ArpState web_arp_bank[BANK_SLOTS];
static uint8_t web_used[UISAVE_COUNT][PATCH_SLOTS / 8u];

static uint8_t web_slot_used(uint8_t target, uint8_t slot)
{
    return (uint8_t)((web_used[target][slot >> 3] >> (slot & 7u)) & 1u);
}

static uint8_t web_slot_query(void *ctx, uint8_t target, uint8_t slot, uint8_t name[NAME_LEN])
{
    (void)ctx;
    if (target >= UISAVE_COUNT || slot >= ui_sm_bank_slots(target) || !web_slot_used(target, slot))
        return UISLOT_EMPTY;
    if (target == UISAVE_PATCH_PRESET) memcpy(name, patch_bank[slot].name, NAME_LEN);
    else if (target == UISAVE_WHOLE_PERF) memcpy(name, web_perf_bank[slot].perf.name, NAME_LEN);
    return UISLOT_USED;
}

/* the core's commit_pending: write save_target to save_slot, then report it done. A performance: the live one and
 * everything it plays */
static void web_commit(void)
{
    uint8_t t = web_ui.save_target, s = web_ui.save_slot;
    WholePerformance *w;
    if (t >= UISAVE_COUNT || s >= ui_sm_bank_slots(t)) {
        ui_sm_commit_done(&web_ui, 0u);
        return;
    }
    switch (t) {
    case UISAVE_PATCH_PRESET: patch_store(web_ui.s.active_synth_layer, s); break;
    case UISAVE_SEQ_SETTINGS: memcpy(web_seq_bank[s], seq_lanes, sizeof web_seq_bank[s]); break;
    case UISAVE_ARP_SEQ: web_arp_bank[s] = arp_state; break;
    default:
        w = &web_perf_bank[s];
        w->perf = perf_state;
        w->engine[0] = engine_state[0];
        w->engine[1] = engine_state[1];
        memcpy(w->seq, seq_lanes, sizeof w->seq);
        w->arp = arp_state;
        break;
    }
    web_used[t][s >> 3] |= (uint8_t)(1u << (s & 7u));
    ui_sm_commit_done(&web_ui, 1u);
}

/* the core's load_pending (the Preset Encoder): load_target from load_slot into the live state. A performance brings
 * everything it saved: its settings, both layers' patches, the sequence and the arp pattern */
static void web_load(void)
{
    uint8_t t = web_ui.load_target, s = web_ui.load_slot, ok;
    const WholePerformance *w;
    ok = (uint8_t)(t < UISAVE_COUNT && s < ui_sm_bank_slots(t) && web_slot_used(t, s));
    if (ok) {
        switch (t) {
        case UISAVE_PATCH_PRESET: patch_load(web_ui.load_layer, s); break;
        case UISAVE_SEQ_SETTINGS: memcpy(seq_lanes, web_seq_bank[s], sizeof seq_lanes); break;
        case UISAVE_ARP_SEQ: arp_state = web_arp_bank[s]; break;
        default:
            w = &web_perf_bank[s];
            perf_state = w->perf;
            engine_state[0] = w->engine[0];
            engine_state[1] = w->engine[1];
            memcpy(seq_lanes, w->seq, sizeof seq_lanes);
            arp_state = w->arp;
            break;
        }
    }
    ui_sm_load_done(&web_ui, ok);
}

EXPORT void web_init(void)
{
    if (web_ready)
        return;
    synth_init();
    /* the UI edits the live sequencer slot and arp pattern the render's engines play (preset.c seq_lanes, arp_state) */
    ui_sm_init(&web_ui, &perf_state, &engine_state[0], &engine_state[1], seq_lanes, &arp_state);
    ui_sm_attach_seq(&web_ui, synth_seq());
    ui_sm_attach_arp(&web_ui, synth_arp());
    ui_sm_attach_store(&web_ui, web_slot_query, NULL);
    web_ready = 1;
}

EXPORT void web_midi(uint32_t status, uint32_t data1, uint32_t data2)
{
    if (!web_ready)
        return;
    synth_midi_3((uint8_t)status, (uint8_t)data1, (uint8_t)data2);
}

EXPORT uint32_t web_render(uint32_t frames)
{
    int32_t *lr = synth_web_buffer();

    if (frames > SYNTH_WEB_MAX_FRAMES)
        frames = SYNTH_WEB_MAX_FRAMES;
    if (!web_ready) {
        memset(lr, 0, frames * 2u * sizeof(int32_t));
        return frames;
    }
    synth_web_render(frames);                         /* at full level: the core's master limiter sets the ceiling */
    return frames;
}

EXPORT int32_t *web_out_ptr(void) { return synth_web_buffer(); }
EXPORT uint32_t web_max_frames(void) { return SYNTH_WEB_MAX_FRAMES; }
EXPORT uint32_t web_sample_rate(void) { return WEB_SAMPLE_RATE; }

/* A knob's position, 0..127. A list knob reads back the lowest position that selects its stored index, so
 * web_knob_set(v) then web_knob_get() returns the same index. WEB_KNOB_NONE when the page has no such knob. */
EXPORT uint32_t web_knob_get(uint32_t page, uint32_t layer, uint32_t knob)
{
    uint32_t n;
    UiKnobInfo info;
    uint8_t *p = knob_slot(page, layer, knob, &n, &info);
    if (info.kind == UIKNOB_SEQ) return ui_sm_seq_knob_get(&web_ui, (uint8_t)knob);   /* a step value (7.1 / 7.2) */
    if (!p) return WEB_KNOB_NONE;
    if (n == 0u) return *p;
    return ((uint32_t)*p * 128u + n - 1u) / n;
}

/* value 0..127 from the knob; a list knob keeps the index it falls in (value * n / 128) */
EXPORT void web_knob_set(uint32_t page, uint32_t layer, uint32_t knob, uint32_t value)
{
    uint32_t n;
    UiKnobInfo info;
    uint8_t *p = knob_slot(page, layer, knob, &n, &info);
    if (value > VAL_MAX) value = VAL_MAX;
    if (info.kind == UIKNOB_SEQ) {                    /* through the core: a NOTE lane's chord moves with its first note */
        ui_sm_seq_knob_set(&web_ui, (uint8_t)knob, (uint8_t)value);
        return;
    }
    if (!p) return;
    *p = (uint8_t)(n == 0u ? value : (value * n) / 128u);
    ui_sm_knob_written(&web_ui, web_uipage(page), (uint8_t)knob);   /* ARP: into the held steps too (8.1) */
}

/* A knob's name for the layer's voice architecture ("" when the page has nothing on it) and how its value reads.
 * HOME knobs 1..3 (UIKNOB_PERF) keep their last value in the core (the page also sends them as MIDI), so the screen
 * shows them:
 * UIKNOB_* (0 none, 1 raw, 2 bipolar, 3 octave, 4 mod source, 5 function, 6 HOME performance controller, 7 SEQ step,
 * 8 list: SUB OCTAVE, XMOD TYPE, 9 BPM: GLOBAL's INT TEMPO). */
EXPORT const char *web_knob_label(uint32_t page, uint32_t layer, uint32_t knob)
{
    uint32_t n;
    UiKnobInfo info;
    knob_slot(page, layer, knob, &n, &info);
    return info.label;
}

EXPORT uint32_t web_knob_kind(uint32_t page, uint32_t layer, uint32_t knob)
{
    uint32_t n;
    UiKnobInfo info;
    knob_slot(page, layer, knob, &n, &info);
    return info.kind;
}

/* the knob's value as its list names it ("-1 OCT", "RING", "env2"), NULL for a number the page formats itself */
EXPORT const char *web_knob_text(uint32_t page, uint32_t layer, uint32_t knob)
{
    if (!web_ready || layer >= NLAYER || knob >= 4u || web_uipage(page) >= UIPAGE_COUNT) return NULL;
    return ui_sm_knob_text(&web_ui, web_uipage(page), (uint8_t)layer, (uint8_t)knob);
}

/* 1 when the page shows for the layer's voice architecture (OSC2, FILTER2 and FILTER MIXER can be hidden) */
EXPORT uint32_t web_page_visible(uint32_t page, uint32_t layer)
{
    if (!web_ready || layer >= NLAYER || web_uipage(page) >= UIPAGE_COUNT) return 0;
    return ui_sm_page_visible(&web_ui, web_uipage(page), (uint8_t)layer);
}

/* the layer's voice architecture (VARCH_*), its name, and the voices it may sound now (HOME's MAX VOICES) */
EXPORT uint32_t web_layer_arch(uint32_t layer) { return layer < NLAYER ? varch_of(&engine_state[layer]) : 0u; }
EXPORT const char *web_arch_name(uint32_t arch) { return ui_sm_arch_name((uint8_t)(arch > 255u ? 255u : arch)); }
EXPORT uint32_t web_layer_voices(uint32_t layer) { return web_ready ? ui_sm_layer_voices(&web_ui, (uint8_t)layer) : 0u; }

/* PERF_SINGL_SYNTH / PERF_DUAL_SYNTH / PERF_SPLIT_SYNTH; returns the mode set, or the current one if out of range */
EXPORT uint32_t web_perf_mode(uint32_t mode)
{
    if (mode < PERF_COUNT) perf_state.perf_mode = (uint8_t)mode;
    if (web_ready) ui_sm_enforce(&web_ui);            /* SINGL drops LAYER 2 and HOME views 2 and 3 (5.3) */
    return perf_state.perf_mode;
}

/* ---- the Select and Algorithm encoders (spec 3, 2.1): the page's selectable parameters as rows ----
 * web_ui_page takes a browser page id (WPAGE_*). A page with no selectable rows (the MOD pages, whose four
 * parameters are all knobs) has a count of 0. Text is a NUL-terminated string in the module's memory; NULL means
 * a plain number, which the caller draws itself. */
EXPORT void web_ui_page(uint32_t page)
{
    if (!web_ready) return;
    if (page == WPAGE_HOME)          /* HOME with LAYER 2 in focus: View 2, the layer 2 rows (SINGL falls back to View 1) */
        ui_sm_goto(&web_ui, web_ui.s.active_synth_layer ? (uint8_t)UIPAGE_HOME2 : (uint8_t)UIPAGE_HOME1);
    else if (page < WPAGE_COUNT) ui_sm_goto(&web_ui, web_uipage(page));
}

/* A section button (BTN_*, spec 2.1), through the core as the FM1's button handler sends it: the first press opens the
 * section, the next cycle its submenus (skipping hidden pages), SAVE runs 6.2.2's ACTION (SAVE / COPY / PASTE) and its
 * popups, and any button clears SAVED. The emulator has no flash: a confirmed save lands in RAM at once (web_commit). */
EXPORT void web_ui_button(uint32_t btn)
{
    if (!web_ready || btn >= BTN_COUNT) return;
    ui_sm_handle(&web_ui, UIEV_BUTTON, (int8_t)btn);
    if (web_ui.commit_pending) web_commit();
}

/* where the core is: its page as the browser's id, the raw UIPAGE_* (HOME View 1 / 2 / 3), the layer in focus */
EXPORT uint32_t web_ui_wpage(void) { return web_ready ? web_wpage(web_ui.page) : 0u; }
EXPORT uint32_t web_ui_uipage(void) { return web_ready ? web_ui.page : 0u; }
EXPORT uint32_t web_ui_focus_layer(void) { return web_ready ? web_ui.s.active_synth_layer : 0u; }
EXPORT uint32_t web_ui_save_armed(void) { return web_ready ? ui_sm_save_armed(&web_ui) : 0u; }   /* SAVE flashes */

EXPORT void web_ui_layer(uint32_t layer)
{
    if (web_ready) ui_sm_set_layer_focus(&web_ui, (uint8_t)layer);   /* refused under SINGL, as the knobs are */
}

/* detents: + = clockwise (down the list / value up), as UIEV_SELECT_ENC and UIEV_ALGO_ENC take them */
static int8_t web_detents(int32_t d)
{
    if (d > 127) d = 127;
    if (d < -127) d = -127;
    return (int8_t)d;
}

EXPORT void web_ui_select(int32_t detents)
{
    if (web_ready) ui_sm_handle(&web_ui, UIEV_SELECT_ENC, web_detents(detents));
}

EXPORT void web_ui_algo(int32_t detents)
{
    if (web_ready) ui_sm_handle(&web_ui, UIEV_ALGO_ENC, web_detents(detents));
}

/* the Preset Encoder (5.2): the page's kind of preset, the next saved slot that way (the emulator's RAM store) */
EXPORT void web_ui_preset(int32_t detents)
{
    if (!web_ready) return;
    ui_sm_handle(&web_ui, UIEV_PRESET_ENC, web_detents(detents));
    if (web_ui.load_pending) web_load();
}

/* what the Preset Encoder loads on this page (UISAVE_*: 0 ARP, 1 SEQ, 2 PATCH, 3 PERF) and the slot playing now */
EXPORT uint32_t web_ui_preset_target(void) { return web_ready ? ui_sm_preset_target(&web_ui) : 0u; }
EXPORT uint32_t web_ui_preset_slot(void) { return web_ready ? ui_sm_preset_slot(&web_ui) : 0u; }

EXPORT uint32_t web_ui_count(void) { return web_ready ? ui_sm_row_count(&web_ui) : 0u; }

/* the highlighted row, 255 when the page has none */
EXPORT uint32_t web_ui_cursor(void) { return web_ready ? web_ui.s.current_param_focus : UI_FOCUS_NONE; }

EXPORT uint32_t web_ui_active(uint32_t row)
{
    return web_ready && ui_sm_row_state(&web_ui, (uint8_t)row) != UIROW_GREYED;   /* greyed rows read 0 */
}

/* UIROW_*: 0 active, 1 greyed, 2 read-only, 3 hidden (not drawn: SEQ TARGET with one option) */
EXPORT uint32_t web_ui_state(uint32_t row) { return web_ready ? ui_sm_row_state(&web_ui, (uint8_t)row) : UIROW_GREYED; }

EXPORT uint32_t web_ui_value(uint32_t row) { return web_ready ? ui_sm_row_value(&web_ui, (uint8_t)row) : 0u; }

EXPORT const char *web_ui_label(uint32_t row) { return web_ready ? ui_sm_row_label(&web_ui, (uint8_t)row) : ""; }

EXPORT const char *web_ui_text(uint32_t row) { return web_ready ? ui_sm_row_text(&web_ui, (uint8_t)row) : NULL; }

/* ---- the MAQ sequencer (spec 7): transport, the selector keys, and what the page's buttons show ---- */
/* the machine the screen renderer reads (web_seq_screen.c) */
UIStateMachine *web_ui_sm(void) { return &web_ui; }

/* UITR_PLAY 0 / UITR_REC 1, as the FM1's PLAY and REC buttons */
EXPORT void web_ui_transport(uint32_t which)
{
    if (web_ready && which < UITR_COUNT) ui_sm_handle(&web_ui, UIEV_TRANSPORT, (int8_t)which);
}

/* A keyboard key on the SEQ page: key = keys above the lowest, note = its MIDI note, mods = UISEQ_MOD_* (OCT- / OCT+
 * held). 1: the core took it as a step selector or loop shortcut (do not play it); 0: play it as a note. */
EXPORT uint32_t web_seq_key(uint32_t key, uint32_t note, uint32_t down, uint32_t mods)
{
    if (!web_ready || key > 255u || note > 127u) return 0;
    return ui_sm_seq_key(&web_ui, (uint8_t)key, (uint8_t)note, (uint8_t)(down != 0u), (uint8_t)mods);
}

/* A key on the ARP page (8.1): the lowest 13 keys are chord keys, the next 8 white keys step keys. 1: the core took
 * it (do not play it); 0: play it. */
EXPORT uint32_t web_arp_key(uint32_t key, uint32_t note, uint32_t down, uint32_t vel)
{
    if (!web_ready || key > 255u || note > 127u) return 0;
    return ui_sm_arp_key(&web_ui, (uint8_t)key, (uint8_t)note, (uint8_t)(down != 0u), (uint8_t)(vel > 127u ? 127u : vel));
}

/* the ARP step playing (0..7, 255 stopped) and the step keys held (bit n = step n): the page lights the step keys */
EXPORT uint32_t web_arp_pos(void) { return web_ready ? carp_pos(synth_arp()) : 255u; }
EXPORT uint32_t web_arp_held(void) { return web_ready ? web_ui.arp_held : 0u; }

/* the key LEDs where the page gives the keys a job (ui_sm_key_leds), from the keyboard's lowest note and the OCT
 * buttons held (UISEQ_MOD_*): which 0 = lit, 1 = mid, 2 = dim; bit k = key k */
EXPORT uint32_t web_key_leds(uint32_t lowest, uint32_t mods, uint32_t which)
{
    UiKeyLeds kl;
    if (!web_ready || lowest > 127u)
        return 0u;
    ui_sm_key_leds(&web_ui, (uint8_t)lowest, (uint8_t)mods, &kl);
    return which == 0u ? kl.lit : which == 1u ? kl.mid : kl.dim;
}

EXPORT uint32_t web_seq_playing(void) { return web_ready ? ui_sm_play_lit(&web_ui) : 0u; }   /* the PLAY light */
EXPORT uint32_t web_seq_recording(void) { return web_ready ? (uint32_t)(maq_recording(synth_seq()) || web_ui.arp_rec) : 0u; }   /* the REC light: SEQ step record or ARP record */
