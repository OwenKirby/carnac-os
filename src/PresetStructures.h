/* Mvave FM1 preset storage: sequencer lanes, arp pattern, engine (layer patch) state, master performance
 * preset, plus the (unsaved) UI state registers.
 * mvave_fm1_master_specification.txt: section 1 (every value a 7-bit uint8_t 0..127, no float / double),
 * 7.3 - 7.5 (SequencerStep, SequencerLane), 8 (ArpState), 4 (the per-layer parameter registry),
 * 5.3 and 6 (the performance file and its links).
 * Bipolar values are stored 0..127 with BIPOLAR_CENTER = 0 on screen (rendered -64..+64).
 * Every member is a uint8_t: no padding, the sizes below are the flash layout.
 * Compiles as C (the Felucca unity build) and as C++. */
#pragma once
#include <stdint.h>
#include "dsp/seq_data.h"            /* SequencerStep, SEQEV_*, SEQ_NNOTE, SEQ_FLAG_* (7.3, 7.5) */

#ifdef __cplusplus
#define PS_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define PS_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

/* ------------------------------------------------------------ sizes --- */
#define VAL_MAX 127u                 /* the 7-bit ceiling of every stored value */
#define BIPOLAR_CENTER 64u           /* stored 64 = 0 on a bipolar display */
#define NLAYER 2u                    /* LAYER1, LAYER2 */
#define NOSC 2u
#define NFILTER 2u
#define NENV 3u                      /* ENV1 is hardwired to the amp */
#define NLFO 3u                      /* LFO1 global, LFO2 / LFO3 per voice */
#define NROUTE 16u                   /* the mod matrix's routes (2026-11-11, replacing the 4 MOD slots, 12 MOD SENDS and
                                      * the pages' source pickers) */
#define SEQ_NLANE 4u
#define SEQ_NSTEP 16u
#define ARP_NSTEP 16u                /* 2026-11-13: ARP page 2's 16 white keys (was 8 keys above the chord zone) */
#define ARP_LEN_DEFAULT 8u           /* a new pattern's SEQ LENGTH (REC grows it) */
#define NAME_LEN 16u                 /* 6.2.1: character cursor 0..15 */
#define CHAR_MAP_LEN 41u             /* char_map[42]: " A..Z0..9.-_/" and its NUL */
#define PATCH_SLOTS 128u             /* stored EngineState patches; each layer plays one via layer_patch */
#define BANK_SLOTS 32u               /* stored sequences, arp patterns and performances (2026-10-17: was 128, like the
                                      * patches; a performance is now a whole snapshot, WholePerformance) */

/* -------------------------------------------------- shared value lists --- */
enum {                               /* mod_source_list (4); 2026-11-03 (v13): RANDOM, POLY AT, the per-note sources first,
                                      * then the controllers; 2026-11-11 (v14): MOD1..4 gone with the slots. After V1: new
                                      * sources only at the end */
    MSRC_NONE, MSRC_VELOCITY, MSRC_KEYBOARD,
    MSRC_RANDOM,                     /* a value per note, picked at its note-on, -1 .. +1 */
    MSRC_MODWHEEL, MSRC_AFTERTOUCH,
    MSRC_POLY_AT,                    /* the note's own pressure (MIDI poly aftertouch, a SEQ POLY AT lane); a voice that
                                      * has had none reads the channel's AFTERTOUCH */
    MSRC_CUSTOMCC1, MSRC_CUSTOMCC2,
    MSRC_ENV1, MSRC_ENV2, MSRC_ENV3, MSRC_LFO1, MSRC_LFO2, MSRC_LFO3,
    MSRC_COUNT
};
enum { XMOD_FM, XMOD_AM, XMOD_RING, XMOD_PM, XMOD_COUNT };                  /* OSCMIXER cross-mod type */
enum { SUBOCT_DOWN1, SUBOCT_DOWN2, SUBOCT_COUNT };                          /* sub-oscillator offset */
enum {                                                                      /* sub-oscillator waveform */
    SUBWAVE_SINE,                    /* wavetable_sine, always in the app (gen_wavetables.py sub_sine) */
    SUBWAVE_SQUARE,
    SUBWAVE_CURRENT,                 /* OSC1's live bank and morph frame, at the sub's own octave-down rate */
    SUBWAVE_COUNT
};
enum { FLT_SH2, FLT_MULTI, FLT_MS10, FLT_FORMANT, FLT_COUNT };  /* filter circuit flavour, in the FLAVOUR knob's
        * order (dsp/filter.h). SH-2: the Roland OTA cascade, its morph LP24 .. BP .. HP24. MULTI: the zero-delay 4-pole
        * ladder multimode, its morph the mode LP24 .. LP18 .. LP12 .. LP6 .. BP .. HP12 .. HP24 .. NOTCH (slot 1 was the
        * LADDER until 2026-10-15: its slopes are MULTI's first four modes). MS-20: the Korg 35, its morph LP .. BP .. HP.
        * 2026-10-21 (FREC_VER_ENGINE 9): the SH-2 replaced the SVF (0; old SVF patches load as MULTI, engine_filters_from_v8)
        * and MS-20 LOW (2) / HIGH (3) became one MS-20; FORMANT moved from 4 to 3 */
PS_STATIC_ASSERT(FLT_MULTI == 1, "filter_feedback_q12 (dsp/filter.h) tests flavour 1 for MULTI");
/* OSCMIXER noise colour, a morph 0..127, neighbours crossfaded (2026-11-06): white 0, pink 16, brown 32, then the
 * resonance, a sine on an overtone of OSC1 (HARMONIC) ringing in the noise: brown ringing it 56, the voice's own band
 * ~30 Hz wide ringing it 80, the band narrowing to ~4 Hz at 127 (almost a tone, slowly wavering) */
#define NOISE_WHITE 0u
#define NOISE_PINK 16u
#define NOISE_BROWN 32u
#define NOISE_RING 56u
#define NOISE_BAND 80u
#define NOISE_AIR 127u
/* the split (2026-11-21): COLOUR (EngineState.noise_tint) is the plain noise, white 0 .. pink 64 .. brown 127; WHISTLE
 * (oscmix.noise_colour, the old byte) rings it on the harmonic, evenly: none 0, ring 32, the voice's band 64, AIR 127.
 * Inside, a WHISTLE w > 0 plays as the old morph's 32 + w x 95 / 127 (NOISE_RING .. NOISE_AIR above), on COLOUR's noise */
#define WHISTLE_RING 32u
#define WHISTLE_BAND 64u
#define WHISTLE_AIR 127u
#define NOISE_MORPH_OF_WHISTLE(w) ((int)NOISE_BROWN + ((int)(w) * 95 + 63) / 127)
enum {                               /* voice architecture, per patch: the signal path every voice runs (4.0) */
    VARCH_SINGLE,                    /* OSC1 -> FILTER1. Not offered since 2026-10-26: loads as SPREAD at SPREAD 0, the same path
                                      * (varch_single_to_spread) */
    VARCH_DUAL_OSC,                  /* OSC1 + OSC2 (cross-mod) -> FILTER1 */
    VARCH_SPREAD,                    /* OSC1 as 4 detuned playheads -> FILTER1 */
    VARCH_DUAL_FILTER,               /* OSC1 -> FILTER1 -> FILTER2 (serial .. parallel) */
    VARCH_STACK,                     /* (OSC1 -> FILTER1, amp ENV1) + (OSC2 -> FILTER2, amp ENV2), blended */
    VARCH_BINAURAL,                  /* OSC1 -> FILTER1 once per ear; the right ear is beat-offset, its LFO mod phase-offset */
    VARCH_FULL,                      /* OSC1 + OSC2 (cross-mod) -> FILTER1 / FILTER2 (serial .. parallel): DUAL OSC into
                                      * DUAL FILTER (2026-10-26) */
    VARCH_DUAL_SPREAD,               /* retired 2026-11-06 (was OSC1 + OSC2, each as 4 detuned playheads): loads as DUAL OSC at
                                      * SPREAD 0 (varch_single_to_spread). Too dear for what DUAL OSC already gives */
    VARCH_COUNT
};
#define VARCH_BIT(a) (1u << (a))
#define VARCH_ALL ((1u << VARCH_COUNT) - 1u)
enum { DRIVE_SOFT, DRIVE_HARD, DRIVE_FOLD, DRIVE_TAPE, DRIVE_TUBE, DRIVE_OVERDRIVE, DRIVE_COUNT };   /* AMP drive model (TUBE, OVERDRIVE 2026-10-25: new ones at the end) */
/* reverb type (2026-10-16: PLATE and HALL removed, ROOM renamed TOMB, numbers shifted, FREC_VER_PERF 5; new ones at the end). MOD: Lexicon-inspired, chorused;
 * CELESTIAL: was SHIMMER; DOLMEN: a cave (a small stone chamber); TUMULUS: a cavern (Tumulus Saint-Michel, Carnac) */
