/* Polyphonic arpeggiator & chord memory engine: the ARP page's 8-step chord pattern and its chord keys.
 * mvave_fm1_master_specification.txt 8 (8.1 keybed split and step capture, 8.2 menu, 8.3 chord bank and rhythm table,
 * 8.4 stuck-note shield and 1-cycle mask), 2.2 (the four ARP knobs), 6.3 (navigation never touches the clock).
 *
 * Every carp_ name: on the FM1 this file is #included into Felucca's unity build, whose seq.c has its own arp_* statics.
 *
 * Timing: carp_frame() runs once per audio frame inside the render, right after maq_frame(), on the same clock. The
 * ARP has its own transport (2026-10-17; carp_set_playing): PLAY on the ARP page starts and stops it alone, on the
 * SEQ page the MAQ alone, on any other page both. Started together, both take their first step on the same 24 PPQN
 * pulse; one started while the other plays waits for the other's next bar (MAQ_SYNC_PULSES from its first step), so
 * the two stay on one grid. Under CLOCK SOURCE MIDI, MIDI Start starts it from the top and MIDI Stop stops it, as
 * the MAQ. Every step lasts the pattern's TIMING (ARPDIV_*: 2 .. 1/32T, ARP_TIMING) and plays its phrase (2026-11-23:
 * STYLE x VARIATION, 9 families x 12, arp_seq.c CARP_PHRASES; was the J-6's 4-cell rhythm table) at its own RATE (knob
 * 1, ARPRATE_*: 1/2 .. 1/128, one event), round and round until the step ends. The phrase's place is the cells since the
 * transport started: it runs on across steps as a bar loop, the steps changing the chord under it. A new chord arriving
 * while a strike is still held (a ~) is struck at once (that strike's event, on the new chord).
 * Cells, gates and strums are scheduled in frames from the pulse period, so they stay sample-accurate at any tempo.
 *
 * An event strikes part of the step's voicing (the chord spread over SPAN octaves, ascending, doubles dropped; SPAN AUTO
 * is the phrase's own): the bass (lowest an octave down), one voice (lowest, a third up, two thirds, top), the chord
 * spread from lowest to top, the chord without its lowest, a strum up / down (notes ~9 ms apart, a ringing phrase's 4x),
 * a muted chk (short, 5/8 the velocity), or the next voicing note in an order (up, down, up-down, random; the order runs
 * on across steps). An event may be an octave up / two up / down, accented (half way to 127) or staccato (a quarter of
 * the gate). A strike lasts its cells (it and the ~ after it) x (GATE + 1) / 128 (the pattern's GATE); GATE 127 is legato;
 * a ringing phrase's notes ring on, the oldest let go for a fifth (and all of them at a new chord).
 * BEAT's HOLD variations strike the chord once at the step's start and hold it to the step's end, whatever the GATE; a
 * HOLD step whose chord is the HOLD chord already sounding (or still strumming) holds it on, unstruck, so a chord
 * repeated step after step is never retriggered by the clock, at any TIMING. HOLD UP / DN strum it, the notes RATE apart.
 * An accented step's first strike plays at ACCENT VEL instead of the base velocity.
 *
 * A TIE step (ARP_CHORD_TIE) carries on the chord before it, its phrase running on with the TIE step's RATE, unstruck
 * by the step change. A TIE after a rest, or at the transport start, rests.
 *
 * A chord key held outside a capture (carp_hold) arpeggiates its own chord live, while the captured pattern is not playing
 * (transport stopped, or no chord in the pattern's SEQ LENGTH steps): steps of the live knob values (carp_set_knobs:
 * RATE, SPAN, STYLE, VARIATION; the pattern's GATE and TIMING) at the key's velocity, the first step on the key's own
 * frame (stopped) or at once inside the running step (playing). GATE, SPAN, STYLE and VARIATION changes take effect at
 * once (the new phrase in the old one's place), the rate on the next step. With the captured pattern playing, a held
 * chord key sounds its chord as a block, as a capture does.
 *
 * The UI never sends notes itself: a chord key is a request (carp_audition / carp_hold) and so is its transposition
 * (carp_set_transpose); the engine applies both on its next frame, in the audio context, so no menu navigation can
 * reach the clock (6.3). The transposition offsets every strike from then on and, with TRANSPOSE MAQ on, the MAQ NOTE
 * lanes. With TRANSPOSE MAQ on the MAQ also follows the pattern's chord progression: each step's chord root, as semitones
 * from the root of the pattern's first sounding step (a rest keeps the root before it), on top of the transposition.
 * The render hands carp_maq_transpose() to maq_set_ext_transpose() after this frame, so the MAQ reads it one frame late
 * (8.4's 1-cycle mask: no same-frame path from one engine into the other); the root published is the one of the step
 * the next pulse plays, so a MAQ step on an ARP step's first pulse already takes that step's root. A TIE keeps the root
 * before it, as a rest does.
 *
 * Latch (the UI's LATCH key): carp_latch_up() ends a held key's block chord but keeps the key held for the engine, so
 * it arpeggiates on whenever the pattern is not playing, until the next request.
 * Integer only. Written in the common C / C++ subset. */
