#ifndef MIDI_CLOCK_H
#define MIDI_CLOCK_H

#include <stdint.h>

/* Tempo clock for the clock-synced LFOs and envelope retriggers (spec 4.3, 4.4, 4.6 Clock Sync Reference Priority).
 * CLKSRC_INTERNAL: the engine's own clock at INT TEMPO. CLKSRC_MIDI: the 0xF8 pulses of the incoming MIDI
 * stream, 24 per quarter note. Integer only: periods are Q8 frames, phases are Q32 cycles. */

#define MIDI_CLOCK_NDIV 8u           /* musical grid divisions: rate knob / 16, 0 = 1/1 (slowest) .. 7 = 1/32 (fastest) */
#define MIDI_CLOCK_BPM_BASE 40u      /* INT TEMPO value v is BPM_BASE + v */
#define MIDI_CLOCK_TEMPO_MAX 215u    /* the highest INT TEMPO value: 255 BPM, the top of the uint8_t BPM */

void midi_clock_init(void);

/* once per audio frame, before the voices read the clock: runs the engine clock and stamps the frame */
void midi_clock_frame(uint32_t frame);

/* How many frames after the one midi_clock_frame just ran change nothing but the clock's own count (internal clock;
 * 0 for MIDI). The render skips their midi_clock_frame calls and calls midi_clock_skipped(n, last frame) in their
 * place, before a MIDI event, the next full call, or the pass's end. Bit-exact with running every frame. */
uint32_t midi_clock_quiet(void);
void midi_clock_skipped(uint32_t n, uint32_t last);

/* Whether the next midi_clock_frame changes what the clock tells the voices (a pulse, or a reset when CLOCK SOURCE
 * changed). Between such frames the clock's answer depends only on the frame asked about, so the block-major render
 * (synth_runtime.c) renders the voices up to this frame first, and they ask midi_clock_phase_at their own frame. */
uint8_t midi_clock_frame_changes(void);

/* a MIDI system realtime byte: 0xF8 clock tick, 0xFA start, 0xFB continue, 0xFC stop. Ignored unless
 * CLOCK SOURCE is MIDI. */
void midi_clock_realtime(uint8_t status);

/* The playhead phase of a cycle of grid division `division`, locked to the clock grid, Q32 (2^32 = one cycle).
 * Returns 0 when there is no clock to follow (no pulses yet, or the MIDI clock has stopped). */
uint8_t midi_clock_phase(uint8_t division, uint32_t *phase_q32);

/* The phase advance per frame of that division, Q32. Same validity as midi_clock_phase. */
uint8_t midi_clock_step(uint8_t division, uint32_t *step_q32);

/* The same two, as of audio frame `frame` instead of the frame midi_clock_frame last stamped. The voices use these
 * (the frame they are rendering): in the block-major render the clock has already been run ahead to a later frame of
 * the block, with no pulse in between (midi_clock_frame_changes). Read-only, so both FM1 cores may call them. */
uint8_t midi_clock_phase_at(uint8_t division, uint32_t frame, uint32_t *phase_q32);
uint8_t midi_clock_step_at(uint8_t division, uint32_t frame, uint32_t *step_q32);

/* For the MAQ sequencer (spec 7), which steps on whole pulses rather than on a division's phase:
 * every pulse since boot (internal or MIDI, never reset, wraps), the current pulse period in Q8 frames
 * (frames per 24 PPQN tick), whether CLOCK SOURCE is MIDI, MIDI Start messages since boot, and the MIDI transport
 * state (1 after Start / Continue, 0 after Stop). */
uint32_t midi_clock_pulse_count(void);
uint32_t midi_clock_period_q8(void);
uint8_t midi_clock_is_external(void);
uint32_t midi_clock_start_count(void);
uint8_t midi_clock_ext_running(void);

/* The clock probe (2026-11-12, src/hardware/fm1_clock.py): while armed, the frame of every 0xF8 the clock is handed
 * (rx) and of every pulse it plays (play), MIDI clock only, MIDI_CLOCK_LOG_N of each, then it stops logging; the
 * transport messages (and slips: status 0x10 | halves, at the first frame moved) with the rx count they came after;
 * and the lock's own events. Not armed it costs a load a
 * pulse. `force_ext` 1 makes the clock follow MIDI whatever CLOCK SOURCE says, 2 the internal clock (the probe's runs:
 * the setting and its saved copy are never touched). */
#define MIDI_CLOCK_LOG_N 1024u
#define MIDI_CLOCK_LOG_EV 32u
typedef struct {
    uint32_t rx[MIDI_CLOCK_LOG_N], play[MIDI_CLOCK_LOG_N];
    uint32_t ev_frame[MIDI_CLOCK_LOG_EV];
    uint16_t ev_rx[MIDI_CLOCK_LOG_EV];
    uint8_t ev_status[MIDI_CLOCK_LOG_EV];
    uint16_t nrx, nplay;
    uint8_t nev, armed, force_ext;
    uint16_t relocks, clipped, owed, acquires;   /* tempo re-measures, errors clipped, pulses played late, runs locked */
} MidiClockLog;
extern MidiClockLog midi_clock_log;
void midi_clock_log_arm(uint8_t force_ext);

/* The FM1 lost an audio half (or n frames): its frame count fell behind real time by n. The MIDI clock's lock moves its
 * predictions with it, so the pulses that fell in the lost frames play at once and the rest stay on the beat
 * (src/hardware/synth_main_fm1.c, midi_drain). */
void midi_clock_slip(uint32_t n);

#endif
