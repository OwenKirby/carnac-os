/* Mvave FM1 menu navigation: the 10 section buttons, the Select Encoder and the Algorithm Encoder.
 * mvave_fm1_master_specification.txt: 2.1 (button -> submenu cycling), 3 (the middle-box menu list),
 * 2.2 (the knob map, per voice architecture), 4.0 (pages and rows an architecture hides),
 * 5.2 (layer focus; the Preset Encoder loads presets), 5.3 (HOME views), 6 / 6.2.2 / 6.2.3 (SAVE submenus, the commit dialogs, the overwrite warning,
 * the clipboard), 6.3 (a context shift never touches the clock: nothing here does), 7.1 / 7.2 (SEQ knob groups, loop
 * keys, step record),
 * 7.4 (the SEQ footer strings, TARGET's 4-slot layout), 7.5 / 8.2 (SEQ and ARP value availability), 8.1 (the ARP
 * keybed split, step capture) and 2.2's ARP knobs.
 * The input ISRs only call ui_sm_post(); the main loop calls ui_sm_process(), which owns every write.
 * Integer only, every edited value is clamped to its row's min..max inside 0..VAL_MAX.
 * Written in the common C / C++ subset: compiles as C (the Felucca unity build) and as C++. */
#pragma once
#include "PresetStructures.h"
#include "dsp/maq_seq.h"
#include "dsp/arp_seq.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ input --- */
enum {                               /* 2: the 10 section buttons */
    BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO, BTN_HOME, BTN_ARP, BTN_SAVE, BTN_SEQ, BTN_COUNT
};
enum {                               /* what an ISR posts */
    UIEV_BUTTON,                     /* arg: BTN_* (press edge only) */
    UIEV_SELECT_ENC,                 /* arg: signed detents, + = clockwise = down the list */
    UIEV_ALGO_ENC,                   /* arg: signed detents, + = clockwise = value up */
    UIEV_TRANSPORT,                  /* arg: UITR_* (press edge only) */
    UIEV_PRESET_ENC,                 /* arg: signed detents, + = clockwise = next stored preset (ui_sm_preset_target) */
    UIEV_COUNT
};
enum {                               /* the FM1's transport buttons, outside spec 2's 10 section buttons */
    UITR_PLAY,                       /* from any page: SEQ starts / stops the MAQ, ARP the ARP, any other page both */
    UITR_REC,                        /* 7.2: SEQ page, enter / leave single-step record; ARP page, arm / disarm record */
    UITR_COUNT
};
/* ui_sm_seq_key mods: the FM1's octave buttons held with a selector key (7.1 loop boundaries) */
#define UISEQ_MOD_OCT_DOWN (1u << 0)
#define UISEQ_MOD_OCT_UP (1u << 1)
#define UISEQ_SELECTOR_KEYS 16u      /* 7.1: the bottom 16 white keys: the active lane's steps 1..16 (2026-10-21) */
#define UISEQ_NGROUP (SEQ_NSTEP / 4u)
#define UISEQ_STEP0_MAX (SEQ_NSTEP - 4u)   /* the knob window's last start: steps 13..16 */
#define UI_QUEUE_LEN 16u             /* power of 2; one slot stays empty, so 15 events in flight */

/* ------------------------------------------------------------ pages --- */
enum {
    UIPAGE_OSC1, UIPAGE_OSC2, UIPAGE_ATTACK, UIPAGE_OSCMIX,       /* FX (ATTACK: STRIKE only, 2026-10-16) */
    UIPAGE_FILTER1, UIPAGE_FILTER2, UIPAGE_FLTMIX,                /* SEL */
    UIPAGE_ENV1, UIPAGE_ENV2, UIPAGE_ENV3,                        /* ENV */
    UIPAGE_LFO1, UIPAGE_LFO2, UIPAGE_LFO3,                        /* LFO */
    UIPAGE_MOD1, UIPAGE_MOD2, UIPAGE_MOD3,                        /* EDIT: NMODSLOT pages (4.5) */
    UIPAGE_AMP, UIPAGE_EFFECTS,                                   /* GLO */
    UIPAGE_HOME1, UIPAGE_HOME2, UIPAGE_HOME3,                     /* HOME: LAYER (5.3 View 1 / 2 by layer focus), ZONES
                                                                   * (View 3), */
    UIPAGE_PERF, UIPAGE_GLOBAL,                                   /* .. the performance browser (6.1), GLOBAL
                                                                   * (2026-10-17 order) */
    UIPAGE_ARP,                                                   /* ARP: 8.2 */
    UIPAGE_SAVE_COMMIT,                                           /* SAVE: 6.2 */
    UIPAGE_SEQ,                                                   /* SEQ: 7.4 footer of the active lane */
    UIPAGE_COUNT
};
#define UI_HOME_NVIEW_MULTI 3u       /* 5.3: DUAL / SPLIT have Views 1..3; View 1 / 2 is shown by layer focus */
#define UI_HOME_NVIEW_SINGLE 1u      /* 5.3: SINGL has View 1 only (HOME then cycles View 1 -> GLOBAL) */
#define UI_FOCUS_NONE 0xFFu          /* current_param_focus when the page has nothing highlightable */

