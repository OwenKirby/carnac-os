/* Mvave FM1 menu navigation state machine (see UIStateMachine.h).
 * Every page is a row table: a row names one stored byte (an offset into the page's data block) and its
 * min..max, so the Algorithm Encoder edits through one clamped path and the Select Encoder walks the same
 * table, skipping the rows ui_sm_row_state greys out. A sound design row also lists the voice architectures
 * that show it (4.0): rows the focused layer's architecture does not use are hidden, not greyed, so every call
 * below counts and indexes shown rows only. Integer only: no float / double anywhere. */
#include "UIStateMachine.h"
#include "dsp/attacks.h"            /* ATK_COUNT and attack_names: STRIKE's ATTACK knob */
#include "dsp/wavetables.h"          /* NUM_BANKS */
#include "dsp/pcm_osc.h"             /* the WAVE row: the wave list (banks, PCM waves, attacks) */
#include "dsp/arp_chords.h"          /* ARP_GENRE_COUNT and arp_genre_names: the GENRE BANK row */
#include "dsp/seq_scale.h"           /* a NOTE lane's SCALE: the knob and the keyboard held to it */
#include "dsp/midi_clock.h"          /* MIDI_CLOCK_BPM_BASE: the INT TEMPO row in BPM */
#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define UI_BARRIER() __asm__ volatile("" ::: "memory")   /* the spelling X0X's register check allows */
#else
#define UI_BARRIER() ((void)0)       /* the queue fields are volatile, so their order holds without it */
#endif

PS_STATIC_ASSERT((UI_QUEUE_LEN & (UI_QUEUE_LEN - 1u)) == 0u && UI_QUEUE_LEN <= 128u, "UI_QUEUE_LEN power of 2");
PS_STATIC_ASSERT(UIPAGE_MOD3 - UIPAGE_MOD1 + 1 == NMODSLOT, "one EDIT page per mod slot");
PS_STATIC_ASSERT(sizeof(EngineState) <= 256u && sizeof(MasterPerformancePreset) <= 256u, "uint8_t row offsets");
PS_STATIC_ASSERT(VARCH_COUNT <= 16u, "UiRow.archs is a uint16_t mask");

/* ------------------------------------------------------- row tables --- */
enum {                               /* how a row reads and writes its value */
    RK_BYTE,                         /* the byte at offset in the page's data block */
    RK_ENG,                          /* the byte at offset in a layer's EngineState (RF_LAYER2: layer 2's), HOME */
    RK_VOICES,                       /* read-only: the layer's voice limit (RF_LAYER2: layer 2's) */
    RK_CHAIN,                        /* read-only: a MOD page destination that is another slot's source (no amount) */
    RK_SEQ_STEPS,                    /* SEQ: the knob window, seq_step0; no value of its own (the Select Encoder moves
                                      * it, the Algorithm Encoder does nothing) */
    RK_SEQ_EVENT,                    /* EV:, written through seq_lane_set_event */
    RK_SEQ_TARGET,                   /* TARGET:, max from seq_target_max, values from seq_target_ok */
    RK_ARP_LAYER,                    /* 8.2 Layer: L2 needs a second layer, BOTH needs DUAL_SYNTH (layer_timing's low nibble) */
    RK_ARP_TIMING,                   /* TIMING: every step's length, ARPDIV_* (layer_timing's high nibble) */
    RK_PERF_SLOT,                    /* SAVE submenu 1 performance pointer */
    RK_RENAME,                       /* RENAME: no value, the knobs edit the name (6.2.1) */
    RK_SAVE_ACTION,                  /* ACTION: UIACT_* (6.2.2) */
    RK_THEME,                        /* GLOBAL > THEME: UIStateMachine.theme */
    RK_INST,                         /* the byte at offset in the instrument settings (GLOBAL, sm->inst) */
    RK_FINE,                         /* RK_BYTE shown in cents (OSC FINE TUNE, osc_fine_cents) */
    RK_PINGPONG,                     /* PING PONG: bit 0 of the byte at offset (delay_pingpong, DELAY_PINGPONG) */
    RK_DELAY_HP,                     /* DELAY HP: bits 1..7 of the byte at offset (delay_pingpong, DELAY_HP) */
    RK_WAVE,                         /* RK_BYTE holding a wave code (pcm_osc.h): steps through the wave list (banks,
                                      * PCM waves, attacks), shows the wave's name */
    RK_DEPTH_SRC                     /* RK_BYTE holding an LFO's DEPTH MOD SRC: steps over the LFOs and MOD slots */
};
#define RF_LAYER2 (1u << 0)          /* a Layer 2 parameter: greyed under SINGL_SYNTH */
#define RF_BIPOLAR (1u << 2)         /* stored 0..127 with 64 = 0: shown as -64..+63 */
#define RF_UNISON (1u << 3)          /* greyed unless the row's layer plays VOICE MODE UNISON */
/* how a plain number reads (ui_sm_row_text): a note name, a MIDI channel 1..16, BPM, a 3-digit slot */
#define RF_NOTE (1u << 4)
#define RF_CHANNEL (1u << 5)
#define RF_BPM (1u << 6)
#define RF_SLOT (1u << 7)

enum { ST_NONE, ST_ENGINE, ST_PERF, ST_ARP, ST_LANE };          /* the data block a page's rows index */

typedef struct {
    const char *label;
    uint8_t offset;                  /* RK_BYTE / RK_ENG / RK_SEQ_EVENT / RK_SEQ_TARGET: byte offset in the block */
    uint8_t min, max;                /* inclusive, within 0..VAL_MAX */
    uint8_t kind;                    /* RK_* */
    uint8_t flags;                   /* RF_* */
    const char *const *names;        /* value text for list values: names[value], NULL for a plain number */
    uint16_t archs;                  /* VARCH_BIT mask of the architectures that show the row (VARCH_ALL: always) */
    uint8_t via;                     /* MOD pages: 1 + the EngineState offset of the source selector that must name
                                      * the page's slot for the row to show; 0: every other row */
} UiRow;

#define OFS(type, member) ((uint8_t)offsetof(type, member))
#define SRC_MAX ((uint8_t)(MSRC_COUNT - 1))
#define A_ALL ((uint16_t)VARCH_ALL)
#define A(x) ((uint16_t)VARCH_BIT(VARCH_##x))

/* value names for the row tables below, in enum order (the value is the index) */
/* at most 5 letters: a source row's label and its value fit the menu at 2x (src/host/test_ui_rows_fit.c) */
static const char *const MSRC_NAMES[] = {
    "none", "velo", "keyb", "modwh", "aftch", "env1", "env2", "env3",
    "lfo1", "lfo2", "lfo3", "mod1", "mod2", "mod3", "cc1", "cc2",
};
static const char *const FLT_NAMES[] = {"SH-2", "MULTI", "MS-20", "FORMANT"};
static const char *const XMOD_NAMES[] = {"FM", "AM", "RING", "PM"};
static const char *const SUBOCT_NAMES[] = {"-1 OCT", "-2 OCT"};
static const char *const SUBWAVE_NAMES[] = {"SINE", "SQUARE", "CURRENT"};
static const char *const DRIVE_NAMES[] = {"SOFT", "HARD", "FOLD", "TAPE", "TUBE", "OVERDRIVE"};
PS_STATIC_ASSERT(sizeof(DRIVE_NAMES) / sizeof(DRIVE_NAMES[0]) == DRIVE_COUNT, "one name per DRIVE_*");
static const char *const REVTYPE_NAMES[] = {"SPRING", "TOMB", "MOD", "CELESTIAL", "DOLMEN", "TUMULUS"};
static const char *const PERF_NAMES[] = {"SINGL", "DUAL", "SPLIT"};
static const char *const VARCH_NAMES[] = {"SINGLE", "DUAL OSC", "SPREAD", "DUAL FILT", "STACK", "BINAURAL", "STRIKE", "FULL", "DUAL SPRD"};
static const char *const VMODE_NAMES[] = {"MONO RETRIG", "MONO LEGATO", "POLY REUSE", "POLY RESET", "UNISON"};
static const char *const PORTA_NAMES[] = {"CONST RATE", "CONST TIME", "LEGATO TIED"};
static const char *const CLKSRC_NAMES[] = {"INTERNAL", "MIDI"};
static const char *const VELCURVE_NAMES[] = {"SOFT", "LINEAR", "HARD"};
static const char *const CLKOUT_NAMES[] = {"OFF", "USB"};
PS_STATIC_ASSERT(sizeof(VELCURVE_NAMES) / sizeof(VELCURVE_NAMES[0]) == VELCURVE_COUNT, "one name per VELCURVE_*");
PS_STATIC_ASSERT(sizeof(CLKOUT_NAMES) / sizeof(CLKOUT_NAMES[0]) == CLKOUT_COUNT, "one name per CLKOUT_*");
static const char *const THEME_NAMES[UITHEME_COUNT] = {"SAMHAIN", "BLOOD MOON", "FIRE", "SOLSTICE", "LICHEN", "HEATHER",
                                                       "RITUAL", "GRANITE", "OAK"};
static const char *const DELAYTYPE_NAMES[] = {"TAPE", "BBD"};
static const char *const ARPLAYER_NAMES[] = {"L1", "L2", "BOTH"};
static const char *const ARPDIV_NAMES[] = {"2", "1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32",
                                           "1/32T"};
PS_STATIC_ASSERT(sizeof(ARPDIV_NAMES) / sizeof(ARPDIV_NAMES[0]) == ARPDIV_COUNT, "one name per ARPDIV_*");
static const char *const ARPRATE_NAMES[] = {"1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T",
                                            "1/64", "1/64T", "1/128"};
