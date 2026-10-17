/* MAQ 16/4 sequencer engine: 4 lanes x 16 steps, each lane its own event type (7.5), clock prescale, playback
 * mode and loop window. mvave_fm1_master_specification.txt 7 (7.1 loop window, 7.2 single-step record, 7.3 storage,
 * 7.5 event registry) and 8.4 (stuck-note shield, 1-cycle cross-lane mask).
 *
 * Timing: maq_frame() runs once per audio frame inside the render (the audio interrupt context on the FM1), right
 * after midi_clock_frame(). Lanes step on whole 24 PPQN clock pulses (internal or MIDI); gates, ratchets and strums are
 * scheduled in frames from the pulse period, so they stay sample-accurate at any tempo. Nothing in the UI calls into
 * the render path: the UI only writes the request bytes below (maq_set_*), and the engine picks them up on its next
 * frame, so no menu navigation can stop or reset the clock (6.3).
 *
 * Cross-lane evaluation (8.4): on each pulse, pass A moves every due lane to its next step and publishes that step's
 * value into a buffer; pass B then fires the lanes that moved, reading other lanes' values only from that buffer. Pass
 * B never writes the buffer, and pass A's skip decisions read the previous pulse's copy, so no lane's output can
 * feed back into its own evaluation within a pulse.
 *
 * Interpretations where the spec is open (also in the spec addendum notes):
 *   TIMING  value 0 skips the target's step (silent, one clock tick); 1..31 / 32..63 / 64..95 / 96..127 set its
 *           length to 4 / 8 / 16 / 32 clock ticks ("base step clock x4 .. x32", base = one 24 PPQN tick).
 *   GATE    note length = step length x (value + 1) / 128; default 64 (half the step). 127 on a single burst is
 *           legato: the note is held until the next step releases it.
 *   RATCHET 0..31 / 32..63 / 64..95 / 96..127 = 1 / 2 / 3 / 4 bursts per step. With a chord and more than one
 *           burst, each burst strums its notes across the first half of the burst.
 *   TIE     SEQ_FLAG_TIE or note_buffer[0] == SEQ_NOTE_TIE holds the lane's sounding notes through the step (a
 *           pause when nothing is sounding).
 *   LAYER   value 0..42 / 43..85 / 86..127 = L1 / L2 / BOTH for the target row.
 * Integer only. Written in the common C / C++ subset. */
#pragma once
#include <stdint.h>
#include "PresetStructures.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAQ_POS_NONE 0xFFu           /* a lane before its first step */
#define MAQ_GATE_DEFAULT 64u         /* no GATE lane on it: half the step */
#define MAQ_PULSES_PER_FRAME_MAX 4u  /* a MIDI clock burst catches up at most this many pulses in one frame */

/* where a lane's events go: a channel message, as synth_midi_3 takes it (2-byte messages carry data2 = 0) */
typedef void (*MaqMidiOut)(uint8_t status, uint8_t data1, uint8_t data2);

typedef struct {
    uint8_t pos;                     /* step on the playhead 0..15, MAQ_POS_NONE before the first */
    int8_t dir;                      /* pendulum direction, +1 / -1 */
    uint8_t ticks_left;              /* clock pulses until the next step */
    uint8_t stepped;                 /* pass A -> pass B: moved on this pulse */
    /* playback cache (8.4): what this lane has sounding and where it went */
    uint8_t on[SEQ_NNOTE];           /* SEQ_NOTE_NULL = empty */
    uint8_t on_ch[2];                /* MIDI channels, 0xFF = unused */
    /* the current step's note schedule */
    uint8_t notes[SEQ_NNOTE];
    uint8_t n_notes, vel;
    uint8_t ch[2];
    uint8_t bursts_left;             /* ratchet bursts still to come after the running one */
    uint8_t strike;                  /* the next note of the running burst to strike */
    uint8_t due_kind;                /* MAQ_DUE_* */
    uint32_t due;                    /* frame of the next scheduled event */
    uint32_t burst_start, burst_frames, strum_frames, gate_frames;
} MaqLaneRun;