enum {
    UIROW_ACTIVE,
    UIROW_GREYED,                    /* drawn dimmed, never highlighted: the Select Encoder skips it */
    UIROW_INFO,                      /* drawn normally but read-only (MAX VOICES): the Select Encoder skips it too */
    UIROW_HIDDEN                     /* not drawn and skipped: SEQ TARGET with one option or none (7.4) */
};

/* how a knob's stored byte reads (2.2): its display and, for a list, how 0..127 picks an entry */
enum {
    UIKNOB_NONE,                     /* no parameter on this knob for this page / architecture: draw it greyed */
    UIKNOB_RAW,                      /* 0..127 */
    UIKNOB_BIP,                      /* stored 0..127, shown -64..+63 */
    UIKNOB_OCT,                      /* stored 0..127, shown -3..+3 */
    UIKNOB_SRC,                      /* a mod_source_list index (n_enum entries) */
    UIKNOB_FN,                       /* a function_list index (n_enum entries) */
    UIKNOB_PERF,                     /* a HOME performance controller sent as MIDI (velocity, bend, mod wheel): the byte is
                                      * UIStateMachine.perf_knob, which the knob handler writes as it sends the message */
    UIKNOB_SEQ,                      /* a SEQ step value: read ui_sm_seq_knob_get, write ui_sm_seq_knob_set / _turn */
    UIKNOB_LIST,                     /* an index into names (n_enum entries): SUB OCTAVE, XMOD TYPE */
    UIKNOB_BPM,                      /* stored 0..127, shown as MIDI_CLOCK_BPM_BASE + value BPM: GLOBAL's INT TEMPO */
    UIKNOB_NOTE,                     /* a MIDI note 0..127, shown by name: ZONES' SPLIT NOTE */
    UIKNOB_FINE,                     /* OSC FINE TUNE: shown in cents, osc_fine_cents */
    UIKNOB_PORTA                     /* LAYER's knob 4: the focused layer's PORTA TIME in the performance, 0 shown OFF
                                      * (no glide) */
};
typedef struct {
    const char *label;               /* "" for UIKNOB_NONE */
    uint8_t kind;                    /* UIKNOB_* */
    uint8_t n_enum;                  /* list length for SRC / FN / LIST, else 0 */
    const char *const *names;        /* the entries' names for SRC / LIST, else NULL */
} UiKnobInfo;
enum {
    UIDLG_NONE,
    UIDLG_CONFIRM,                   /* 6.2.2 step 5: ARE YOU SURE? / OVERWRITE? / PASTE?, Algorithm Encoder picks NO / YES */
    UIDLG_WRITING,                   /* step 6 YES: waiting for the caller's flash write */
    UIDLG_SAVED                      /* SAVED [NAME] TO SLOT [N]: any encoder or button clears it */
};
/* 6.2.1 SAVE submenu 2, knob 1: what the commit writes, and what COPY / PASTE move (spec order) */
enum { UISAVE_ARP_SEQ, UISAVE_SEQ_SETTINGS, UISAVE_PATCH_PRESET, UISAVE_WHOLE_PERF, UISAVE_COUNT };
/* 6.2.2 SAVE submenu 2, ACTION row: what a SAVE press runs. NO: the press goes back to submenu 1 */
enum { UIACT_NO, UIACT_SAVE, UIACT_COPY, UIACT_PASTE, UIACT_COUNT };
/* GLOBAL > THEME: the screen's palette, one for every page and both layers (ui_page_draw.c pu_page_pal) */
enum { UITHEME_SAMHAIN, UITHEME_BLOOD_MOON, UITHEME_FIRE, UITHEME_SOLSTICE, UITHEME_LICHEN, UITHEME_HEATHER, UITHEME_RITUAL,
       UITHEME_GRANITE, UITHEME_OAK, UITHEME_COUNT };
/* 6.2.3 a stored slot, as the caller's store reports it (ui_sm_attach_store) */
enum {
    UISLOT_UNKNOWN,                  /* no store attached: the save asks ARE YOU SURE? */
    UISLOT_EMPTY,                    /* never written */
    UISLOT_USED                      /* holds a record: the save asks OVERWRITE? */
};
#define UICLIP_EMPTY 0xFFu           /* clip_kind before the first COPY */
#define UI_BROWSE_MS 2000u           /* the preset list stays up this long after the Preset Encoder's last detent */
#define UI_KB_MS 2500u               /* SAVE: the letter keyboard stays up this long after the last name knob detent */
#define UI_HOLD_MS 200u              /* HOME / SAVE held this long: the hold strip shows what OCT- / OCT+ do */
#define UI_ROWPOP_MS 1500u           /* the parameter list stays up this long after the last Select / Algorithm detent */
/* The store's answer for one slot of a save target (UISAVE_*): UISLOT_EMPTY / UISLOT_USED. For a used slot it fills
 * name with the stored record's name (char_map indices; a sequence's or arp's since 2026-10-25). */