#pragma once
#include <stdint.h>
#include "PresetStructures.h"
#include "maq_seq.h"                 /* MaqMidiOut */
#include "arp_chords.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CARP_POS_NONE 0xFFu          /* stopped, or before the first step */
#define CARP_NOTE_NONE 0xFFu
#define CARP_NCELL 16u               /* a phrase's events, one RATE each (2026-11-23: was a 4-cell rhythm) */
#define CARP_POOL_MAX (ARP_CHORD_NOTES * ARP_NOCT)

/* A phrase (2026-11-23): up to 16 events, each one RATE long, looping on the transport's clock (its place = the cells
 * since the start, so it runs on across steps: the steps change the chord under it). An event's low nibble says what
 * it strikes, from the step's voicing (the chord spread over SPAN octaves, ascending): */
enum {
    CARP_EV_REST,                    /* _ silences a sounding strike (not in a ringing phrase) */
    CARP_EV_TIE,                     /* ~ the strike before holds on */
    CARP_EV_BASS,                    /* the lowest voice an octave down */
    CARP_EV_LOW, CARP_EV_MID2, CARP_EV_MID3, CARP_EV_TOP,   /* one voice: lowest, a third of the way up, two thirds, top */
    CARP_EV_CHORD,                   /* the chord, its notes spread over the voicing */
    CARP_EV_UPPER,                   /* the chord without its lowest note */
    CARP_EV_STRUM_UP,                /* the chord, its notes rising a few ms apart (a guitar's down-stroke) */
    CARP_EV_STRUM_DN,                /* .. falling (an up-stroke) */
    CARP_EV_MUTE,                    /* a muted chk: the chord, short and soft */
    CARP_EV_ARP_UP, CARP_EV_ARP_DN, CARP_EV_ARP_PP, CARP_EV_ARP_RND,   /* the next note of the voicing in that order */
    CARP_EV_COUNT
};
#define CARP_EV_PART(e) ((uint8_t)((e) & 15u))
#define CARP_EV_OCT(e) ((uint8_t)(((e) >> 4) & 3u))   /* 0 as is, 1 an octave up, 2 two up, 3 an octave down */
#define CARP_EV_ACCENT 0x40u         /* louder: half way from the velocity to 127 */
#define CARP_EV_SHORT 0x80u          /* staccato: a quarter of the gate */
/* a phrase's flags: its own span (AUTO), whether its notes ring on (up to 4, the oldest let go), HOLD */
#define CARP_PH_SPAN(f) ((uint8_t)((f) & 3u) ? (uint8_t)((f) & 3u) : 1u)
#define CARP_PH_RING 0x04u
#define CARP_PH_HOLD 0x08u           /* the first event's chord struck once and held (BEAT's HOLD variations) */

