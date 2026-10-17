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
#define NMODSLOT 3u                  /* 4.5 lists 4; 2.1's menu row lists 3, and MSRC_MODSLOT1..3 reach only 3 (MOD 4 removed 2026-10-15) */
#define SEQ_NLANE 4u
#define SEQ_NSTEP 16u
#define ARP_NSTEP 8u                 /* 8.1: keys 14..21, white keys only */
#define NAME_LEN 16u                 /* 6.2.1: character cursor 0..15 */
#define CHAR_MAP_LEN 41u             /* char_map[42]: " A..Z0..9.-_/" and its NUL */
#define PATCH_SLOTS 128u             /* stored EngineState patches; each layer plays one via layer_patch */

/* -------------------------------------------------- shared value lists --- */
enum {                               /* mod_source_list (4) */
    MSRC_NONE, MSRC_VELOCITY, MSRC_KEYBOARD, MSRC_MODWHEEL, MSRC_AFTERTOUCH,
    MSRC_ENV1, MSRC_ENV2, MSRC_ENV3, MSRC_LFO1, MSRC_LFO2, MSRC_LFO3,
    MSRC_MODSLOT1, MSRC_MODSLOT2, MSRC_MODSLOT3, MSRC_CUSTOMCC1, MSRC_CUSTOMCC2,
    MSRC_COUNT
};
enum {                               /* function_list (4.5) */
    MFN_MUL, MFN_ADD, MFN_SUB, MFN_MAX, MFN_MIN, MFN_SAMPLE_B_ON_A, MFN_QUANTIZE_B_TO_A,
    MFN_ABS_DIFF, MFN_LAG, MFN_DIODE, MFN_COUNT
};
enum { XMOD_FM, XMOD_AM, XMOD_RING, XMOD_PM, XMOD_COUNT };                  /* OSCMIXER cross-mod type */
enum { SUBOCT_DOWN1, SUBOCT_DOWN2, SUBOCT_COUNT };                          /* sub-oscillator offset */
enum {                                                                      /* sub-oscillator waveform */
    SUBWAVE_SINE,                    /* wavetable_sine, always in the app (gen_wavetables.py sub_sine) */
    SUBWAVE_SQUARE,
    SUBWAVE_CURRENT,                 /* OSC1's live bank and morph frame, at the sub's own octave-down rate */
    SUBWAVE_COUNT
};
enum { FLT_SVF, FLT_MULTI, FLT_MS20, FLT_MS20_HP, FLT_FORMANT, FLT_COUNT };  /* filter circuit flavour, in the
        * FLAVOUR knob's order (MS20: its lowpass, MS20_HP its highpass; reordered 2026-10-15, FREC_VER_ENGINE 4). MULTI: the zero-delay 4-pole ladder multimode
        * (dsp/filter.h), its morph the mode LP24 .. LP18 .. LP12 .. LP6 .. BP .. HP12 .. HP24 .. NOTCH. Slot 1 was the
        * LADDER (2026-10-15): its slopes are MULTI's first four modes, the same circuit */
PS_STATIC_ASSERT(FLT_MULTI == 1, "filter_feedback_q12 (dsp/filter.h) tests flavour 1 for MULTI");
/* OSCMIXER noise colour, a morph 0..127: white at 0, pink at 64, brown at 127, crossfaded in between */
#define NOISE_WHITE 0u
#define NOISE_PINK 64u
#define NOISE_BROWN 127u
enum {                               /* voice architecture, per patch: the signal path every voice runs (4.0) */
    VARCH_SINGLE,                    /* OSC1 -> FILTER1 */
    VARCH_DUAL_OSC,                  /* OSC1 + OSC2 (cross-mod) -> FILTER1 */
    VARCH_SPREAD,                    /* OSC1 as 4 detuned playheads -> FILTER1 */
    VARCH_DUAL_FILTER,               /* OSC1 -> FILTER1 -> FILTER2 (serial .. parallel) */
    VARCH_STACK,                     /* (OSC1 -> FILTER1, amp ENV1) + (OSC2 -> FILTER2, amp ENV2), blended */
    VARCH_BINAURAL,                  /* OSC1 -> FILTER1 once per ear; the right ear is beat-offset, its LFO mod phase-offset */
    VARCH_STRIKE,                    /* SINGLE + a one-shot attack sample (dsp/attacks.h) at each note start, unfiltered:
                                      * the LA synth's PCM attack in front of its partial (2026-10-16) */
    VARCH_COUNT
};
#define VARCH_BIT(a) (1u << (a))
#define VARCH_ALL ((1u << VARCH_COUNT) - 1u)
enum { DRIVE_SOFT, DRIVE_HARD, DRIVE_FOLD, DRIVE_TAPE, DRIVE_COUNT };       /* AMP drive model */
/* reverb type (2026-10-16: PLATE and HALL removed, ROOM renamed TOMB, numbers shifted, FREC_VER_PERF 5; new ones at the end). MOD: Lexicon-inspired, chorused;
 * CELESTIAL: was SHIMMER; DOLMEN: a cave (a small stone chamber); TUMULUS: a cavern (Tumulus Saint-Michel, Carnac) */
