/* Polyphonic arpeggiator & chord memory engine: the ARP page's 8-step chord pattern and its chord keys.
 * mvave_fm1_master_specification.txt 8 (8.1 keybed split and step capture, 8.2 menu, 8.3 chord bank and rhythm table,
 * 8.4 stuck-note shield and 1-cycle mask), 2.2 (the four ARP knobs), 6.3 (navigation never touches the clock).
 *
 * Every carp_ name: on the FM1 this file is #included into Felucca's unity build, whose seq.c has its own arp_* statics.
 *
 * Timing: carp_frame() runs once per audio frame inside the render, right after maq_frame(), on the same transport:
 * PLAY runs the MAQ and the ARP together, and both take their first step on the same 24 PPQN pulse. A step lasts one
 * TIME DIVISION (2 .. 1/32) and is split into the cells of its rhythm (8.3 rhythm_variation_table: 4 cells, or 3 for a
 * triplet B column). o strikes, ~ holds the strike before it, _ rests. Cells, gates and strums are scheduled in frames
 * from the pulse period, so they stay sample-accurate at any tempo.
 *
 * A strike plays the step's chord as its STYLE orders it: BLOCK the whole chord, UP / DOWN / UP-DN / DN-UP / RANDOM one
 * note of the chord spread over OCTAVE RANGE octaves (the order runs on across steps, from the transport start), STRUM UP
 * / DN the whole chord with its notes staggered over the first half of the cell. BLOCK and STRUM move up an octave per
 * strike within the range. A strike lasts its cells (the o and the ~ after it) x (GATE + 1) / 128; GATE 127 is legato.
 * An accented step's first strike plays at ACCENT VEL instead of the base velocity.
 *
 * A TIE step (ARP_CHORD_TIE) carries on the chord before it with that step's knobs: a one-note style runs its rhythm again
 * and the note order runs on, as one long phrase; BLOCK and the strums hold the chord, struck once, through every tied
 * step (the source step's last strike is legato and its rest cells after it do not silence it). A TIE after a rest, or at
 * the transport start, rests.
 *
 * A chord key held outside a capture (carp_hold) arpeggiates its own chord live, while the captured pattern is not playing
 * (transport stopped, or no chord in the pattern's SEQ LENGTH steps): TIME DIVISION steps of the live knob values
 * (carp_set_knobs: GATE, OCTAVE, STYLE, RHYTHM) at the key's velocity, the first step on the key's own frame (stopped) or
 * at once inside the running step (playing). Gate, octave and style changes take effect on the next strike, the rhythm
 * on the next step. With the captured pattern playing, a held chord key sounds its chord as a block, as a capture does.
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
#define CARP_ZONE_KEYS 13u           /* 8.1: the lowest 13 keys, chromatic: chord keys */
#define CARP_STEP_KEYS ARP_NSTEP     /* .. then the next 8 white keys: step keys */
#define CARP_NCELL 4u                /* a step's rhythm cells (3 for a triplet) */
#define CARP_POOL_MAX (ARP_CHORD_NOTES * ARP_NOCT)
enum { CARP_CELL_REST, CARP_CELL_STRIKE, CARP_CELL_TIE };          /* 8.3: _ o ~ */