typedef uint8_t (*UiSlotQuery)(void *ctx, uint8_t target, uint8_t slot, uint8_t name[NAME_LEN]);
/* the slots a save target's bank has: PATCH_SLOTS patches, BANK_SLOTS of the rest (2026-10-17) */
static inline uint8_t ui_sm_bank_slots(uint8_t target)
{
    return (uint8_t)(target == UISAVE_PATCH_PRESET ? PATCH_SLOTS : BANK_SLOTS);
}
/* 6.2.3 the clipboard: one item of the kind COPY took (clip_kind) */
typedef union {
    EngineState patch;               /* PATCH_PRESET: the focused layer's patch */
    SequencerLane seq[SEQ_NLANE];    /* SEQ_SETTINGS: the live sequencer slot */
    ArpState arp;                    /* ARP_SEQ: the live arp pattern */
    WholePerformance whole;          /* WHOLE_PERF: the performance and everything it plays */
} UiClipboard;
PS_STATIC_ASSERT(sizeof(UiClipboard) == 911u, "the clipboard's RAM (spec 6.2.3)");   /* 911: two patches grew 9 bytes (STRIKE), then 2 (BEND MOD), then 2 (DRIVE), then 1 (ANALOG), then 2 (FILTER KEY TRACK), then 9 (VEL > LEVEL, DEPTH MOD) */
/* spec 1: the name characters; MasterPerformancePreset.name holds indices into it (0 = space) */
#define UI_CHAR_MAP " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_/"
/* 5.1: the play controllers. LAYER's knobs (2026-10-17): VELOCITY, MOD WHEEL, CC1, (2026-10-21) PORTA TIME, not CC2;
 * PITCH BEND has no knob now. CC2 keeps its slot (MIDI / the SEQ still set the CUSTOMCC2 source) */
enum { UIPERF_VELOCITY, UIPERF_BEND, UIPERF_MODWHEEL, UIPERF_CC1, UIPERF_CC2, UIPERF_COUNT };