typedef struct {
    ArpState *st;                    /* the live pattern (owned by the caller; the ARP page edits it) */
    const MasterPerformancePreset *perf;   /* base velocity, MIDI channels, performance mode */
    MaqMidiOut out;
    /* the running step */
    uint8_t pos;                     /* step on the playhead, CARP_POS_NONE stopped */
    uint8_t ticks_left;              /* clock pulses until the next step (TIMING 2 = 192 at most, a bar's wait 96) */
    uint8_t step_ticks;              /* the running step's length in pulses (TIMING) */
    uint8_t cells[CARP_NCELL], ncell, cell;   /* the step's phrase (CARP_EV_*) and the next cell's place in it */
    uint16_t cell_k, cell_max;       /* cells fired this step, and how many fit in it (the phrase runs round) */
    uint8_t rate_q;                  /* the step's RATE in 96ths of a beat (arp_rate_q) */
    uint8_t ncell_var;               /* the phrase's length (a tied step runs it on) */
    uint8_t gate, oct, style, var;   /* the step's knob values, range-checked (oct: SPAN in octaves, AUTO resolved) */
    uint8_t ph_flags;                /* the phrase's CARP_PH_* */
    uint8_t chord[ARP_CHORD_NOTES], nchord;   /* the step's chord (untransposed, ascending); 0 notes: a rest step */
    uint8_t pool[CARP_POOL_MAX], npool;       /* .. its voicing: the chord over oct octaves, ascending, no doubles */
    uint32_t step_pulse;             /* the running step's first pulse, counted from the start (the phrase's place) */
    uint8_t ph_started;              /* step_pulse counts (0: the next step is the first) */
    uint8_t ring_i;                  /* a ringing phrase: the on[] slot let go next */
    uint8_t retrig;                  /* a new step's chord: its first cell strikes even on a ~ (the note it holds) */
    uint8_t counter;                 /* strikes since the transport started: the note order's place */
    uint32_t step_start, cell_frames, cell_due;
    uint8_t rel_pending;             /* a gate end is scheduled at rel_due */
    uint32_t rel_due;
    uint8_t strum[ARP_CHORD_NOTES], nstrum, strum_i;   /* a strum's notes still to strike, strum_frames apart */
    uint32_t strum_frames, strum_due;
    uint8_t on[ARP_CHORD_NOTES];     /* 8.4 playback cache: the pattern's sounding notes (CARP_NOTE_NONE = empty) */
    uint8_t on_ch[2];
    uint8_t ch[2], vel;              /* where and how loud the step strikes */
    uint8_t accent, strike_vel;      /* the step's first strike is still to come accented; the running strike's velocity */
    uint8_t hold_strum;              /* a HOLD step's strum: 0 none, 1 up, 2 down */
    uint8_t hold_notes[ARP_CHORD_NOTES], hold_n;   /* the HOLD chord sounding (0 notes: none) */
    /* a chord key's chord, sounding while the key is held */
    uint8_t aud_on[ARP_CHORD_NOTES], aud_ch[2];
    volatile uint32_t req_aud;       /* UI request, one store: root | vel << 8 | serial << 16 (vel 0: release, CARP_AUD_HOLD: live) */
    uint16_t aud_serial;
    /* a chord key held outside a capture: arpeggiated live (carp_hold) */
    uint8_t live_root, live_vel;     /* the held key (CARP_NOTE_NONE: none) and its velocity */
    uint8_t live;                    /* the running step is the held key's, not the pattern's */
    volatile uint32_t req_knobs;     /* UI: the live knob values, one store: rate | span << 8 | style << 16 | var << 24 */
    uint32_t live_k;                 /* req_knobs >> 8 the live step's phrase was made from */
    volatile int8_t req_transpose;   /* UI request: semitones from the chord zone's first key */
    int8_t transpose;
    int8_t follow;                   /* TRANSPOSE MAQ: the next pulse's step root, from the first sounding step's */
    /* what the display reads: the chord sounding last (pattern step or chord key), transposed */
    uint8_t shown[ARP_CHORD_NOTES], nshown;
    uint8_t shown_oct, shown_style, shown_var;   /* .. and how it plays: octaves 1..ARP_NOCT, its phrase (a chord key's
                                                  * block: 1, BEAT HOLD) */
    /* transport */
    volatile uint8_t req_play;       /* UI request (carp_set_playing); MIDI Start / Stop write it too */
    uint8_t running, ext_running_seen;
    uint32_t origin;                 /* the pulse count the pattern's first step played on (or will) */
    uint32_t pulse_seen, start_seen;
    uint32_t frame;
    uint32_t rng;                    /* xorshift32, never 0 */
} CarpSeq;