typedef struct {
    ArpState *st;                    /* the live pattern (owned by the caller; the ARP page edits it) */
    const MasterPerformancePreset *perf;   /* base velocity, MIDI channels, performance mode */
    MaqMidiOut out;
    /* the running step */
    uint8_t pos;                     /* step on the playhead, CARP_POS_NONE stopped */
    uint8_t ticks_left;              /* clock pulses until the next step (TIME DIVISION 2 = 192 at most) */
    uint8_t cells[CARP_NCELL], ncell, cell;   /* the step's rhythm and the next cell to fire */
    uint8_t ncell_var;               /* the rhythm's cell count (a tied step runs it again) */
    uint8_t gate, oct, style;        /* the step's knob values, range-checked */
    uint8_t chord[ARP_CHORD_NOTES], nchord;   /* the step's chord (untransposed, ascending); 0 notes: a rest step */
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
    uint8_t tie_hold;                /* a BLOCK / strum chord sustains into the TIE step after it */
    /* a chord key's chord, sounding while the key is held */
    uint8_t aud_on[ARP_CHORD_NOTES], aud_ch[2];
    volatile uint32_t req_aud;       /* UI request, one store: root | vel << 8 | serial << 16 (vel 0: release, CARP_AUD_HOLD: live) */
    uint16_t aud_serial;
    /* a chord key held outside a capture: arpeggiated live (carp_hold) */
    uint8_t live_root, live_vel;     /* the held key (CARP_NOTE_NONE: none) and its velocity */
    uint8_t live;                    /* the running step is the held key's, not the pattern's */
    volatile uint32_t req_knobs;     /* UI: the live knob values, one store: gate | oct << 8 | style << 16 | var << 24 */
    volatile int8_t req_transpose;   /* UI request: semitones from the chord zone's first key */
    int8_t transpose;
    int8_t follow;                   /* TRANSPOSE MAQ: the next pulse's step root, from the first sounding step's */
    /* what the display reads: the chord sounding last (pattern step or chord key), transposed */
    uint8_t shown[ARP_CHORD_NOTES], nshown;
    uint8_t shown_oct, shown_style;  /* .. and how it plays: octaves 1..ARP_NOCT, ARPSTYLE_* (a chord key's block: 1, BLOCK) */
    /* transport */
    uint8_t running;
    uint32_t pulse_seen, start_seen;
    uint32_t frame;
    uint32_t rng;                    /* xorshift32, never 0 */
} CarpSeq;

/* st: the live pattern; out: where notes go (synth_midi_3). Stopped, nothing sounding. */
void carp_init(CarpSeq *a, ArpState *st, const MasterPerformancePreset *perf, MaqMidiOut out);

/* audio context: once per frame, after maq_frame(). run: the shared transport is free running (maq_free_running). */
void carp_frame(CarpSeq *a, uint32_t frame, uint8_t run);

/* main-loop context: requests applied on the next frame */
void carp_audition(CarpSeq *a, uint8_t root, uint8_t vel);   /* sound root's chord; vel 0 releases it (or a hold) */
void carp_hold(CarpSeq *a, uint8_t root, uint8_t vel);       /* arpeggiate root's chord live while held; vel 0 releases */
void carp_set_knobs(CarpSeq *a, const uint8_t knobs[4]);     /* the live knob values a held key plays (2.2) */
void carp_set_transpose(CarpSeq *a, int8_t semitones);
void carp_latch_up(CarpSeq *a, uint8_t root);    /* LATCH: the held key's block chord off, its hold kept */

/* for the MAQ (through the render) and the display */
int8_t carp_maq_transpose(const CarpSeq *a);       /* the MAQ NOTE lanes' offset: transposition + chord root, 0 with TRANSPOSE MAQ off */
int8_t carp_transpose(const CarpSeq *a);           /* the requested transposition */
uint8_t carp_pos(const CarpSeq *a);                /* step on the playhead, CARP_POS_NONE stopped */
uint8_t carp_shown(const CarpSeq *a, uint8_t out[ARP_CHORD_NOTES]);   /* the chord sounding last, ascending; its size */
uint8_t carp_shown_shape(const CarpSeq *a, uint8_t *style);  /* .. its octave range 1..ARP_NOCT, and its style */
/* real-time record: the step a chord played now lands in, quantized to the nearest step (the playing one in its first
 * half, the next one after); 0 before the first step, CARP_POS_NONE stopped */
uint8_t carp_rec_step(const CarpSeq *a);

/* shared with the UI: the chord a chord key plays in a genre (ascending, notes above 127 dropped; returns how many),
 * a variation's rhythm cells (CARP_CELL_*; returns 4, or 3 for a triplet), TIME DIVISION in 24 PPQN pulses */
uint8_t carp_chord_notes(uint8_t genre, uint8_t root, uint8_t out[ARP_CHORD_NOTES]);
uint8_t carp_rhythm(uint8_t variation, uint8_t cells[CARP_NCELL]);
uint16_t carp_div_ticks(uint8_t division);

#ifdef __cplusplus
}
#endif