/* ------------------------------------------------------------ state --- */
typedef struct {
    UIState s;                       /* the PresetStructures.h registers, kept in step with page */
    uint8_t page;                    /* UIPAGE_* */
    uint8_t perf_slot;               /* SAVE submenu 1: PERF PRESET POINTER, 0..BANK_SLOTS - 1 */
    uint8_t save_action;             /* SAVE submenu 2: ACTION row, UIACT_* */
    uint8_t dialog;                  /* UIDLG_* */
    uint8_t dialog_choice;           /* ARE YOU SURE?: 0 NO / 1 YES */
    uint8_t commit_pending;          /* 1: the caller must write flash, then call ui_sm_commit_done */
    uint8_t save_target;             /* SAVE submenu 2 knob 1: UISAVE_*, what the commit writes */
    uint8_t save_slot;               /* .. knob 2 / the Algorithm Encoder: the slot it writes to, below ui_sm_bank_slots */
    uint8_t save_from;               /* the page SAVE was pressed on: SAVED returns there */
    uint8_t name_cursor;             /* .. knob 3 (RENAME focused): the name character knob 4 edits, 0..NAME_LEN-1 */
    uint8_t clip_kind;               /* UISAVE_* COPY last took, UICLIP_EMPTY before the first */
    uint8_t clip_layer;              /* .. for a patch: the layer it came from */
    UiClipboard clip;
    uint8_t load_pending;            /* 1: the Preset Encoder picked a slot; the caller loads it, then ui_sm_load_done */
    uint8_t load_target;             /* .. what to load: UISAVE_* (ui_sm_preset_target when it turned) */
    uint8_t load_slot;               /* .. from which slot, below ui_sm_bank_slots */
    uint8_t load_layer;              /* .. into which layer (PATCH_PRESET: the layer in focus when it turned) */
    uint8_t browse;                  /* 1: the preset list is up (the Preset Encoder turned; any other control or
                                      * UI_BROWSE_MS without a detent puts it away) */
    uint16_t browse_ms;              /* .. time left, ui_sm_tick counts it down */
    uint8_t rowpop;                  /* 1: the parameter list is up, large (the Select Encoder moved the cursor; the
                                      * Algorithm Encoder keeps it up; any other control or UI_ROWPOP_MS puts it away) */
    uint16_t rowpop_ms;              /* .. time left, ui_sm_tick counts it down */
    uint16_t kb_ms;                  /* SAVE: the letter keyboard's time left (knobs 3 / 4 edit the name), ui_sm_tick */
    InstrumentSettings *inst;        /* GLOBAL's instrument settings (ui_sm_attach_instrument; NULL: a private copy) */
    uint8_t hold;                    /* the button held as a modifier, BTN_HOME / BTN_SAVE, for the hold strip (the
                                      * caller sets it UI_HOLD_MS after the press, ui_sm_set_hold); BTN_COUNT: none */
    uint8_t theme;                   /* GLOBAL > THEME, UITHEME_*: a setting of the instrument, not of a performance
                                      * (no preset load changes it) */
    UiSlotQuery slot_query;          /* ui_sm_attach_store; NULL: slots read UISLOT_UNKNOWN */
    void *slot_ctx;
    uint8_t perf_knob[UIPERF_COUNT]; /* 5.1 HOME knobs 1..3 as last sent (velocity, bend 64 = centre, mod wheel) */
    /* the data the menu rows edit; owned by the caller, must stay valid while the machine runs */
    MasterPerformancePreset *perf;
    EngineState *engine[NLAYER];
    SequencerLane *lanes;            /* SEQ_NLANE of them */
    ArpState *arp;
    uint8_t seq_name[NAME_LEN];      /* the live sequence's and arp pattern's names (char_map; 2026-10-25): RENAME edits
                                      * them, the caller's store saves them with the slot and sets them on a load */
    uint8_t arp_name[NAME_LEN];
    uint8_t clip_name[NAME_LEN];     /* .. COPY's of the one it took (a sequence or arp on the clipboard) */
    MaqSeq *seq;                     /* the sequencer engine (ui_sm_attach_seq); NULL: no transport, no step record */
    uint8_t seq_step0;               /* 7.1: the active lane's step on knob 1, 0..UISEQ_STEP0_MAX; knobs 2..4 the next
                                      * three; always 0, 4, 8 or 12 (the Select Encoder moves it a group of 4 at a time,
                                      * a step key to its group, 2026-10-26) */
    uint8_t seq_cap_held;            /* 7.2 chord capture: keys still down since the chord began */
    uint8_t seq_cap_n;               /* .. and the notes written into it so far */
    CarpSeq *carp;                   /* the ARP engine (ui_sm_attach_arp); NULL: chord keys capture but sound nothing */
    uint8_t arp_knob[4];             /* 2.2 ARP knobs, live: RATE ARPDIV_*, OCTAVE 0..3, STYLE ARPSTYLE_*, RHYTHM 0..23 */
    uint8_t arp_held;                /* 8.1: step keys held, bit n = step n */
    uint8_t arp_peek;                /* a held step key's values are on the knobs (2026-10-25): arp_live holds the
                                      * live dials to restore when the last step key is let go */
    uint8_t arp_live[4];
    uint8_t arp_chord_key;           /* the chord key sounding or latched (its note), CARP_NOTE_NONE */
    uint8_t arp_chord_down;          /* the chord key physically down (its note), CARP_NOTE_NONE */
    uint8_t arp_chord_hold;          /* arp_chord_key is a live hold (latchable), not a capture's block chord */
    uint8_t arp_latch;               /* LATCH on: a chord key's hold outlives the key */
    uint8_t arp_fn;                  /* function keys held, bit ARPFN_* */
    uint8_t arp_copy_src;            /* COPY: the source step tapped, CARP_POS_NONE before it */
    uint8_t arp_rec;                 /* REC armed on the ARP page */
    uint8_t arp_rec_cursor;          /* stopped record: the step the next chord goes into */
    uint8_t arp_rec_last;            /* the step written last while recording (ACCENT alone toggles it), CARP_POS_NONE */
    /* ISR -> main loop queue: the ISR writes q_head, the main loop writes q_tail */
    volatile uint8_t q_type[UI_QUEUE_LEN];
    volatile int8_t q_arg[UI_QUEUE_LEN];
    volatile uint8_t q_head;
    volatile uint8_t q_tail;
    volatile uint8_t q_dropped;      /* events lost to a full queue, saturates at 255 */
} UIStateMachine;

/* boots onto HOME View 1 with nothing pending; the data pointers must be non-NULL */
void ui_sm_init(UIStateMachine *sm, MasterPerformancePreset *perf, EngineState *layer1,
                EngineState *layer2, SequencerLane *lanes, ArpState *arp);

/* ISR side: queue one event; 0 when the queue is full (the event is dropped and counted) */
uint8_t ui_sm_post(UIStateMachine *sm, uint8_t type, int8_t arg);

/* main loop side: apply every queued event in order */
void ui_sm_process(UIStateMachine *sm);

/* apply one event directly (main-loop context only; also what ui_sm_process calls) */
void ui_sm_handle(UIStateMachine *sm, uint8_t type, int8_t arg);
/* the PLAY light: what PLAY here would stop is playing (SEQ page the MAQ, ARP page the ARP, any other page either) */
uint8_t ui_sm_play_lit(const UIStateMachine *sm);

/* re-apply the mode rules after the caller changes perf or a patch from outside (a preset load, MIDI): SINGL forces
 * layer focus to LAYER1 and HOME back to View 1, a page the architecture hides falls back to its section's first
 * page, the cursor leaves greyed and hidden rows, and a TRANSPOSE / LAYER lane whose TARGET has only one option left
 * is pointed at it (the row is hidden, so it could not be fixed by hand) */
void ui_sm_enforce(UIStateMachine *sm);