PS_STATIC_ASSERT(sizeof(ARPRATE_NAMES) / sizeof(ARPRATE_NAMES[0]) == ARPRATE_COUNT, "one name per ARPRATE_*");
static const char *const BOOL_NAMES[] = {"OFF", "ON"};
static const char *const ATKTRK_NAMES[] = {"AUTO", "FIXED", "TRACK"};
PS_STATIC_ASSERT(sizeof(ATKTRK_NAMES) / sizeof(ATKTRK_NAMES[0]) == ATKTRK_COUNT, "one name per ATKTRK_*");
/* 4.5 function_list, as the knob shows it */
static const char *const MFN_NAMES[] = {"a*b", "a+b", "a-b", "max", "min", "smpleBonA", "QuntzBtoA", "|a-b|", "lag", "diode"};
PS_STATIC_ASSERT(sizeof(MFN_NAMES) / sizeof(MFN_NAMES[0]) == MFN_COUNT, "one name per MFN_*");
/* 6.2.1 SAVE knobs: TARGET (UISAVE_*), the name position 1..16, and each UI_CHAR_MAP entry as a knob shows it */
static const char *const SAVE_TARGET_NAMES[] = {"ARP", "SEQ", "PATCH", "PERF"};
static const char *const NAME_POS_NAMES[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static const char *const CHAR_NAMES[] = {
    "SPACE", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U",
    "V", "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "-", "_", "/",
};
PS_STATIC_ASSERT(sizeof(SAVE_TARGET_NAMES) / sizeof(SAVE_TARGET_NAMES[0]) == UISAVE_COUNT, "one name per UISAVE_*");
PS_STATIC_ASSERT(sizeof(NAME_POS_NAMES) / sizeof(NAME_POS_NAMES[0]) == NAME_LEN, "one name per name position");
PS_STATIC_ASSERT(sizeof(CHAR_NAMES) / sizeof(CHAR_NAMES[0]) == CHAR_MAP_LEN && sizeof(UI_CHAR_MAP) == CHAR_MAP_LEN + 1u,
                 "one name per character");
/* 2.2 ARP knobs: RATE (ARPRATE_NAMES), OCTAVE RANGE, STYLE (arp_seq.h; HOLD first, 2026-10-21), RHYTHM (8.3 row and
 * column: A / B, T for the triplet B column; a HOLD step's strum, carp_hold_strum, under STYLE HOLD) */
static const char *const ARPOCT_NAMES[] = {"1 OCT", "2 OCT", "3 OCT", "4 OCT"};
static const char *const ARPSTYLE_NAMES[] = {"HOLD", "BLOCK", "UP", "DOWN", "UP-DN", "DN-UP", "RANDOM", "STRUM UP",
                                             "STRUM DN"};
static const char *const ARPVAR_NAMES[] = {
    "01A", "01B", "02A", "02B", "03A", "03B", "04A", "04B", "05A", "05B", "06A", "06B",
    "07A", "07T", "08A", "08T", "09A", "09T", "10A", "10T", "11A", "11T", "12A", "12T",
};
PS_STATIC_ASSERT(sizeof(ARPOCT_NAMES) / sizeof(ARPOCT_NAMES[0]) == ARP_NOCT, "one name per octave range");
PS_STATIC_ASSERT(sizeof(ARPSTYLE_NAMES) / sizeof(ARPSTYLE_NAMES[0]) == ARPSTYLE_COUNT, "one name per ARPSTYLE_*");
static const char *const ARPSTRUM_NAMES[] = {"NONE", "UP", "DOWN"};   /* its notes RATE apart */
PS_STATIC_ASSERT(sizeof(ARPVAR_NAMES) / sizeof(ARPVAR_NAMES[0]) == ARP_NVAR, "one name per variation");
PS_STATIC_ASSERT(sizeof(ARPSTRUM_NAMES) / sizeof(ARPSTRUM_NAMES[0]) == ARP_NSTRUM, "one name per strum");
/* 7.4 footer: EV: and MD: (MAQ-style short names), LANE as 1..4 */
static const char *const SEQEV_NAMES[] = {
    "Note", "Dyn", "Trns", "Time", "Gate", "Rtch", "PSkp", "PMut", "MW", "AT", "CC1", "CC2", "PC", "Layr",
};
static const char *const SEQMODE_NAMES[] = {"FOr", "bAC", "PE1", "PE2", "rnd"};
/* SCALE (2026-10-16): MAQ-style short names, as EV: and MD: (the footer has no labels); ROOT: the pitch classes */
static const char *const SEQSCALE_NAMES[] = {"OFF", "Genr", "Maj", "Min", "Dor", "Phr", "Lyd", "Mix", "HMin", "MajP",
                                             "MinP", "Blus"};   /* (the screen font is capitals: not MPen / mPen) */
static const char *const ROOT_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
PS_STATIC_ASSERT(sizeof(SEQSCALE_NAMES) / sizeof(SEQSCALE_NAMES[0]) == SEQSCALE_COUNT, "one name per SEQSCALE_*");
PS_STATIC_ASSERT(sizeof(SEQEV_NAMES) / sizeof(SEQEV_NAMES[0]) == SEQEV_COUNT, "one name per SEQEV_*");
PS_STATIC_ASSERT(sizeof(SEQMODE_NAMES) / sizeof(SEQMODE_NAMES[0]) == SEQ_MODE_COUNT, "one name per SEQ_MODE_*");
PS_STATIC_ASSERT(sizeof(VARCH_NAMES) / sizeof(VARCH_NAMES[0]) == VARCH_COUNT, "one name per VARCH_*");

/* OSC1 / OSC2: knobs 1..4 are not rows (3: the knob frame cannot be highlighted); sync last, OSC2 only */
/* SINGLE, SPREAD and STRIKE (2026-10-17): knob 3 is SCAN AMT, so FINE TUNE is a row there (OSC_KNOBS_SCAN) */
#define A_SCANKNOB ((uint16_t)(VARCH_BIT(VARCH_SINGLE) | VARCH_BIT(VARCH_SPREAD) | VARCH_BIT(VARCH_STRIKE)))
static const UiRow OSC_ROWS[] = {
    {"WAVE", OFS(OscParams, wavebank), 0, 255, RK_WAVE, 0, NULL, A_ALL},
    {"FINE TUNE", OFS(OscParams, fine), 0, VAL_MAX, RK_FINE, 0, NULL, A_SCANKNOB},
    {"SCAN MOD SRC", OFS(OscParams, scan_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},
    {"SCAN MOD AMT", OFS(OscParams, scan_mod_amt), 0, VAL_MAX, RK_BYTE, 0, NULL, (uint16_t)(VARCH_ALL & ~A_SCANKNOB)},
    {"PCH MOD SRC", OFS(OscParams, pitch_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},
    {"KEY TRACK", OFS(OscParams, key_track), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"OSC SYNC", OFS(OscParams, sync), 0, 1, RK_BYTE, 0, BOOL_NAMES, A_ALL},
};
/* OSCMIXER (4.1): a parameter on one of the architecture's knobs (OSCMIX_KNOBS) has no row there. Balance and
 * cross-mod need OSC2 (STACK has XMOD TYPE on knob 1), SPREAD has its source row (spread and its amount are knobs),
 * BINAURAL adds the right ear's scan offset. The noise generator and the sub show everywhere; SINGLE has the noise
 * colour and the sub octave on knobs. */
#define A_NOT_SINGLE ((uint16_t)(VARCH_ALL & ~(VARCH_BIT(VARCH_SINGLE) | VARCH_BIT(VARCH_STRIKE))))   /* NOISE COLOUR: a knob in both */
static const UiRow OSCMIX_ROWS[] = {
    {"MIX MOD SRC", OFS(OscMixerParams, mix_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(DUAL_OSC) | A(FULL) | A(DUAL_SPREAD)},
    {"MIX MOD AMT", OFS(OscMixerParams, mix_mod_amt), 0, VAL_MAX, RK_BYTE, 0, NULL, A(DUAL_OSC) | A(FULL) | A(DUAL_SPREAD)},
    {"XMOD MOD SRC", OFS(OscMixerParams, xmod_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(DUAL_OSC) | A(STACK) | A(FULL)},
    {"XMOD TYPE", OFS(OscMixerParams, xmod_type), 0, XMOD_COUNT - 1, RK_BYTE, 0, XMOD_NAMES, A(DUAL_OSC) | A(FULL)},
    {"XMOD MOD AMT", OFS(OscMixerParams, xmod_mod_amt), 0, VAL_MAX, RK_BYTE, 0, NULL, A(DUAL_OSC) | A(STACK) | A(FULL)},
    {"SPR MOD SRC", OFS(OscMixerParams, spread_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(SPREAD) | A(DUAL_SPREAD)},
    {"NOISE COLOUR", OFS(OscMixerParams, noise_colour), 0, VAL_MAX, RK_BYTE, 0, NULL, A_NOT_SINGLE},
    {"COL MOD SRC", OFS(OscMixerParams, noise_col_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},
    {"COL MOD AMT", OFS(OscMixerParams, noise_col_mod_amt), 0, VAL_MAX, RK_BYTE, RF_BIPOLAR, NULL, A_ALL},
    {"NOISE MOD SRC", OFS(OscMixerParams, noise_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},
    {"NOISE MOD AMT", OFS(OscMixerParams, noise_mod_amt), 0, VAL_MAX, RK_BYTE, RF_BIPOLAR, NULL, A_ALL},
    {"R SCAN OFFSET", OFS(OscMixerParams, bin_scan_offset), 0, VAL_MAX, RK_BYTE, RF_BIPOLAR, NULL, A(BINAURAL)},
    {"SUB OCTAVE", OFS(OscMixerParams, sub_octave), 0, SUBOCT_COUNT - 1, RK_BYTE, 0, SUBOCT_NAMES,
     (uint16_t)(VARCH_ALL & ~VARCH_BIT(VARCH_SINGLE))},
    {"SUB WAVE", OFS(OscMixerParams, sub_waveform), 0, SUBWAVE_COUNT - 1, RK_BYTE, 0, SUBWAVE_NAMES, A_ALL},
};
/* ATTACK (STRIKE only, 2026-10-16): the attack sound's settings beyond its knobs, in OscMixerParams; BEND MOD's source
 * last (its amount is knob 4, as OSC1's PITCH MOD SRC / PITCH MOD) */
static const UiRow ATTACK_ROWS[] = {
    {"SOUND", OFS(OscMixerParams, atk_sample), 0, ATK_COUNT - 1, RK_BYTE, 0, attack_names, A(STRIKE)},
    {"START", OFS(OscMixerParams, atk_start), 0, VAL_MAX, RK_BYTE, 0, NULL, A(STRIKE)},
    {"TUNE", OFS(OscMixerParams, atk_tune), 40, 88, RK_BYTE, RF_BIPOLAR, NULL, A(STRIKE)},          /* -24..+24 */
    {"VELOCITY", OFS(OscMixerParams, atk_vel), 0, VAL_MAX, RK_BYTE, 0, NULL, A(STRIKE)},
    {"KEY TRACK", OFS(OscMixerParams, atk_track), 0, ATKTRK_COUNT - 1, RK_BYTE, 0, ATKTRK_NAMES, A(STRIKE)},
    {"BEND MOD SRC", OFS(OscMixerParams, atk_bend_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(STRIKE)},
};
/* FILTER1 / FILTER2 (4.2): DRIVE first (2026-10-21), then the circuit and KEY TRACK; the type morph's own mod source
 * and amount show in every architecture. KEY TRACK lives at the patch's end (EngineState.filter_key_track), so each
 * filter page has its own table: the row's offset counts from that filter's block. */
#define FKT_OFS(i) ((uint8_t)(offsetof(EngineState, filter_key_track) + (i) - offsetof(EngineState, filter) - \
                              (i) * sizeof(FilterParams)))
#define FILTER_ROWS(i) {                                                                                   \
    {"DRIVE", OFS(FilterParams, drive), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},                               \
    {"FLAVOUR", OFS(FilterParams, flavour), 0, FLT_COUNT - 1, RK_BYTE, 0, FLT_NAMES, A_ALL},                \
    {"KEY TRACK", FKT_OFS(i), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},                                         \
    {"MOD SRC", OFS(FilterParams, mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},                     \
    {"MORPH MOD SRC", OFS(FilterParams, morph_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},         \
    {"MORPH MOD AMT", OFS(FilterParams, morph_mod_amt), 0, VAL_MAX, RK_BYTE, RF_BIPOLAR, NULL, A_ALL},      \
}
static const UiRow FILTER1_ROWS[] = FILTER_ROWS(0);
static const UiRow FILTER2_ROWS[] = FILTER_ROWS(1);
PS_STATIC_ASSERT(offsetof(EngineState, filter_key_track) + 1u - offsetof(EngineState, filter[1]) <= 255u,
                 "FILTER2's KEY TRACK in reach of a row offset");
/* FILTER MIXER (4.2): DUAL FILTER's balance / routing sources, STACK's blend source, BINAURAL's phase mod source and
 * CUTOFF L/R's own mod source and amount */
static const UiRow FLTMIX_ROWS[] = {
    {"BAL MOD SRC", OFS(FilterMixerParams, balance_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(DUAL_FILTER) | A(FULL)},
    {"RTG MOD SRC", OFS(FilterMixerParams, routing_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(DUAL_FILTER) | A(FULL)},
    {"BLEND MOD SRC", OFS(FilterMixerParams, balance_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(STACK)},
    {"PHASE MOD SRC", OFS(FilterMixerParams, bin_phase_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(BINAURAL)},
    {"CUT L/R SRC", OFS(FilterMixerParams, bin_cutoff_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A(BINAURAL)},
    {"CUT L/R AMT", OFS(FilterMixerParams, bin_cutoff_mod_amt), 0, VAL_MAX, RK_BYTE, RF_BIPOLAR, NULL, A(BINAURAL)},
};
/* invert last, ENV2 / ENV3 only. VEL > LEVEL lives at the patch's end (EngineState.env_vel_level, v10), so each
 * envelope page has its own table, as the FILTER pages: the row's offset counts from that envelope's block. */
#define ENV_X_OFS(i) ((uint8_t)(offsetof(EngineState, env_vel_level) + (i) - offsetof(EngineState, env) - \
                                (i) * sizeof(EnvParams)))
#define ENV_ROWS(i) {                                                                                      \
    {"VEL > LEVEL", ENV_X_OFS(i), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},                                     \
    {"VEL > TIME", OFS(EnvParams, vel_to_time), 0, VAL_MAX, RK_BYTE, RF_BIPOLAR, NULL, A_ALL},              \
    {"KEY > TIME", OFS(EnvParams, key_to_time), 0, VAL_MAX, RK_BYTE, RF_BIPOLAR, NULL, A_ALL},              \
    {"RETRIG RATE", OFS(EnvParams, retrig_rate), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},                      \
    {"RETRIG SYNC", OFS(EnvParams, retrig_sync), 0, 1, RK_BYTE, 0, BOOL_NAMES, A_ALL},                      \
    {"LEGATO", OFS(EnvParams, legato), 0, 1, RK_BYTE, 0, BOOL_NAMES, A_ALL},                                \
    {"AUTO DECAY", OFS(EnvParams, auto_decay), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},                        \
    {"INVERT", OFS(EnvParams, invert), 0, 1, RK_BYTE, 0, BOOL_NAMES, A_ALL},                                \
}
static const UiRow ENV1_ROWS[] = ENV_ROWS(0);
static const UiRow ENV2_ROWS[] = ENV_ROWS(1);
static const UiRow ENV3_ROWS[] = ENV_ROWS(2);
/* DEPTH MOD SRC / AMT (v10) live at the patch's end too (EngineState.lfo_depth_src / _amt): a table per LFO page */
#define LFO_X_OFS(member, i) ((uint8_t)(offsetof(EngineState, member) + (i) - offsetof(EngineState, lfo) - \
                                        (i) * sizeof(LfoParams)))
#define LFO_ROWS(i) {                                                                                      \
    {"DEPTH MOD SRC", LFO_X_OFS(lfo_depth_src, i), 0, SRC_MAX, RK_DEPTH_SRC, 0, MSRC_NAMES, A_ALL},        \
    {"DEPTH MOD AMT", LFO_X_OFS(lfo_depth_amt, i), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},                   \
    {"SPD MOD SRC", OFS(LfoParams, speed_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},              \
    {"SPD MOD AMT", OFS(LfoParams, speed_mod_amt), 0, VAL_MAX, RK_BYTE, RF_BIPOLAR, NULL, A_ALL},           \
    {"CLOCK SYNC", OFS(LfoParams, clock_sync), 0, 1, RK_BYTE, 0, BOOL_NAMES, A_ALL},                        \
    {"PHASE", OFS(LfoParams, phase), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},                                  \
    {"KEY RESET", OFS(LfoParams, key_reset), 0, 1, RK_BYTE, 0, BOOL_NAMES, A_ALL},                          \
}
static const UiRow LFO1_ROWS[] = LFO_ROWS(0);
static const UiRow LFO2_ROWS[] = LFO_ROWS(1);
static const UiRow LFO3_ROWS[] = LFO_ROWS(2);
PS_STATIC_ASSERT(offsetof(EngineState, lfo_depth_amt) + 2u - offsetof(EngineState, lfo[2]) <= 255u &&
                 offsetof(EngineState, env_vel_level) + 2u - offsetof(EngineState, env[2]) <= 255u,
                 "VEL > LEVEL and DEPTH MOD in reach of a row offset");
static const UiRow AMP_ROWS[] = {
    {"AMP MOD SRC", OFS(AmpParams, amp_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},
    {"PAN MOD SRC", OFS(AmpParams, pan_mod_src), 0, SRC_MAX, RK_BYTE, 0, MSRC_NAMES, A_ALL},
    {"PAN MOD DEPTH", OFS(AmpParams, pan_mod_depth), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"DRV MODEL", OFS(AmpParams, drive_model), 0, DRIVE_COUNT - 1, RK_BYTE, 0, DRIVE_NAMES, A_ALL},
};
/* MOD1..3 (4.5): the slot's own parameters are the knobs; the rows are where its output goes. A row per source
 * selector in the patch (the *_ROWS above carry the same selectors), shown while that selector names this page's slot
 * and the architecture uses it; the Algorithm Encoder sets the amount beside the selector. A selector that is another
 * slot's source A / B is a chain: shown, read-only. */
#define EOFS(member) OFS(EngineState, member)
#define MDEST(label, src, amt, flags, archs) {label, EOFS(amt), 0, VAL_MAX, RK_BYTE, flags, NULL, archs, (uint8_t)(EOFS(src) + 1u)}
#define MCHAIN(label, src) {label, EOFS(src), 0, 0, RK_CHAIN, 0, NULL, A_ALL, (uint8_t)(EOFS(src) + 1u)}
#define A_OSC2 ((uint16_t)(A(DUAL_OSC) | A(STACK) | A(FULL) | A(DUAL_SPREAD)))
#define A_FLT2 ((uint16_t)(A(DUAL_FILTER) | A(STACK) | A(FULL)))
static const UiRow MOD_ROWS[] = {
    MDEST("OSC1 SCAN", osc[0].scan_mod_src, osc[0].scan_mod_amt, 0, A_ALL),
    MDEST("OSC1 PITCH", osc[0].pitch_mod_src, osc[0].pitch_mod_amt, 0, A_ALL),
    MDEST("OSC2 SCAN", osc[1].scan_mod_src, osc[1].scan_mod_amt, 0, A_OSC2),
    MDEST("OSC2 PITCH", osc[1].pitch_mod_src, osc[1].pitch_mod_amt, 0, A_OSC2),
    MDEST("OSC MIX", oscmix.mix_mod_src, oscmix.mix_mod_amt, 0, A(DUAL_OSC) | A(FULL) | A(DUAL_SPREAD)),
    MDEST("CROSS-MOD", oscmix.xmod_mod_src, oscmix.xmod_mod_amt, 0, A(DUAL_OSC) | A(STACK) | A(FULL)),
    MDEST("SPREAD", oscmix.spread_mod_src, oscmix.spread_mod_amt, 0, A(SPREAD) | A(DUAL_SPREAD)),
    MDEST("NOISE LEVEL", oscmix.noise_mod_src, oscmix.noise_mod_amt, RF_BIPOLAR, A_ALL),
    MDEST("NOISE COLOUR", oscmix.noise_col_mod_src, oscmix.noise_col_mod_amt, RF_BIPOLAR, A_ALL),
    MDEST("F1 CUTOFF", filter[0].mod_src, filter[0].mod_amt, 0, A_ALL),
    MDEST("F1 MORPH", filter[0].morph_mod_src, filter[0].morph_mod_amt, RF_BIPOLAR, A_ALL),
    MDEST("F2 CUTOFF", filter[1].mod_src, filter[1].mod_amt, 0, A_FLT2),
    MDEST("F2 MORPH", filter[1].morph_mod_src, filter[1].morph_mod_amt, RF_BIPOLAR, A_FLT2),
    MDEST("BALANCE", fltmix.balance_mod_src, fltmix.balance_mod_amt, RF_BIPOLAR, A(DUAL_FILTER) | A(FULL)),
    MDEST("BLEND A/B", fltmix.balance_mod_src, fltmix.balance_mod_amt, RF_BIPOLAR, A(STACK)),
    MDEST("ROUTING", fltmix.routing_mod_src, fltmix.routing_mod_amt, RF_BIPOLAR, A(DUAL_FILTER) | A(FULL)),
    MDEST("PHASE", fltmix.bin_phase_mod_src, fltmix.bin_phase_mod_amt, RF_BIPOLAR, A(BINAURAL)),
    MDEST("CUTOFF L/R", fltmix.bin_cutoff_mod_src, fltmix.bin_cutoff_mod_amt, RF_BIPOLAR, A(BINAURAL)),
    MDEST("LFO1 SPEED", lfo[0].speed_mod_src, lfo[0].speed_mod_amt, RF_BIPOLAR, A_ALL),
    MDEST("LFO2 SPEED", lfo[1].speed_mod_src, lfo[1].speed_mod_amt, RF_BIPOLAR, A_ALL),
    MDEST("LFO3 SPEED", lfo[2].speed_mod_src, lfo[2].speed_mod_amt, RF_BIPOLAR, A_ALL),
    MDEST("AMP LEVEL", amp.amp_mod_src, amp.amp_mod_depth, 0, A_ALL),
    MDEST("PAN", amp.pan_mod_src, amp.pan_mod_depth, 0, A_ALL),
    MCHAIN("MOD 1 A", mod[0].src_a), MCHAIN("MOD 1 B", mod[0].src_b),
    MCHAIN("MOD 2 A", mod[1].src_a), MCHAIN("MOD 2 B", mod[1].src_b),
    MCHAIN("MOD 3 A", mod[2].src_a), MCHAIN("MOD 3 B", mod[2].src_b),
};
PS_STATIC_ASSERT(sizeof(EngineState) < 255u, "MOD_ROWS: via = 1 + an EngineState offset fits a byte");
#undef MDEST
#undef MCHAIN

#define PERF(member) OFS(MasterPerformancePreset, member)
#define ENGB(member) OFS(EngineState, member)
static const UiRow EFFECTS_ROWS[] = {
    /* the delay's rows first, then the reverb's (2026-10-26) */
    {"DELAY TYPE", PERF(delay_type), 0, DELAYTYPE_COUNT - 1, RK_BYTE, 0, DELAYTYPE_NAMES, A_ALL},
    {"DELAY SYNC", PERF(delay_sync), 0, 1, RK_BYTE, 0, BOOL_NAMES, A_ALL},
    {"DEGRADE", PERF(delay_degrade), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"PING PONG", PERF(delay_pingpong), 0, 1, RK_PINGPONG, 0, BOOL_NAMES, A_ALL},
    {"DELAY HP", PERF(delay_pingpong), 0, VAL_MAX, RK_DELAY_HP, 0, NULL, A_ALL},
    {"REV TYPE", PERF(reverb_type), 0, REVTYPE_COUNT - 1, RK_BYTE, 0, REVTYPE_NAMES, A_ALL},
    {"REV PREDELAY", PERF(reverb_predelay), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
};
#define INST(member) OFS(InstrumentSettings, member)
static const UiRow GLOBAL_ROWS[] = {
    {"MASTER TUNE", INST(master_tune), MASTER_TUNE_MIN, MASTER_TUNE_MAX, RK_INST, RF_BIPOLAR, NULL, A_ALL},
    {"VEL CURVE", INST(vel_curve), 0, VELCURVE_COUNT - 1, RK_INST, 0, VELCURVE_NAMES, A_ALL},
    {"CLOCK OUT", INST(clock_out), 0, CLKOUT_COUNT - 1, RK_INST, 0, CLKOUT_NAMES, A_ALL},
    {"MIDI CH LAYER1", PERF(midi_ch), 0, 15, RK_BYTE, RF_CHANNEL, NULL, A_ALL},
    {"MIDI CH LAYER2", (uint8_t)(PERF(midi_ch) + 1u), 0, 15, RK_BYTE, RF_LAYER2 | RF_CHANNEL, NULL, A_ALL},
    {"CUSTOM CC1", PERF(cc1_num), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"CUSTOM CC2", PERF(cc2_num), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"BASE VELOCITY", PERF(base_velocity), 12, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"BEND RANGE", PERF(bend_range), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"CLOCK SRC", PERF(clock_source), 0, CLKSRC_COUNT - 1, RK_BYTE, 0, CLKSRC_NAMES, A_ALL},
    {"INT TEMPO", PERF(int_tempo), 0, VAL_MAX, RK_BYTE, RF_BPM, NULL, A_ALL},
    {"THEME", 0, 0, UITHEME_COUNT - 1, RK_THEME, 0, THEME_NAMES, A_ALL},
};
/* 5.3 View 1 / View 2: the architecture and ANALOG live in the layer's patch (RK_ENG), the rest in the performance. Since
 * 2026-10-16 they are one HOME page that follows the layer focus (View 2 = layer 2 in focus), so both keep the same
 * rows in the same order (the cursor stays on its row across a layer switch). PERF MODE is on the performance browser
 * (UIPAGE_PERF) since 2026-10-17 */
static const UiRow HOME1_ROWS[] = {
    {"ARCH", ENGB(arch), VARCH_DUAL_OSC, VARCH_COUNT - 1, RK_ENG, 0, VARCH_NAMES, A_ALL},   /* SINGLE: SPREAD at 0 */
    {"ANALOG", ENGB(analog), 0, VAL_MAX, RK_ENG, 0, NULL, A_ALL},
    {"VOICING", PERF(voice_mode), 0, VMODE_COUNT - 1, RK_BYTE, 0, VMODE_NAMES, A_ALL},
    {"UNISON DETUNE", PERF(unison_detune), 0, VAL_MAX, RK_BYTE, RF_UNISON, NULL, A_ALL},
    {"PORTA", PERF(porta_mode), 0, PORTA_COUNT - 1, RK_BYTE, 0, PORTA_NAMES, A_ALL},
    {"PORTA TIME", PERF(porta_time), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"MAX VOICES", 0, 0, VOICE_POOL, RK_VOICES, 0, NULL, A_ALL},
};
static const UiRow HOME2_ROWS[] = {
    {"ARCH", ENGB(arch), VARCH_DUAL_OSC, VARCH_COUNT - 1, RK_ENG, RF_LAYER2, VARCH_NAMES, A_ALL},
    {"ANALOG", ENGB(analog), 0, VAL_MAX, RK_ENG, RF_LAYER2, NULL, A_ALL},
    {"VOICING", (uint8_t)(PERF(voice_mode) + 1u), 0, VMODE_COUNT - 1, RK_BYTE, RF_LAYER2, VMODE_NAMES, A_ALL},
    {"UNISON DETUNE", (uint8_t)(PERF(unison_detune) + 1u), 0, VAL_MAX, RK_BYTE,
     RF_LAYER2 | RF_UNISON, NULL, A_ALL},
    {"PORTA", (uint8_t)(PERF(porta_mode) + 1u), 0, PORTA_COUNT - 1, RK_BYTE, RF_LAYER2, PORTA_NAMES, A_ALL},
    {"PORTA TIME", (uint8_t)(PERF(porta_time) + 1u), 0, VAL_MAX, RK_BYTE, RF_LAYER2, NULL, A_ALL},
    {"MAX VOICES", 0, 0, VOICE_POOL, RK_VOICES, RF_LAYER2, NULL, A_ALL},
};
static const UiRow HOME3_ROWS[] = {
    {"SPLIT NOTE", PERF(split_note), 0, VAL_MAX, RK_BYTE, RF_LAYER2 | RF_NOTE, NULL, A_ALL},
    {"SPLIT FADE", PERF(split_fade), 0, VAL_MAX, RK_BYTE, RF_LAYER2, NULL, A_ALL},
    {"VEL SPLIT", PERF(vel_split), 0, VAL_MAX, RK_BYTE, RF_LAYER2, NULL, A_ALL},
    {"VEL FADE", PERF(vel_fade), 0, VAL_MAX, RK_BYTE, RF_LAYER2, NULL, A_ALL},
};
static const UiRow PERF_ROWS[] = {          /* HOME's first page: the performance browser (6.1) */
    {"PERF PRESET", 0, 0, BANK_SLOTS - 1u, RK_PERF_SLOT, RF_SLOT, NULL, A_ALL},
    {"PERF MODE", PERF(perf_mode), 0, PERF_COUNT - 1, RK_BYTE, 0, PERF_NAMES, A_ALL},
    {"LAYER1 PATCH", PERF(layer_patch), 0, VAL_MAX, RK_BYTE, RF_SLOT, NULL, A_ALL},
    {"LAYER2 PATCH", (uint8_t)(PERF(layer_patch) + 1u), 0, VAL_MAX, RK_BYTE, RF_LAYER2 | RF_SLOT, NULL, A_ALL},
    {"SEQ LINK", PERF(seq_link), 0, VAL_MAX, RK_BYTE, RF_SLOT, NULL, A_ALL},
    {"ARP LINK", PERF(arp_link), 0, VAL_MAX, RK_BYTE, RF_SLOT, NULL, A_ALL},
};
static const UiRow SAVE_COMMIT_ROWS[] = {
    {"RENAME PERF", 0, 0, 0, RK_RENAME, 0, NULL, A_ALL},
};
static const UiRow ARP_ROWS[] = {
    {"LAYER", OFS(ArpState, layer_timing), 0, ARPLAYER_COUNT - 1, RK_ARP_LAYER, 0, ARPLAYER_NAMES, A_ALL},
    {"TIMING", OFS(ArpState, layer_timing), 0, ARPDIV_COUNT - 1, RK_ARP_TIMING, 0, ARPDIV_NAMES, A_ALL},
    {"GATE", OFS(ArpState, gate_length), 0, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"SEQ LENGTH", OFS(ArpState, sequence_length), 1, ARP_NSTEP, RK_BYTE, 0, NULL, A_ALL},
    {"TRANSPOSE MAQ", OFS(ArpState, transpose_maq_option), 0, 1, RK_BYTE, 0, BOOL_NAMES, A_ALL},
    {"GENRE BANK", OFS(ArpState, current_genre_bank), 0, ARP_GENRE_COUNT - 1, RK_BYTE, 0, arp_genre_names, A_ALL},
    {"ACCENT VEL", OFS(ArpState, accent_velocity), 1, VAL_MAX, RK_BYTE, 0, NULL, A_ALL},
};
/* 7.4: the knob window, then the active lane's footer left to right (2026-10-21; ui_seq_draw.c highlights the field the
 * cursor is on). The Select Encoder walks STEPS a group of 4 at a time, then the footer, then wraps (seq_cursor_move). */
static const UiRow SEQ_ROWS[] = {
    {"STEPS", 0, 0, UISEQ_STEP0_MAX, RK_SEQ_STEPS, 0, NULL, A_ALL},
    {"PRESCALE", OFS(SequencerLane, prescale), SEQ_PRESCALE_MIN, SEQ_PRESCALE_MAX, RK_BYTE, 0, NULL, A_ALL},
    {"EVENT TYPE", OFS(SequencerLane, event_type), 0, SEQEV_COUNT - 1, RK_SEQ_EVENT, 0, SEQEV_NAMES, A_ALL},
    {"TARGET", OFS(SequencerLane, target_mask), 0, SEQ_TGT_MASK, RK_SEQ_TARGET, 0, NULL, A_ALL},
    {"SCALE", OFS(SequencerLane, scale), 0, SEQSCALE_COUNT - 1, RK_BYTE, 0, SEQSCALE_NAMES, A_ALL},   /* NOTE lanes */
    {"ROOT", OFS(SequencerLane, scale_root), 0, 11, RK_BYTE, 0, ROOT_NAMES, A_ALL},                 /* .. with a scale */
    {"PLAY MODE", OFS(SequencerLane, playback_mode), 0, SEQ_MODE_COUNT - 1, RK_BYTE, 0, SEQMODE_NAMES, A_ALL},
};

/* -------------------------------------------------------- page table --- */
typedef struct {
    const UiRow *rows;
    uint8_t nrows;
    uint8_t store;                   /* ST_* */
    uint8_t base;                    /* offset of the page's block inside the store */
    uint8_t ctx;                     /* UICTX_* */
} UiPage;

#define NROWS(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
#define ENG(member, i, type) ((uint8_t)(offsetof(EngineState, member) + (i) * sizeof(type)))
#define SYNTH(rows, n, member, i, type) {rows, n, ST_ENGINE, ENG(member, i, type), UICTX_SYNTH}
#define MODPAGE {MOD_ROWS, NROWS(MOD_ROWS), ST_ENGINE, 0, UICTX_SYNTH}   /* 4.5: the knobs; the rows its destinations */

static const UiPage UI_PAGES[] = {
    SYNTH(OSC_ROWS, NROWS(OSC_ROWS) - 1u, osc, 0, OscParams),           /* OSC1: no sync */
    SYNTH(OSC_ROWS, NROWS(OSC_ROWS), osc, 1, OscParams),
    SYNTH(ATTACK_ROWS, NROWS(ATTACK_ROWS), oscmix, 0, OscMixerParams),
    SYNTH(OSCMIX_ROWS, NROWS(OSCMIX_ROWS), oscmix, 0, OscMixerParams),
    SYNTH(FILTER1_ROWS, NROWS(FILTER1_ROWS), filter, 0, FilterParams),
    SYNTH(FILTER2_ROWS, NROWS(FILTER2_ROWS), filter, 1, FilterParams),
    SYNTH(FLTMIX_ROWS, NROWS(FLTMIX_ROWS), fltmix, 0, FilterMixerParams),
    SYNTH(ENV1_ROWS, NROWS(ENV1_ROWS) - 1u, env, 0, EnvParams),         /* ENV1: no invert */
    SYNTH(ENV2_ROWS, NROWS(ENV2_ROWS), env, 1, EnvParams),
    SYNTH(ENV3_ROWS, NROWS(ENV3_ROWS), env, 2, EnvParams),
    SYNTH(LFO1_ROWS, NROWS(LFO1_ROWS), lfo, 0, LfoParams),
    SYNTH(LFO2_ROWS, NROWS(LFO2_ROWS), lfo, 1, LfoParams),
    SYNTH(LFO3_ROWS, NROWS(LFO3_ROWS), lfo, 2, LfoParams),
    MODPAGE, MODPAGE, MODPAGE,
    SYNTH(AMP_ROWS, NROWS(AMP_ROWS), amp, 0, AmpParams),
    {EFFECTS_ROWS, NROWS(EFFECTS_ROWS), ST_PERF, 0, UICTX_SYNTH},
    {HOME1_ROWS, NROWS(HOME1_ROWS), ST_PERF, 0, UICTX_SYNTH},
    {HOME2_ROWS, NROWS(HOME2_ROWS), ST_PERF, 0, UICTX_SYNTH},
    {HOME3_ROWS, NROWS(HOME3_ROWS), ST_PERF, 0, UICTX_SYNTH},
    {PERF_ROWS, NROWS(PERF_ROWS), ST_PERF, 0, UICTX_SAVE},
    {GLOBAL_ROWS, NROWS(GLOBAL_ROWS), ST_PERF, 0, UICTX_SYNTH},
    {ARP_ROWS, NROWS(ARP_ROWS), ST_ARP, 0, UICTX_ARP},
    {SAVE_COMMIT_ROWS, NROWS(SAVE_COMMIT_ROWS), ST_NONE, 0, UICTX_SAVE},
    {SEQ_ROWS, NROWS(SEQ_ROWS), ST_LANE, 0, UICTX_SEQ},
};
PS_STATIC_ASSERT(sizeof(UI_PAGES) / sizeof(UI_PAGES[0]) == UIPAGE_COUNT, "one UI_PAGES entry per UIPAGE_*");

/* 2.1: each section button's first submenu and how many submenus its presses cycle (HOME: 5.3, by mode) */
static const uint8_t SECTION_FIRST[BTN_COUNT] = {
    UIPAGE_OSC1, UIPAGE_FILTER1, UIPAGE_ENV1, UIPAGE_LFO1, UIPAGE_MOD1, UIPAGE_AMP,
    UIPAGE_HOME1, UIPAGE_ARP, UIPAGE_SAVE_COMMIT, UIPAGE_SEQ
};
static const uint8_t SECTION_COUNT[BTN_COUNT] = {
    4, 3, 3, 3, NMODSLOT, 2, UI_HOME_NVIEW_MULTI + 2u, 1, 1, 1   /* HOME: LAYER, ZONES, the browser, GLOBAL */
};

/* -------------------------------------------------------- knob table --- */
/* 2.2: the four knobs of each page. offset is in the page's data block (as the rows' offsets are), except HOME's
 * knob 4 (UIKNOB_PORTA), which is the focused layer's PORTA TIME in the performance. */
typedef struct {
    const char *label;
    uint8_t offset;
    uint8_t kind;                    /* UIKNOB_* */
    uint8_t n_enum;                  /* UIKNOB_LIST: entries */
    const char *const *names;        /* UIKNOB_LIST: their names */
} UiKnob;

#define UIK(label, offset, kind) {label, offset, kind, 0, NULL}
#define KNONE UIK("", 0, UIKNOB_NONE)
#define KOSC(m, label, kind) UIK(label, OFS(OscParams, m), kind)
#define KMIX(m, label, kind) UIK(label, OFS(OscMixerParams, m), kind)
#define KMIXL(m, label, names, n) {label, OFS(OscMixerParams, m), UIKNOB_LIST, n, names}
#define KFLT(m, label, kind) UIK(label, OFS(FilterParams, m), kind)
#define KFMX(m, label, kind) UIK(label, OFS(FilterMixerParams, m), kind)

static const UiKnob OSC_KNOBS[4] = {
    KOSC(scan, "WAVE SCAN", UIKNOB_RAW), KOSC(octave, "OCTAVE", UIKNOB_OCT),
    KOSC(fine, "FINE TUNE", UIKNOB_FINE), KOSC(pitch_mod_amt, "PITCH MOD", UIKNOB_RAW),
};
static const UiKnob OSC_KNOBS_SCAN[4] = {   /* SINGLE / SPREAD / STRIKE: the scan's mod amount on knob 3 */
    KOSC(scan, "WAVE SCAN", UIKNOB_RAW), KOSC(octave, "OCTAVE", UIKNOB_OCT),
    KOSC(scan_mod_amt, "SCAN AMT", UIKNOB_RAW), KOSC(pitch_mod_amt, "PITCH MOD", UIKNOB_RAW),
};
/* OSCMIXER by architecture. SINGLE: the noise colour and level, the sub's octave and level. STRIKE: the attack's level
 * (its sound and shape on the ATTACK page), the noise colour, sub, noise. SPREAD: spread, noise, sub, spread mod. STACK: the cross-mod type and amount, sub, noise. The others: knob 3 the sub, knob 4 the noise,
 * knobs 1 / 2 need OSC2 (BINAURAL reuses them for the right ear); with no OSC2 they are greyed. */
static const UiKnob OSCMIX_KNOBS[VARCH_COUNT][4] = {
    {KMIX(noise_colour, "NOISE COLOUR", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW),
     KMIXL(sub_octave, "SUB OCTAVE", SUBOCT_NAMES, SUBOCT_COUNT), KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW)},    /* SINGLE */
    {KMIX(balance, "OSC MIX", UIKNOB_BIP), KMIX(xmod_amt, "CROSS-MOD", UIKNOB_RAW),
     KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW)},                        /* DUAL OSC */
    {KMIX(spread, "SPREAD", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW),
     KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW), KMIX(spread_mod_amt, "SPREAD MOD", UIKNOB_RAW)},                /* SPREAD */
    {KNONE, KNONE, KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW)},           /* DUAL FILTER */
    {KMIXL(xmod_type, "XMOD TYPE", XMOD_NAMES, XMOD_COUNT), KMIX(xmod_amt, "CROSS-MOD", UIKNOB_RAW),
     KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW)},                        /* STACK */
    {KMIX(bin_mod_phase, "OSC PHASE", UIKNOB_RAW), KMIX(bin_beat, "BEAT HZ", UIKNOB_RAW),
     KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW)},                        /* BINAURAL */
    {KMIX(atk_level, "ATK LEVEL", UIKNOB_RAW), KMIX(noise_colour, "NOISE COLOUR", UIKNOB_RAW),
     KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW)},                        /* STRIKE */
    {KMIX(balance, "OSC MIX", UIKNOB_BIP), KMIX(xmod_amt, "CROSS-MOD", UIKNOB_RAW),
     KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW)},                        /* FULL */
    {KMIX(balance, "OSC MIX", UIKNOB_BIP), KMIX(spread, "SPREAD", UIKNOB_RAW),
     KMIX(sub_level, "SUB LEVEL", UIKNOB_RAW), KMIX(noise_level, "NOISE", UIKNOB_RAW)},                        /* DUAL SPREAD */
};
/* ATTACK (STRIKE, 2026-10-17): how soon it fades, how much of it goes into FILTER1, its pitch bend, and how far the
 * BEND MOD SRC row's source moves that bend (knob 4, as OSC1's PITCH MOD). The sound and START are rows; the level
 * is on OSCMIXER, with the other sources' */
static const UiKnob ATTACK_KNOBS[4] = {
    KMIX(atk_decay, "DECAY", UIKNOB_RAW), KMIX(atk_filter, "TO FILTER", UIKNOB_RAW),
    KMIX(atk_bend, "BEND", UIKNOB_BIP), KMIX(atk_bend_mod_amt, "BEND MOD", UIKNOB_BIP),
};
static const UiKnob FILTER_KNOBS[4] = {
    KFLT(morph, "TYPE MORPH", UIKNOB_RAW), KFLT(cutoff, "CUTOFF", UIKNOB_RAW),
    KFLT(resonance, "RESONANCE", UIKNOB_RAW), KFLT(mod_amt, "MOD AMOUNT", UIKNOB_RAW),
};
/* FILTER MIXER by architecture (the page is hidden for SINGLE, DUAL OSC and SPREAD) */
static const UiKnob FLTMIX_KNOBS[VARCH_COUNT][4] = {
    {KNONE, KNONE, KNONE, KNONE},
    {KNONE, KNONE, KNONE, KNONE},
    {KNONE, KNONE, KNONE, KNONE},
    {KFMX(balance, "BALANCE", UIKNOB_BIP), KFMX(routing, "ROUTING", UIKNOB_RAW),
     KFMX(balance_mod_amt, "BALANCE MOD", UIKNOB_BIP), KFMX(routing_mod_amt, "ROUTING MOD", UIKNOB_BIP)},     /* DUAL FILTER */
    {KFMX(balance, "BLEND A/B", UIKNOB_BIP), KFMX(stack_width, "WIDTH", UIKNOB_RAW),
     KFMX(balance_mod_amt, "BLEND MOD", UIKNOB_BIP), KFMX(env3_cutoff, "ENV3>CUTOFF", UIKNOB_BIP)},          /* STACK */
    {KFMX(bin_mod_phase, "FLT PHASE", UIKNOB_RAW), KFMX(bin_width, "WIDTH", UIKNOB_RAW),
     KFMX(bin_cutoff_offset, "CUTOFF L/R", UIKNOB_BIP), KFMX(bin_phase_mod_amt, "PHASE MOD", UIKNOB_BIP)},    /* BINAURAL */
    {KNONE, KNONE, KNONE, KNONE},                                                                               /* STRIKE */
    {KFMX(balance, "BALANCE", UIKNOB_BIP), KFMX(routing, "ROUTING", UIKNOB_RAW),
     KFMX(balance_mod_amt, "BALANCE MOD", UIKNOB_BIP), KFMX(routing_mod_amt, "ROUTING MOD", UIKNOB_BIP)},     /* FULL */
    {KNONE, KNONE, KNONE, KNONE},                                                                               /* DUAL SPREAD */
};
static const UiKnob ENV_KNOBS[4] = {
    UIK("ATTACK", OFS(EnvParams, attack), UIKNOB_RAW), UIK("DECAY", OFS(EnvParams, decay), UIKNOB_RAW),
    UIK("SUSTAIN", OFS(EnvParams, sustain), UIKNOB_RAW), UIK("RELEASE", OFS(EnvParams, release), UIKNOB_RAW),
};
static const UiKnob LFO_KNOBS[4] = {
    UIK("SHAPE", OFS(LfoParams, shape), UIKNOB_RAW), UIK("RATE", OFS(LfoParams, rate), UIKNOB_RAW),
    UIK("DELAY", OFS(LfoParams, delay), UIKNOB_RAW), UIK("FADE", OFS(LfoParams, fade), UIKNOB_BIP),
};
static const UiKnob MOD_KNOBS[4] = {
    UIK("SOURCE A", OFS(ModSlotParams, src_a), UIKNOB_SRC), UIK("FUNCTION", OFS(ModSlotParams, func), UIKNOB_FN),
    UIK("SOURCE B", OFS(ModSlotParams, src_b), UIKNOB_SRC), UIK("AMOUNT", OFS(ModSlotParams, amount), UIKNOB_BIP),
};
static const UiKnob AMP_KNOBS[4] = {
    UIK("DRIVE", OFS(AmpParams, drive), UIKNOB_RAW), UIK("AMP MOD", OFS(AmpParams, amp_mod_depth), UIKNOB_RAW),
    UIK("DELAY SEND", OFS(AmpParams, delay_send), UIKNOB_RAW), UIK("REVERB SEND", OFS(AmpParams, reverb_send), UIKNOB_RAW),
};
static const UiKnob EFFECTS_KNOBS[4] = {
    UIK("DELAY TIME", PERF(delay_time), UIKNOB_RAW), UIK("DELAY FDBK", PERF(delay_feedback), UIKNOB_RAW),
    UIK("REVERB TIME", PERF(reverb_decay), UIKNOB_RAW), UIK("REVERB DAMP", PERF(reverb_damp), UIKNOB_RAW),
};
/* 2.2 ARP (chord page): the live dial values a capture copies into a step (UIStateMachine.arp_knob, by index) */
static const UiKnob ARP_KNOBS[4] = {
    {"RATE", 0, UIKNOB_LIST, ARPRATE_COUNT, ARPRATE_NAMES}, {"OCTAVE", 1, UIKNOB_LIST, ARP_NOCT, ARPOCT_NAMES},
    {"STYLE", 2, UIKNOB_LIST, ARPSTYLE_COUNT, ARPSTYLE_NAMES}, {"RHYTHM", 3, UIKNOB_LIST, ARP_NVAR, ARPVAR_NAMES},
};
/* LAYER (2026-10-17): the play controllers. VELOCITY is the keys' own; MOD WHEEL goes out as CC 1; CC1 sets the
 * CUSTOMCC1 mod source directly (no CC number needed). Knob 4 (2026-10-21, was CC2): the focused layer's PORTA TIME,
 * 0 is portamento off */
static const UiKnob HOME_KNOBS[4] = {
    UIK("VELOCITY", UIPERF_VELOCITY, UIKNOB_PERF), UIK("MOD WHEEL", UIPERF_MODWHEEL, UIKNOB_PERF),
    UIK("CC1", UIPERF_CC1, UIKNOB_PERF), UIK("PORTA TIME", 0, UIKNOB_PORTA),
};
/* ZONES: its four rows */
static const UiKnob ZONES_KNOBS[4] = {
    UIK("SPLIT", PERF(split_note), UIKNOB_NOTE), UIK("FADE", PERF(split_fade), UIKNOB_RAW),
    UIK("VEL SPLIT", PERF(vel_split), UIKNOB_RAW), UIK("VEL FADE", PERF(vel_fade), UIKNOB_RAW),
};
/* 6.1: the links, also menu rows. 6.2.1: offsets index the UIStateMachine's own fields (ui_sm_knob) */
static const UiKnob SAVE_PRESET_KNOBS[4] = {
    UIK("L1 PATCH", PERF(layer_patch), UIKNOB_RAW), UIK("L2 PATCH", PERF(layer_patch) + 1u, UIKNOB_RAW),
    UIK("SEQ LINK", PERF(seq_link), UIKNOB_RAW), UIK("ARP LINK", PERF(arp_link), UIKNOB_RAW),
};
static const UiKnob SAVE_COMMIT_KNOBS[4] = {
    {"TARGET", 0, UIKNOB_LIST, UISAVE_COUNT, SAVE_TARGET_NAMES}, UIK("SLOT", 1, UIKNOB_RAW),
    {"CHAR POS", 2, UIKNOB_LIST, NAME_LEN, NAME_POS_NAMES}, {"CHARACTER", 3, UIKNOB_LIST, CHAR_MAP_LEN, CHAR_NAMES},
};
/* GLOBAL: knobs 1..3 the instrument's (sm->inst), knob 4 the internal clock's tempo (greyed while CLOCK SOURCE is
 * MIDI) */
static const UiKnob GLOBAL_INST_KNOBS[3] = {
    UIK("TUNE", INST(master_tune), UIKNOB_BIP),       /* (knob labels short: the rows have the full names) */
    {"CURVE", INST(vel_curve), UIKNOB_LIST, VELCURVE_COUNT, VELCURVE_NAMES},
    {"CLOCK", INST(clock_out), UIKNOB_LIST, CLKOUT_COUNT, CLKOUT_NAMES},
};
static const UiKnob GLOBAL_TEMPO_KNOB = UIK("TEMPO", PERF(int_tempo), UIKNOB_BPM);
#undef INST
#undef PERF
#undef ENGB

/* ---------------------------------------------------------- helpers --- */
/* GLOBAL's instrument settings: the attached ones, else a private copy at the defaults */
static InstrumentSettings *ui_inst(const UIStateMachine *sm)
{
    static InstrumentSettings own = {64u, VELCURVE_LINEAR, CLKOUT_OFF};
    return sm->inst != 0 ? sm->inst : &own;
}

void ui_sm_attach_instrument(UIStateMachine *sm, InstrumentSettings *inst) { sm->inst = inst; }

/* an out-of-range mode (a corrupt flash read) is treated as the most restrictive one */
static uint8_t perf_mode(const UIStateMachine *sm)
{
    uint8_t m = sm->perf->perf_mode;
    return m < PERF_COUNT ? m : (uint8_t)PERF_SINGL_SYNTH;
}

uint8_t ui_sm_key_channel(const UIStateMachine *sm)
{
    uint8_t l = (uint8_t)(perf_mode(sm) == PERF_DUAL_SYNTH && sm->s.active_synth_layer == 1u);
    return (uint8_t)(sm->perf->midi_ch[l] & 0x0Fu);
}

static const UiPage *ui_cur_page(const UIStateMachine *sm) { return &UI_PAGES[sm->page]; }

static SequencerLane *cur_lane(const UIStateMachine *sm) { return &sm->lanes[sm->s.active_seq_lane]; }

static uint8_t layer_arch(const UIStateMachine *sm, uint8_t layer)
{
    return varch_of(sm->engine[layer < NLAYER ? layer : 0u]);
}

/* a row shows when the page is not a sound design page, or the focused layer's architecture uses it. A MOD page
 * (2026-10-17) shows every destination the architecture uses, routed here or not, so it can route the slot to any of
 * them (mod_claim); the chains (another slot's sources) are not shown */
static uint8_t row_shown(const UIStateMachine *sm, const UiPage *pg, const UiRow *r)
{
    if (pg->store == ST_ENGINE && (r->archs & VARCH_BIT(layer_arch(sm, sm->s.active_synth_layer))) == 0u)
        return 0u;
    return (uint8_t)(r->via == 0u || r->kind != RK_CHAIN);
}

/* a MOD page destination's selector: what feeds it now (UIMOD_*: this slot, another source, none) */
static uint8_t mod_route(const UIStateMachine *sm, const UiRow *r)
{
    uint8_t src, slot = (uint8_t)(sm->page - UIPAGE_MOD1);
    if (r->via == 0u || sm->page < UIPAGE_MOD1 || sm->page > UIPAGE_MOD3)
        return UIMOD_HERE;                           /* (not a destination: its own value) */
    src = ((const uint8_t *)sm->engine[sm->s.active_synth_layer < NLAYER ? sm->s.active_synth_layer : 0u])[r->via - 1u];
    return src == MSRC_MODSLOT1 + slot ? (uint8_t)UIMOD_HERE : src == MSRC_NONE ? (uint8_t)UIMOD_NONE : (uint8_t)UIMOD_OTHER;
}

/* turning an unrouted destination's amount routes this slot to it, from an amount of 0 (what fed it is replaced) */
static void mod_claim(UIStateMachine *sm, const UiRow *r)
{
    uint8_t *e = (uint8_t *)sm->engine[sm->s.active_synth_layer < NLAYER ? sm->s.active_synth_layer : 0u];
    if (mod_route(sm, r) == UIMOD_HERE)
        return;
    e[r->via - 1u] = (uint8_t)(MSRC_MODSLOT1 + (sm->page - UIPAGE_MOD1));
    e[r->offset] = (r->flags & RF_BIPOLAR) ? (uint8_t)BIPOLAR_CENTER : 0u;
}

/* the shown rows of the current page: how many, and the i-th (NULL past the end) */
static uint8_t vis_count(const UIStateMachine *sm)
{
    const UiPage *pg = ui_cur_page(sm);
    uint8_t i, n = 0u;
    for (i = 0u; i < pg->nrows; i++)
        if (row_shown(sm, pg, &pg->rows[i]))
            n++;
    return n;
}

static const UiRow *vis_row(const UIStateMachine *sm, uint8_t row)
{
    const UiPage *pg = ui_cur_page(sm);
    uint8_t i;
    for (i = 0u; i < pg->nrows; i++)
        if (row_shown(sm, pg, &pg->rows[i]) && row-- == 0u)
            return &pg->rows[i];
    return NULL;
}

static uint8_t *page_block_for(const UIStateMachine *sm, const UiPage *pg, uint8_t layer)
{
    switch (pg->store) {
    case ST_ENGINE: return (uint8_t *)sm->engine[layer < NLAYER ? layer : 0u] + pg->base;
    case ST_PERF: return (uint8_t *)sm->perf + pg->base;
    case ST_ARP: return (uint8_t *)sm->arp;
    case ST_LANE: return (uint8_t *)cur_lane(sm);
    default: return 0;
    }
}

static uint8_t *page_block(const UIStateMachine *sm, const UiPage *pg)
{
    return page_block_for(sm, pg, sm->s.active_synth_layer);
}

static uint8_t row_layer(const UiRow *r) { return (r->flags & RF_LAYER2) != 0u ? 1u : 0u; }

/* TARGET (7.5): may lane l's target be t? A layer target: L2 and BOTH need a second layer. A lane bitmask: any. A single
 * lane: TRANSPOSE moves pitches, so only a NOTE lane; LAYER moves a lane's layer, so only a lane that plays on a layer
 * (NOTE, the controllers, PROG CHANGE); neither can be the lane itself, whose type is neither. */
static uint8_t seq_target_ok(const UIStateMachine *sm, uint8_t l, uint8_t t)
{
    uint8_t ev = sm->lanes[l].event_type, max = seq_target_max(ev), tev;
    if (t > max)
        return 0u;
    if (max == SEQ_TGT_BOTH)
        return (uint8_t)(perf_mode(sm) != PERF_SINGL_SYNTH || t == SEQ_TGT_L1);
    if (max == SEQ_TGT_MASK)
        return 1u;
    tev = sm->lanes[t].event_type;
    return (uint8_t)(t != l && (ev == SEQEV_TRANSPOSE ? tev == SEQEV_NOTE : seq_target_max(tev) == SEQ_TGT_BOTH));
}

uint8_t ui_sm_seq_target_count(const UIStateMachine *sm, uint8_t lane)
{
    uint8_t t, n = 0u, max;
    if (lane >= SEQ_NLANE)
        return 0u;
    max = seq_target_max(sm->lanes[lane].event_type);
    for (t = 0u; t <= max; t++)
        n = (uint8_t)(n + seq_target_ok(sm, lane, t));
    return n;
}

uint8_t ui_sm_seq_target_slots(const UIStateMachine *sm, uint8_t lane, uint8_t *on, uint8_t *ok)
{
    const SequencerLane *ln;
    uint8_t max, i;
    *on = 0u;
    *ok = 0u;
    if (lane >= SEQ_NLANE)
        return 0u;
    ln = &sm->lanes[lane];
    max = seq_target_max(ln->event_type);
    if (max == SEQ_TGT_BOTH)
        return 0u;
    for (i = 0u; i < SEQ_NLANE; i++) {
        if (max == SEQ_TGT_MASK ? ((ln->target_mask >> i) & 1u) != 0u : ln->target_mask == i)
            *on |= (uint8_t)(1u << i);
        if (max == SEQ_TGT_MASK || seq_target_ok(sm, lane, i))
            *ok |= (uint8_t)(1u << i);
    }
    return 1u;
}

/* a TRANSPOSE / LAYER lane left with one possible target is pointed at it (its TARGET row is hidden) */
static void seq_target_settle(UIStateMachine *sm)
{
    uint8_t l, t, n, only;
    for (l = 0u; l < SEQ_NLANE; l++) {
        SequencerLane *ln = &sm->lanes[l];
        if (seq_target_max(ln->event_type) != SEQ_NLANE - 1u || seq_target_ok(sm, l, ln->target_mask))
            continue;
        for (t = 0u, n = 0u, only = 0u; t < SEQ_NLANE; t++)
            if (seq_target_ok(sm, l, t)) {
                n++;
                only = t;
            }
        if (n == 1u)
            ln->target_mask = only;
    }
}

static uint8_t row_max(const UIStateMachine *sm, const UiRow *r)
{
    return r->kind == RK_SEQ_TARGET ? seq_target_max(cur_lane(sm)->event_type) : r->max;
}

/* values a row may hold under the current mode; the Algorithm Encoder steps over the rest */
static uint8_t value_allowed(const UIStateMachine *sm, const UiRow *r, uint8_t v)
{
    uint8_t mode = perf_mode(sm);
    switch (r->kind) {
    case RK_SEQ_EVENT:
        return seq_event_available(v, mode);
    case RK_SEQ_TARGET:              /* a layer needs a second layer, TRANSPOSE a NOTE lane, LAYER a layer lane */
        return seq_target_ok(sm, sm->s.active_seq_lane, v);
    case RK_ARP_LAYER:
        return v == ARPLAYER_L1 || (v == ARPLAYER_L2 && mode != PERF_SINGL_SYNTH) ||
               (v == ARPLAYER_BOTH && mode == PERF_DUAL_SYNTH);
    case RK_DEPTH_SRC:               /* an LFO or a MOD slot would scale the LFOs it is worked out before */
        return (uint8_t)(v < MSRC_LFO1 || v > MSRC_MODSLOT3);
    case RK_SAVE_ACTION:             /* PASTE needs a clipboard of the TARGET's kind */
        return (uint8_t)(v != UIACT_PASTE || ui_sm_paste_ok(sm));
    default:
        return 1u;
    }
}

static uint8_t row_get(const UIStateMachine *sm, const UiRow *r)
{
    if (r->via != 0u && mod_route(sm, r) != UIMOD_HERE)
        return (r->flags & RF_BIPOLAR) ? (uint8_t)BIPOLAR_CENTER : 0u;   /* not fed by this slot: none of it */
    switch (r->kind) {
    case RK_SEQ_STEPS: return sm->seq_step0;
    case RK_PERF_SLOT: return sm->perf_slot;
    case RK_SAVE_ACTION: return sm->save_action;
    case RK_THEME: return sm->theme;
    case RK_INST: return ((const uint8_t *)ui_inst(sm))[r->offset];
    case RK_RENAME: case RK_CHAIN: return 0u;
    case RK_ENG: return ((const uint8_t *)sm->engine[row_layer(r)])[r->offset];
    case RK_VOICES: return ui_sm_layer_voices(sm, row_layer(r));
    case RK_ARP_LAYER: return ARP_LAYER(sm->arp);
    case RK_ARP_TIMING: return ARP_TIMING(sm->arp);
    case RK_PINGPONG: return (uint8_t)(page_block(sm, ui_cur_page(sm))[r->offset] & 1u);
    case RK_DELAY_HP: return (uint8_t)(page_block(sm, ui_cur_page(sm))[r->offset] >> 1);
    default: return page_block(sm, ui_cur_page(sm))[r->offset];
    }
}

static void row_set(UIStateMachine *sm, const UiRow *r, uint8_t v)
{
    switch (r->kind) {
    case RK_PERF_SLOT: sm->perf_slot = v; break;
    case RK_SAVE_ACTION: sm->save_action = v; break;
    case RK_THEME: sm->theme = v; break;
    case RK_INST: ((uint8_t *)ui_inst(sm))[r->offset] = v; break;
    case RK_RENAME: case RK_VOICES: case RK_CHAIN: case RK_SEQ_STEPS: break;
    case RK_ENG: ((uint8_t *)sm->engine[row_layer(r)])[r->offset] = v; break;
    case RK_SEQ_EVENT: seq_lane_set_event(cur_lane(sm), v); break;   /* also restarts TARGET: */
    case RK_ARP_LAYER: arp_set_layer(sm->arp, v); break;
    case RK_ARP_TIMING: arp_set_timing(sm->arp, v); break;
    case RK_PINGPONG: case RK_DELAY_HP: {
        uint8_t *b = &page_block(sm, ui_cur_page(sm))[r->offset];
        *b = r->kind == RK_PINGPONG ? (uint8_t)((*b & 0xFEu) | (v & 1u)) : (uint8_t)((*b & 1u) | (uint8_t)(v << 1));
        break;
    }
    default: page_block(sm, ui_cur_page(sm))[r->offset] = v; break;
    }
}

/* Algorithm Encoder: one detent = one allowed value; clamps at min / max, never wraps */
static void row_step(UIStateMachine *sm, const UiRow *r, int16_t delta)
{
    int16_t lo = r->min, hi = row_max(sm, r), dir = delta > 0 ? 1 : -1;
    int16_t cur = row_get(sm, r), v, t;
    int16_t n = delta > 0 ? delta : (int16_t)-delta;
    if (r->kind == RK_RENAME || r->kind == RK_VOICES || r->kind == RK_CHAIN || r->kind == RK_SEQ_STEPS)
        return;
    if (r->kind == RK_WAVE) {                 /* by list position: banks, PCM waves, attacks */
        int32_t p = (int32_t)wave_list_pos((uint8_t)cur) + delta, last = (int32_t)wave_list_count() - 1;
        p = p < 0 ? 0 : p > last ? last : p;
        if (wave_list_code((uint32_t)p) != (uint8_t)cur)
            row_set(sm, r, wave_list_code((uint32_t)p));
        return;
    }
    v = cur < lo ? lo : cur > hi ? hi : cur;  /* a stored value out of range (flash) is pulled in first */
    while (n-- > 0) {
        t = v;
        do
            t = (int16_t)(t + dir);
        while (t >= lo && t <= hi && !value_allowed(sm, r, (uint8_t)t));
        if (t < lo || t > hi)
            break;
        v = t;
    }
    if (v != cur)
        row_set(sm, r, (uint8_t)v);
}

/* the voice mode a layer plays (its own in SPLIT too since 2026-10-25) */
static uint8_t layer_vmode(const UIStateMachine *sm, uint8_t layer)
{
    return sm->perf->voice_mode[layer < NLAYER ? layer : 0u];
}

/* --------------------------------------------- rule 3: Layer 2 mask --- */
uint8_t ui_sm_row_state(const UIStateMachine *sm, uint8_t row)
{
    const UiPage *pg = ui_cur_page(sm);
    const UiRow *r = vis_row(sm, row);
    uint8_t mode = perf_mode(sm), flags;
    if (r == NULL)
        return UIROW_GREYED;
    flags = r->flags;
    /* SINGL_SYNTH: every Layer 2 parameter, and a sound design page pointed at layer 2, is un-highlightable */
    if (mode == PERF_SINGL_SYNTH &&
        ((flags & RF_LAYER2) != 0u || (pg->store == ST_ENGINE && sm->s.active_synth_layer != 0u)))
        return UIROW_GREYED;
    if ((flags & RF_UNISON) != 0u && layer_vmode(sm, row_layer(r)) != VMODE_UNISON_STACK)
        return UIROW_GREYED;
    if (r->kind == RK_VOICES || r->kind == RK_CHAIN)
        return UIROW_INFO;
    if (r->kind == RK_RENAME && ui_sm_rename_name(sm) == NULL)
        return UIROW_GREYED;         /* (every TARGET carries a name since 2026-10-25) */
    if (r->kind == RK_SEQ_TARGET && ui_sm_seq_target_count(sm, sm->s.active_seq_lane) <= 1u)
        return UIROW_HIDDEN;         /* 7.4: one option (a layer under SINGL) or none: nothing to pick */
    if (pg->store == ST_LANE && r->kind == RK_BYTE &&
        (r->offset == OFS(SequencerLane, scale) || r->offset == OFS(SequencerLane, scale_root))) {
        const SequencerLane *ln = cur_lane(sm);
        if (ln->event_type != SEQEV_NOTE)
            return UIROW_HIDDEN;     /* SCALE: a NOTE lane's; ROOT: a scale's that has no root of its own */
        if (r->offset == OFS(SequencerLane, scale_root) && (ln->scale == SEQSCALE_OFF || ln->scale == SEQSCALE_GENRE))
            return UIROW_HIDDEN;
    }
    return UIROW_ACTIVE;
}

/* the first highlightable row from `from` walking in dir (+1 / -1), UI_FOCUS_NONE when there is none */
static uint8_t cursor_seek(const UIStateMachine *sm, int16_t from, int16_t dir)
{
    int16_t i, n = vis_count(sm);
    for (i = from; i >= 0 && i < n; i = (int16_t)(i + dir))
        if (ui_sm_row_state(sm, (uint8_t)i) == UIROW_ACTIVE)
            return (uint8_t)i;
    return UI_FOCUS_NONE;
}

/* keep the cursor where it is if it may stay there, else the nearest highlightable row below, then above */
static void cursor_snap(UIStateMachine *sm)
{
    uint8_t n = vis_count(sm), f = sm->s.current_param_focus, to;
    if (f < n && ui_sm_row_state(sm, f) == UIROW_ACTIVE)
        return;
    if (f >= n)
        f = f == UI_FOCUS_NONE || n == 0u ? 0u : (uint8_t)(n - 1u);
    to = cursor_seek(sm, f, 1);
    sm->s.current_param_focus = to != UI_FOCUS_NONE ? to : cursor_seek(sm, f, -1);
}

/* Select Encoder: each detent moves to the next highlightable row, greyed rows are skipped; stops at the ends */
static void cursor_move(UIStateMachine *sm, int16_t delta)
{
    int16_t dir = delta > 0 ? 1 : -1, n = delta > 0 ? delta : (int16_t)-delta;
    uint8_t next;
    cursor_snap(sm);
    if (sm->s.current_param_focus == UI_FOCUS_NONE)
        return;
    while (n-- > 0) {
        next = cursor_seek(sm, (int16_t)(sm->s.current_param_focus + dir), dir);
        if (next == UI_FOCUS_NONE)
            break;
        sm->s.current_param_focus = next;
    }
}

/* ------------------------------------------------------- navigation --- */
static void sync_registers(UIStateMachine *sm)
{
    sm->s.current_context = ui_cur_page(sm)->ctx;
    sm->s.save_submenu_page = sm->page == UIPAGE_SAVE_COMMIT ? (uint8_t)SAVEPAGE_SAVE : (uint8_t)SAVEPAGE_PRESET;
}

static void ui_arp_chord_off(UIStateMachine *sm);
static void arp_unpeek(UIStateMachine *sm);

static void enter_page(UIStateMachine *sm, uint8_t page)
{
    if (page != sm->page || page != UIPAGE_SAVE_COMMIT)
        sm->save_action = page == UIPAGE_SAVE_COMMIT ? (uint8_t)UIACT_SAVE : (uint8_t)UIACT_NO;
                                     /* the save page arms SAVE (its button flashes: one press asks to save); ACTION
                                      * is never left armed off it */
    sm->page = page;
    sm->browse = 0u;                 /* the preset list belongs to the page it opened on */
    sm->rowpop = 0u;                 /* .. and so does the parameter list */
    sm->kb_ms = 0u;                  /* .. and SAVE's keyboard */
    sm->s.current_param_focus = cursor_seek(sm, 0, 1);
    sm->seq_cap_held = 0u;           /* a 7.2 chord capture ends with the page */
    sm->seq_cap_n = 0u;
    sm->arp_held = 0u;               /* .. and so do the ARP's held step keys, function keys, REC and its sounding chord */
    arp_unpeek(sm);
    sm->arp_fn = 0u;
    sm->arp_copy_src = CARP_POS_NONE;
    sm->arp_rec = 0u;
    sm->arp_chord_down = CARP_NOTE_NONE;
    if (sm->arp_chord_key != CARP_NOTE_NONE) {
        if (sm->arp_latch && sm->arp_chord_hold) {
            if (sm->carp != 0)
                carp_latch_up(sm->carp, sm->arp_chord_key);   /* a latched arpeggio plays on under every page */
        } else {
            ui_arp_chord_off(sm);
        }
    }
    sync_registers(sm);
}

uint8_t ui_sm_home_view_count(uint8_t mode)
{
    return mode == PERF_DUAL_SYNTH || mode == PERF_SPLIT_SYNTH ? (uint8_t)UI_HOME_NVIEW_MULTI
                                                               : (uint8_t)UI_HOME_NVIEW_SINGLE;
}

static uint8_t in_section(uint8_t page, uint8_t btn)
{
    return page >= SECTION_FIRST[btn] && page < SECTION_FIRST[btn] + SECTION_COUNT[btn];
}

/* the first page of the section a page belongs to (always shown) */
static uint8_t section_first_of(uint8_t page)
{
    uint8_t b;
    for (b = 0u; b < BTN_COUNT; b++)
        if (in_section(page, b))
            return SECTION_FIRST[b];
    return UIPAGE_HOME1;
}

static void on_section(UIStateMachine *sm, uint8_t btn);

/* rule 1 (5.3, 2026-10-17 order): HOME opens LAYER, then cycles ZONES (View 3, DUAL / SPLIT) -> the performance
 * browser -> GLOBAL -> LAYER. LAYER is View 1, or View 2 while layer 2 is in focus (ui_sm_page_visible,
 * ui_sm_enforce). GLOBAL moved here from GLO 2026-10-16, the browser from SAVE 2026-10-17 */
static void on_home(UIStateMachine *sm) { on_section(sm, BTN_HOME); }

/* 6.2.3 COPY: the live item of the TARGET's kind onto the clipboard (a patch: the focused layer's) */
static void clip_copy(UIStateMachine *sm)
{
    uint8_t layer = sm->s.active_synth_layer, i;
    UiClipboard *c = &sm->clip;
    switch (sm->save_target) {
    case UISAVE_PATCH_PRESET: c->patch = *sm->engine[layer]; break;
    case UISAVE_SEQ_SETTINGS:
        for (i = 0u; i < SEQ_NLANE; i++) c->seq[i] = sm->lanes[i];
        for (i = 0u; i < NAME_LEN; i++) sm->clip_name[i] = sm->seq_name[i];
        break;
    case UISAVE_ARP_SEQ:
        c->arp = *sm->arp;
        for (i = 0u; i < NAME_LEN; i++) sm->clip_name[i] = sm->arp_name[i];
        break;
    default:
        c->whole.perf = *sm->perf;
        for (i = 0u; i < NLAYER; i++) c->whole.engine[i] = *sm->engine[i];
        for (i = 0u; i < SEQ_NLANE; i++) c->whole.seq[i] = sm->lanes[i];
        c->whole.arp = *sm->arp;
        break;
    }
    sm->clip_kind = sm->save_target;
    sm->clip_layer = layer;
}

/* 6.2.3 PASTE: the clipboard over the live item (a patch: into the focused layer). Flash is not touched: SAVE writes */
static void clip_paste(UIStateMachine *sm)
{
    uint8_t i;
    const UiClipboard *c = &sm->clip;
    if (!ui_sm_paste_ok(sm))
        return;
    switch (sm->clip_kind) {
    case UISAVE_PATCH_PRESET: *sm->engine[sm->s.active_synth_layer] = c->patch; break;
    case UISAVE_SEQ_SETTINGS:
        for (i = 0u; i < SEQ_NLANE; i++) sm->lanes[i] = c->seq[i];
        for (i = 0u; i < NAME_LEN; i++) sm->seq_name[i] = sm->clip_name[i];
        break;
    case UISAVE_ARP_SEQ:
        *sm->arp = c->arp;
        for (i = 0u; i < NAME_LEN; i++) sm->arp_name[i] = sm->clip_name[i];
        break;
    default:
        *sm->perf = c->whole.perf;
        for (i = 0u; i < NLAYER; i++) *sm->engine[i] = c->whole.engine[i];
        for (i = 0u; i < SEQ_NLANE; i++) sm->lanes[i] = c->whole.seq[i];
        *sm->arp = c->whole.arp;
        break;
    }
}

uint8_t ui_sm_save_kind(const UIStateMachine *sm)
{
    return sm->page == UIPAGE_SAVE_COMMIT ? sm->save_target : ui_sm_preset_target(sm);
}

/* the save page, on what this page would save and the slot it plays from (2026-10-17) */
static void save_open(UIStateMachine *sm)
{
    uint8_t target = ui_sm_preset_target(sm), slot = ui_sm_preset_slot(sm);
    sm->save_from = sm->page;
    enter_page(sm, UIPAGE_SAVE_COMMIT);
    sm->save_target = target;
    sm->save_slot = slot < ui_sm_bank_slots(target) ? slot : 0u;
}

/* rule 2 (6, 2026-10-17): from another page SAVE opens the save page on what that page saves (a sound page: the
 * focused layer's patch; HOME: the performance; SEQ / ARP: theirs) at the slot it plays from. On the save page SAVE
 * stores: at once into an empty slot, after the confirm popup into a used (or unknown) one. COPY / PASTE are SAVE held
 * + OCT- / OCT+ (ui_sm_save_oct) */
static void on_save(UIStateMachine *sm)
{
    char n[NAME_LEN + 1];
    if (sm->page != UIPAGE_SAVE_COMMIT) {
        save_open(sm);
        return;
    }
    sm->save_action = UIACT_SAVE;
    if (ui_sm_slot_state(sm, n) == UISLOT_EMPTY) {
        sm->dialog = UIDLG_WRITING;
        sm->commit_pending = 1u;
    } else {
        sm->dialog = UIDLG_CONFIRM;
        sm->dialog_choice = 0u;      /* the popup opens on NO */
    }
}

void ui_sm_set_hold(UIStateMachine *sm, uint8_t btn)
{
    sm->hold = btn == BTN_HOME || btn == BTN_SAVE ? btn : (uint8_t)BTN_COUNT;
}

void ui_sm_save_oct(UIStateMachine *sm, uint8_t up)
{
    if (sm->dialog != UIDLG_NONE)
        return;
    if (sm->page != UIPAGE_SAVE_COMMIT)
        sm->save_target = ui_sm_preset_target(sm);   /* (save_open sets it again for a PASTE) */
    if (!up) {
        clip_copy(sm);
        return;
    }
    if (!ui_sm_paste_ok(sm))
        return;
    if (sm->page != UIPAGE_SAVE_COMMIT)
        save_open(sm);
    sm->save_action = UIACT_PASTE;
    sm->dialog = UIDLG_CONFIRM;
    sm->dialog_choice = 0u;
}

/* 6.2.2 step 6: the second SAVE press registers the popup choice */
static void confirm_register(UIStateMachine *sm)
{
    if (sm->dialog_choice && sm->save_action == UIACT_SAVE) {
        sm->dialog = UIDLG_WRITING;
        sm->commit_pending = 1u;
        return;
    }
    if (sm->dialog_choice && sm->save_action == UIACT_PASTE)
        clip_paste(sm);
    sm->dialog = UIDLG_NONE;         /* back to submenu 2, ACTION back to its default, SAVE */
    sm->save_action = UIACT_SAVE;
}

/* 2.1: first press from another section opens submenu 1, presses within the section cycle its submenus, skipping
 * the pages the focused layer's architecture hides (4.0) */
static void on_section(UIStateMachine *sm, uint8_t btn)
{
    uint8_t first = SECTION_FIRST[btn], n = SECTION_COUNT[btn], k, p;
    if (!in_section(sm->page, btn)) {
        enter_page(sm, first);
        return;
    }
    for (k = 1u; k <= n; k++) {
        p = (uint8_t)(first + (sm->page - first + k) % n);
        if (ui_sm_page_visible(sm, p, sm->s.active_synth_layer)) {
            enter_page(sm, p);
            return;
        }
    }
}

/* SEQ (2026-10-21): from another page, lane 1 with steps 1..4 on the knobs; each press on the page, the next lane
 * (1 -> 2 -> 3 -> 4 -> 1), the same steps on the knobs and the cursor where it was */
static void on_seq(UIStateMachine *sm)
{
    if (sm->page != UIPAGE_SEQ) {
        sm->s.active_seq_lane = 0u;
        sm->seq_step0 = 0u;
        enter_page(sm, UIPAGE_SEQ);
        return;
    }
    sm->s.active_seq_lane = (uint8_t)((sm->s.active_seq_lane + 1u) % SEQ_NLANE);
}

/* SEQ's Select Encoder (2026-10-21): on STEPS each detent moves the knob window to the next group of 4 (1..4, 5..8,
 * 9..12, 13..16; a window a step key left between them goes to the group after it, or back to the one it starts in);
 * past 13..16 the cursor goes on through the footer's fields (SEQ_ROWS), and past the last back to 1..4. Turning back
 * walks the same ring the other way, onto STEPS at 13..16. */
static void seq_cursor_move(UIStateMachine *sm, int16_t delta)
{
    int16_t dir = delta > 0 ? 1 : -1, n = delta > 0 ? delta : (int16_t)-delta;
    uint8_t f, g;
    cursor_snap(sm);
    if (sm->s.current_param_focus == UI_FOCUS_NONE)
        return;
    while (n-- > 0) {
        f = sm->s.current_param_focus;
        g = sm->seq_step0;
        if (f == 0u && dir > 0 && g < UISEQ_STEP0_MAX) {
            sm->seq_step0 = (uint8_t)((g / 4u + 1u) * 4u);
        } else if (f == 0u && dir < 0 && g > 0u) {
            sm->seq_step0 = (uint8_t)(g % 4u ? g / 4u * 4u : g - 4u);
        } else {
            f = cursor_seek(sm, (int16_t)(f + dir), dir);
            if (f == UI_FOCUS_NONE)          /* past either end: round to the other */
                f = dir > 0 ? 0u : cursor_seek(sm, (int16_t)(vis_count(sm) - 1u), -1);
            if (f == 0u)                     /* onto STEPS: at its first group going on, its last coming back */
                sm->seq_step0 = dir > 0 ? 0u : (uint8_t)UISEQ_STEP0_MAX;
            sm->s.current_param_focus = f;
        }
    }
}

/* -------------------------------------------------------------- SEQ --- */
uint8_t ui_sm_seq_recording(const UIStateMachine *sm) { return (uint8_t)(sm->seq != 0 && maq_recording(sm->seq)); }

/* 7.2 REC: free running halts on the active lane's playhead (stopped: its loop start); REC again resumes */
static void seq_record_toggle(UIStateMachine *sm)
{
    uint8_t lo, hi, p;
    if (sm->seq == 0)
        return;
    if (maq_recording(sm->seq)) {
        maq_set_record(sm->seq, 0u);
        return;
    }
    maq_loop_window(cur_lane(sm), &lo, &hi);
    p = maq_lane_pos(sm->seq, sm->s.active_seq_lane);
    maq_set_rec_step(sm->seq, p < SEQ_NSTEP ? p : lo);   /* the step first: the engine reads both on one frame */
    maq_set_record(sm->seq, 1u);
    sm->seq_cap_held = 0u;
    sm->seq_cap_n = 0u;
}

void ui_sm_seq_knob_target(const UIStateMachine *sm, uint8_t knob, uint8_t *lane, uint8_t *step)
{
    knob &= 3u;
    if (ui_sm_seq_recording(sm)) {                   /* 7.2: knob n = lane n at the record step */
        *lane = knob;
        *step = maq_rec_step(sm->seq);
    } else {                                         /* 7.1: knob n = the window's step n in the active lane */
        *lane = sm->s.active_seq_lane < SEQ_NLANE ? sm->s.active_seq_lane : (uint8_t)0u;
        *step = (uint8_t)((sm->seq_step0 <= UISEQ_STEP0_MAX ? sm->seq_step0 : 0u) + knob);
    }
}

/* "STEP 05" (live) or "LANE 1" (step record): the knob frame's labels */
static char seq_lbl[4][8];
static const char *seq_knob_label(const UIStateMachine *sm, uint8_t knob)
{
    static const char step_txt[] = "STEP ", lane_txt[] = "LANE ";
    uint8_t l, st, i;
    char *b = seq_lbl[knob & 3u];
    ui_sm_seq_knob_target(sm, knob, &l, &st);
    if (ui_sm_seq_recording(sm)) {
        for (i = 0u; lane_txt[i]; i++) b[i] = lane_txt[i];
        b[i++] = (char)('1' + l);
    } else {
        for (i = 0u; step_txt[i]; i++) b[i] = step_txt[i];
        b[i++] = (char)('0' + (st + 1u) / 10u);
        b[i++] = (char)('0' + (st + 1u) % 10u);
    }
    b[i] = '\0';
    return b;
}

/* a lane's scale (seq_scale.h): 0 for none, or not a NOTE lane */
static uint16_t lane_scale(const UIStateMachine *sm, uint8_t l)
{
    return seq_lane_scale_mask(&sm->lanes[l], sm->arp ? sm->arp->current_genre_bank : 0u);
}

uint8_t ui_sm_seq_note_shown(const UIStateMachine *sm, uint8_t lane, uint8_t note)
{
    return lane < SEQ_NLANE ? seq_scale_snap(note, lane_scale(sm, lane)) : note;
}

const char *ui_sm_seq_scale_name(uint8_t scale) { return scale < SEQSCALE_COUNT ? SEQSCALE_NAMES[scale] : ""; }
const char *ui_sm_seq_root_name(uint8_t root) { return ROOT_NAMES[root % 12u]; }

/* a NOTE lane's knob shows the note that plays: under a scale, a stored note outside it as the scale holds it */
uint8_t ui_sm_seq_knob_get(const UIStateMachine *sm, uint8_t knob)
{
    uint8_t l, st, v;
    const SequencerStep *s;
    ui_sm_seq_knob_target(sm, knob, &l, &st);
    s = &sm->lanes[l].steps[st];
    v = sm->lanes[l].event_type == SEQEV_NOTE ? seq_scale_snap(s->note_buffer[0], lane_scale(sm, l)) : s->value;
    return v > VAL_MAX ? (uint8_t)VAL_MAX : v;
}

/* 7.2: a knob turn overwrites the step at once; on a NOTE lane the other notes of a chord follow the first */
void ui_sm_seq_knob_set(UIStateMachine *sm, uint8_t knob, uint8_t value)
{
    uint8_t l, st, i, old;
    int16_t d, n;
    uint16_t sc;
    SequencerStep *s;
    ui_sm_seq_knob_target(sm, knob, &l, &st);
    s = &sm->lanes[l].steps[st];
    if (value > VAL_MAX)
        value = VAL_MAX;
    if (sm->lanes[l].event_type != SEQEV_NOTE) {
        s->value = value;
        return;
    }
    sc = lane_scale(sm, l);
    value = seq_scale_snap(value, sc);               /* SCALE: never a note outside it (TIE passes) */
    old = seq_scale_snap(s->note_buffer[0], sc);
    if (old < SEQ_NOTE_TIE && value != SEQ_NOTE_TIE) {   /* from or to TIE there is no interval to keep */
        d = (int16_t)((int16_t)value - (int16_t)old);
        for (i = 1u; i < SEQ_NNOTE; i++)
            if (s->note_buffer[i] <= VAL_MAX) {
                n = (int16_t)(s->note_buffer[i] + d);
                n = n < 0 ? 0 : n > (int16_t)VAL_MAX ? (int16_t)VAL_MAX : n;
                s->note_buffer[i] = seq_scale_snap((uint8_t)n, sc);   /* the chord's notes into the scale too */
            }
    }
    s->note_buffer[0] = value;
}

/* a turn: under a scale on a NOTE lane, a detent is a degree of the scale (past its top note: TIE, as the full turn
 * is without one) */
void ui_sm_seq_knob_turn(UIStateMachine *sm, uint8_t knob, int8_t detents)
{
    uint8_t l, st, cur = ui_sm_seq_knob_get(sm, knob);
    int16_t v;
    uint16_t sc;
    ui_sm_seq_knob_target(sm, knob, &l, &st);
    sc = lane_scale(sm, l);
    if (sc != 0u && detents != 0) {
        uint8_t top = seq_scale_snap(SEQ_NOTE_TIE - 1u, sc), n;
        if (cur == SEQ_NOTE_TIE)
            n = detents < 0 ? seq_scale_step(top, (int16_t)(detents + 1), sc, top) : (uint8_t)SEQ_NOTE_TIE;
        else if (detents > 0 && cur >= top)
            n = SEQ_NOTE_TIE;
        else
            n = seq_scale_step(cur, detents, sc, top);
        ui_sm_seq_knob_set(sm, knob, n);
        return;
    }
    v = (int16_t)(cur + detents);
    ui_sm_seq_knob_set(sm, knob, (uint8_t)(v < 0 ? 0 : v > (int16_t)VAL_MAX ? (int16_t)VAL_MAX : v));
}

/* 7.2 keyboard capture: the first key of a chord replaces every NOTE lane's notes at the record step, keys struck
 * while it is still held add to it (up to SEQ_NNOTE, repeats ignored) */
static void seq_capture(UIStateMachine *sm, uint8_t note, uint8_t down)
{
    uint8_t st = maq_rec_step(sm->seq), l, i, first = SEQ_NLANE;
    SequencerStep *s;
    if (!down) {
        if (sm->seq_cap_held) sm->seq_cap_held--;
        return;
    }
    if (sm->seq_cap_held == 0u)
        sm->seq_cap_n = 0u;                          /* every key was up: a new chord */
    if (sm->seq_cap_held != 0xFFu)
        sm->seq_cap_held++;
    if (note >= SEQ_NOTE_TIE)
        note = SEQ_NOTE_TIE - 1u;                    /* 127 in the first slot would read as TIE */
    for (l = 0u; l < SEQ_NLANE && first == SEQ_NLANE; l++)
        if (sm->lanes[l].event_type == SEQEV_NOTE)
            first = l;
    if (first == SEQ_NLANE || sm->seq_cap_n >= SEQ_NNOTE)
        return;
    for (i = 0u; i < sm->seq_cap_n; i++)            /* (as the first lane's scale stores it) */
        if (sm->lanes[first].steps[st].note_buffer[i] == seq_scale_snap(note, lane_scale(sm, first)))
            return;
    for (l = first; l < SEQ_NLANE; l++) {
        if (sm->lanes[l].event_type != SEQEV_NOTE)
            continue;
        s = &sm->lanes[l].steps[st];
        if (sm->seq_cap_n == 0u) {
            for (i = 1u; i < SEQ_NNOTE; i++)
                s->note_buffer[i] = SEQ_NOTE_NULL;
            s->flags &= (uint8_t)~SEQ_FLAG_TIE;      /* a tie would hold the old notes instead */
        }
        s->note_buffer[sm->seq_cap_n] = seq_scale_snap(note, lane_scale(sm, l));   /* each lane its own scale */
    }
    sm->seq_cap_n++;
}

#define NOTE_IS_WHITE(n) ((0xAB5u >> ((n) % 12u)) & 1u)   /* C D E F G A B */

/* the white keys below this one, counted from the lowest key (note - key), whatever note that is: F3 on the FM1 */
static uint8_t seq_whites_below(uint8_t key, uint8_t note)
{
    uint8_t n, w = 0u;
    for (n = (uint8_t)(note - key); n != note; n++)
        w = (uint8_t)(w + NOTE_IS_WHITE(n));
    return w;
}

uint8_t ui_sm_seq_key(UIStateMachine *sm, uint8_t key, uint8_t note, uint8_t down, uint8_t mods)
{
    SequencerLane *ln;
    uint8_t w;
    if (sm->page != UIPAGE_SEQ || sm->dialog != UIDLG_NONE)
        return 0u;
    if (ui_sm_seq_recording(sm)) {                   /* 7.2: the keyboard plays as normal and writes the step */
        seq_capture(sm, note, down);
        return 0u;
    }
    if (key > note || note > VAL_MAX)
        return 0u;
    w = seq_whites_below(key, note);
    if (w >= UISEQ_SELECTOR_KEYS)
        return 0u;                                   /* above the 16 white selector keys: plays */
    if (!NOTE_IS_WHITE(note))
        return 1u;                                   /* 7.1: the black keys among them do nothing */
    if (down) {
        ln = cur_lane(sm);
        if (mods & UISEQ_MOD_OCT_DOWN) {
            ln->loop_start = w;                      /* 7.1: OCT- + white key n: step n is the first */
        } else if (mods & UISEQ_MOD_OCT_UP) {
            ln->loop_end = w;                        /* OCT+ + white key n: the last */
        } else {                                     /* (2026-10-26) white key n: its block of four on the
                                                      * knobs (1..4, 5..8, 9..12, 13..16); the cursor on STEPS */
            sm->seq_step0 = (uint8_t)(w & ~3u);
            sm->s.current_param_focus = 0u;
        }
    }
    return 1u;
}

void ui_sm_attach_seq(UIStateMachine *sm, MaqSeq *seq) { sm->seq = seq; }

const char *ui_sm_seq_ev_name(uint8_t event_type)
{
    return SEQEV_NAMES[event_type < SEQEV_COUNT ? event_type : (uint8_t)SEQEV_NOTE];
}

const char *ui_sm_seq_mode_name(uint8_t mode)
{
    return SEQMODE_NAMES[mode < SEQ_MODE_COUNT ? mode : (uint8_t)SEQ_MODE_FORWARD];
}

const char *ui_sm_seq_target_text(const UIStateMachine *sm, uint8_t lane, char *buf)
{
    static const char *const LAYER_TXT[SEQ_TGT_LAYER_COUNT] = {"[L1]", "[L2]", "[BOTH]"};
    uint8_t on, ok, i, k = 0u, t;
    if (!ui_sm_seq_target_slots(sm, lane, &on, &ok)) {   /* a layer target */
        t = lane < SEQ_NLANE ? sm->lanes[lane].target_mask : (uint8_t)SEQ_TGT_L1;
        return LAYER_TXT[t <= SEQ_TGT_BOTH ? t : (uint8_t)SEQ_TGT_L1];
    }
    for (i = 0u; i < SEQ_NLANE; i++) {               /* 7.4: one slot per lane, "[X][ ][-][ ]" */
        buf[k++] = '[';
        buf[k++] = (on >> i) & 1u ? 'X' : (ok >> i) & 1u ? ' ' : '-';
        buf[k++] = ']';
    }
    buf[k] = '\0';
    return buf;
}

/* OSC FINE TUNE as text: "+125C", "-7C", "0C" (cents, osc_fine_cents) */
const char *ui_sm_fine_text(uint8_t v, char *buf)
{
    int32_t c = osc_fine_cents(v), n = c < 0 ? -c : c, i = 0, d = 100;
    if (c != 0)
        buf[i++] = c < 0 ? '-' : '+';
    while (d > 1 && n < d)
        d /= 10;
    for (; d >= 1; d /= 10)
        buf[i++] = (char)('0' + (n / d) % 10);
    buf[i++] = 'C';
    buf[i] = '\0';
    return buf;
}

const char *ui_sm_note_name(uint8_t note, char *buf)
{
    static const char NAME[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    uint8_t i = 0u, oct;
    const char *n;
    if (note >= SEQ_NOTE_TIE) {
        buf[0] = 'T'; buf[1] = 'I'; buf[2] = 'E'; buf[3] = '\0';
        return buf;
    }
    for (n = NAME[note % 12u]; *n; n++)
        buf[i++] = *n;
    oct = (uint8_t)(note / 12u);                     /* octave + 1: C-1 is note 0 */
    if (oct == 0u) {
        buf[i++] = '-';
        buf[i++] = '1';
    } else {
        buf[i++] = (char)('0' + oct - 1u);
    }
    buf[i] = '\0';
    return buf;
}

/* -------------------------------------------------------------- ARP --- */
void ui_sm_attach_arp(UIStateMachine *sm, CarpSeq *carp)
{
    sm->carp = carp;
    if (carp != 0)
        carp_set_knobs(carp, sm->arp_knob);          /* a held chord key plays the live dials */
}

/* 8.1 step data capture: the chord key's note and the four dials at this moment, into step i (its accent stays) */
static void arp_capture(UIStateMachine *sm, uint8_t i, uint8_t note)
{
    ArpChordStep *s = &sm->arp->steps[i];
    s->chord_root = note;
    s->rate = sm->arp_knob[0];
    s->octave_range = sm->arp_knob[1];
    s->style = sm->arp_knob[2];
    s->variation = sm->arp_knob[3];
}

/* a step key held: its chord step's values onto the knobs, the live dials kept (2026-10-25); a rest or TIE leaves
 * them. The last step key let go (or the page left) puts the live dials back. */
static void arp_peek(UIStateMachine *sm, uint8_t i)
{
    const ArpChordStep *s = &sm->arp->steps[i];
    uint8_t k;
    if (sm->arp_peek || !ARP_ROOT_IS_CHORD(s->chord_root))
        return;
    for (k = 0u; k < 4u; k++)
        sm->arp_live[k] = sm->arp_knob[k];
    sm->arp_knob[0] = s->rate;
    sm->arp_knob[1] = s->octave_range;
    sm->arp_knob[2] = s->style;
    sm->arp_knob[3] = s->variation;
    sm->arp_peek = 1u;
    if (sm->carp != 0)
        carp_set_knobs(sm->carp, sm->arp_knob);
}

static void arp_unpeek(UIStateMachine *sm)
{
    uint8_t k;
    if (!sm->arp_peek)
        return;
    for (k = 0u; k < 4u; k++)
        sm->arp_knob[k] = sm->arp_live[k];
    sm->arp_peek = 0u;
    if (sm->carp != 0)
        carp_set_knobs(sm->carp, sm->arp_knob);
}

/* TIE, CLEAR or ACCENT on step i */
static void arp_step_fn(UIStateMachine *sm, uint8_t fn, uint8_t i)
{
    ArpChordStep *s = &sm->arp->steps[i];
    if (fn == ARPFN_TIE) {
        s->chord_root = ARP_CHORD_TIE;
    } else if (fn == ARPFN_CLEAR) {
        s->chord_root = ARP_CHORD_EMPTY;
        s->accent = 0u;
    } else if (fn == ARPFN_ACCENT) {
        s->accent = (uint8_t)!s->accent;
    }
}

static uint8_t arp_playing(const UIStateMachine *sm) { return (uint8_t)(sm->carp != 0 && carp_playing(sm->carp)); }

/* TRANSPOSE MAQ over an empty pattern (no chord in its SEQ LENGTH steps): nothing to follow but the keybed, so the
 * chord keys transpose the MAQ stopped as well as playing, and a start keeps the key last played */
static uint8_t arp_keys_lead_maq(const UIStateMachine *sm)
{
    uint8_t len = sm->arp->sequence_length, i;
    if (sm->arp->transpose_maq_option != 1u)
        return 0u;
    if (len < 1u) len = 1u;
    if (len > ARP_NSTEP) len = ARP_NSTEP;
    for (i = 0u; i < len; i++)
        if (ARP_ROOT_IS_CHORD(sm->arp->steps[i].chord_root))
            return 0u;
    return 1u;
}

/* the sounding chord key off, a latched one too */
static void ui_arp_chord_off(UIStateMachine *sm)
{
    if (sm->arp_chord_key != CARP_NOTE_NONE && sm->carp != 0)
        carp_audition(sm->carp, sm->arp_chord_key, 0u);
    sm->arp_chord_key = CARP_NOTE_NONE;
    sm->arp_chord_hold = 0u;
}

/* REC on the ARP page: arm (stopped, the next chord goes into step 1) or disarm */
static void arp_record_toggle(UIStateMachine *sm)
{
    sm->arp_rec = (uint8_t)!sm->arp_rec;
    sm->arp_rec_cursor = 0u;
    sm->arp_rec_last = CARP_POS_NONE;
}

/* REC: the step the next recorded chord, TIE or rest goes into. Playing: the nearest step. Stopped: steps 1, 2, 3 ..
 * in turn, SEQ LENGTH growing with them on the first pass (arp_rec_cursor 0..7), staying after a wrap (8..15). */
static uint8_t arp_rec_take(UIStateMachine *sm)
{
    uint8_t i, c = sm->arp_rec_cursor;
    if (arp_playing(sm)) {
        i = sm->carp != 0 ? carp_rec_step(sm->carp) : (uint8_t)CARP_POS_NONE;
        if (i >= ARP_NSTEP)
            i = 0u;                                  /* PLAY not applied by the engine yet: step 1 plays first */
    } else {
        if (c >= 2u * ARP_NSTEP)
            c = 0u;
        i = (uint8_t)(c % ARP_NSTEP);
        if (c < ARP_NSTEP)
            sm->arp->sequence_length = (uint8_t)(i + 1u);
        sm->arp_rec_cursor = (uint8_t)(c + 1u < 2u * ARP_NSTEP ? c + 1u : ARP_NSTEP);
    }
    sm->arp_rec_last = i;
    return i;
}

/* a function key (the black keys among the step keys) */
static void arp_fn_key(UIStateMachine *sm, uint8_t fn, uint8_t down)
{
    uint8_t i;
    if (!down) {
        sm->arp_fn &= (uint8_t)~(1u << fn);
        if (fn == ARPFN_COPY)
            sm->arp_copy_src = CARP_POS_NONE;
        return;
    }
    sm->arp_fn |= (uint8_t)(1u << fn);
    if (fn == ARPFN_LATCH) {
        sm->arp_latch = (uint8_t)!sm->arp_latch;
        if (!sm->arp_latch && sm->arp_chord_key != sm->arp_chord_down)
            ui_arp_chord_off(sm);                       /* the latched chord stops (a key still down plays until let go) */
    } else if (fn == ARPFN_COPY) {
        sm->arp_copy_src = CARP_POS_NONE;
    } else if (sm->arp_held != 0u) {                 /* TIE, CLEAR, ACCENT on the step keys held */
        for (i = 0u; i < ARP_NSTEP; i++)
            if ((sm->arp_held >> i) & 1u)
                arp_step_fn(sm, fn, i);
    } else if (sm->arp_rec) {                        /* .. or recorded: a tie or a rest; ACCENT on the last step recorded */
        if (fn != ARPFN_ACCENT)
            arp_step_fn(sm, fn, arp_rec_take(sm));
        else if (sm->arp_rec_last < ARP_NSTEP)
            arp_step_fn(sm, fn, sm->arp_rec_last);
    }
}

/* step key w: held for a capture, or the target of the function keys held */
static void arp_step_key(UIStateMachine *sm, uint8_t w, uint8_t down)
{
    uint8_t f;
    if (!down) {
        sm->arp_held &= (uint8_t)~(1u << w);
        if (sm->arp_held == 0u)
            arp_unpeek(sm);
        return;
    }
    if ((sm->arp_fn >> ARPFN_COPY) & 1u) {          /* COPY: the first step tapped is the source */
        if (sm->arp_copy_src >= ARP_NSTEP)
            sm->arp_copy_src = w;
        else if (w != sm->arp_copy_src)
            sm->arp->steps[w] = sm->arp->steps[sm->arp_copy_src];
        return;
    }
    if (sm->arp_fn & (uint8_t)(1u << ARPFN_TIE | 1u << ARPFN_CLEAR | 1u << ARPFN_ACCENT)) {
        for (f = ARPFN_TIE; f < ARPFN_COUNT; f++)
            if (f != ARPFN_COPY && ((sm->arp_fn >> f) & 1u))
                arp_step_fn(sm, f, w);
        return;
    }
    sm->arp_held |= (uint8_t)(1u << w);
    arp_peek(sm, w);
}

/* a black key by the white keys below it in the step zone (ui_sm_arp_key, ui_sm_key_leds) */
static const uint8_t ARP_FN_BY_WHITE[CARP_STEP_KEYS] = {
    ARPFN_NONE, ARPFN_LATCH, ARPFN_TIE, ARPFN_NONE, ARPFN_CLEAR, ARPFN_COPY, ARPFN_NONE, ARPFN_ACCENT
};

uint8_t ui_sm_arp_key(UIStateMachine *sm, uint8_t key, uint8_t note, uint8_t down, uint8_t vel)
{
    uint8_t n, w = 0u, i, rec;
    if (sm->page != UIPAGE_ARP || sm->dialog != UIDLG_NONE || key > note || note > VAL_MAX)
        return 0u;
    if (key < CARP_ZONE_KEYS) {                      /* chord key */
        if (note >= ARP_CHORD_TIE)
            note = ARP_CHORD_TIE - 1u;               /* 126 marks a TIE, 127 a rest */
        if (!down) {
            if (note == sm->arp_chord_down)
                sm->arp_chord_down = CARP_NOTE_NONE;
            if (note == sm->arp_chord_key) {         /* (an older key released after a newer one: nothing sounds) */
                if (sm->arp_latch && sm->arp_chord_hold) {
                    if (sm->carp != 0)
                        carp_latch_up(sm->carp, note);   /* latched: it plays on */
                } else {
                    ui_arp_chord_off(sm);
                }
            }
            return 1u;
        }
        sm->arp_chord_down = note;
        rec = (uint8_t)(sm->arp_held == 0u && sm->arp_rec);
        if (sm->arp_held != 0u) {
            for (i = 0u; i < ARP_NSTEP; i++)
                if ((sm->arp_held >> i) & 1u)
                    arp_capture(sm, i, note);
        } else if (rec) {
            arp_capture(sm, arp_rec_take(sm), note);     /* REC: into the nearest step, or the next in turn */
        } else if (sm->carp != 0 && (arp_playing(sm) || arp_keys_lead_maq(sm) ||
                                     (sm->arp->transpose_maq_option == 1u && sm->seq != 0 && maq_playing(sm->seq)))) {
            carp_set_transpose(sm->carp, (int8_t)key);   /* playing: the key transposes the pattern (and the MAQ) */
        }
        sm->arp_chord_key = note;
        sm->arp_chord_hold = (uint8_t)(sm->arp_held == 0u && !rec);
        if (sm->carp == 0)
            return 1u;
        if (!sm->arp_chord_hold)
            carp_audition(sm->carp, note, vel ? vel : (uint8_t)1u);   /* a capture: the chord, as a block */
        else
            carp_hold(sm->carp, note, vel ? vel : (uint8_t)1u);       /* else arpeggiated live while held */
        return 1u;
    }
    for (n = (uint8_t)(note - key + CARP_ZONE_KEYS); n != note; n++)   /* white keys between the zone and this key */
        w = (uint8_t)(w + NOTE_IS_WHITE(n));
    if (w >= CARP_STEP_KEYS)
        return 0u;                                   /* above the 8 step keys: plays */
    if (!NOTE_IS_WHITE(note)) {                      /* a function key (F#4, below step 1, does nothing) */
        if (ARP_FN_BY_WHITE[w] != ARPFN_NONE)
            arp_fn_key(sm, ARP_FN_BY_WHITE[w], down);
        return 1u;
    }
    arp_step_key(sm, w, down);
    return 1u;
}

void ui_sm_knob_written(UIStateMachine *sm, uint8_t page, uint8_t knob)
{
    static const uint8_t lim[4] = {ARPRATE_COUNT - 1u, ARP_NOCT - 1u, ARPSTYLE_COUNT - 1u, ARP_NVAR - 1u};
    uint8_t i, v, held;
    if (page == UIPAGE_SAVE_COMMIT && knob == 0u) {
        ui_sm_enforce(sm);           /* a new TARGET: RENAME greys or returns, PASTE may lose its clipboard */
        return;
    }
    if (page == UIPAGE_GLOBAL && knob == 0u) {
        InstrumentSettings *in = ui_inst(sm);
        if (in->master_tune < MASTER_TUNE_MIN) in->master_tune = MASTER_TUNE_MIN;
        if (in->master_tune > MASTER_TUNE_MAX) in->master_tune = MASTER_TUNE_MAX;
        return;
    }
    if (page == UIPAGE_SAVE_COMMIT) {
        if (sm->save_slot >= ui_sm_bank_slots(sm->save_target))
            sm->save_slot = (uint8_t)(ui_sm_bank_slots(sm->save_target) - 1u);
        if (knob >= 2u)
            sm->kb_ms = UI_KB_MS;    /* the name knobs: the keyboard up while they turn */
        return;
    }
    if (page != UIPAGE_ARP || knob >= 4u)
        return;
    v = sm->arp_knob[knob] <= lim[knob] ? sm->arp_knob[knob] : lim[knob];
    sm->arp_knob[knob] = v;
    if (sm->arp_knob[2] == ARPSTYLE_HOLD && sm->arp_knob[3] >= ARP_NSTRUM) {
        sm->arp_knob[3] = ARP_NSTRUM - 1u;           /* HOLD: RHYTHM is the strum, none / up / down */
        if (knob == 3u)
            v = sm->arp_knob[3];
    }
    if (sm->carp != 0)
        carp_set_knobs(sm->carp, sm->arp_knob);      /* a held chord key follows the dials */
    held = sm->arp_held;
    if (held == 0u && sm->arp_rec && sm->carp != 0 && arp_playing(sm) && carp_pos(sm->carp) < ARP_NSTEP)
        held = (uint8_t)(1u << carp_pos(sm->carp));  /* REC, playing: the knob overdubs the playing step */
    for (i = 0u; i < ARP_NSTEP; i++) {
        ArpChordStep *s = &sm->arp->steps[i];
        if (!((held >> i) & 1u))
            continue;
        switch (knob) {
        case 0: s->rate = v; break;
        case 1: s->octave_range = v; break;
        case 2:
            s->style = v;
            if (v == ARPSTYLE_HOLD && s->variation >= ARP_NSTRUM)
                s->variation = sm->arp_knob[3];      /* (clamped above) */
            break;
        default: s->variation = v; break;
        }
    }
}

/* ------------------------------------------------------------ key LEDs --- */
#define UI_LED_KEYS 32u
enum { LV_OFF, LV_DIM, LV_MID, LV_LIT };

/* the keys of the first n white keys from key `from` (0xFF past the keyboard's reach) */
static void ui_led_whites(uint8_t lowest_note, uint8_t from, uint8_t n, uint8_t *wk)
{
    uint8_t k, w = 0u;
    for (k = 0u; k < n; k++)
        wk[k] = 0xFFu;
    for (k = from; k < UI_LED_KEYS && w < n && (uint16_t)lowest_note + k <= VAL_MAX; k++)
        if (NOTE_IS_WHITE((uint8_t)(lowest_note + k)))
            wk[w++] = k;
}

static void ui_led_put(uint8_t *lv, uint8_t key, uint8_t level)
{
    if (key < UI_LED_KEYS)
        lv[key] = level;
}

/* SEQ (7.1, 7.2): the active lane's steps live, its loop lane's steps with OCT held, the record step's notes in step record */
static void ui_seq_key_leds(const UIStateMachine *sm, uint8_t lowest_note, uint8_t mods, uint8_t *lv)
{
    const SequencerLane *ln = cur_lane(sm);
    const SequencerStep *s;
    uint8_t wk[UISEQ_SELECTOR_KEYS], i, lo, hi, edge, pos, l, n;
    if (ui_sm_seq_recording(sm)) {
        l = sm->s.active_seq_lane;                   /* the active lane's notes, else the first NOTE lane's (7.2 */
        if (ln->event_type != SEQEV_NOTE)            /* writes the same chord into every NOTE lane) */
            for (l = 0u; l < SEQ_NLANE && sm->lanes[l].event_type != SEQEV_NOTE; l++) {}
        if (l >= SEQ_NLANE)
            return;
        s = &sm->lanes[l].steps[maq_rec_step(sm->seq) & (SEQ_NSTEP - 1u)];
        for (i = 0u; i < SEQ_NNOTE; i++) {
            n = s->note_buffer[i];
            if (n == SEQ_NOTE_NULL || (i == 0u && n == SEQ_NOTE_TIE))
                continue;
            if (n >= lowest_note)
                ui_led_put(lv, (uint8_t)(n - lowest_note), LV_LIT);
        }
        return;
    }
    ui_led_whites(lowest_note, 0u, UISEQ_SELECTOR_KEYS, wk);
    if (mods & (UISEQ_MOD_OCT_DOWN | UISEQ_MOD_OCT_UP)) {
        maq_loop_window(ln, &lo, &hi);
        for (i = lo; i <= hi; i++)
            ui_led_put(lv, wk[i], LV_DIM);
        edge = (mods & UISEQ_MOD_OCT_DOWN) ? ln->loop_start : ln->loop_end;   /* as ui_sm_seq_key: OCT- first */
        pos = sm->seq != 0 && maq_playing(sm->seq) ? maq_lane_pos(sm->seq, sm->s.active_seq_lane) : (uint8_t)MAQ_POS_NONE;
        if (edge < SEQ_NSTEP)
            ui_led_put(lv, wk[edge], pos < SEQ_NSTEP ? (uint8_t)LV_MID : (uint8_t)LV_LIT);
        if (pos < SEQ_NSTEP)
            ui_led_put(lv, wk[pos], LV_LIT);
        return;
    }
    maq_loop_window(ln, &lo, &hi);                   /* (2026-10-21) the active lane's steps: the ones that play */
    for (i = lo; i <= hi; i++)                       /* dim, the knob window mid, the playhead lit */
        if (!(ln->steps[i].flags & (SEQ_FLAG_MUTE | SEQ_FLAG_SKIP)))
            ui_led_put(lv, wk[i], LV_DIM);
    for (i = 0u; i < 4u; i++)
        ui_led_put(lv, wk[(sm->seq_step0 + i) & (UISEQ_SELECTOR_KEYS - 1u)], LV_MID);
    pos = sm->seq != 0 && maq_playing(sm->seq) ? maq_lane_pos(sm->seq, sm->s.active_seq_lane) : (uint8_t)MAQ_POS_NONE;
    if (pos < SEQ_NSTEP)
        ui_led_put(lv, wk[pos], LV_LIT);
}

/* ARP (8.1): the step keys, the function keys among them, and the chord key of the transposition */
static void ui_arp_key_leds(const UIStateMachine *sm, uint8_t lowest_note, uint8_t *lv)
{
    uint8_t wk[CARP_STEP_KEYS], i, f, pos = CARP_POS_NONE, len = sm->arp->sequence_length;
    int8_t t;
    if (sm->carp != 0 && arp_playing(sm)) {
        pos = carp_pos(sm->carp);
        t = carp_transpose(sm->carp);
        if (t >= 0 && t < (int8_t)CARP_ZONE_KEYS)
            ui_led_put(lv, (uint8_t)t, LV_MID);         /* the chord key the pattern is transposed to */
    }
    ui_led_whites(lowest_note, CARP_ZONE_KEYS, CARP_STEP_KEYS, wk);
    for (i = 0u; i < CARP_STEP_KEYS; i++) {
        if (i < len && sm->arp->steps[i].chord_root != ARP_CHORD_EMPTY)
            ui_led_put(lv, wk[i], LV_DIM);              /* past SEQ LENGTH, and a rest: dark */
        if (pos == CARP_POS_NONE && sm->arp_rec && i == sm->arp_rec_cursor % ARP_NSTEP)
            ui_led_put(lv, wk[i], LV_MID);              /* step entry: where the next chord goes */
        if (i == pos || ((sm->arp_held >> i) & 1u) || i == sm->arp_copy_src)
            ui_led_put(lv, wk[i], LV_LIT);
        f = ARP_FN_BY_WHITE[i];                      /* the black key after white step key i - 1 */
        if (f == ARPFN_NONE || i == 0u || wk[i - 1u] >= UI_LED_KEYS - 1u ||
            NOTE_IS_WHITE((uint8_t)(lowest_note + wk[i - 1u] + 1u)))
            continue;
        ui_led_put(lv, (uint8_t)(wk[i - 1u] + 1u),
                (f == ARPFN_LATCH ? sm->arp_latch : (sm->arp_fn >> f) & 1u) ? (uint8_t)LV_LIT : (uint8_t)LV_DIM);
    }
}

uint8_t ui_sm_key_leds(const UIStateMachine *sm, uint8_t lowest_note, uint8_t mods, UiKeyLeds *out)
{
    uint8_t lv[UI_LED_KEYS], k;
    out->lit = out->mid = out->dim = 0u;
    if (sm->dialog != UIDLG_NONE || lowest_note > VAL_MAX || (sm->page != UIPAGE_SEQ && sm->page != UIPAGE_ARP))
        return 0u;
    for (k = 0u; k < UI_LED_KEYS; k++)
        lv[k] = LV_OFF;
    if (sm->page == UIPAGE_SEQ)
        ui_seq_key_leds(sm, lowest_note, mods, lv);
    else
        ui_arp_key_leds(sm, lowest_note, lv);
    for (k = 0u; k < UI_LED_KEYS; k++) {
        if (lv[k] == LV_LIT) out->lit |= 1u << k;
        else if (lv[k] == LV_MID) out->mid |= 1u << k;
        else if (lv[k] == LV_DIM) out->dim |= 1u << k;
    }
    return 1u;
}

/* ----------------------------------------------------- Preset Encoder --- */
uint8_t ui_sm_preset_target(const UIStateMachine *sm)
{
    if (sm->page == UIPAGE_SEQ) return UISAVE_SEQ_SETTINGS;
    if (sm->page == UIPAGE_ARP) return UISAVE_ARP_SEQ;
    if (in_section(sm->page, BTN_HOME)) return UISAVE_WHOLE_PERF;
    return UISAVE_PATCH_PRESET;
}

uint8_t ui_sm_preset_slot(const UIStateMachine *sm)
{
    switch (ui_sm_preset_target(sm)) {
    case UISAVE_SEQ_SETTINGS: return sm->perf->seq_link;
    case UISAVE_ARP_SEQ: return sm->perf->arp_link;
    case UISAVE_WHOLE_PERF: return sm->perf_slot;
    default: return sm->perf->layer_patch[sm->s.active_synth_layer];
    }
}

/* one detent = the next used slot that way; the walk stops at the bank's ends, on the last used slot it passed */
static void ui_preset_step(UIStateMachine *sm, int8_t detents)
{
    uint8_t target = ui_sm_preset_target(sm), name[NAME_LEN];
    int16_t from = ui_sm_preset_slot(sm), s = from, to = from, dir = detents > 0 ? 1 : -1;
    int16_t n = detents > 0 ? detents : (int16_t)-detents;
    if (sm->load_pending)
        return;
    while (n > 0) {
        s = (int16_t)(s + dir);
        if (s < 0 || s >= (int16_t)ui_sm_bank_slots(target))
            break;
        if (sm->slot_query == 0 || sm->slot_query(sm->slot_ctx, target, (uint8_t)s, name) != UISLOT_EMPTY) {
            to = s;
            n--;
        }
    }
    if (to == from)
        return;
    sm->load_target = target;
    sm->load_slot = (uint8_t)to;
    sm->load_layer = sm->s.active_synth_layer;
    sm->load_pending = 1u;
}

void ui_sm_load_done(UIStateMachine *sm, uint8_t ok)
{
    if (!sm->load_pending)
        return;
    sm->load_pending = 0u;
    if (!ok)
        return;
    switch (sm->load_target) {
    case UISAVE_SEQ_SETTINGS: sm->perf->seq_link = sm->load_slot; break;
    case UISAVE_ARP_SEQ: sm->perf->arp_link = sm->load_slot; break;
    case UISAVE_WHOLE_PERF: sm->perf_slot = sm->load_slot; break;
    default: sm->perf->layer_patch[sm->load_layer] = sm->load_slot; break;
    }
    ui_sm_enforce(sm);               /* a performance brings its mode, a patch its architecture */
}

void ui_sm_tick(UIStateMachine *sm, uint16_t ms)
{
    if (sm->browse) {
        if (ms >= sm->browse_ms)
            sm->browse = 0u;
        else
            sm->browse_ms = (uint16_t)(sm->browse_ms - ms);
    }
    if (sm->kb_ms)
        sm->kb_ms = ms >= sm->kb_ms ? (uint16_t)0u : (uint16_t)(sm->kb_ms - ms);
    if (sm->rowpop) {
        if (ms >= sm->rowpop_ms)
            sm->rowpop = 0u;
        else
            sm->rowpop_ms = (uint16_t)(sm->rowpop_ms - ms);
    }
}

uint8_t ui_sm_save_armed(const UIStateMachine *sm)
{
    return (uint8_t)(sm->dialog == UIDLG_CONFIRM ||
                     (sm->dialog == UIDLG_NONE && sm->page == UIPAGE_SAVE_COMMIT && sm->save_action != UIACT_NO));
}

uint8_t ui_sm_browse_slot(const UIStateMachine *sm)
{
    return sm->load_pending ? sm->load_slot : ui_sm_preset_slot(sm);
}

/* -------------------------------------------------------------- API --- */
void ui_sm_init(UIStateMachine *sm, MasterPerformancePreset *perf, EngineState *layer1,
                EngineState *layer2, SequencerLane *lanes, ArpState *arp)
{
    uint8_t i;
    sm->perf = perf;
    sm->theme = UITHEME_SAMHAIN;
    sm->engine[0] = layer1;
    sm->engine[1] = layer2;
    sm->lanes = lanes;
    sm->arp = arp;
    for (i = 0u; i < NAME_LEN; i++)
        sm->seq_name[i] = sm->arp_name[i] = sm->clip_name[i] = 0u;
    sm->seq = 0;
    sm->seq_step0 = 0u;
    sm->carp = 0;
    sm->arp_knob[0] = ARP_RATE_DEFAULT;
    sm->arp_knob[1] = 0u;
    sm->arp_knob[2] = ARPSTYLE_UP;
    sm->arp_knob[3] = 0u;
    sm->arp_held = 0u;
    sm->arp_peek = 0u;
    sm->arp_chord_key = CARP_NOTE_NONE;
    sm->arp_chord_down = CARP_NOTE_NONE;
    sm->arp_chord_hold = 0u;
    sm->arp_latch = 0u;
    sm->arp_fn = 0u;
    sm->arp_copy_src = CARP_POS_NONE;
    sm->arp_rec = 0u;
    sm->arp_rec_cursor = 0u;
    sm->arp_rec_last = CARP_POS_NONE;
    sm->s.active_synth_layer = 0u;
    sm->s.active_seq_lane = 0u;
    sm->perf_slot = 0u;
    sm->dialog = UIDLG_NONE;
    sm->dialog_choice = 0u;
    sm->commit_pending = 0u;
    sm->load_pending = 0u;
    sm->load_target = UISAVE_PATCH_PRESET;
    sm->load_slot = 0u;
    sm->load_layer = 0u;
    sm->browse = 0u;
    sm->browse_ms = 0u;
    sm->rowpop = 0u;
    sm->rowpop_ms = 0u;
    sm->save_target = UISAVE_WHOLE_PERF;
    sm->save_slot = 0u;
    sm->name_cursor = 0u;
    sm->save_action = UIACT_NO;
    sm->save_from = UIPAGE_PERF;
    sm->hold = BTN_COUNT;
    sm->kb_ms = 0u;
    sm->inst = 0;
    sm->clip_kind = UICLIP_EMPTY;
    sm->clip_layer = 0u;
    sm->slot_query = 0;
    sm->slot_ctx = 0;
    sm->perf_knob[UIPERF_VELOCITY] = 100u;
    sm->perf_knob[UIPERF_BEND] = BIPOLAR_CENTER;
    sm->perf_knob[UIPERF_MODWHEEL] = 0u;
    sm->q_head = 0u;
    sm->q_tail = 0u;
    sm->q_dropped = 0u;
    enter_page(sm, UIPAGE_HOME1);    /* 5: the dashboard is active on boot */
}

/* single producer: post from ISRs of one priority level (or with the others masked) */
uint8_t ui_sm_post(UIStateMachine *sm, uint8_t type, int8_t arg)
{
    uint8_t head = sm->q_head, next = (uint8_t)((head + 1u) & (UI_QUEUE_LEN - 1u));
    if (next == sm->q_tail) {
        if (sm->q_dropped != 0xFFu)
            sm->q_dropped = (uint8_t)(sm->q_dropped + 1u);
        return 0u;
    }
    sm->q_type[head] = type;
    sm->q_arg[head] = arg;
    UI_BARRIER();                    /* the slot is written before the ISR publishes it */
    sm->q_head = next;
    return 1u;
}

void ui_sm_process(UIStateMachine *sm)
{
    uint8_t tail, type;
    int8_t arg;
    while ((tail = sm->q_tail) != sm->q_head) {
        UI_BARRIER();
        type = sm->q_type[tail];
        arg = sm->q_arg[tail];
        UI_BARRIER();                /* the slot is read before it is handed back to the ISR */
        sm->q_tail = (uint8_t)((tail + 1u) & (UI_QUEUE_LEN - 1u));
        ui_sm_handle(sm, type, arg);
    }
}

/* PLAY (2026-10-17): on the SEQ page the MAQ alone, on the ARP page the ARP alone, on any other page both: stopped
 * (both), start both; otherwise stop whichever plays. A start plays the ARP pattern as captured (untransposed), unless
 * the chord keys lead the MAQ (arp_keys_lead_maq). */
static void ui_sm_play(UIStateMachine *sm)
{
    uint8_t seq = (uint8_t)(sm->seq != 0 && maq_playing(sm->seq)), arp = arp_playing(sm);
    uint8_t want_seq = seq, want_arp = arp;
    if (sm->page == UIPAGE_SEQ)
        want_seq = (uint8_t)!seq;
    else if (sm->page == UIPAGE_ARP)
        want_arp = (uint8_t)!arp;
    else
        want_seq = want_arp = (uint8_t)!(seq || arp);
    if (sm->carp != 0) {
        if (want_arp && !arp && !arp_keys_lead_maq(sm))
            carp_set_transpose(sm->carp, 0);
        carp_set_playing(sm->carp, want_arp);
    }
    if (sm->seq != 0)
        maq_set_playing(sm->seq, want_seq);
}

uint8_t ui_sm_play_lit(const UIStateMachine *sm)
{
    uint8_t seq = (uint8_t)(sm->seq != 0 && maq_playing(sm->seq)), arp = arp_playing(sm);
    return sm->page == UIPAGE_SEQ ? seq : sm->page == UIPAGE_ARP ? arp : (uint8_t)(seq || arp);
}

void ui_sm_handle(UIStateMachine *sm, uint8_t type, int8_t arg)
{
    const UiRow *r;
    if (type >= UIEV_COUNT || (type == UIEV_BUTTON ? (uint8_t)arg >= BTN_COUNT :
                               type == UIEV_TRANSPORT ? (uint8_t)arg >= UITR_COUNT : arg == 0))
        return;
    if (type == UIEV_TRANSPORT && arg == UITR_PLAY) {   /* the transport is the music: it works on every page, */
        ui_sm_play(sm);                                 /* through every dialog, and never touches the UI state */
        return;
    }
    if (type != UIEV_PRESET_ENC)
        sm->browse = 0u;             /* any other control puts the preset list away, then does its own job */
    if (type != UIEV_SELECT_ENC && type != UIEV_ALGO_ENC)
        sm->rowpop = 0u;             /* .. and the parameter list */
    if (sm->dialog == UIDLG_WRITING)                 /* nothing interrupts a flash write */
        return;
    if (type == UIEV_TRANSPORT) {                    /* REC: 7.2 on the SEQ page, 8.1 on the ARP page */
        if (sm->dialog == UIDLG_NONE && sm->page == UIPAGE_SEQ)
            seq_record_toggle(sm);
        else if (sm->dialog == UIDLG_NONE && sm->page == UIPAGE_ARP)
            arp_record_toggle(sm);
        return;
    }
    if (sm->dialog == UIDLG_SAVED) {                 /* 6.2.2: any encoder or menu key clears it -> back where SAVE
                                                      * was pressed (2026-10-17; it went to the dashboard) */
        sm->dialog = UIDLG_NONE;
        enter_page(sm, sm->save_from < UIPAGE_COUNT && sm->save_from != UIPAGE_SAVE_COMMIT ? sm->save_from
                                                                                             : (uint8_t)UIPAGE_PERF);
    } else if (sm->dialog == UIDLG_CONFIRM) {        /* the popup owns the Algorithm Encoder and SAVE */
        if (type == UIEV_ALGO_ENC)
            sm->dialog_choice = arg > 0 ? 1u : 0u;
        else if (type == UIEV_BUTTON && arg == BTN_SAVE)
            confirm_register(sm);
    } else if (type == UIEV_SELECT_ENC && sm->page == UIPAGE_SEQ && ui_sm_seq_recording(sm)) {
        maq_rec_nav(sm->seq, sm->s.active_seq_lane, arg);   /* 7.2: the encoder walks the record step */
    } else if (type == UIEV_SELECT_ENC && sm->page == UIPAGE_SEQ) {
        seq_cursor_move(sm, arg);
        sm->rowpop = 0u;
    } else if (type == UIEV_SELECT_ENC) {
        cursor_move(sm, arg);
        sm->rowpop = 0u;             /* (2026-10-17) the cursor moves in the page's own list: no popup */
    } else if (type == UIEV_PRESET_ENC) {
        ui_preset_step(sm, arg);
        sm->browse = 1u;             /* the list opens on (or follows) the slot it lands on */
        sm->browse_ms = UI_BROWSE_MS;
    } else if (type == UIEV_ALGO_ENC && sm->page == UIPAGE_SAVE_COMMIT) {
        int16_t v = (int16_t)(sm->save_slot + arg), top = (int16_t)(ui_sm_bank_slots(sm->save_target) - 1u);
        sm->save_slot = (uint8_t)(v < 0 ? 0 : v > top ? top : v);   /* the slot (the stones and the moon follow it) */
    } else if (type == UIEV_ALGO_ENC) {
        r = sm->s.current_param_focus != UI_FOCUS_NONE ? vis_row(sm, sm->s.current_param_focus) : NULL;
        if (r != NULL && r->kind != RK_SEQ_STEPS) {   /* (STEPS has no value: no popup either) */
            mod_claim(sm, r);            /* a MOD page: an unrouted destination is routed here first */
            row_step(sm, r, arg);
            sm->rowpop = 1u;         /* (2026-10-17) the parameter, large, only while its value turns */
            sm->rowpop_ms = UI_ROWPOP_MS;
        }
    } else if (arg == BTN_HOME) {
        on_home(sm);
    } else if (arg == BTN_SAVE) {
        on_save(sm);
    } else if (arg == BTN_SEQ) {
        on_seq(sm);
    } else {
        on_section(sm, (uint8_t)arg);
    }
    ui_sm_enforce(sm);               /* an edit (PERF MODE, ARCH, EV:, LANE) may have changed what is reachable */
}

void ui_sm_enforce(UIStateMachine *sm)
{
    uint8_t mode = perf_mode(sm);
    if (sm->s.active_synth_layer >= NLAYER || (mode == PERF_SINGL_SYNTH && sm->s.active_synth_layer != 0u))
        sm->s.active_synth_layer = 0u;
    if (sm->s.active_seq_lane >= SEQ_NLANE)
        sm->s.active_seq_lane = 0u;
    if (sm->seq_step0 > UISEQ_STEP0_MAX)
        sm->seq_step0 = UISEQ_STEP0_MAX;
    sm->seq_step0 &= (uint8_t)~3u;                   /* the knob window is always a block of four (2026-10-26) */
    if (sm->save_target >= UISAVE_COUNT)
        sm->save_target = UISAVE_WHOLE_PERF;
    if (sm->save_slot >= ui_sm_bank_slots(sm->save_target))   /* a TARGET with a smaller bank (2026-10-17) */
        sm->save_slot = (uint8_t)(ui_sm_bank_slots(sm->save_target) - 1u);
    if (sm->perf_slot >= BANK_SLOTS)
        sm->perf_slot = BANK_SLOTS - 1u;
    if (sm->save_action >= UIACT_COUNT || (sm->save_action == UIACT_PASTE && !ui_sm_paste_ok(sm)))
        sm->save_action = sm->page == UIPAGE_SAVE_COMMIT ? (uint8_t)UIACT_SAVE : (uint8_t)UIACT_NO;
                                     /* a new TARGET can leave PASTE with nothing of its kind to paste */
    if (sm->name_cursor >= NAME_LEN)
        sm->name_cursor = NAME_LEN - 1u;
    seq_target_settle(sm);
    {                                /* 8.2 LAYER: a mode without its layer plays L1 (the engine), the choice kept for
                                      * the next mode that has it, as a SEQ lane's (2026-10-26) */
        if (ARP_LAYER(sm->arp) >= ARPLAYER_COUNT)
            arp_set_layer(sm->arp, ARPLAYER_L1);
        if (ARP_TIMING(sm->arp) >= ARPDIV_COUNT)
            arp_set_timing(sm->arp, ARP_TIMING_DEFAULT);
    }
    if (sm->page >= UIPAGE_COUNT) {
        enter_page(sm, UIPAGE_HOME1);
        return;
    }
    if ((sm->page == UIPAGE_HOME1 || sm->page == UIPAGE_HOME2) &&
        !ui_sm_page_visible(sm, sm->page, sm->s.active_synth_layer)) {
        uint8_t f = sm->s.current_param_focus;   /* HOME follows the layer focus, the cursor stays on its row */
        enter_page(sm, sm->page == UIPAGE_HOME1 ? (uint8_t)UIPAGE_HOME2 : (uint8_t)UIPAGE_HOME1);
        sm->s.current_param_focus = f;
    }
    if (!ui_sm_page_visible(sm, sm->page, sm->s.active_synth_layer)) {
        enter_page(sm, section_first_of(sm->page));  /* HOME View 2 / 3 under SINGL, or a page the architecture hides */
        return;
    }
    sync_registers(sm);
    cursor_snap(sm);
}

uint8_t ui_sm_set_layer_focus(UIStateMachine *sm, uint8_t layer)
{
    if (layer >= NLAYER || (layer != 0u && perf_mode(sm) == PERF_SINGL_SYNTH))
        return 0u;
    sm->s.active_synth_layer = layer;
    ui_sm_enforce(sm);               /* the new layer's architecture may hide this page or some of its rows */
    return 1u;
}

void ui_sm_commit_done(UIStateMachine *sm, uint8_t ok)
{
    if (sm->dialog != UIDLG_WRITING)
        return;
    sm->commit_pending = 0u;
    sm->save_action = UIACT_SAVE;          /* a failed write: back to submenu 2, SAVE armed to retry */
    sm->dialog = ok ? (uint8_t)UIDLG_SAVED : (uint8_t)UIDLG_NONE;
}

/* -------------------------------------------------- voice architecture --- */
uint8_t ui_sm_page_visible(const UIStateMachine *sm, uint8_t page, uint8_t layer)
{
    uint8_t a = layer_arch(sm, layer);
    switch (page) {
    case UIPAGE_OSC2: return varch_uses_osc2(a);
    case UIPAGE_ATTACK: return (uint8_t)(a == VARCH_STRIKE);
    case UIPAGE_FILTER2: return varch_uses_filter2(a);
    case UIPAGE_FLTMIX: return varch_uses_fltmix(a);
    case UIPAGE_HOME1: return (uint8_t)(layer == 0u);              /* one HOME page: the focused layer's */
    case UIPAGE_HOME2: return (uint8_t)(layer == 1u && ui_sm_home_view_count(perf_mode(sm)) == UI_HOME_NVIEW_MULTI);
    case UIPAGE_HOME3: return (uint8_t)(ui_sm_home_view_count(perf_mode(sm)) == UI_HOME_NVIEW_MULTI);
    default: return (uint8_t)(page < UIPAGE_COUNT);
    }
}

uint8_t ui_sm_layer_voices(const UIStateMachine *sm, uint8_t layer)
{
    return layer < NLAYER ? layer_voice_limit(perf_mode(sm), sm->engine[0], sm->engine[1], sm->perf->voice_mode[0],
                                              sm->perf->voice_mode[1], layer) : (uint8_t)0u;
}

const char *ui_sm_arch_name(uint8_t arch) { return VARCH_NAMES[arch < VARCH_COUNT ? arch : (uint8_t)VARCH_SINGLE]; }

uint8_t *ui_sm_knob(UIStateMachine *sm, uint8_t page, uint8_t layer, uint8_t knob, UiKnobInfo *info)
{
    static const UiKnob none = KNONE;
    const UiKnob *k = &none;
    uint8_t a, *block = 0;
    if (layer >= NLAYER)
        layer = 0u;
    if (page == UIPAGE_SEQ && knob < 4u) {           /* 7.1 / 7.2: a step value, written through ui_sm_seq_knob_set */
        info->label = seq_knob_label(sm, knob);
        info->kind = UIKNOB_SEQ;
        info->n_enum = 0u;
        info->names = NULL;
        return 0;
    }
    a = layer_arch(sm, layer);
    if (knob < 4u && page < UIPAGE_COUNT && ui_sm_page_visible(sm, page, layer)) {
        switch (page) {
        case UIPAGE_OSC1: case UIPAGE_OSC2:
            k = (VARCH_BIT(a) & A_SCANKNOB) != 0u ? &OSC_KNOBS_SCAN[knob] : &OSC_KNOBS[knob];
            break;
        case UIPAGE_ATTACK: k = &ATTACK_KNOBS[knob]; break;
        case UIPAGE_OSCMIX: k = &OSCMIX_KNOBS[a][knob]; break;
        case UIPAGE_FILTER1: case UIPAGE_FILTER2: k = &FILTER_KNOBS[knob]; break;
        case UIPAGE_FLTMIX: k = &FLTMIX_KNOBS[a][knob]; break;
        case UIPAGE_ENV1: case UIPAGE_ENV2: case UIPAGE_ENV3: k = &ENV_KNOBS[knob]; break;
        case UIPAGE_LFO1: case UIPAGE_LFO2: case UIPAGE_LFO3: k = &LFO_KNOBS[knob]; break;
        case UIPAGE_MOD1: case UIPAGE_MOD2: case UIPAGE_MOD3: k = &MOD_KNOBS[knob]; break;
        case UIPAGE_AMP: k = &AMP_KNOBS[knob]; break;
        case UIPAGE_EFFECTS: k = &EFFECTS_KNOBS[knob]; break;
        case UIPAGE_HOME1: case UIPAGE_HOME2: k = &HOME_KNOBS[knob]; break;
        case UIPAGE_HOME3: k = &ZONES_KNOBS[knob]; break;
        case UIPAGE_ARP: k = &ARP_KNOBS[knob]; break;
        case UIPAGE_PERF:     /* (layer 2's patch is frozen under SINGL, as its row is) */
            if (knob != 1u || perf_mode(sm) != PERF_SINGL_SYNTH)
                k = &SAVE_PRESET_KNOBS[knob];
            break;
        case UIPAGE_SAVE_COMMIT:     /* 6.2.1: knobs 3 / 4 only while the cursor is on RENAME; none under a popup */
            if (sm->dialog == UIDLG_NONE && (knob < 2u || ui_sm_rename_name(sm) != NULL))
                k = &SAVE_COMMIT_KNOBS[knob];
            break;
        case UIPAGE_GLOBAL:
            if (knob < 3u)
                k = &GLOBAL_INST_KNOBS[knob];
            else if (sm->perf->clock_source == CLKSRC_INTERNAL)
                k = &GLOBAL_TEMPO_KNOB;
            break;
        default: break;
        }
    }
    info->label = k->label;
    if ((page == UIPAGE_FILTER1 || page == UIPAGE_FILTER2) && knob == 0u) {
        /* the type morph is each circuit's own (dsp/filter.h): the SH-2's and MS-20's LP .. BP .. HP, the multimode's
         * mode, the formant's band spread */
        static const char *const MORPH_LABEL[FLT_COUNT] = {"LP-BP-HP", "MODE", "LP-BP-HP", "SPREAD"};
        uint8_t fl = sm->engine[layer]->filter[page - UIPAGE_FILTER1].flavour;
        info->label = MORPH_LABEL[fl < FLT_COUNT ? fl : FLT_MULTI];
    }
    info->kind = k->kind;
    info->n_enum = k->kind == UIKNOB_SRC ? (uint8_t)MSRC_COUNT : k->kind == UIKNOB_FN ? (uint8_t)MFN_COUNT : k->n_enum;
    info->names = k->kind == UIKNOB_SRC ? MSRC_NAMES : k->kind == UIKNOB_FN ? MFN_NAMES : k->names;
    if (page == UIPAGE_ARP && knob == 3u && sm->arp_knob[2] == ARPSTYLE_HOLD) {
        info->label = "STRUM";                       /* HOLD: RHYTHM is the strike's strum */
        info->names = ARPSTRUM_NAMES;
        info->n_enum = ARP_NSTRUM;
    }
    if (k->kind == UIKNOB_NONE)
        return 0;
    if (k->kind == UIKNOB_PERF)
        return &sm->perf_knob[k->offset];
    if (k->kind == UIKNOB_PORTA)
        return &sm->perf->porta_time[layer < NLAYER ? layer : 0u];
    if (page == UIPAGE_GLOBAL && knob < 3u)
        return (uint8_t *)ui_inst(sm) + k->offset;
    if (page == UIPAGE_SAVE_COMMIT) {
        uint8_t *own[4];
        own[0] = &sm->save_target;
        own[1] = &sm->save_slot;
        own[2] = &sm->name_cursor;
        own[3] = (uint8_t *)ui_sm_rename_name(sm);   /* (NULL: the TARGET has no name) */
        if (own[3] != 0)
            own[3] += sm->name_cursor < NAME_LEN ? sm->name_cursor : 0u;
        return own[k->offset];
    }
    if (page == UIPAGE_ARP)
        block = sm->arp_knob;                        /* (written back into held steps by ui_sm_knob_written) */
    else if (page >= UIPAGE_MOD1 && page <= UIPAGE_MOD3)
        block = (uint8_t *)&sm->engine[layer]->mod[page - UIPAGE_MOD1];
    else
        block = page_block_for(sm, &UI_PAGES[page], layer);
    return block ? block + k->offset : 0;
}

const char *ui_sm_knob_text(UIStateMachine *sm, uint8_t page, uint8_t layer, uint8_t knob)
{
    UiKnobInfo info;
    const uint8_t *p = ui_sm_knob(sm, page, layer, knob, &info);
    if (p == NULL || info.names == NULL)
        return NULL;
    return info.names[*p < info.n_enum ? *p : info.n_enum - 1u];   /* (an out-of-range byte: the last entry) */
}

/* ------------------------------------------------------------ rendering --- */
uint8_t ui_sm_mod_route(const UIStateMachine *sm, uint8_t row)
{
    const UiRow *r = vis_row(sm, row);
    return r != NULL ? mod_route(sm, r) : (uint8_t)UIMOD_NONE;
}

uint8_t ui_sm_row_count(const UIStateMachine *sm) { return vis_count(sm); }

const char *ui_sm_row_label(const UIStateMachine *sm, uint8_t row)
{
    const UiRow *r = vis_row(sm, row);
    if (r != NULL && r->kind == RK_RENAME)           /* 6.2.1: the name the TARGET carries */
        return sm->save_target == UISAVE_PATCH_PRESET ? "RENAME PATCH" :
               sm->save_target == UISAVE_WHOLE_PERF ? "RENAME PERF" : "RENAME";
    return r != NULL ? r->label : "";
}

uint8_t ui_sm_row_value(const UIStateMachine *sm, uint8_t row)
{
    const UiRow *r = vis_row(sm, row);
    return r != NULL ? row_get(sm, r) : 0u;
}

uint8_t ui_sm_row_place(const UIStateMachine *sm, uint8_t row)
{
    const UiRow *r = vis_row(sm, row);
    int32_t v;
    if (r == NULL || r->max <= r->min || r->kind == RK_RENAME)
        return 0u;
    if (r->kind == RK_WAVE) {                 /* the knob ring: the place in the wave list */
        int32_t last = (int32_t)wave_list_count() - 1;
        return last > 0 ? (uint8_t)(((int32_t)wave_list_pos(row_get(sm, r)) * 127 + last / 2) / last) : 0u;
    }
    v = (int32_t)row_get(sm, r) - r->min;
    if (v < 0) v = 0;
    if (v > r->max - r->min) v = r->max - r->min;
    return (uint8_t)((v * 127 + (r->max - r->min) / 2) / (r->max - r->min));
}

static char target_buf[13];         /* the TARGET row's text (ui_sm_seq_target_text) */

/* a bipolar value as -64..+63: at most "-64" plus NUL */
static char bip_buf[4];
static const char *bip_text(uint8_t v)
{
    int b = (int)v - (int)BIPOLAR_CENTER, n = 0;
    if (b < 0) { bip_buf[n++] = '-'; b = -b; }
    else if (b > 0) bip_buf[n++] = '+';
    if (b >= 10) bip_buf[n++] = (char)('0' + b / 10);
    bip_buf[n++] = (char)('0' + b % 10);
    bip_buf[n] = '\0';
    return bip_buf;
}

/* a number with at least `digits` digits (zero-padded) and a suffix, into num_buf */
static char num_buf[NAME_LEN + 1u];
static const char *num_text(uint32_t v, uint32_t digits, const char *suffix)
{
    char t[4];
    uint32_t n = 0u, k = 0u;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u && n < 3u);
    while (n < digits && n < 3u)
        t[n++] = '0';
    while (n)
        num_buf[k++] = t[--n];
    while (*suffix && k < NAME_LEN)
        num_buf[k++] = *suffix++;
    num_buf[k] = '\0';
    return num_buf;
}

const char *ui_sm_name_text(const uint8_t name[NAME_LEN], char *buf)
{
    static const char map[] = UI_CHAR_MAP;
    uint8_t i, end = 0u;
    for (i = 0u; i < NAME_LEN; i++) {
        uint8_t c = name[i];
        buf[i] = map[c < CHAR_MAP_LEN ? c : 0u];
        if (buf[i] != ' ')
            end = (uint8_t)(i + 1u);
    }
    buf[end] = '\0';
    return buf;
}

const char *ui_sm_row_text(const UIStateMachine *sm, uint8_t row)
{
    const UiRow *r = vis_row(sm, row);
    const uint8_t *name;
    uint8_t v;
    if (r == NULL) return NULL;
    v = row_get(sm, r);
    if (r->kind == RK_SEQ_TARGET) return ui_sm_seq_target_text(sm, sm->s.active_seq_lane, target_buf);
    if (r->kind == RK_CHAIN) return "CHAIN";
    if (r->via != 0u && mod_route(sm, r) != UIMOD_HERE) {   /* what feeds it now, or OFF */
        uint8_t src = ((const uint8_t *)sm->engine[sm->s.active_synth_layer < NLAYER ? sm->s.active_synth_layer : 0u])[r->via - 1u];
        return src == MSRC_NONE || src >= MSRC_COUNT ? "OFF" : MSRC_NAMES[src];
    }
    if (r->kind == RK_FINE) return ui_sm_fine_text(v, num_buf);
    if (r->kind == RK_WAVE) return wave_name(v, num_buf);
    if (r->kind == RK_RENAME) {       /* the name, trailing spaces dropped; "-" when the TARGET has none */
        name = ui_sm_rename_name(sm);
        return name != NULL ? ui_sm_name_text(name, num_buf) : "-";
    }
    if (r->flags & RF_BIPOLAR) return bip_text(v);
    if (r->flags & RF_NOTE) return ui_sm_note_name(v, num_buf);
    if (r->flags & RF_CHANNEL) return num_text(v + 1u, 1u, "");
    if (r->flags & RF_BPM) return num_text(MIDI_CLOCK_BPM_BASE + v, 1u, " BPM");
    if (r->flags & RF_SLOT) return num_text(v, 3u, "");
    if (r->names != NULL && v <= r->max) return r->names[v];
    return NULL;                      /* a plain 0..127 number: the caller draws it */
}

void ui_sm_goto(UIStateMachine *sm, uint8_t page)
{
    if (page >= UIPAGE_COUNT) return;
    enter_page(sm, page);             /* the cursor starts on the page's first highlightable row */
    ui_sm_enforce(sm);                /* a page the architecture or the mode hides falls back */
}

uint8_t ui_sm_keyboard_visible(const UIStateMachine *sm)
{
    return sm->page == UIPAGE_SAVE_COMMIT && sm->dialog == UIDLG_NONE && sm->kb_ms != 0u &&   /* knobs 3 / 4 turning */
           ui_sm_rename_name(sm) != NULL;
}

/* -------------------------------------------- 6.2.3 slots, clipboard --- */
void ui_sm_attach_store(UIStateMachine *sm, UiSlotQuery query, void *ctx)
{
    sm->slot_query = query;
    sm->slot_ctx = ctx;
}

uint8_t ui_sm_slot_query(const UIStateMachine *sm, uint8_t target, uint8_t slot, char *name)
{
    uint8_t raw[NAME_LEN], i, st;
    name[0] = '\0';
    if (sm->slot_query == 0)
        return UISLOT_UNKNOWN;
    for (i = 0u; i < NAME_LEN; i++)
        raw[i] = 0u;
    st = sm->slot_query(sm->slot_ctx, target, slot, raw);
    if (st != UISLOT_USED)
        return st == UISLOT_EMPTY ? (uint8_t)UISLOT_EMPTY : (uint8_t)UISLOT_UNKNOWN;
    ui_sm_name_text(raw, name);
    return UISLOT_USED;
}

uint8_t ui_sm_slot_state(const UIStateMachine *sm, char *name)
{
    return ui_sm_slot_query(sm, sm->save_target, sm->save_slot, name);
}

uint8_t ui_sm_paste_ok(const UIStateMachine *sm) { return (uint8_t)(sm->clip_kind == sm->save_target); }

const uint8_t *ui_sm_rename_name(const UIStateMachine *sm)
{
    uint8_t layer = sm->s.active_synth_layer < NLAYER ? sm->s.active_synth_layer : 0u;
    if (sm->save_target == UISAVE_PATCH_PRESET)
        return sm->engine[layer]->name;
    if (sm->save_target == UISAVE_SEQ_SETTINGS)
        return sm->seq_name;
    if (sm->save_target == UISAVE_ARP_SEQ)
        return sm->arp_name;
    return sm->perf->name;           /* WHOLE_PERF (and an out-of-range target, read as WHOLE_PERF) */
}