enum { REVTYPE_SPRING, REVTYPE_TOMB, REVTYPE_MOD, REVTYPE_CELESTIAL, REVTYPE_DOLMEN, REVTYPE_TUMULUS, REVTYPE_COUNT };
/* delay type (2026-10-16): TAPE the tape echo, BBD a bucket-brigade echo (fx.c bbd_delay_block). New ones at the end */
enum { CLKSRC_INTERNAL, CLKSRC_MIDI, CLKSRC_COUNT };
enum { VELCURVE_SOFT, VELCURVE_LINEAR, VELCURVE_HARD, VELCURVE_COUNT };   /* GLOBAL > VEL CURVE: incoming MIDI */
enum { CLKOUT_OFF, CLKOUT_USB, CLKOUT_COUNT };                            /* GLOBAL > CLOCK OUT (X0X has no TRS out) */
#define MASTER_TUNE_MIN 14u          /* GLOBAL > MASTER TUNE: stored 14..114, 64 = A440, shown -50..+50 cents */
#define MASTER_TUNE_MAX 114u

/* OSC TUNE (2026-11-11): one knob for octaves, intervals and the detune (UIStateMachine.c ui_sm_tune_turn: a slow
 * turn steps cents, a quicker one semitones, a fast one octaves). Two bytes: COARSE semitones (OSC_COARSE_ZERO = unison,
 * +-OSC_COARSE_MAX) and FINE cents (64 = 0, +-OSC_FINE_MAX: past it the knob rolls into the next semitone) */
#define OSC_COARSE_ZERO 36u
#define OSC_COARSE_MAX 36
#define OSC_FINE_MAX 50
static inline int32_t osc_coarse_semis(uint8_t c)
{
    int32_t s = (int32_t)c - (int32_t)OSC_COARSE_ZERO;
    return s > OSC_COARSE_MAX ? OSC_COARSE_MAX : s < -OSC_COARSE_MAX ? -OSC_COARSE_MAX : s;
}
static inline int32_t osc_fine_cents(uint8_t v)
{
    int32_t c = (int32_t)v - 64;
    return c > OSC_FINE_MAX ? OSC_FINE_MAX : c < -OSC_FINE_MAX ? -OSC_FINE_MAX : c;
}

/* The instrument's own settings (GLOBAL, 2026-10-17): kept across power cycles with the THEME (the FM1's settings
 * record), never part of a performance, so loading one never retunes the instrument */
typedef struct {
    uint8_t master_tune;             /* MASTER_TUNE_MIN..MAX, 64 = 0 cents */
    uint8_t vel_curve;               /* VELCURVE_* */
    uint8_t clock_out;               /* CLKOUT_*: MIDI clock, start and stop out while the clock is internal */
    uint8_t cc_in, cc_out;           /* GLOBAL CC IN / CC OUT (2026-11-18): the CC map and NRPNs in; the panel's moves
                                      * out (CC, else NRPN) and a dump at each load */
    uint8_t pc_in, pc_out;           /* GLOBAL PC IN / PC OUT: Program Changes in; the panel's loads out */
} InstrumentSettings;
#define INSTRUMENT_DEFAULTS {64u, VELCURVE_LINEAR, CLKOUT_OFF, 1u, 0u, 1u, 0u}   /* (CC OUT, PC OUT off: user) */
enum { PERF_SINGL_SYNTH, PERF_DUAL_SYNTH, PERF_SPLIT_SYNTH, PERF_COUNT };   /* 5.3 View 1 */
/* POLY UNISON (2026-11-20, Jupiter-6 style): a poly mode whose notes share the layer's voices as unison stacks, one
 * note the whole stack, two half each, ... (synth_runtime.c poly_unison_start) */
enum { VMODE_MONO_RETRIG, VMODE_MONO_LEGATO, VMODE_POLY_REUSE, VMODE_POLY_RESET, VMODE_UNISON_STACK,
       VMODE_POLY_UNISON, VMODE_COUNT };
enum { PORTA_CONST_RATE, PORTA_CONST_TIME, PORTA_LEGATO_TIED, PORTA_COUNT };
enum { ARPLAYER_L1, ARPLAYER_L2, ARPLAYER_BOTH, ARPLAYER_COUNT };          /* BOTH: DUAL_SYNTH only */
enum {                               /* 8.2 time division: the ARP's TIMING, a step's length (1/32T 2026-10-21) */
    ARPDIV_2, ARPDIV_1, ARPDIV_1_2, ARPDIV_1_4, ARPDIV_1_4T, ARPDIV_1_8, ARPDIV_1_8T,
    ARPDIV_1_16, ARPDIV_1_16T, ARPDIV_1_32, ARPDIV_1_32T, ARPDIV_COUNT
};
enum {                               /* an ARP step's RATE (knob 1, 2026-10-21): its rhythm cells (a 4-cell rhythm's; a
                                      * triplet's 3 cells span the same 4) and a HOLD strum's gap between notes */
    ARPRATE_1_2, ARPRATE_1_4, ARPRATE_1_4T, ARPRATE_1_8, ARPRATE_1_8T, ARPRATE_1_16, ARPRATE_1_16T,
    ARPRATE_1_32, ARPRATE_1_32T, ARPRATE_1_64, ARPRATE_1_64T, ARPRATE_1_128, ARPRATE_COUNT
};
enum { UICTX_SYNTH, UICTX_SEQ, UICTX_ARP, UICTX_SAVE, UICTX_COUNT };
enum { SAVEPAGE_PRESET, SAVEPAGE_SAVE, SAVEPAGE_COUNT };                    /* 6: SAVE button presses */

/* ------------------------------------------------------- sequencer --- */
/* 7.3 SequencerStep, SEQ_NOTE_* and SEQ_FLAG_* now live in dsp/seq_data.h (included above) */

/* TARGET: target_mask, read according to event_type:
 *   NOTE, MODWHEEL, AFTERTOUCH, CUSTOMCC1, CUSTOMCC2, PROG_CHANGE: synth layer, SEQ_TGT_L1 / L2 / BOTH
 *     (PROG_CHANGE exists only in PERF_DUAL_SYNTH: seq_event_available)
 *   TRANSPOSE: lane whose pitch is offset, 0..3, a NOTE lane (shown as 4 lane slots, one picked)
 *   LAYER: row whose layer assignment is shifted, 0..3, a lane that plays on a layer (4 lane slots, one picked)
 *   DYNAMICS, TIMING, GATE, RATCHET, PROB_SKIP, PROB_MUTE: lane bitmask, one slot per lane, left to right:
 *     "[X][ ][X][ ]" = SEQ_TGT_LANE1 | SEQ_TGT_LANE3 = 0x05 */
#define SEQ_TGT_LANE1 (1u << 0)
#define SEQ_TGT_LANE2 (1u << 1)
#define SEQ_TGT_LANE3 (1u << 2)
#define SEQ_TGT_LANE4 (1u << 3)
#define SEQ_TGT_MASK 0x0Fu           /* bits 4..7 unused, always 0 */
enum { SEQ_TGT_L1, SEQ_TGT_L2, SEQ_TGT_BOTH, SEQ_TGT_LAYER_COUNT };        /* "[L1]" "[L2]" "[BOTH]" */
enum {                               /* MD: */
    SEQ_MODE_FORWARD,                /* "FOr" */
    SEQ_MODE_BACKWARD,               /* "bAC" */
    SEQ_MODE_PENDULUM1,              /* "PE1" */
    SEQ_MODE_PENDULUM2,              /* "PE2" */
    SEQ_MODE_RANDOM,                 /* "rnd" */
    SEQ_MODE_COUNT
};
/* P: master MIDI clock ticks per step, any value 1..32, shown "gNN" */
#define SEQ_PRESCALE_MIN 1u
#define SEQ_PRESCALE_MAX 32u
#define SEQ_PRESCALE_1_16 6u         /* "g06" */
#define SEQ_PRESCALE_1_8 12u         /* "g12" */
#define SEQ_PRESCALE_1_4 24u         /* "g24" */

/* SCALE (2026-10-16), a NOTE lane's footer: its notes held to a scale, as they are entered (the knob steps through the
 * scale's degrees, the keyboard's notes snap to it) and as they play (a note stored outside it plays the nearest note
 * in it: the stored notes are kept, so a scale change or OFF gives them back). GENRE follows the ARP's GENRE BANK
 * (arp_chords.h arp_genre_scale); the others start on the lane's ROOT. seq_scale.h does the arithmetic. */
enum {
    SEQSCALE_OFF, SEQSCALE_GENRE,
    SEQSCALE_MAJOR, SEQSCALE_MINOR, SEQSCALE_DORIAN, SEQSCALE_PHRYGIAN, SEQSCALE_LYDIAN, SEQSCALE_MIXOLYDIAN,
    SEQSCALE_HARM_MINOR, SEQSCALE_MAJ_PENTA, SEQSCALE_MIN_PENTA, SEQSCALE_BLUES,
    SEQSCALE_COUNT
};

/* 7.4: one MAQ lane, its 16-step grid row and its footer row (P: / EV: / TARGET: / MD:).
 * SEQ_SETTINGS saves SEQ_NLANE of these; the performance reaches them through seq_link */