/* 5.2 Preset Encoder: focus LAYER1 / LAYER2; 0 when refused (LAYER2 under SINGL_SYNTH). A page the new layer's
 * architecture hides falls back to its section's first page. */
uint8_t ui_sm_set_layer_focus(UIStateMachine *sm, uint8_t layer);

/* The Preset Encoder (UIEV_PRESET_ENC) loads a stored preset; what it loads follows the page: SEQ a sequencer slot
 * (SEQ_SETTINGS), HOME View 1..3 a performance (WHOLE_PERF), ARP an arp pattern (ARP_SEQ), every other page the focused
 * layer's patch (PATCH_PRESET). Each detent steps from the slot playing now (layer_patch, seq_link, arp_link, or
 * perf_slot for a performance) to the next one the store reports used; with no store attached every slot counts.
 * It stops at slot 0 and 127 and does nothing while a dialog is up. A move sets load_pending: the caller loads
 * load_target from load_slot (PATCH_PRESET into load_layer; WHOLE_PERF: the performance, then the patches, SEQ and ARP
 * it links), then calls ui_sm_load_done. ok 1 points the link (or perf_slot) at the slot and re-applies the mode rules
 * (ui_sm_enforce). */
uint8_t ui_sm_preset_target(const UIStateMachine *sm);             /* UISAVE_*: what the Preset Encoder loads here */
uint8_t ui_sm_preset_slot(const UIStateMachine *sm);               /* the slot of that kind playing now */
void ui_sm_load_done(UIStateMachine *sm, uint8_t ok);
/* The PERF page's links (2026-10-27): knob or row k = 0 L1 PATCH, 1 L2 PATCH, 2 SEQ LINK, 3 ARP LINK. A turn walks to
 * the next used slot of that kind, as the Preset Encoder does, and sets load_pending (the patch into layer k for
 * 0 / 1); the link points at the slot once ui_sm_load_done says it loaded. L2 PATCH does nothing under SINGL */
void ui_sm_perf_link_turn(UIStateMachine *sm, uint8_t link, int16_t detents);
/* The preset list (the screen's popup): each Preset Encoder detent opens it, or keeps it up, on the slot it lands on
 * (ui_sm_browse_slot: the one loading, else the one playing). Any other button or encoder puts it away and still does
 * its own job; PLAY and the note keys leave it up. ui_sm_tick, once per UI frame with the ms since the last, closes it
 * UI_BROWSE_MS after the last detent. The parameter popup (rowpop, 2026-10-17): only the Algorithm Encoder opens it,
 * on the row under the cursor while its value turns; the Select Encoder moves the cursor in the page's own list and
 * puts it away; ui_sm_tick closes it UI_ROWPOP_MS after the last value detent. */
void ui_sm_tick(UIStateMachine *sm, uint16_t ms);
/* 1 while a SAVE press would act, so its button flashes: the save page with an ACTION armed (SAVE when the page opens:
 * the button is the shortcut to it), or the confirm popup waiting for SAVE */
uint8_t ui_sm_save_armed(const UIStateMachine *sm);
/* a MOD page row (a destination, 2026-10-17): fed by this slot, by another source, or by nothing. Turning the amount
 * of one not fed by this slot routes it here (from 0); its value reads 0 and its text the source it has now */
enum { UIMOD_NONE, UIMOD_HERE, UIMOD_OTHER };
uint8_t ui_sm_mod_route(const UIStateMachine *sm, uint8_t row);
/* A held button's second job (2026-10-17): while HOME or SAVE is held, OCT- / OCT+ do something else and a strip at the
 * bottom of the screen names it, so no hold is hidden. The caller sets hold UI_HOLD_MS after the press and clears it
 * (BTN_COUNT) on the release; a button used that way does not act on its release. HOME: OCT- / OCT+ focus layer 1 / 2
 * (ui_sm_set_layer_focus). SAVE: ui_sm_save_oct: OCT- copies what SAVE would save here into the clipboard, OCT+ pastes
 * the clipboard over it (the save page opens on it and asks first). */
void ui_sm_set_hold(UIStateMachine *sm, uint8_t btn);
/* the instrument settings GLOBAL's MASTER TUNE / VEL CURVE / CLOCK OUT rows and knobs edit (dsp/preset.c instrument) */
void ui_sm_attach_instrument(UIStateMachine *sm, InstrumentSettings *inst);
void ui_sm_save_oct(UIStateMachine *sm, uint8_t up);
/* what SAVE saves from this page: the save page's TARGET, else the kind the page loads (ui_sm_preset_target) */
uint8_t ui_sm_save_kind(const UIStateMachine *sm);
uint8_t ui_sm_browse_slot(const UIStateMachine *sm);

/* commit_pending 1: the caller writes save_target to slot save_slot (PATCH_PRESET: the focused layer's patch,
 * s.active_synth_layer), then calls this. ok 1 shows SAVED, ok 0 drops back to SAVE submenu 2 */
void ui_sm_commit_done(UIStateMachine *sm, uint8_t ok);