/* st: the live pattern; out: where notes go (synth_midi_3). Stopped, nothing sounding. */
void carp_init(CarpSeq *a, ArpState *st, const MasterPerformancePreset *perf, MaqMidiOut out);

/* audio context: once per frame, after maq_frame(). q: the MAQ, whose grid a start while it plays waits for (NULL: none) */
uint8_t carp_frame(CarpSeq *a, uint32_t frame, const MaqSeq *q);   /* 1: idle (as maq_frame's) */
/* the frame its next cell, strum note or gate end is due on (1), as maq_next_due's; 0: none scheduled */
uint8_t carp_next_due(const CarpSeq *a, uint32_t *due);

/* main-loop context: requests applied on the next frame */
void carp_audition(CarpSeq *a, uint8_t root, uint8_t vel);   /* sound root's chord; vel 0 releases it (or a hold) */
void carp_hold(CarpSeq *a, uint8_t root, uint8_t vel);       /* arpeggiate root's chord live while held; vel 0 releases */
void carp_set_knobs(CarpSeq *a, const uint8_t knobs[4]);     /* the live knob values a held key plays (2.2) */
void carp_set_playing(CarpSeq *a, uint8_t on);               /* the ARP's transport: start from step 1 / stop */
void carp_set_transpose(CarpSeq *a, int8_t semitones);
void carp_latch_up(CarpSeq *a, uint8_t root);    /* LATCH: the held key's block chord off, its hold kept */

/* for the MAQ (through the render) and the display */
int8_t carp_maq_transpose(const CarpSeq *a);       /* the MAQ NOTE lanes' offset: transposition + chord root, 0 with TRANSPOSE MAQ off */
int8_t carp_transpose(const CarpSeq *a);           /* the requested transposition */
uint8_t carp_playing(const CarpSeq *a);            /* requested state: the UI shows it at once */
uint8_t carp_running(const CarpSeq *a);            /* applied state (the MAQ's sync reads it, with carp_origin) */
uint32_t carp_origin(const CarpSeq *a);            /* the pulse count of the pattern's first step */
uint8_t carp_pos(const CarpSeq *a);                /* step on the playhead, CARP_POS_NONE stopped */
uint8_t carp_shown(const CarpSeq *a, uint8_t out[ARP_CHORD_NOTES]);   /* the chord sounding last, ascending; its size */
uint8_t carp_shown_shape(const CarpSeq *a, uint8_t *style, uint8_t *var);   /* .. its span 1..ARP_NOCT, its phrase */
/* real-time record: the step a chord played now lands in, quantized to the nearest step (the playing one in its first
 * half, the next one after); 0 before the first step, CARP_POS_NONE stopped */
uint8_t carp_rec_step(const CarpSeq *a);

/* shared with the UI: the chord a chord key plays in a genre (ascending, notes above 127 dropped; returns how many),
 * TIMING (ARPDIV_*) in 24 PPQN pulses */
uint8_t carp_chord_notes(uint8_t genre, uint8_t root, uint8_t out[ARP_CHORD_NOTES]);
uint16_t carp_div_ticks(uint8_t division);
/* a style's variation: its events (returns how many, 1..CARP_NCELL) and flags (CARP_PH_*); out of range: style BEAT, variation the last */
uint8_t carp_phrase(uint8_t style, uint8_t var, uint8_t ev[CARP_NCELL], uint8_t *flags);
/* SPAN as octaves: the knob (0 AUTO, 1..ARP_NOCT) for this phrase */
uint8_t carp_span(uint8_t style, uint8_t var, uint8_t span);
/* the voicing: the chord's notes over oct octaves, ascending, doubles dropped; returns how many */
uint8_t carp_voicing(const uint8_t *chord, uint8_t n, uint8_t oct, uint8_t pool[CARP_POOL_MAX]);
extern const char *const carp_style_names[ARPSTYLE_COUNT];
extern const char *const carp_phrase_names[ARPSTYLE_COUNT][ARP_NVAR];

#ifdef __cplusplus
}
#endif