typedef struct {
    SequencerStep steps[SEQ_NSTEP];
    uint8_t event_type;              /* SEQEV_* */
    uint8_t target_mask;             /* TARGET:, layer / lane index / row index / SEQ_TGT_* bits by event_type */
    uint8_t playback_mode;           /* MD:, SEQ_MODE_* */
    uint8_t prescale;                /* P:, SEQ_PRESCALE_MIN..MAX ticks per step */
    uint8_t loop_start;              /* 0..15, OCT- + selector key (7.1) */
    uint8_t loop_end;                /* 0..15, OCT+ + selector key */
    uint8_t scale;                   /* SEQSCALE_*: a NOTE lane's scale (OFF: every note) */
    uint8_t scale_root;              /* 0..11, C .. B: the scale's root (not GENRE's, which brings its own) */
} SequencerLane;
PS_STATIC_ASSERT(sizeof(SequencerLane) == 104u, "SequencerLane flash layout");

/* the largest valid target_mask for an event type; also the range check for a lane read from flash */
static inline uint8_t seq_target_max(uint8_t event_type)
{
    switch (event_type) {
    case SEQEV_TRANSPOSE: case SEQEV_LAYER:
        return SEQ_NLANE - 1u;
    case SEQEV_DYNAMICS: case SEQEV_TIMING: case SEQEV_GATE: case SEQEV_RATCHET:
    case SEQEV_PROB_SKIP: case SEQEV_PROB_MUTE:
        return SEQ_TGT_MASK;
    default:
        return SEQ_TGT_BOTH;
    }
}

/* PROG_CHANGE picks a layer, so it is only offered (and only fires) in DUAL_SYNTH */
static inline uint8_t seq_event_available(uint8_t event_type, uint8_t perf_mode)
{
    return event_type != SEQEV_PROG_CHANGE || perf_mode == PERF_DUAL_SYNTH;
}

/* the only way to change a lane's event type: target_mask means something else under the new type
 * (0x0F as a layer, lane index 2 read as the lane-2 bit), so it restarts at that type's first option */
static inline void seq_lane_set_event(SequencerLane *lane, uint8_t event_type)
{
    if (lane->event_type == event_type)
        return;
    lane->event_type = event_type;
    lane->target_mask = seq_target_max(event_type) == SEQ_TGT_MASK ? SEQ_TGT_LANE1 : 0u;
}

/* ---------------------------------------------------- arpeggiator --- */
#define ARP_CHORD_EMPTY 127u         /* chord_root of a step no chord was captured into (or CLEARed): the step rests */
#define ARP_CHORD_TIE 126u           /* chord_root of a TIE step: the chord before it carries on (8.1) */
#define ARP_ROOT_IS_CHORD(r) ((r) < ARP_CHORD_TIE)   /* a chord key's note, 0..125 */
#define ARP_NOCT 4u                  /* a phrase spans 1..4 octaves of its chord (SPAN) */
#define ARP_NSPAN (ARP_NOCT + 1u)    /* octave_range (SPAN, knob 2): 0 AUTO (the phrase's own span), 1..4 octaves */
#define ARP_NVAR 12u                 /* variation (knob 4): the style's phrase, 0..11 (arp_seq.c CARP_PHRASES) */
enum {                               /* style (knob 3, 2026-11-23): the family of phrases VARIATION picks from (arp_seq.c) */
    ARPSTYLE_BEAT,                   /* the chord on a rhythm; variations 0..2 HOLD: struck once, held while the steps after
                                      * it play the same chord, strummed none / up / down with its notes RATE apart */
    ARPSTYLE_ARP,                    /* one note at a time through the chord: up, down, up-down, converge .. */
    ARPSTYLE_PULSE,                  /* sequencer lines: octave pulses, Berlin-school runs, gallops, offbeat bass */
    ARPSTYLE_STAB,                   /* syncopated chord stabs: offbeat, skank, house, funk, clave, gate */
    ARPSTYLE_STRUM,                  /* guitar strumming: down / up strokes, folk, island, waltz, fingerpicking */
    ARPSTYLE_GUITAR,                 /* rhythm guitar: 16th funk with muted chks, reggae, ska, palm mutes, highlife */
    ARPSTYLE_KEYS,                   /* piano figures: Alberti, ballad, stride, gospel, montuno, ripples */
    ARPSTYLE_BASS,                   /* a bass note under the chord: oom-pah, boom-chick, bossa, habanera, walking */
    ARPSTYLE_HARP,                   /* ringing figures: cascades, rolls, kora, bells, rain */
    ARPSTYLE_COUNT
};

/* 8.1: one step key's capture: the chord struck plus the 4 ARP knob values at that moment (2.2) */
typedef struct {
    uint8_t chord_root;              /* the chord key's MIDI note 0..125 (the chord's bass), ARP_CHORD_TIE, ARP_CHORD_EMPTY */
    uint8_t rate;                    /* knob 1: ARPRATE_*, its notes' rate (FREC_VER_ARP 5, 2026-10-21: was the step's
                                      * length, ARPDIV_*, now the pattern's TIMING; v4, 2026-10-17: was its GATE) */
    uint8_t octave_range;            /* knob 2, SPAN: 0 AUTO, 1..ARP_NOCT octaves (2026-11-23: was the octave range 0..3) */
    uint8_t style;                   /* knob 3: ARPSTYLE_* */
    uint8_t variation;               /* knob 4: 0..ARP_NVAR - 1 */
    uint8_t accent;                  /* 0 / 1: the step's first strike plays at ACCENT VEL (the ACCENT key toggles it) */
} ArpChordStep;
PS_STATIC_ASSERT(sizeof(ArpChordStep) == 6u, "ArpChordStep flash layout");

/* the 16-step chord pattern ARP_SEQ saves and arp_link loads; footers are the 8.2 menu list */
typedef struct {
    ArpChordStep steps[ARP_NSTEP];
    uint8_t layer_timing;            /* low nibble LAYER (ARPLAYER_*), high nibble TIMING (ARPDIV_*, every step's length;
                                      * FREC_VER_ARP 5, 2026-10-21): ARP_LAYER / ARP_TIMING, arp_set_layer / _timing */
    uint8_t gate_length;             /* GATE: a strike's length = its cells x (value + 1) / 128, 127 = legato (FREC_VER_ARP
                                      * 4, 2026-10-17: where TIME DIVISION was, before each step had its own RATE) */
    uint8_t sequence_length;         /* 1..16 (ARP_NSTEP) */
    uint8_t transpose_maq_option;    /* 0 / 1: the MAQ NOTE lanes follow each step's chord root and the chord key */
    uint8_t current_genre_bank;      /* arp_chords.h genre, 0..ARP_GENRE_COUNT - 1 */
    uint8_t accent_velocity;         /* 1..127: an accented step's first strike */
} ArpState;
PS_STATIC_ASSERT(sizeof(ArpState) == 102u, "ArpState flash layout");

/* the factory arp slot: every step empty (captured steps start from the default knob values), 1/4 steps (8.3's
 * rhythm cells are then the 16ths its table names), 8 steps, layer 2 (2026-10-26: the SEQ has layer 1), no MAQ
 * transpose, Phrygian, no accents */
#define ARP_GATE_DEFAULT 96u
#define ARP_RATE_DEFAULT ARPRATE_1_16  /* .. a 4-cell rhythm a beat long: one TIMING 1/4 step */
#define ARP_TIMING_DEFAULT ARPDIV_1_4
#define ARP_LAYER(st) ((uint8_t)((st)->layer_timing & 0x0Fu))
#define ARP_TIMING(st) ((uint8_t)((st)->layer_timing >> 4))
static inline void arp_set_layer(ArpState *a, uint8_t layer)
{
    a->layer_timing = (uint8_t)((a->layer_timing & 0xF0u) | (layer & 0x0Fu));
}
static inline void arp_set_timing(ArpState *a, uint8_t div)
{
    a->layer_timing = (uint8_t)((a->layer_timing & 0x0Fu) | (uint8_t)(div << 4));
}
/* RATE as 96ths of a beat (a 4-cell rhythm lasts 4 of these, a HOLD strum's notes are one apart) */
static inline uint8_t arp_rate_q(uint8_t rate)
{
    static const uint8_t Q[ARPRATE_COUNT] = {192u, 96u, 64u, 48u, 32u, 24u, 16u, 12u, 8u, 6u, 4u, 3u};
    return Q[rate < ARPRATE_COUNT ? rate : (uint8_t)ARP_RATE_DEFAULT];
}
/* the RATE whose 4 cells fill a step of TIME DIVISION div (before 2026-10-21 the rhythm always filled its step) */
static inline uint8_t arp_rate_of_div(uint8_t div)
{
    static const uint8_t R[ARPDIV_COUNT] = {ARPRATE_1_2, ARPRATE_1_4, ARPRATE_1_8, ARPRATE_1_16, ARPRATE_1_16T,
                                            ARPRATE_1_32, ARPRATE_1_32T, ARPRATE_1_64, ARPRATE_1_64T, ARPRATE_1_128,
                                            ARPRATE_1_128};
    return R[div < ARPDIV_COUNT ? div : (uint8_t)ARP_TIMING_DEFAULT];
}
#define ARP_ACCENT_DEFAULT 120u
#define ARP_GENRE_DEFAULT 100u       /* arp_chords.h ARP_GENRE_PHRYGIAN (asserted there) */
static inline void arp_defaults(ArpState *a)
{
    uint8_t i;
    for (i = 0u; i < ARP_NSTEP; i++) {
        a->steps[i].chord_root = ARP_CHORD_EMPTY;
        a->steps[i].rate = ARP_RATE_DEFAULT;
        a->steps[i].octave_range = 0u;
        a->steps[i].style = ARPSTYLE_ARP;
        a->steps[i].variation = 0u;
        a->steps[i].accent = 0u;
    }
    a->layer_timing = (uint8_t)(ARPLAYER_L2 | ARP_TIMING_DEFAULT << 4);   /* L2, the SEQ's L1 (SINGL: plays on L1) */
    a->gate_length = ARP_GATE_DEFAULT;
    a->sequence_length = ARP_LEN_DEFAULT;
    a->transpose_maq_option = 0u;
    a->current_genre_bank = ARP_GENRE_DEFAULT;
    a->accent_velocity = ARP_ACCENT_DEFAULT;
}