/* 6.2.3: the store the overwrite warning asks (NULL: no warning, slots read UISLOT_UNKNOWN) */
void ui_sm_attach_store(UIStateMachine *sm, UiSlotQuery query, void *ctx);
/* the slot save_target / save_slot point at: UISLOT_*, and for a used, named one its name as text into name (at least
 * NAME_LEN + 1 bytes, trailing spaces dropped; "" when it has none) */
uint8_t ui_sm_slot_state(const UIStateMachine *sm, char *name);
/* the same for any slot of any target (UISAVE_*): what the preset list shows */
uint8_t ui_sm_slot_query(const UIStateMachine *sm, uint8_t target, uint8_t slot, char *name);
/* 1 when PASTE may run: the clipboard holds an item of save_target's kind */
uint8_t ui_sm_paste_ok(const UIStateMachine *sm);
/* the name RENAME edits for save_target: the focused layer's patch name (PATCH_PRESET), the performance name
 * (WHOLE_PERF), the live sequence's or arp pattern's (SEQ_SETTINGS, ARP_SEQ: sm->seq_name, sm->arp_name) */
const uint8_t *ui_sm_rename_name(const UIStateMachine *sm);
/* char_map indices as text into buf (at least NAME_LEN + 1 bytes), trailing spaces dropped; "" when all spaces */
const char *ui_sm_name_text(const uint8_t name[NAME_LEN], char *buf);

/* ------------------------------------------------------------- SEQ --- */
/* hand the machine the sequencer engine that plays sm->lanes (the transport and step record go through it) */
void ui_sm_attach_seq(UIStateMachine *sm, MaqSeq *seq);

/* A note key, on the SEQ page. key: 0 = the lowest key; note: its MIDI note; down: press 1 / release 0; mods:
 * UISEQ_MOD_*. Live (7.1, 2026-10-21): the bottom UISEQ_SELECTOR_KEYS white keys, counted from the lowest key, are the
 * active lane's steps 1..16: white key n puts its block of four on the knobs (1..4, 5..8, 9..12, 13..16) and the Select
 * Encoder cursor on STEPS (the lane itself is the SEQ button's: each press on the page, the next lane). With OCT- /
 * OCT+ held, white key n (0..15) sets the active lane's loop start / end to step n instead. The black keys among them
 * do nothing. Returns 1 for every key of that span (the caller must not play it); keys above it play. Step record
 * (7.2): the key plays as normal (returns 0) and its note is written into every NOTE lane at the record step; keys
 * struck while others are still held build up a chord of up to 4 notes. Every other page: returns 0. */
uint8_t ui_sm_seq_key(UIStateMachine *sm, uint8_t key, uint8_t note, uint8_t down, uint8_t mods);


/* SEQ knob k (0..3): live, step seq_step0 + k of the active lane; step record, lane k at the record step. The value
 * is the step's data byte, or a NOTE lane's first pitch (127 = TIE). Setting a NOTE lane's pitch moves the step's
 * other notes by the same amount (7.2: a chord is transposed). _turn: relative, as the FM1's knob encoders give it. */
uint8_t ui_sm_seq_knob_get(const UIStateMachine *sm, uint8_t knob);
void ui_sm_seq_knob_set(UIStateMachine *sm, uint8_t knob, uint8_t value);
void ui_sm_seq_knob_turn(UIStateMachine *sm, uint8_t knob, int8_t detents);
/* which lane and step knob k edits now */
void ui_sm_seq_knob_target(const UIStateMachine *sm, uint8_t knob, uint8_t *lane, uint8_t *step);
uint8_t ui_sm_seq_recording(const UIStateMachine *sm);   /* 7.2 single-step record is on */

/* 7.4 footer strings: EV: ("Note" .. "Layr") and MD: ("FOr" .. "rnd") */
const char *ui_sm_seq_ev_name(uint8_t event_type);
const char *ui_sm_seq_mode_name(uint8_t mode);
/* TARGET: (7.4, 7.5). A layer target reads as text, "[L1]" / "[L2]" / "[BOTH]". A lane target (the modifier lanes'
 * lane bitmask, TRANSPOSE's lane, LAYER's row) is four slots, one per lane: ui_sm_seq_target_slots returns 1 and sets
 * *on (bit n: the target includes lane n) and *ok (bit n: lane n may be picked; TRANSPOSE offers the NOTE lanes, LAYER
 * the lanes that play on a layer, the bitmask every lane), 0 for a layer target. ui_sm_seq_target_text gives either as
 * text into buf (at least 13 bytes): "[L1]", or "[X][ ][-][ ]" (X picked, - not available). ui_sm_seq_target_count:
 * how many values TARGET offers now; at 1 or 0 the row is UIROW_HIDDEN and the footer leaves the field empty (one
 * option is set by ui_sm_enforce). */