typedef struct {
    SequencerLane *lanes;            /* SEQ_NLANE, owned by the caller (the live sequencer slot) */
    const MasterPerformancePreset *perf;   /* base velocity, MIDI channels, custom CCs, performance mode */
    MaqMidiOut out;
    MaqLaneRun run[SEQ_NLANE];
    uint8_t xl[SEQ_NLANE];           /* 8.4 cross-lane buffer: each lane's published step value (pass A) */
    uint8_t xl_prev[SEQ_NLANE];      /* .. as it was one pulse earlier (pass A's own reads) */
    uint8_t xl_valid, xl_prev_valid; /* bit per lane: its value is live (not muted, not stopped) */
    /* requests, written by the UI (main loop), read by maq_frame */
    volatile uint8_t req_play;
    volatile uint8_t req_rec;
    volatile uint8_t req_rec_step;
    volatile int8_t ext_transpose;   /* the ARP's chord-key transposition (8.2 TRANSPOSE MAQ), set by the render */
    volatile uint8_t genre;          /* the ARP's GENRE BANK, for a NOTE lane on SCALE = GENRE (seq_scale.h) */
    /* what maq_frame has applied */
    uint8_t playing, rec, rec_step;
    uint8_t ext_running_seen;
    uint32_t pulse_seen, start_seen;
    uint32_t frame;
    uint32_t rng;                    /* xorshift32, never 0 */
} MaqSeq;

/* lanes: SEQ_NLANE lanes; out: where notes and controllers go. Stopped, not recording. */
void maq_init(MaqSeq *q, SequencerLane *lanes, const MasterPerformancePreset *perf, MaqMidiOut out);

/* audio context: once per frame, after midi_clock_frame(frame) */
void maq_frame(MaqSeq *q, uint32_t frame);

/* main-loop context: requests the engine applies on its next frame. Under CLOCK SOURCE MIDI, MIDI Start also starts
 * the sequencer from the top and MIDI Stop stops it. */
void maq_set_playing(MaqSeq *q, uint8_t on);
void maq_set_record(MaqSeq *q, uint8_t on);        /* 7.2: lanes lock to the record step (playing: retriggered) */
void maq_set_rec_step(MaqSeq *q, uint8_t step);    /* the record step 0..15 (auditioned when it changes) */

/* 7.2 Select Encoder in record mode: move the record step `detents` steps along `lane`'s playback direction,
 * wrapping inside its loop window. Returns the new step. */
uint8_t maq_rec_nav(MaqSeq *q, uint8_t lane, int8_t detents);

/* for the display */
uint8_t maq_playing(const MaqSeq *q);              /* requested state: the UI shows it at once */
uint8_t maq_recording(const MaqSeq *q);
uint8_t maq_rec_step(const MaqSeq *q);
uint8_t maq_lane_pos(const MaqSeq *q, uint8_t lane);   /* MAQ_POS_NONE when stopped */
uint8_t maq_free_running(const MaqSeq *q);         /* applied state: playing and not in step record (the ARP follows it) */

/* 8.2 TRANSPOSE MAQ: semitones added to every NOTE lane's pitches from the next step struck. The render sets it after
 * the ARP's frame, so it lands one frame later (8.4's 1-cycle mask). */
void maq_set_ext_transpose(MaqSeq *q, int8_t semitones);
/* the ARP's GENRE BANK, which a NOTE lane on SCALE = GENRE follows (set by the render, as the transposition) */
void maq_set_genre(MaqSeq *q, uint8_t genre);

/* a lane's loop window, low .. high, whatever order OCT- / OCT+ set them in (out-of-range bytes clamped) */
void maq_loop_window(const SequencerLane *lane, uint8_t *lo, uint8_t *hi);

/* the layer a layer-targeting lane plays: 0 L1, 1 L2, 2 BOTH (SEQ_TGT_*), out-of-range read as L1 */
uint8_t maq_value_to_layer(uint8_t value);         /* LAYER lane value -> SEQ_TGT_* */
uint8_t maq_timing_ticks(uint8_t value);           /* TIMING lane value -> ticks, 0 = skip */
uint8_t maq_ratchet_count(uint8_t value);          /* RATCHET lane value -> 1..4 */

#ifdef __cplusplus
}
#endif