/* --------------------------------------------- engine (layer patch) --- */
/* the parameter pages a layer owns (4.1 .. 4.6), one block per layer; (bip) = bipolar */
/* The pages keep only the fixed paths whose source is right almost every time (2026-11-11, the mod matrix, v14):
 * their amounts are here, their sources fixed in the engine. Everything else is a route (EngineState.route). */
typedef struct {                     /* OSC1 / OSC2 */
    uint8_t scan;                    /* knob 1 */
    uint8_t coarse;                  /* knob 2 TUNE, with fine: semitones (osc_coarse_semis) */
    uint8_t fine;                    /* knob 2 TUNE, with coarse (the next byte, ui_sm_tune_turn): cents (osc_fine_cents) */
    uint8_t lfo2_pitch;              /* knob 3: LFO2 > PITCH (LFO1's vibrato is the LFO1 page's) */
    uint8_t wavebank;
    uint8_t scan_mod_amt;            /* ENV3 > SCAN, a row */
    uint8_t env2_pitch;              /* knob 4 (bip): ENV2 > PITCH, up to +-24 semitones, finer near 0 (env_pitch_cents) */
    uint8_t key_track;               /* (bip), default 127 = full 1V/oct; shown with two oscillators */
    uint8_t sync;                    /* 0 / 1, OSC2 only */
} OscParams;

typedef struct {                     /* OSCMIXER; which knob a field sits on depends on the architecture (2.2) */
    uint8_t balance;                 /* knob 1 DUAL OSC, (bip) OSC1 .. OSC2; SPREAD (2026-11-21), (bip) OSC .. NOISE */
    uint8_t xmod_amt;                /* knob 2 DUAL OSC / STACK */
    uint8_t sub_level;               /* knob 3, every architecture */
    uint8_t spread;                  /* knob 2 SPREAD only */
    uint8_t xmod_type;               /* XMOD_*, knob 1 STACK */
    uint8_t sub_octave;              /* SUBOCT_* (with sub_waveform: SUB WAVE's 6 steps, the next byte) */
    uint8_t sub_waveform;            /* SUBWAVE_* */
    uint8_t noise_level;             /* NOISE LEVEL, every architecture but SPREAD (its noise is the balance: noise_amount_of) */
    /* noise generator (4.1) */
    uint8_t noise_colour;            /* WHISTLE (2026-11-21, was the COLOUR morph): none 0, ring 32, band 64, AIR 127 (WHISTLE_*) */
    uint8_t noise_mod_amt;           /* (bip) LEVEL MOD: ENV3 > NOISE LEVEL */
    /* BINAURAL: how the right ear's oscillator differs from the left's */
    uint8_t bin_mod_phase;           /* knob 1: LFO phase offset of the right ear's pitch / scan mod, 0..127 = 0..357 deg */
    uint8_t bin_beat;                /* knob 2: right ear pitch offset, 0..127 = 0..15.875 Hz in 0.125 Hz steps */
    uint8_t bin_scan_offset;         /* (bip): right ear wavetable scan offset */
    uint8_t noise_col_mod_amt;       /* (bip) COL MOD: ENV3 > NOISE COLOUR */
} OscMixerParams;

typedef struct {                     /* FILTER1 / FILTER2 */
    uint8_t morph;                   /* knob 1, by flavour: SH-2 and MS-20 LP -> BP -> HP, MULTI mode LP24 -> .. -> NOTCH,
                                      * FORMANT band spread (dsp/filter.h) */
    uint8_t cutoff;                  /* knob 2 */
    uint8_t resonance;               /* knob 3 */
    uint8_t mod_amt;                 /* knob 4 ENV2 > CUTOFF, 0..127 (ENV2 INVERT for a sweep down) */
    uint8_t flavour;                 /* FLT_* */
    uint8_t drive;                   /* the first row: the level into the circuit, 0 clean .. 127 driven hard */
} FilterParams;

typedef struct {                     /* FILTER MIXER (DUAL FILTER, STACK, BINAURAL; hidden otherwise) */
    uint8_t balance;                 /* knob 1, (bip) DUAL FILTER: FILTER1 .. FILTER2; STACK: blend stack A .. B */
    uint8_t routing;                 /* knob 2 DUAL FILTER: serial .. parallel */
    uint8_t balance_mod_amt;         /* knob 3, (bip): ENV3 > BALANCE (DUAL FILTER, FULL), ENV3 > BLEND (STACK) */
    uint8_t routing_mod_amt;         /* knob 4, (bip): ENV3 > ROUTING */
    /* STACK */
    uint8_t stack_xfeed;             /* knob 2, (bip) CROSS-FEED: + FILTER1's output also into FILTER2 (so through ENV2),
                                      * - FILTER2's into FILTER1 (through ENV1) */
    uint8_t env3_cutoff;             /* knob 4, (bip): ENV3 to both filter cutoffs */
    /* BINAURAL */
    uint8_t bin_mod_phase;           /* knob 1: LFO phase offset of the right ear's filter mod, 0..127 = 0..357 deg */
    uint8_t bin_width;               /* knob 2: 0 = both ears summed (mono), 127 = full left / right separation */
    uint8_t bin_cutoff_offset;       /* knob 3, (bip): + raises the right ear's cutoff and lowers the left's */
    uint8_t bin_cutoff_mod_amt;      /* knob 4, (bip): LFO1 > CUTOFF L/R (the ears' LFO offsets need LFO1) */
} FilterMixerParams;

typedef struct {                     /* ENVELOPE1..3 */
    uint8_t attack, decay, sustain, release;   /* knobs 1..4 */
    uint8_t vel_to_attack;           /* VEL > ATTACK (bip; 2026-11-03, was VEL > TIME on every stage): the attack only */
    uint8_t key_to_time;             /* (bip) */
    uint8_t retrig_rate;
    uint8_t retrig_sync;             /* 0 / 1 */
    uint8_t legato;                  /* 0 / 1 */
    uint8_t auto_decay;              /* AUTO DECAY: a rapid repeat of the note shortens this envelope's release */
    uint8_t invert;                  /* 0 / 1, ENV2 / ENV3 only */
} EnvParams;

typedef struct {                     /* LFO1..3 */
    uint8_t shape;                   /* knob 1: sine .. slewed random, wraps */
    uint8_t rate;                    /* knob 2 */
    uint8_t delay;                   /* knob 3: > 100 = REL mode */
    uint8_t fade;                    /* knob 4, (bip): - fade out, + fade in */
    uint8_t clock_sync;              /* 0 / 1 */
    uint8_t phase;
    uint8_t key_reset;               /* 0 / 1 */
} LfoParams;

typedef struct {                     /* AMP (4.6 registry; 2.2's knob row differs) */
    uint8_t drive;                   /* knob 1 */
    uint8_t amp_mod_depth;           /* knob 2 TREMOLO: LFO3 > LEVEL */
    uint8_t delay_send;              /* knob 3 */
    uint8_t reverb_send;             /* knob 4 */
    uint8_t drive_model;             /* DRIVE_* */
} AmpParams;

/* the layer's MFX (dsp/mfx.h: the types, their knobs and rows), saved with the patch since v11 (2026-11-03) */
typedef struct {
    uint8_t type;                    /* MFX_* (0: OFF) */
    uint8_t knob[4];                 /* 0..127, the type's own controls */
    uint8_t lr;                      /* the type's menu row, 0..127: L/R PHASE (the right side's LFO behind the left's),
                                      * ENSEMBLE's and ENS+PHASER's WIDTH */
    uint8_t row[2];                  /* further rows, 0..127: ENS+PHASER's AGE, COLOUR; CHORUS's AGE; TAPE FLANGER's
                                      * POLARITY (0, 1) */
} MfxParams;

/* THE MOD MATRIX (2026-11-11, v14): NROUTE routes a patch, each SOURCE times VIA times AMOUNT into DEST, on top of
 * whatever the destination's own source and amount do there (routes and fixed paths on one destination add). One
 * source may feed any number of routes. AMOUNT works as the destination's own amount would: full (+-64) does what the
 * destination's amount at full does (a 0..127 amount: twice the route's, either sign). A route with DEST NONE or SOURCE
 * NONE is free. */