uint8_t ui_sm_seq_target_slots(const UIStateMachine *sm, uint8_t lane, uint8_t *on, uint8_t *ok);
const char *ui_sm_seq_target_text(const UIStateMachine *sm, uint8_t lane, char *buf);
uint8_t ui_sm_seq_target_count(const UIStateMachine *sm, uint8_t lane);
/* SCALE (2026-10-16): the note a NOTE lane's stored note plays (held to the lane's scale; as it is without one), and
 * the footer's SCALE and ROOT values as text */
uint8_t ui_sm_seq_note_shown(const UIStateMachine *sm, uint8_t lane, uint8_t note);
const char *ui_sm_seq_scale_name(uint8_t scale);
const char *ui_sm_seq_root_name(uint8_t root);
/* a MIDI note as text, C-1 .. G9 ("C#3"), or "TIE" for SEQ_NOTE_TIE; buf at least 5 bytes */
const char *ui_sm_note_name(uint8_t note, char *buf);
const char *ui_sm_fine_text(uint8_t v, char *buf);   /* OSC FINE TUNE in cents: "+125C" (at least 6 bytes) */

/* ------------------------------------------------------------- ARP --- */
/* 8.1 function keys: the black keys among the step keys, by the step key below them (G#4 LATCH .. F#5 ACCENT) */
enum { ARPFN_LATCH, ARPFN_TIE, ARPFN_CLEAR, ARPFN_COPY, ARPFN_ACCENT, ARPFN_COUNT, ARPFN_NONE = 0xFF };

/* hand the machine the ARP engine (chord keys sound through it, and set its transposition) */
void ui_sm_attach_arp(UIStateMachine *sm, CarpSeq *carp);

/* A note key, on the ARP page (8.1). key: 0 = the lowest key; note: its MIDI note; down: press 1 / release 0; vel: its
 * velocity. Keys 0..12 (CARP_ZONE_KEYS, chromatic) are chord keys: a press sounds the key's chord in the GENRE BANK
 * while held; with step keys held it is captured into each of them instead (the key's note plus the four ARP knobs);
 * while the ARP plays, a press without step keys also transposes the pattern by the key's distance from key 0
 * (and the MAQ with TRANSPOSE MAQ on, which also moves the MAQ with each step's chord root; with it on, the key
 * transposes the MAQ while the MAQ alone plays too). With TRANSPOSE MAQ on and no chord in the pattern, the MAQ follows
 * the chord keys stopped as well, and PLAY keeps the key last played (with a pattern, the ARP starts untransposed). The next 8 white keys are step keys 1..8 (held: bit n of arp_held).
 * The black keys among them are function keys (ARPFN_*): LATCH toggles the latch (a chord key's live arpeggio plays on
 * after the key, until another chord key or LATCH off); TIE, CLEAR and ACCENT act on the step keys held, or on the step
 * keys tapped while they are held (TIE: the chord before carries on; CLEAR: a rest; ACCENT: toggled); COPY held, the
 * first step key tapped is the source and every later one a copy of it. With REC armed (UITR_REC) a chord key records
 * instead of playing live: transport playing, into carp_rec_step's step (the nearest), and the ARP knobs write into the
 * playing step; stopped, into steps 1, 2, 3 .. in turn, SEQ LENGTH following. TIE or CLEAR alone records a tie or a
 * rest the same way, ACCENT alone toggles the step recorded last. Returns 1 for every key of that span (the caller must
 * not play it); keys above play. Every other page: returns 0. */
uint8_t ui_sm_arp_key(UIStateMachine *sm, uint8_t key, uint8_t note, uint8_t down, uint8_t vel);

/* the MIDI channel (0..15) a played key goes out on: in DUAL the selected layer's, so the keys play the layer being
 * edited (both when the two share a channel); SINGL and SPLIT layer 1's (SPLIT splits it by key and velocity). */
uint8_t ui_sm_key_channel(const UIStateMachine *sm);

/* the caller wrote the byte ui_sm_knob returned for knob k of page: on ARP the value also goes into every held step
 * (8.1: a step key held while a knob turns edits that step at once); on SAVE submenu 2 a new TARGET re-applies the
 * row rules (RENAME greys for ARP_SEQ / SEQ_SETTINGS, PASTE falls back to NO). Other pages: nothing to do. */
void ui_sm_knob_written(UIStateMachine *sm, uint8_t page, uint8_t knob);

/* ------------------------------------------------------- key LEDs --- */
/* The keyboard's LEDs where a page gives the keys a job (SEQ, ARP), bit k = key k (0 = the lowest key, whose MIDI note
 * is lowest_note: F3 on the FM1). Three levels; a key is in one mask at most: lit (bright), mid, dim (a glow). The FM1
 * maps them onto its own levels (lit, fm1_led_mid, the dim glow), so MENU > LEDS keeps working. mods: UISEQ_MOD_* held
 * now. Keys held are not in it: the caller lights those as it always does.
 *   SEQ live      the 16 white keys are the active lane's steps (2026-10-21): the playhead lit while playing, the
 *                 four steps on the knobs mid, the other steps that play (in the loop window, not muted or skipped)
 *                 dim.
 *   SEQ, OCT held the 16 white keys are the active lane's steps: its loop window dim, the boundary the next key
 *                 sets (OCT-: start, OCT+: end) lit while stopped, mid under the lit playhead while playing.
 *   SEQ record    the notes of the record step lit (the chord being built; 7.2, the keyboard plays as normal).
 *   ARP           steps 1..SEQ LENGTH dim (a rest dark), the playing, held and COPY source steps lit, stopped with
 *                 REC armed the step the next chord goes into mid; function keys dim, LATCH lit while on and TIE / CLR
 *                 / CPY / ACC while held; playing, the chord key of the transposition mid.
 * Returns 1 when the page drives the keys this way (the caller shows these and not its own key map), 0 on every other
 * page or under a dialog (all masks 0). */