enum { REVTYPE_SPRING, REVTYPE_TOMB, REVTYPE_MOD, REVTYPE_CELESTIAL, REVTYPE_DOLMEN, REVTYPE_TUMULUS, REVTYPE_COUNT };
/* delay type (2026-10-16): TAPE the tape echo, BBD a bucket-brigade echo (fx.c bbd_delay_block). New ones at the end */
enum { DELAYTYPE_TAPE, DELAYTYPE_BBD, DELAYTYPE_COUNT };
enum { CLKSRC_INTERNAL, CLKSRC_MIDI, CLKSRC_COUNT };
enum { VELCURVE_SOFT, VELCURVE_LINEAR, VELCURVE_HARD, VELCURVE_COUNT };   /* GLOBAL > VEL CURVE: incoming MIDI */
enum { CLKOUT_OFF, CLKOUT_USB, CLKOUT_COUNT };                            /* GLOBAL > CLOCK OUT (X0X has no TRS out) */
#define MASTER_TUNE_MIN 14u          /* GLOBAL > MASTER TUNE: stored 14..114, 64 = A440, shown -50..+50 cents */
#define MASTER_TUNE_MAX 114u

/* OSC FINE TUNE (2026-10-17): the stored -64..+63 (64 = 0) on a square law, +-7 semitones at the ends (+-700 cents,
 * -64 held there), fine near the middle: 1 step 0.2 cents, 8 steps 11, 23 steps 92 */
static inline int32_t osc_fine_cents(uint8_t v)
{
    int32_t b = (int32_t)v - 64, c = b * (b < 0 ? -b : b) * 700 / 3969;
    return c > 700 ? 700 : c < -700 ? -700 : c;
}

/* The instrument's own settings (GLOBAL, 2026-10-17): kept across power cycles with the THEME (the FM1's settings
 * record), never part of a performance, so loading one never retunes the instrument */
typedef struct {
    uint8_t master_tune;             /* MASTER_TUNE_MIN..MAX, 64 = 0 cents */
    uint8_t vel_curve;               /* VELCURVE_* */
    uint8_t clock_out;               /* CLKOUT_*: MIDI clock, start and stop out while the clock is internal */
} InstrumentSettings;
enum { PERF_SINGL_SYNTH, PERF_DUAL_SYNTH, PERF_SPLIT_SYNTH, PERF_COUNT };   /* 5.3 View 1 */
enum { VMODE_MONO_RETRIG, VMODE_MONO_LEGATO, VMODE_POLY_REUSE, VMODE_POLY_RESET, VMODE_UNISON_STACK,
       VMODE_COUNT };