enum {                               /* the destinations, in the pages' order (the DEST knob lists them so); a voice's
                                      * route mask holds them all (SourceCtx.sends, two words). After V1: new ones at the end */
    MDST_NONE,
    MDST_OSC1_SCAN, MDST_OSC1_PITCH, MDST_OSC2_SCAN, MDST_OSC2_PITCH,
    MDST_PITCH,                      /* both oscillators' pitch: the whole voice bends */
    MDST_OSC_MIX, MDST_XMOD, MDST_SPREAD, MDST_BEAT_HZ, MDST_NOISE_LEVEL, MDST_NOISE_COLOUR, MDST_HARMONIC,
    MDST_F1_CUTOFF, MDST_F1_RES, MDST_F1_MORPH, MDST_F1_DRIVE, MDST_F2_CUTOFF, MDST_F2_RES, MDST_F2_MORPH, MDST_F2_DRIVE,
    MDST_BALANCE,                    /* DUAL FILTER's BALANCE, STACK's BLEND A/B (one byte) */
    MDST_ROUTING, MDST_PHASE, MDST_CUTOFF_LR,
    MDST_LFO1_SPEED, MDST_LFO1_DEPTH, MDST_LFO2_SPEED, MDST_LFO2_DEPTH, MDST_LFO3_SPEED, MDST_LFO3_DEPTH,
    MDST_AMP_LEVEL, MDST_PAN,
    MDST_GLIDE,                      /* the layer's PORTA TIME (a performance setting), a glide's speed as it goes */
    MDST_DLY_SEND, MDST_REV_SEND,    /* the layer's AMP DELAY / REVERB SEND (taken from the layer's sum: the newest voice's) */
    MDST_MFX_K1, MDST_MFX_K2, MDST_MFX_K3, MDST_MFX_K4,   /* the layer's MFX knobs 1..4, whatever the type names them (a
                                                         * list knob, PITCH SHIFT's PITCH, takes none; OFF: none) */
    MDST_COUNT
};
typedef struct {
    uint8_t src;                     /* MSRC_*: what moves */
    uint8_t via;                     /* MSRC_*: multiplies the route (NONE: 1); WHEEL = a depth control, an LFO = a swell */
    uint8_t dest;                    /* MDST_*: what is moved */
    uint8_t amount;                  /* (bip) -64..+63 */
    uint8_t via_depth;               /* VIA DEPTH 0..127 (2026-11-11): 0 the VIA ignored, 127 the VIA alone sets the depth */
} ModRoute;
#define ROUTE_FREE {MSRC_NONE, MSRC_NONE, MDST_NONE, BIPOLAR_CENTER, VAL_MAX}   /* a free route, as a new one starts */

#define ENGINE_RESERVED 0u           /* spare patch bytes (v11): none left (CLUSTER took the last, 2026-11-21); were stored, 0 until a later feature took one (so 0 must
                                      * mean that feature's off / default) */

/* one layer's patch: what PATCH_PRESET saves and LAYER n PATCH MEM LINK loads */
typedef struct {
    OscParams osc[NOSC];
    OscMixerParams oscmix;
    FilterParams filter[NFILTER];
    FilterMixerParams fltmix;
    EnvParams env[NENV];
    LfoParams lfo[NLFO];
    AmpParams amp;
    uint8_t arch;                    /* VARCH_*: the voice architecture, HOME View 1 / 2 (5.3) */
    uint8_t name[NAME_LEN];          /* char_map indices (0 = space), not ASCII: RENAME PATCH (6.2.1) */
    uint8_t analog;                  /* ANALOG 0..127 (LAYER page, dsp/analog.h): 0 = clean */
    uint8_t filter_key_track[NFILTER];   /* FILTER1 / FILTER2 KEY TRACK 0..127 (2026-10-21): the cutoff follows the
                                          * keys about C4, 100 = an octave an octave (127: 127 %). Here, not in FilterParams, so no
                                          * offset moved */
    /* v10 (2026-10-26), at the end so no offset moves: */
    uint8_t env_vel_level[NENV];     /* ENV1..3 VEL > LEVEL 0..127: how far a soft note lowers the envelope's level
                                      * (0 none; ENV1's replaces the old fixed velocity-to-amp curve) */
    uint8_t lfo_depth_amt[NLFO];     /* LFO1..3 WHEEL > DEPTH 0..127: 0 the LFO at full depth, 127 the wheel alone sets
                                      * the depth (as AMP MOD) */
    /* the LFO1 page (2026-11-11, the Jupiter-6's way): LFO1's own pitch and filter amounts */
    uint8_t lfo1_pitch;              /* knob 3 PITCH AMT: LFO1 > PITCH, both oscillators at their share below */
    uint8_t lfo1_filter;             /* knob 4 FILTER AMT: LFO1 > CUTOFF, every filter the architecture plays */
    uint8_t lfo1_osc_amt[NOSC];      /* rows OSC1 AMT, OSC2 AMT: each oscillator's share of PITCH AMT (127 all of it) */
    /* v11 (2026-11-03): */
    MfxParams mfx;                   /* the layer's MFX (dsp/mfx.c plays engine_state[layer].mfx) */
    /* 2026-11-06: three of the four reserved bytes (nothing moved; 0 is each one's old behaviour) */
    uint8_t noise_harmonic;          /* HARMONIC 0..127: the noise resonance on overtone 1..8 of OSC1, a stop on each */
    uint8_t noise_cluster;           /* CLUSTER 0..127 (2026-11-21, the last reserved byte): the whistle's next three overtones join it
                                      * (fading in to ~45), then drift apart up to ~20 cents (beating air); 0 = one whistle (off) */
    ModRoute route[NROUTE];          /* the mod matrix (2026-11-11, v14) */
    /* 2026-11-21, at the end (the zero rule: 0 = as before): */
    uint8_t noise_tint;              /* COLOUR 0..127: the plain noise, white 0 .. pink 64 .. brown 127 (WHISTLE rings it) */
    uint8_t noise_inharm;            /* INHARM 0..127: CLUSTER's overtones, harmonic 0 .. stretched (a bell) 64 .. metal 127 */
} EngineState;
PS_STATIC_ASSERT(sizeof(EngineState) == 235u, "EngineState layout");
/* COLOUR + WHISTLE as the old one-knob morph (white 0 .. brown 32 .. AIR 127), for the pictures drawn from it */
static inline int32_t noise_morph_of(const EngineState *e)
{
    int32_t w = e->oscmix.noise_colour, t = e->noise_tint;
    return w ? NOISE_MORPH_OF_WHISTLE(w) : (t < 64 ? t / 4 : (int32_t)NOISE_PINK + (t - 64) / 4);
}
PS_STATIC_ASSERT(MDST_COUNT <= 64u, "a route's DEST in 6 bits, a voice's mask in two words");
#define ENV1_VEL_LEVEL_V9 111u       /* ENV1 VEL > LEVEL that plays as v9's fixed curve did (-18 dB at velocity 1) */
/* In flash a patch is ENGINE_STORED_SIZE bytes (dsp/flash_record.h engine_store_pack): 184 is the most that keeps 22
 * patches to a store chunk (preset_store_fm1.c: (4064 - 16) / 184), so the chunks stay where they were. Since v12
 * (2026-11-03) every byte is stored at its own bit width (ENG_W): 7 for a value, 5 for a source, 1 for a switch, 6 for
 * a name character; what that leaves after the last field is room for fields added at the end (see ENG_W). Older
 * patches (v11 and before) are not migrated (before V1 they may go). */
#define ENGINE_STORED_SIZE 184u

/* ------------------------------------------------ voice architecture --- */
/* an out-of-range architecture (a corrupt flash read) plays as SINGLE, the cheapest */
static inline uint8_t varch_of(const EngineState *e)
{
    return e->arch < VARCH_COUNT ? e->arch : (uint8_t)VARCH_SINGLE;
}

/* SINGLE left the ARCH list (2026-10-26): SPREAD at SPREAD 0 renders exactly its path (osc_render_block's undetuned
 * playhead alone). A patch that still says SINGLE (a stored one, a factory one) becomes SPREAD with its spread and spread
 * at 0, so it sounds as it did; SINGLE ignored both, so they may hold leftovers. New patches never say SINGLE, and an
 * older firmware reads SPREAD as it is, so no record version moves. */
static inline void varch_single_to_spread(EngineState *e)
{
    if (e->arch == VARCH_SINGLE) {
        e->arch = VARCH_SPREAD;
        e->oscmix.spread = 0;
    } else if (e->arch == VARCH_DUAL_SPREAD) {           /* (retired 2026-11-06: its two oscillators without the playheads) */
        e->arch = VARCH_DUAL_OSC;
        e->oscmix.spread = 0;
    }
}

/* the noise amount, 0..127: NOISE LEVEL, or in SPREAD (2026-11-21) the OSC/NOISE balance's noise side (balance 0 = osc only,
 * 127 = noise only; OSC1's gain is 127 - balance) */