typedef struct { uint32_t lit, mid, dim; } UiKeyLeds;
uint8_t ui_sm_key_leds(const UIStateMachine *sm, uint8_t lowest_note, uint8_t mods, UiKeyLeds *out);

/* -------------------------------------------------- voice architecture --- */
/* Whether a page shows for a layer's architecture (and, for HOME View 2 / 3, the performance mode). Hidden pages
 * are skipped by their section button; landing on one (an architecture or focus change) moves to the section's
 * first page. OSC2: DUAL OSC / STACK. FILTER2: DUAL FILTER / STACK. FILTER MIXER: DUAL FILTER / STACK / BINAURAL. */
uint8_t ui_sm_page_visible(const UIStateMachine *sm, uint8_t page, uint8_t layer);
/* the voices a layer may sound now (PresetStructures.h layer_voice_limit): the HOME page's MAX VOICES */
uint8_t ui_sm_layer_voices(const UIStateMachine *sm, uint8_t layer);
const char *ui_sm_arch_name(uint8_t arch);                         /* "SINGLE" .. "BINAURAL" */

/* 2.2 knob map: what knob 0..3 of `page` controls for `layer`'s architecture. Fills *info and returns the stored
 * byte it edits, or NULL (UIKNOB_NONE, UIKNOB_SEQ, a hidden page). The same byte may sit on different knobs in
 * different architectures (OSCMIXER knob 4: NOISE, or SPREAD in SPREAD). HOME knobs 1..3 (UIKNOB_PERF) return
 * perf_knob[k]: the caller sends the MIDI message and stores the value there. SAVE submenu 1 (6.1): the layer 1 / 2
 * patch, SEQ and ARP links (layer 2 greyed under SINGL). SAVE submenu 2 (6.2.1): TARGET, SLOT, and while RENAME is
 * focused the character position and its character (name[name_cursor] of ui_sm_rename_name, a list knob over
 * UI_CHAR_MAP); none while a dialog is up (the popup owns the controls). GLOBAL: knob 4 is INT TEMPO while CLOCK
 * SOURCE is INTERNAL (greyed on MIDI clock); knobs 1..3 have nothing. */
uint8_t *ui_sm_knob(UIStateMachine *sm, uint8_t page, uint8_t layer, uint8_t knob, UiKnobInfo *info);
/* the knob's value as its list names it (UIKNOB_LIST, UIKNOB_SRC), NULL for a number the caller draws itself */
const char *ui_sm_knob_text(UIStateMachine *sm, uint8_t page, uint8_t layer, uint8_t knob);

/* -------------------------------------------------------- rendering --- */
/* Rows are counted and indexed among the rows the focused layer's architecture shows (hidden rows do not exist
 * for these calls, so row 0 is the first shown row). */
uint8_t ui_sm_row_count(const UIStateMachine *sm);                 /* rows on the current page */
const char *ui_sm_row_label(const UIStateMachine *sm, uint8_t row);
uint8_t ui_sm_row_state(const UIStateMachine *sm, uint8_t row);   /* UIROW_*: only ACTIVE rows take the cursor, HIDDEN are not drawn */
uint8_t ui_sm_row_value(const UIStateMachine *sm, uint8_t row);   /* stored 0..127, 0 for RENAME */
uint8_t ui_sm_row_place(const UIStateMachine *sm, uint8_t row);   /* the value's place in the row's range, 0..127 */
/* the value as text (an enum name, a source name, ON / OFF, or a bipolar -64..+63), NULL for a plain number */
const char *ui_sm_row_text(const UIStateMachine *sm, uint8_t row);
/* a PERF page slot row (PERF PRESET, the links): the name of the slot it points at, "EMPTY", or NULL (another row, an
 * unnamed or unknown slot) */
const char *ui_sm_row_slot_name(const UIStateMachine *sm, uint8_t row);
/* jump to a page directly, as a section button would (the cursor starts on its first highlightable row) */
void ui_sm_goto(UIStateMachine *sm, uint8_t page);
uint8_t ui_sm_home_view_count(uint8_t perf_mode);                  /* 5.3: 1 or 3 */
/* 6.2.2 step 1: the floating keyboard draws only while the cursor is on RENAME (and it has a name to edit) */
uint8_t ui_sm_keyboard_visible(const UIStateMachine *sm);

#ifdef __cplusplus
}
#endif