enum { PORTA_CONST_RATE, PORTA_CONST_TIME, PORTA_LEGATO_TIED, PORTA_COUNT };
enum { ARPLAYER_L1, ARPLAYER_L2, ARPLAYER_BOTH, ARPLAYER_COUNT };          /* BOTH: DUAL_SYNTH only */
enum {                               /* 8.2 time division reference */
    ARPDIV_2, ARPDIV_1, ARPDIV_1_2, ARPDIV_1_4, ARPDIV_1_4T, ARPDIV_1_8, ARPDIV_1_8T,
    ARPDIV_1_16, ARPDIV_1_16T, ARPDIV_1_32, ARPDIV_COUNT
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
#define ARP_NOCT 4u                  /* octave_range 0..3 = the pattern spans 1..4 octaves */
#define ARP_NVAR 24u                 /* variation: 8.3 rhythm_variation_table row r, column c (A / B) = r * 2 + c */
enum {                               /* style (knob 3, "phrase category"): the order a step's strikes take the notes */
    ARPSTYLE_BLOCK,                  /* the whole chord on every strike */
    ARPSTYLE_UP, ARPSTYLE_DOWN, ARPSTYLE_UPDN, ARPSTYLE_DNUP, ARPSTYLE_RANDOM,   /* one note per strike */
    ARPSTYLE_STRUM_UP, ARPSTYLE_STRUM_DN,                                       /* the chord, spread across the cell */
    ARPSTYLE_COUNT
};

/* 8.1: one step key's capture: the chord struck plus the 4 ARP knob values at that moment (2.2) */
typedef struct {
    uint8_t chord_root;              /* the chord key's MIDI note 0..125 (the chord's bass), ARP_CHORD_TIE, ARP_CHORD_EMPTY */
    uint8_t gate_length;             /* knob 1: strike length = its cells x (value + 1) / 128, 127 = legato */
    uint8_t octave_range;            /* knob 2: 0..ARP_NOCT - 1 */
    uint8_t style;                   /* knob 3: ARPSTYLE_* */
    uint8_t variation;               /* knob 4: 0..ARP_NVAR - 1 */
    uint8_t accent;                  /* 0 / 1: the step's first strike plays at ACCENT VEL (the ACCENT key toggles it) */
} ArpChordStep;
PS_STATIC_ASSERT(sizeof(ArpChordStep) == 6u, "ArpChordStep flash layout");

/* the 8-step chord pattern ARP_SEQ saves and arp_link loads; footers are the 8.2 menu list */
typedef struct {
    ArpChordStep steps[ARP_NSTEP];
    uint8_t target_layer;            /* ARPLAYER_* */
    uint8_t time_division;           /* ARPDIV_*: one step's length */
    uint8_t sequence_length;         /* 1..8 */
    uint8_t transpose_maq_option;    /* 0 / 1: the MAQ NOTE lanes follow each step's chord root and the chord key */
    uint8_t current_genre_bank;      /* arp_chords.h genre, 0..ARP_GENRE_COUNT - 1 */
    uint8_t accent_velocity;         /* 1..127: an accented step's first strike */
} ArpState;
PS_STATIC_ASSERT(sizeof(ArpState) == 54u, "ArpState flash layout");

/* the factory arp slot: every step empty (captured steps start from the default knob values), 1/4 steps (8.3's
 * rhythm cells are then the 16ths its table names), 8 steps, layer 1, no MAQ transpose, the first genre, no accents */
#define ARP_GATE_DEFAULT 96u
#define ARP_ACCENT_DEFAULT 120u
static inline void arp_defaults(ArpState *a)
{
    uint8_t i;
    for (i = 0u; i < ARP_NSTEP; i++) {
        a->steps[i].chord_root = ARP_CHORD_EMPTY;
        a->steps[i].gate_length = ARP_GATE_DEFAULT;
        a->steps[i].octave_range = 0u;
        a->steps[i].style = ARPSTYLE_UP;
        a->steps[i].variation = 0u;
        a->steps[i].accent = 0u;
    }
    a->target_layer = ARPLAYER_L1;
    a->time_division = ARPDIV_1_4;
    a->sequence_length = ARP_NSTEP;
    a->transpose_maq_option = 0u;
    a->current_genre_bank = 0u;
    a->accent_velocity = ARP_ACCENT_DEFAULT;
}

/* --------------------------------------------- engine (layer patch) --- */
/* the parameter pages a layer owns (4.1 .. 4.6), one block per layer; (bip) = bipolar */
typedef struct {                     /* OSC1 / OSC2 */
    uint8_t scan;                    /* knob 1 */
    uint8_t octave;                  /* knob 2, (bip) -3..+3 */
    uint8_t fine;                    /* knob 3 (a row in SINGLE / SPREAD / STRIKE), (bip): osc_fine_cents, +-7 semitones */
    uint8_t pitch_mod_amt;           /* knob 4 */
    uint8_t wavebank;
    uint8_t scan_mod_src;            /* MSRC_* (default ENV3) */
    uint8_t scan_mod_amt;
    uint8_t pitch_mod_src;           /* MSRC_* (default LFO1) */
    uint8_t key_track;               /* (bip), default 127 = full 1V/oct */
    uint8_t sync;                    /* 0 / 1, OSC2 only */
} OscParams;

typedef struct {                     /* OSCMIXER; which knob a field sits on depends on the architecture (2.2) */
    uint8_t balance;                 /* knob 1 DUAL OSC, (bip) OSC1 .. OSC2 */
    uint8_t xmod_amt;                /* knob 2 DUAL OSC / STACK */
    uint8_t sub_level;               /* knob 3 (knob 4 SINGLE), every architecture */
    uint8_t spread;                  /* knob 1 SPREAD only */
    uint8_t mix_mod_src;             /* MSRC_* (default KEYBOARD) */
    uint8_t mix_mod_amt;
    uint8_t xmod_mod_src;            /* MSRC_* (default MODWHEEL) */
    uint8_t xmod_type;               /* XMOD_*, knob 1 STACK */
    uint8_t xmod_mod_amt;
    uint8_t spread_mod_src;          /* MSRC_* (default MODWHEEL), SPREAD only */
    uint8_t spread_mod_amt;          /* knob 4 SPREAD only */
    uint8_t sub_octave;              /* SUBOCT_*, knob 3 SINGLE */
    uint8_t sub_waveform;            /* SUBWAVE_* */
    uint8_t noise_level;             /* knob 4 (knob 2 SINGLE / SPREAD), every architecture */
    /* noise generator (4.1) */
    uint8_t noise_colour;            /* morph: NOISE_WHITE 0 .. NOISE_PINK 64 .. NOISE_BROWN 127, knob 1 SINGLE */
    uint8_t noise_mod_src;           /* MSRC_* (default MODWHEEL): moves the noise level */
    uint8_t noise_mod_amt;           /* (bip) */
    /* BINAURAL: how the right ear's oscillator differs from the left's */
    uint8_t bin_mod_phase;           /* knob 1: LFO phase offset of the right ear's pitch / scan mod, 0..127 = 0..357 deg */
    uint8_t bin_beat;                /* knob 2: right ear pitch offset, 0..127 = 0..31.75 Hz in 0.25 Hz steps */
    uint8_t bin_scan_offset;         /* (bip): right ear wavetable scan offset */
    /* noise colour modulation (4.1) */
    uint8_t noise_col_mod_src;       /* MSRC_* (default MODWHEEL): moves the noise colour morph */
    uint8_t noise_col_mod_amt;       /* (bip) */
    /* STRIKE's attack (2026-10-16, FREC_VER_ENGINE 6): which sound, its level, its tuning. A pitched sound follows
     * OSC1's pitch, an unpitched one plays at its own rate; either way TUNE transposes it. */
    uint8_t atk_sample;              /* knob 1 STRIKE: 0..ATK_COUNT-1 (dsp/attacks.h) */
    uint8_t atk_level;               /* knob 2 STRIKE, default ATK_LEVEL_DEFAULT */
    uint8_t atk_tune;                /* (bip) semitones, -24..+24 used */
    /* 2026-10-16 (FREC_VER_ENGINE 7) */
    uint8_t atk_filter;              /* 0 = the attack skips the filter and ENV1 (the D-50's way), 127 = all of it goes into
                                      * FILTER1 with the oscillator (and so through ENV1 too); between: both, crossfaded */
    uint8_t atk_decay;               /* 127 = the sound's own length; lower fades it out sooner: a time constant of
                                      * 2 + d^2/16 ticks (64: 94 ms, 20: 10 ms, 0: under 1 ms) */
    uint8_t atk_start;               /* where it starts: 0 = the beginning .. 127 = halfway in (skips a click, a tongue) */
    uint8_t atk_vel;                 /* velocity to attack level: 0 = fixed, 127 = as the voice (default) */
    uint8_t atk_track;               /* ATKTRK_*: follow the keys or not */
    uint8_t atk_bend;                /* (bip) BEND: the attack starts this far off pitch, half a semitone a step (+-32
                                      * semitones; + above, - below) and glides to it, a ~40 ms time constant */
    /* 2026-10-17 (FREC_VER_ENGINE 8) */
    uint8_t atk_bend_mod_src;        /* MSRC_* (default VELOCITY): moves BEND while the attack glides */
    uint8_t atk_bend_mod_amt;        /* (bip) knob 4 STRIKE ATTACK: a full source adds up to +-32 semitones to BEND */
} OscMixerParams;
enum { ATKTRK_AUTO, ATKTRK_FIXED, ATKTRK_TRACK, ATKTRK_COUNT };   /* AUTO: pitched sounds track, the others do not.
        * TRACK on an unpitched sound: its own pitch at C4 (60) */

typedef struct {                     /* FILTER1 / FILTER2 */
    uint8_t morph;                   /* knob 1, by flavour: SVF LP -> BP -> HP, MULTI mode LP24 -> .. -> NOTCH, MS-20 drive,
                                      * FORMANT band spread (dsp/filter.h) */
    uint8_t cutoff;                  /* knob 2 */
    uint8_t resonance;               /* knob 3 */
    uint8_t mod_amt;                 /* knob 4, (bip) */
    uint8_t flavour;                 /* FLT_* */
    uint8_t mod_src;                 /* MSRC_* (default ENV2) */
    uint8_t morph_mod_src;           /* MSRC_* (default LFO1): moves the type morph, every architecture */
    uint8_t morph_mod_amt;           /* (bip) */
} FilterParams;

typedef struct {                     /* FILTER MIXER (DUAL FILTER, STACK, BINAURAL; hidden otherwise) */
    uint8_t balance;                 /* knob 1, (bip) DUAL FILTER: FILTER1 .. FILTER2; STACK: blend stack A .. B */
    uint8_t routing;                 /* knob 2 DUAL FILTER: serial .. parallel */
    uint8_t balance_mod_amt;         /* knob 3, (bip) DUAL FILTER balance mod / STACK blend mod */
    uint8_t routing_mod_amt;         /* knob 4, (bip) DUAL FILTER */
    uint8_t balance_mod_src;         /* MSRC_* (default ENV3) */
    uint8_t routing_mod_src;         /* MSRC_* (default ENV3), DUAL FILTER */
    /* STACK */
    uint8_t stack_width;             /* knob 2: 0 = A and B centred, 127 = A hard left, B hard right */
    uint8_t env3_cutoff;             /* knob 4, (bip): ENV3 to both filter cutoffs */
    /* BINAURAL */
    uint8_t bin_mod_phase;           /* knob 1: LFO phase offset of the right ear's filter mod, 0..127 = 0..357 deg */
    uint8_t bin_width;               /* knob 2: 0 = both ears summed (mono), 127 = full left / right separation */
    uint8_t bin_cutoff_offset;       /* knob 3, (bip): + raises the right ear's cutoff and lowers the left's */
    uint8_t bin_phase_mod_amt;       /* knob 4, (bip): modulates both phase offsets (OSCMIXER and FILTER MIXER) */
    uint8_t bin_phase_mod_src;       /* MSRC_* (default MODWHEEL) */
    uint8_t bin_cutoff_mod_src;      /* MSRC_* (default LFO1): moves CUTOFF L/R */
    uint8_t bin_cutoff_mod_amt;      /* (bip) */
} FilterMixerParams;

typedef struct {                     /* ENVELOPE1..3 */
    uint8_t attack, decay, sustain, release;   /* knobs 1..4 */
    uint8_t vel_to_time;             /* (bip) */
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
    uint8_t speed_mod_src;           /* MSRC_* (default VELOCITY) */
    uint8_t speed_mod_amt;           /* (bip) */
    uint8_t clock_sync;              /* 0 / 1 */
    uint8_t phase;
    uint8_t key_reset;               /* 0 / 1 */
} LfoParams;

typedef struct {                     /* MODSLOT1..3 */
    uint8_t src_a;                   /* knob 1, MSRC_* */
    uint8_t func;                    /* knob 2, MFN_* */
    uint8_t src_b;                   /* knob 3, MSRC_* */
    uint8_t amount;                  /* knob 4, (bip) */
} ModSlotParams;

typedef struct {                     /* AMP (4.6 registry; 2.2's knob row differs) */
    uint8_t drive;                   /* knob 1 */
    uint8_t amp_mod_depth;           /* knob 2 */
    uint8_t delay_send;              /* knob 3 */
    uint8_t reverb_send;             /* knob 4 */
    uint8_t amp_mod_src;             /* MSRC_*: modulates the hardwired ENV1 level */
    uint8_t pan_mod_src;             /* MSRC_* (default KEYBOARD) */
    uint8_t pan_mod_depth;
    uint8_t drive_model;             /* DRIVE_* */
} AmpParams;

/* one layer's patch: what PATCH_PRESET saves and LAYER n PATCH MEM LINK loads */
typedef struct {
    OscParams osc[NOSC];
    OscMixerParams oscmix;
    FilterParams filter[NFILTER];
    FilterMixerParams fltmix;
    EnvParams env[NENV];
    LfoParams lfo[NLFO];
    ModSlotParams mod[NMODSLOT];
    AmpParams amp;
    uint8_t arch;                    /* VARCH_*: the voice architecture, HOME View 1 / 2 (5.3) */
    uint8_t name[NAME_LEN];          /* char_map indices (0 = space), not ASCII: RENAME PATCH (6.2.1) */
} EngineState;
PS_STATIC_ASSERT(sizeof(EngineState) == 181u, "EngineState layout");
/* In flash a patch is ENGINE_STORED_SIZE bytes (dsp/flash_record.h engine_store_pack): 173 is the most that keeps 22
 * patches to a store chunk (preset_store_fm1.c: (3840 - 16) / 173), so the chunks stay where they were. Since v7 the
 * stored form packs the name 6 bits a character and leaves out OSC1's SYNC and ENV1's INVERT (nothing reads them); v8
 * also packs the three LFOs' CLOCK SYNC and KEY RESET into one byte: 181 in RAM, 171 + 2 spare in flash. v7 (179 in
 * RAM, atk_bend in the last stored byte) lacked the two BEND MOD bytes, v6 (173, stored as in RAM) atk_filter on,
 * v5 (170 bytes) all the attack bytes. */
#define ENGINE_STORED_SIZE 173u
#define ENGINE_V6_SIZE 173u
#define ENGINE_V6_INSERT ((uint32_t)offsetof(EngineState, oscmix) + (uint32_t)offsetof(OscMixerParams, atk_filter))
#define ATK_LEVEL_DEFAULT 100u
#define ATK_V8_DEFAULTS {0u, 127u, 0u, 127u, ATKTRK_AUTO, 64u, MSRC_VELOCITY, 64u}   /* atk_filter .. atk_bend_mod_amt */
#define ENGINE_V7_RAM 179u
#define ENGINE_V5_SIZE 170u
#define ENGINE_V5_INSERT ((uint32_t)offsetof(EngineState, oscmix) + (uint32_t)offsetof(OscMixerParams, atk_sample))

/* ------------------------------------------------ voice architecture --- */
/* an out-of-range architecture (a corrupt flash read) plays as SINGLE, the cheapest */
static inline uint8_t varch_of(const EngineState *e)
{
    return e->arch < VARCH_COUNT ? e->arch : (uint8_t)VARCH_SINGLE;
}

/* the pages an architecture uses; the UI hides the rest (OSC1, OSCMIXER and FILTER1 always show) */
static inline uint8_t varch_uses_osc2(uint8_t a) { return (uint8_t)(a == VARCH_DUAL_OSC || a == VARCH_STACK); }
static inline uint8_t varch_uses_filter2(uint8_t a) { return (uint8_t)(a == VARCH_DUAL_FILTER || a == VARCH_STACK); }
static inline uint8_t varch_uses_fltmix(uint8_t a)
{
    return (uint8_t)(a == VARCH_DUAL_FILTER || a == VARCH_STACK || a == VARCH_BINAURAL);
}

/* The most voices one patch may sound so its worst case still renders in real time, by architecture. The filter
 * circuit does not change it: every flavour, MULTI included, fits each cap on the FM1 (PERF TEST 2026-10-15, two
 * cores, heavy patch: worst SPREAD + MULTI x 7 at 64 %, budget 70 %; docs/fm1-measure-checklist.md). */
#define VOICE_POOL 8u                /* physical voices: SINGL_SYNTH may use all of them */
static inline uint8_t varch_voice_cap(const EngineState *e)
{
    static const uint8_t cap[VARCH_COUNT] = {
        8u,                          /* SINGLE */
        5u,                          /* DUAL OSC */
        6u,                          /* SPREAD (7 -> 6 2026-10-15: 7 with CELESTIAL went over budget) */
        7u,                          /* DUAL FILTER (5 -> 7 2026-10-15, measured) */
        6u,                          /* STACK (4 -> 6 2026-10-15, measured) */
        6u,                          /* BINAURAL: FILTER1's settings run twice, once per ear (4 -> 6 2026-10-15) */
        8u,                          /* STRIKE: SINGLE + an 8-bit sample read (~5 % of a voice on the host; to measure) */
    };
    return cap[varch_of(e)];
}

/* Voices one layer may use (5.3). SINGL_SYNTH: layer 1 gets its patch's cap, layer 2 none. DUAL / SPLIT: both
 * layers sound at once, so the pair shares the lower cap of the two patches, layer 1 taking the odd voice (two
 * 8-voice patches: 4 + 4, the spec's split; 5: 3 + 2; 4: 2 + 2). Every voice then costs at most what the more
 * expensive patch's voice costs, so the pair stays inside that patch's budget. Never below 1. */
static inline uint8_t layer_voice_limit(uint8_t perf_mode, const EngineState *l1, const EngineState *l2, uint8_t layer)
{
    uint8_t c1 = varch_voice_cap(l1), c2 = varch_voice_cap(l2), lo, n;
    if (perf_mode != PERF_DUAL_SYNTH && perf_mode != PERF_SPLIT_SYNTH)
        return layer == 0u ? c1 : (uint8_t)0u;
    lo = c1 < c2 ? c1 : c2;
    n = (uint8_t)(layer == 0u ? (lo + 1u) >> 1 : lo >> 1);
    return n > 0u ? n : (uint8_t)1u;
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
/* WHOLE_PERF: the performance file. Its components are child indices (6.3), not copies, so a patch,
 * a sequence or an arp pattern can be swapped without rewriting the performance */
typedef struct {
    uint8_t name[NAME_LEN];          /* char_map indices (0 = space), not ASCII */
    /* links (6.1), 0..127 each */
    uint8_t layer_patch[NLAYER];     /* EngineState slots for Engine A / B */
    uint8_t seq_link;                /* 4-lane MAQ sequencer slot */
    uint8_t arp_link;                /* 8-step arp / chord slot */
    /* voice allocation (5.3) */
    uint8_t perf_mode;               /* PERF_* */
    uint8_t voice_mode[NLAYER];      /* VMODE_*; layer 2 mirrors layer 1 in SPLIT_SYNTH */
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
    uint8_t reverb_type;             /* REVTYPE_* */
    uint8_t reverb_predelay;
    uint8_t delay_degrade;           /* TAPE DEGRADE: one macro over wow, flutter and the playback filtering (fx.c
                                      * tape_degrade): 0 = a new tape, 127 = worn out, default 36 */
    uint8_t delay_pingpong;          /* PING PONG: 0 / 1, the echoes alternate left and right */
    uint8_t delay_type;              /* DELAYTYPE_*: TAPE (default) or BBD */
    /* GLOBAL (4.6) */
    uint8_t midi_ch[NLAYER];         /* 0..15 = channel 1..16 (defaults 1, 2) */
    uint8_t cc1_num, cc2_num;        /* custom CC1 / CC2 controller numbers */
    uint8_t base_velocity;           /* shown 12..127, default 69 */
    uint8_t bend_range;
    uint8_t clock_source;            /* CLKSRC_* */
    uint8_t int_tempo;               /* INT TEMPO when CLOCK SOURCE is INTERNAL: BPM = 40 + value (default 80 = 120 BPM) */
} MasterPerformancePreset;
PS_STATIC_ASSERT(sizeof(MasterPerformancePreset) == 51u, "MasterPerformancePreset flash layout");

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