static inline uint8_t noise_amount_of(const EngineState *e)
{
    uint8_t a = varch_of(e);
    return a == VARCH_SPREAD || a == VARCH_SINGLE ? e->oscmix.balance : e->oscmix.noise_level;
}

/* the pages an architecture uses; the UI hides the rest (OSC1, OSCMIXER and FILTER1 always show) */
static inline uint8_t varch_uses_osc2(uint8_t a)
{
    return (uint8_t)(a == VARCH_DUAL_OSC || a == VARCH_STACK || a == VARCH_FULL);
}
static inline uint8_t varch_uses_filter2(uint8_t a)
{
    return (uint8_t)(a == VARCH_DUAL_FILTER || a == VARCH_STACK || a == VARCH_FULL);
}
static inline uint8_t varch_uses_fltmix(uint8_t a)
{
    return (uint8_t)(a == VARCH_DUAL_FILTER || a == VARCH_STACK || a == VARCH_BINAURAL || a == VARCH_FULL);
}

/* THE VOICE POOL IS PRICED (2026-11-09). A voice costs what its patch makes it cost, in tenths of a plain one-oscillator,
 * one-filter voice (measured on the FM1, PERF TEST heavy rows, 2026-11-07: points a voice over the idle floor, 4.8 for
 * SPREAD 0): SPREAD 6.2 (13), DUAL OSC 6.0 (12), DUAL FILTER 6.9 (14), STACK 7.4 (15), BINAURAL 7.9 (16), FULL 8.0 (17);
 * an SH-2 filter adds about 1.0 a filter that runs (2) and a PCM wave about 0.8..2 an oscillator that plays one (2).
 * The two cores render alternate voices (CORE1_VOICES: the odd ones are core 1's) and a pass lasts as long as the busier
 * core, so what a patch may sound is what one core can carry: VOICE_CORE_COST. Measured: pass time ~ 14.6 % + 0.95 % a
 * tenth the busier core carries (SINGLE x8 40 -> 52.9 %, SPREAD SH-2 x8 60 -> 70.6 %, DUAL FILTER x7 56 -> 62.9 %). 56 keeps
 * every plain patch at its old voice count (8 SPREAD, 8 DUAL FILTER (the old 7 already put 4 on the busier core), 6 STACK / BINAURAL) and gives an SH-2 / PCM patch on a
 * two-chain architecture fewer. (A PCM wave costs a SPREAD voice only 1: its loops read through the spread block path.)
 * RE-PRICED 2026-11-18 (PT_PRICE_ONLY rows: each architecture held at 2/4/6/8 voices past the table, the slope = points a
 * voice on its core; after the control-tick batch, the adaptive control rate and the PCM passes): SPREAD 0 5.3..6.1 (7),
 * SPREAD 7.8..8.7 (10), DUAL OSC 7.8..8.9 (10), DUAL FILTER 7.8..8.6 (9), STACK 10.2..11.3 (12), BINAURAL 9.6..11.3 (12),
 * FULL 10.3..11.4 (12); an SH-2 filter 1.4..1.5 (2); a PCM oscillator 0.5..1.0 (1), on SPREAD / SPREAD 0 1.6..2.0 (2).
 * Every one of them at 8 voices ran without a late half (worst BINAURAL SH-2 78.9 % avg, 83 peak); the table gives 8
 * everywhere without an MFX but SH-2 on STACK / BINAURAL / FULL (6). The measured room still trims a heavier live floor. */
#define VOICE_POOL 8u                /* physical voices: SINGL_SYNTH may use all of them */
/* (2026-11-24, user: "go for 60": after the delay's quarter-rate tape and the voices' control tick in RAM the factory
 * performances ran 36..64 % avg, peaks <= 80, no late half (PERF 183 / 187 at 56); the measured room still trims a heavy
 * live floor) */
#define VOICE_CORE_COST 60u
#define VOICE_COST_PCM_FIRST 64u     /* OscParams.wavebank from here is a PCM wave (pcm_osc.h WAVE_PCM_FIRST) */
static inline uint8_t route_to(const EngineState *e, uint8_t dest)   /* a route in use goes to MDST_* dest */
{
    uint8_t i;
    for (i = 0u; i < NROUTE; i++)
        if (e->route[i].dest == dest && e->route[i].src != MSRC_NONE && e->route[i].amount != BIPOLAR_CENTER)
            return 1u;
    return 0u;
}
static inline uint8_t varch_voice_cost(const EngineState *e)
{
    uint8_t a = varch_of(e), c, sh2 = 0, pcm = 0, bin = a == VARCH_BINAURAL;
    switch (a) {
    case VARCH_SPREAD:      c = (e->oscmix.spread || route_to(e, MDST_SPREAD)) ? 10u : 7u; break;
    case VARCH_DUAL_OSC:    c = 10u; break;
    case VARCH_DUAL_FILTER: c = 9u; break;
    case VARCH_STACK:       c = 12u; break;
    case VARCH_BINAURAL:    c = 12u; break;
    case VARCH_FULL:        c = 12u; break;
    case VARCH_DUAL_SPREAD: c = 10u; break;     /* (retired: loads as DUAL OSC) */
    default:                c = 7u; break;      /* SINGLE (retired: loads as SPREAD 0) */
    }
    sh2 = (uint8_t)((e->filter[0].flavour == FLT_SH2) * (bin ? 2 : 1) +
                    (varch_uses_filter2(a) && e->filter[1].flavour == FLT_SH2));
    pcm = (uint8_t)((e->osc[0].wavebank >= VOICE_COST_PCM_FIRST) * (bin ? 2 : 1) +
                    (varch_uses_osc2(a) && e->osc[1].wavebank >= VOICE_COST_PCM_FIRST));
    /* THE ROUTES (2026-11-20, measured on the FM1, PT_COST_ONLY rows): each route in use costs every voice ~0.2 points on
     * its core (DUAL OSC, 2/4/6/8 voices: 1 route 8.9 a voice, 8 routes 10.5, 16 routes 11.7). The table above was
     * measured with one route; past it, 2 tenths each 9 routes, rounded: 4..7 routes +1, 8..12 +2, 13..16 +3 */
    uint8_t r = 0;
    for (uint8_t i = 0u; i < NROUTE; i++)
        r = (uint8_t)(r + (e->route[i].src != MSRC_NONE && e->route[i].dest != MDST_NONE &&
                           e->route[i].amount != BIPOLAR_CENTER));
    uint8_t rt = r > 1u ? (uint8_t)(((r - 1u) * 2u + 4u) / 9u) : 0u;
    /* THE NOISE (measured 2026-11-22 on the FM1, PT_NOISE_ONLY rows: 2/4/6/8 voices on SINGLE, SPREAD, STACK; the slope x2 =
     * points a voice on its core, 0.95 a tenth). A noise source on costs ~0.6..1.1 tenths a voice (its band or air); CLUSTER
     * on top of a band or the air ~1.4..1.9 more (its three extra bands, rounded up to 2); INHARM costs nothing. Before this,
     * noise and CLUSTER were free (CLUSTER a tenth, estimated). */
    uint8_t nz = (uint8_t)(noise_amount_of(e) > 0u);
    uint8_t cl = (uint8_t)(e->noise_cluster && nz && e->oscmix.noise_colour >= WHISTLE_BAND ? 2u : 0u);
    return (uint8_t)(c + 2u * sh2 + (a == VARCH_SPREAD || a == VARCH_SINGLE ? 2u : 1u) * pcm + rt + nz + cl);   /* (2026-11-18: PCM on
                                    * SPREAD reads four playheads, 1.6..2.0 a voice; elsewhere 0.5..1.0 an oscillator) */
}

/* The cost one core carries when layer A sounds voices 0..na-1 and layer B the top nb of the pool (voice v is on core
 * v & 1): L2 sits at the top of the pool so an odd count on either layer still splits the cores evenly. */
static inline uint8_t voice_core_cost(uint8_t wa, uint8_t na, uint8_t wb, uint8_t nb, uint8_t core)
{
    uint32_t t = 0;
    for (uint8_t v = 0; v < VOICE_POOL; v++) {
        if ((v & 1u) != core) continue;
        if (v < na) t += wa;
        if (v >= VOICE_POOL - nb) t += wb;
    }
    return (uint8_t)(t > 255u ? 255u : t);
}

/* THE MFX IS PRICED BY TYPE (2026-11-11). It runs after the voices, so what it costs comes off every core's share. Measured
 * on the FM1 (PERF TEST LOOP rows, STACK dual loop, docs/mfx-cost-model.md): the cost depends on the type AND on how many
 * layers run one, because of how the runtime schedules them (synth_runtime.c):
 *  - one MFX on: its two sides split across the cores: a fixed part (the plan, ~3) + half its full cost;
 *  - both on: each whole on its own core, in parallel: the dearer one's full cost, + ~2 when the types differ.
 * A phaser on one layer and a BRONZE AGE on the other therefore costs what BRONZE AGE on both does. The full costs, tenths
 * (1 tenth = 0.95 pass points), are the both-layers rows less the loop without one, rounded up. (BBD ECHO does not split
 * across the cores: one layer costs the same as both; the one-layer price over-charges it by ~3.) */
#define VOICE_MFX_FIXED 3u
#define VOICE_MFX_DIFF 2u
static inline uint8_t mfx_full_cost(uint8_t type)
{
    /* by MFX_* (dsp/mfx.h; test_voice_pool.c checks the count): OFF, STONE PHASER, ENSEMBLE, ENS+PHASER, CHORUS, TAPE FLANGER,
     * BBD FLANGER, PITCH SHIFT, BBD ECHO, BRONZE AGE (METAL), WIND HARP, RINGING STONES, BULLROARER */
    static const uint8_t X[13] = {0, 8, 11, 13, 9, 11, 9, 10, 8, 20, 11, 11, 10};
    return X[type < 13u ? type : 0u];
}
static inline uint8_t voice_overhead(const EngineState *l1, const EngineState *l2, uint8_t both_layers)
{
    uint8_t ta = l1->mfx.type, tb = both_layers ? l2->mfx.type : 0u;
    uint8_t xa = mfx_full_cost(ta), xb = mfx_full_cost(tb);
    if (ta && tb)
        return (uint8_t)((xa > xb ? xa : xb) + (ta != tb ? VOICE_MFX_DIFF : 0u));
    if (ta || tb)
        return (uint8_t)(VOICE_MFX_FIXED + ((ta ? xa : xb) + 1u) / 2u);
    return 0;
}

/* THE TRANSPORT IS ALWAYS PRICED (2026-11-20, the user: "voice pricing needs to take into account basic sequencer /
 * arp / record use. Don't ever assume that those will be off"): what the SEQ, the ARP and the REC page's loop cost as
 * a player leaves them running (perf_test.c loop_slot: a 16th bass line, the ARP's 16ths over pad chords, 120 BPM, the
 * REC loop on top), measured on the FM1 as the pass floor's rise over a stopped one, in tenths. Taken off every core's
 * share whether they run or not, so pressing PLAY or REC never finds the voices over budget. Measured (PT_COST_ONLY
 * C LOOP rows, 4 DUAL OSC voices held, floor 16.3 %): SEQ + ARP alone +0.2, with the REC loop +3.3..4.1 (keys' chords
 * over it too: +3.9); 4.1 / 0.95, rounded up */
#define VOICE_TRANSPORT_COST 5u
/* The most voices one patch may sound alone: the most one core's share of them can carry. */
static inline uint8_t varch_voice_cap(const EngineState *e)
{
    uint8_t w = varch_voice_cost(e), n,
            room = (uint8_t)(VOICE_CORE_COST - VOICE_TRANSPORT_COST - voice_overhead(e, e, 0u));
    for (n = VOICE_POOL; n > 1u; n--)
        if (voice_core_cost(w, n, 0u, 0u, 0u) <= room) break;
    return n;
}

/* the voice modes that sound one voice (UNISON stacks the layer's voices, so it is not one of them) */
static inline uint8_t vmode_one_voice(uint8_t m)
{
    return (uint8_t)(m == VMODE_MONO_RETRIG || m == VMODE_MONO_LEGATO);
}

/* Voices one layer may use (5.3). SINGL_SYNTH: layer 1 gets its patch's cap, layer 2 none. DUAL / SPLIT: both layers
 * sound at once and share the pool by price: the most voices in all that each core can carry, layer 1 at the bottom of the
 * pool and layer 2 at the top, as even between the layers as the price allows (two 8-voice patches: 4 + 4; a cheap layer
 * gets what a dear one leaves; the search is a thousand operations: pool_range caches it). A MONO layer (vm1 / vm2, 2026-10-25) sounds one voice and the other layer gets the rest.
 * Never below 1. */
static inline uint8_t layer_voice_limit_w(uint8_t perf_mode, uint8_t w1, uint8_t w2, uint8_t over, uint8_t vm1, uint8_t vm2,
                                          uint8_t layer)
{
    uint8_t m1 = vmode_one_voice(vm1), m2 = vmode_one_voice(vm2);
    uint8_t best0 = 1u, best1 = 1u, n0, n1;
    uint16_t best_key = 0u;
    if (perf_mode != PERF_DUAL_SYNTH && perf_mode != PERF_SPLIT_SYNTH) {
        uint8_t n;
        if (layer != 0u) return 0u;
        if (m1) return 1u;
        for (n = VOICE_POOL; n > 1u; n--)
            if (voice_core_cost(w1, n, 0u, 0u, 0u) <= VOICE_CORE_COST - over) break;
        return n;
    }
    for (n0 = 1u; n0 <= (m1 ? 1u : VOICE_POOL - 1u); n0++) {
        for (n1 = 1u; n1 <= (m2 ? 1u : VOICE_POOL - n0); n1++) {
            uint16_t key;
            if (n0 + n1 > VOICE_POOL) break;
            if (voice_core_cost(w1, n0, w2, n1, 0u) > VOICE_CORE_COST - over || voice_core_cost(w1, n0, w2, n1, 1u) > VOICE_CORE_COST - over)
                continue;
            /* most voices in all, then the evener split, then layer 1 the larger */
            key = (uint16_t)((n0 + n1) * 256u + (15u - (n0 > n1 ? n0 - n1 : n1 - n0)) * 16u + n0);
            if (key > best_key) {
                best_key = key; best0 = n0; best1 = n1;
            }
        }
    }
    return layer == 0u ? best0 : best1;
}

static inline uint8_t layer_voice_limit(uint8_t perf_mode, const EngineState *l1, const EngineState *l2,
                                        uint8_t vm1, uint8_t vm2, uint8_t layer)
{
    return layer_voice_limit_w(perf_mode, varch_voice_cost(l1), varch_voice_cost(l2),
                               voice_overhead(l1, l2, perf_mode == PERF_DUAL_SYNTH || perf_mode == PERF_SPLIT_SYNTH), vm1, vm2,
                               layer);
}

/* UNISON: the outer voices' detune in cents for UNISON DETUNE 0..127, square law (fine at the low end): 0 = none,
 * 24 = +-1.8, 48 = +-7, 127 = +-50 */
static inline int varch_unison_outer_cents(uint8_t detune)
{
    int d = detune > 127u ? 127 : (int)detune;
    return (d * d * 50 + 8064) / 16129;
}

/* UNISON: stereo spread. The outer voices of a stack sit this far either side of the pan (0..127 pan units, 64 =
 * centre), the rest evenly between. Fixed for now: a WIDTH control would need a new byte in the performance record. */
#define UNISON_PAN_OUTER 48
/* Pan offset of stack voice i of n, -UNISON_PAN_OUTER..+UNISON_PAN_OUTER. Voice i is detuned to position i (flattest
 * first), so the pan positions are dealt out alternately from the two ends (0, n-1, 1, n-2, ...): each side gets a mix of
 * flat and sharp voices instead of the pitch fanning out from left to right. */
static inline int varch_unison_pan(int i, int n)
{
    int k;
    if (n < 2) return 0;
    k = (i & 1) ? n - 1 - (i >> 1) : i >> 1;
    return (UNISON_PAN_OUTER * (2 * k - (n - 1))) / (n - 1);
}

/* ------------------------------------------- master performance --- */
#define PERF_NMIX 4u                 /* the MIX page's bytes (dsp/fx.h FX_L_COUNT) */
#define PERF_RESERVED 16u
/* The performance's own settings. Since 2026-10-17 WHOLE_PERF saves a WholePerformance (below): these plus copies of
 * both layers' patches, the sequence and the arp pattern, so a performance plays the same whatever is saved later. The
 * links now only say which slot each part was last loaded from (the PERF page shows them); loading a performance
 * never reads them */
typedef struct {
    uint8_t name[NAME_LEN];          /* char_map indices (0 = space), not ASCII */
    /* where the parts came from (6.1): patch slots 0..127, sequence and arp slots 0..BANK_SLOTS - 1 (127: none) */
    uint8_t layer_patch[NLAYER];     /* EngineState slots for Engine A / B */
    uint8_t seq_link;                /* 4-lane MAQ sequencer slot */
    uint8_t arp_link;                /* 8-step arp / chord slot */
    /* voice allocation (5.3) */
    uint8_t perf_mode;               /* PERF_* */
    uint8_t voice_mode[NLAYER];      /* VMODE_*, per layer in DUAL and SPLIT (SPLIT mirrored layer 1 until 2026-10-25) */
    uint8_t porta_mode[NLAYER];      /* PORTA_* */
    uint8_t porta_time[NLAYER];
    uint8_t unison_detune[NLAYER];   /* UNISON voice mode: 0 = no detune, 127 = outer voices +-50 cents */
    /* split (5.3 View 3): bounds = threshold -/+ (fade >> 1), clamped to 0..127 */
    uint8_t split_note;              /* C-1 .. G9 */
    uint8_t split_fade;
    uint8_t vel_split;
    uint8_t vel_fade;
    /* shared effects, series delay -> reverb (4.6 EFFECTS) */
    uint8_t delay_time;
    uint8_t delay_feedback;
    uint8_t reverb_decay;
    uint8_t reverb_damp;
    uint8_t delay_sync;              /* 0 / 1 */
    uint8_t reverb_type;             /* REVTYPE_* (v11: REV PREDELAY removed; DLY > REV stands in) */
    uint8_t delay_degrade;          /* TAPE DEGRADE: one macro over wow, flutter and the playback filtering (fx.c
                                      * tape_degrade): 0 = a new tape, 127 = worn out, default 36 */
    uint8_t delay_pingpong;          /* bit 0 PING PONG: the echoes alternate left and right; bits 1..7 DELAY HP
                                      * (2026-10-26): the feedback's high-pass, 0 = off (older records: 0, no
                                      * version bump). Read them through DELAY_PINGPONG / DELAY_HP */
    uint8_t delay_type;              /* bits 1..7: TUNING's ROOT (PERF_TUNING_ROOT); bit 0 unused since the BBD went
                                      * (2026-11-14: one delay, the tape; the name kept for the byte's place) */
    /* GLOBAL (4.6) */
    uint8_t midi_ch[NLAYER];         /* 0..15 = channel 1..16 (defaults 1, 2) */
    uint8_t cc1_num, cc2_num;        /* custom CC1 / CC2 controller numbers */
    uint8_t base_velocity;           /* shown 12..127, default 69 */
    uint8_t bend_range;
    uint8_t clock_source;            /* CLKSRC_* */
    uint8_t int_tempo;               /* INT TEMPO when CLOCK SOURCE is INTERNAL: BPM = 40 + value (default 80 = 120 BPM) */
    /* v11 (2026-11-03): */
    uint8_t mix[PERF_NMIX];          /* the MIX page (dsp/fx.h fx_live, FX_L_*): BALANCE, LOOP TONE (64, 64: none),
                                      * DLY > REV, REV > DLY (0: none) */
    uint8_t reserved[PERF_RESERVED]; /* spare (the CC map, the SEQ's MIDI channel, ...): 0 until a feature takes one, so
                                      * 0 must mean that feature's off / default (flash_record.h, THE ZERO RULE) */
} MasterPerformancePreset;
PS_STATIC_ASSERT(sizeof(MasterPerformancePreset) == 70u, "MasterPerformancePreset flash layout");
/* THE EFFECTS ARE PRICED (2026-11-20, the user: "voice counts scale according to our CPU use"). Measured on the FM1
 * (perf_test.c PT_COST_ONLY rows, DUAL OSC at 4 and 8 voices): each a fixed cost, whatever the voices, over the defaults
 * the voice table was measured with (TOMB, no PING PONG): SPRING +2.6 points, TUMULUS +3.3, DOLMEN +3.8, MOD +4.0,
 * CELESTIAL +5.4, PING PONG +0.7. The MIX page's cross-feeds (DLY > REV and REV > DLY, either or both), re-measured
 * 2026-11-20 after the half-rate loop with REV > DLY on core 1 (fx.c, claude-synth-0f): +4.2 at LOOP TONE 64 (was 7.0),
 * +8.3 worn (LOOP TONE off its middle: the cross-feeds' wear and the tape's own, TAPE_WEAR; was 9.0); LOOP TONE's wear
 * without the cross-feeds +1.1 (the tape's TAPE_WEAR; was 0.4, unpriced). In tenths (0.95 points), rounded up. A
 * performance-level overhead on top of the MFX's (voice_overhead), taken off every core's share */
#define VOICE_XF_COST 5u
#define VOICE_XF_WORN_COST 9u
#define VOICE_TAPE_WEAR_COST 2u
static inline uint8_t perf_fx_overhead(const MasterPerformancePreset *p)
{
    static const uint8_t REV[6] = {3u, 0u, 4u, 6u, 4u, 4u};   /* REVTYPE_*: SPRING TOMB MOD CELESTIAL DOLMEN TUMULUS */
    return (uint8_t)(REV[p->reverb_type < 6u ? p->reverb_type : 1u] + (p->delay_pingpong & 1u) +
                     ((p->mix[2] || p->mix[3]) ? (p->mix[1] != 64u ? VOICE_XF_WORN_COST : VOICE_XF_COST)
                                               : (p->mix[1] != 64u ? VOICE_TAPE_WEAR_COST : 0u)));
                                                               /* (FX_L_TONE, FX_L_DLY_REV, FX_L_REV_DLY) */
}
PS_STATIC_ASSERT(REVTYPE_COUNT == 6, "a price for every reverb type");
/* THE VOICE BUDGET (2026-11-20): the voices each layer of performance p may sound with patches a and b, everything
 * priced (each patch's architecture, filters, waves and routes; both layers' MFX; the effects). One reckoning for the
 * pool (voice_allocator.c pool_range), the panel's VOICES popup (UIStateMachine.c) and the tests. */
static inline void perf_voice_limits(const MasterPerformancePreset *p, const EngineState *a, const EngineState *b,
                                     uint8_t n[NLAYER])
{
    uint8_t mode = p->perf_mode < PERF_COUNT ? p->perf_mode : (uint8_t)PERF_SINGL_SYNTH;
    uint8_t both = (uint8_t)(mode == PERF_DUAL_SYNTH || mode == PERF_SPLIT_SYNTH);
    uint32_t over = (uint32_t)voice_overhead(a, b, both) + perf_fx_overhead(p) + VOICE_TRANSPORT_COST;
    uint8_t wa = varch_voice_cost(a), wb = varch_voice_cost(b), l;
    if (over > VOICE_CORE_COST - 8u) over = VOICE_CORE_COST - 8u;   /* (a voice always fits: never below one) */
    for (l = 0u; l < NLAYER; l++)
        n[l] = layer_voice_limit_w(mode, wa, wb, (uint8_t)over, p->voice_mode[0], p->voice_mode[1], l);
}
#define DELAY_PINGPONG(p) ((uint8_t)((p)->delay_pingpong & 1u))
#define DELAY_HP(p) ((uint8_t)((p)->delay_pingpong >> 1))
/* GLOBAL > TUNING and ROOT (2026-10-29, dsp/tuning.h) ride in the high bits of delay_sync and delay_type (bits 1..7;
 * older records: 0 = EQUAL on C, no version bump). Read all four through these */
#define DELAY_SYNC(p) ((uint8_t)((p)->delay_sync & 1u))
#define PERF_TUNING(p) ((uint8_t)((p)->delay_sync >> 1))
#define PERF_TUNING_ROOT(p) ((uint8_t)((p)->delay_type >> 1))
/* DELAY MODE (2026-11-23): bit 0 of delay_type: 0 mono (one line, 0.74 s), 1 stereo (two lines, 0.37 s, the right at 3/4) */
#define DELAY_STEREO(p) ((uint8_t)((p)->delay_type & 1u))
/* the built-in tunings first, the ones nearest Carnac's drones and stone-age sound at the top (user, 2026-10-29) */
enum {
    TUNING_EQUAL,                    /* equal temperament: none */
    TUNING_OVERTONE,                 /* harmonics 16..30 of the root: horns, throat song, drones */
    TUNING_JUST,                     /* 5-limit just intonation */
    TUNING_PYTHAGOREAN,              /* pure fifths, Eb..G# */
    TUNING_SEPTIMAL,                 /* 7-limit just: 7/4, 7/5, 8/7 */
    TUNING_RAST,                     /* maqam Rast: the 3rd and 7th a quarter tone flat */
    TUNING_MEANTONE,                 /* quarter-comma meantone, Eb..G# */
    TUNING_WERCKMEISTER,             /* Werckmeister III */
    TUNING_USER1,                    /* .. the TUNING_NUSER tables MTS SysEx loads */
    TUNING_COUNT = TUNING_USER1 + 4
};
#define TUNING_NUSER (TUNING_COUNT - TUNING_USER1)

/* WHOLE_PERF (2026-10-17): a performance and everything it plays, as SAVE took it. The FM1 keeps these in flash only
 * (preset_store_fm1.c, both patches packed there: WHOLE_PERF_STORED_SIZE) and reads one when it is loaded; the
 * clipboard holds one for COPY / PASTE */
typedef struct {
    MasterPerformancePreset perf;
    EngineState engine[NLAYER];
    SequencerLane seq[SEQ_NLANE];
    ArpState arp;
} WholePerformance;
PS_STATIC_ASSERT(sizeof(WholePerformance) == 1058u,"WholePerformance: no padding (every member is bytes)");

/* ------------------------------------------------ UI state machine --- */
/* runtime registers, never written to flash: a context shift must not touch the clock (6.3) */
typedef struct {
    uint8_t current_context;         /* UICTX_* */
    uint8_t active_synth_layer;      /* 0..NLAYER-1, the Preset Encoder focus (5.2) */
    uint8_t active_seq_lane;         /* 0..SEQ_NLANE-1 */
    uint8_t current_param_focus;     /* Select Encoder cursor row */
    uint8_t save_submenu_page;       /* SAVEPAGE_* */
} UIState;
PS_STATIC_ASSERT(sizeof(UIState) == 5u, "UIState layout");
