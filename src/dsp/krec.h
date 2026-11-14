#ifndef KREC_H
#define KREC_H
/* The REC page's scratch-pad recorder (2026-11-04): an overdubbing loop of what the keys play (the panel's and an
 * external keyboard's), notes with their velocity and length, plus the mod wheel and pitch bend as lanes. Up to 16
 * bars and KREC_NOTES notes; nothing saved (yet). Clocked like the MAQ and the ARP: on the clock's pulses, with
 * KREC_SUB ticks a pulse between them, so the render may skip its frames between ticks (seq-skip rule).
 *
 * The cycle (looper style): REC arms; the first note starts the loop (on the sequencers' bar when they run); REC again
 * closes it, its length snapped to the beat, and overdub goes on; REC then toggles overdub (PLAY <-> ODUB). Each
 * overdub is a pass: krec_undo takes the newest pass back (its notes, its lane writes, a DOUBLE's length).
 * LENGTH (2026-11-13): FREE is the cycle above; 1..16 bars (4 by default) fixes the loop before it starts. Then REC
 * starts it on the next bar (the sequencers' when they run, else after a bar's count-in) and a note starts it at once
 * (as FREE's first note): the loop wraps from the start, empty or not, as the first overdub pass. CLICK: a beat's
 * blip the render mixes into the output (krec.click_hit), no voice. CAPTURE: the keys' last notes are always kept
 * (cap[]); with no loop, krec_capture makes one of the last phrase (LENGTH's bars, or the phrase's own).
 * Thread: every call runs on the render's side, or the UI's inside syn_lock (they never run at once). */
#include <stdint.h>

#define KREC_NOTES 256u
#define KREC_SUB 4u                  /* ticks a 24 PPQN pulse: 96 a beat */
#define KREC_TPB (24u * KREC_SUB)
#define KREC_BAR (4u * KREC_TPB)
#define KREC_MAX_BARS 16u
#define KREC_MAX (KREC_MAX_BARS * KREC_BAR)   /* 6144 ticks */
#define KREC_WHEEL_T 12u             /* the mod wheel lane: a point every 1/32 note */
#define KREC_BEND_T 6u               /* the pitch bend lane: every 1/64 */
#define KREC_WHEEL_N (KREC_MAX / KREC_WHEEL_T)
#define KREC_BEND_N (KREC_MAX / KREC_BEND_T)
#define KREC_LANE_NONE 0xFFu         /* a lane point nothing was written to */
#define KREC_PASSES 16u              /* pass tags 0..15 (older ones merge into pass 0) */
#define KREC_LEN_OPEN 0xFFFFu        /* a note still held */
enum { KREC_L_WHEEL, KREC_L_BEND };

enum { KREC_EMPTY, KREC_ARMED, KREC_REC, KREC_PLAY, KREC_ODUB };
enum { KQ_OFF, KQ_8, KQ_16, KQ_16T, KQ_COUNT };   /* QUANTIZE: notes recorded onto the grid (their starts) */
enum { KC_OFF, KC_COUNT, KC_REC, KC_ON, KC_N };   /* CLICK: never, the count-in, also recording, whenever it plays */
#define KREC_LEN_DEFAULT 4u          /* LENGTH: bars (0 FREE) */
#define KREC_CAP 256u                /* CAPTURE: the keys' last notes kept */

typedef struct {
    uint16_t t;                      /* start, ticks from the loop's top */
    uint16_t len;                    /* ticks, KREC_LEN_OPEN while held */
    uint8_t note, vel;
    uint8_t chp;                     /* MIDI channel | pass << 4 */
    uint8_t fresh;                   /* 1: just recorded, the playhead has not passed it yet (it sounded live) */
} KrecNote;

/* the page's settings, rows / knobs (bytes 0..127) */
typedef struct {
    uint8_t transpose;               /* (bip) 64 = 0, 40..88 used: -24..+24 semitones */
    uint8_t quantize;                /* KQ_* */
    uint8_t gate;                    /* knob 3: 64 = as played, 0 = staccato (a tick), 127 = twice as long */
    uint8_t density;                 /* knob 4: 127 every note, lower thins the loop (the weakest notes first) */
    uint8_t length;                  /* bars, 0 FREE (KREC_LEN_DEFAULT) */
    uint8_t click;                   /* KC_* */
} KrecSettings;

typedef struct {                     /* 8 B (the pool is tight) */
    uint16_t on;                     /* the absolute tick's low 16 bits (cap_last's age from it: 170 bars) */
    uint16_t len;                    /* KREC_LEN_OPEN while held */
    uint8_t note, vel, ch, spare;
} KrecCap;

typedef void (*KrecOut)(uint8_t status, uint8_t data1, uint8_t data2);

typedef struct {
    KrecNote n[KREC_NOTES];          /* by t, ascending */
    uint16_t nn;
    uint8_t wheel[KREC_WHEEL_N], bend[KREC_BEND_N];       /* KREC_LANE_NONE where unwritten */
    uint8_t wheel_undo[KREC_WHEEL_N], bend_undo[KREC_BEND_N];
    uint8_t undo_pass[2];            /* the pass each lane's copy (wheel, bend) was taken before; 0xFF: none */
    KrecSettings set;
    uint8_t state;                   /* KREC_* */
    uint8_t playing;                 /* the loop is running (PLAY / ODUB, or a stopped loop restarted) */
    uint8_t full;                    /* a note was played that found no room (cleared by UNDO / CLEAR) */
    uint8_t pass;                    /* the tag new notes get */
    uint16_t pass_mask;              /* passes that hold something (notes, a lane write, a DOUBLE) */
    uint16_t pass_len[KREC_PASSES];  /* a DOUBLE's pass: the length before it (0: none) */
    uint16_t len;                    /* the loop, ticks (0 until the first pass closes) */
    uint32_t org;                    /* the absolute tick of the loop's top (cycle 0) */
    uint32_t done;                   /* the last absolute tick processed */
    uint16_t cursor;                 /* the first note at or after the playhead */
    /* the clock as last seen */
    uint32_t pc, pulse_frame, period_q8;
    uint8_t clocked;                 /* 1 once a pulse was seen */
    /* the sequencers' bar, for a start (krec_set_sync) */
    uint8_t sync_run;
    uint32_t sync_origin;            /* their pattern's first pulse */
    /* sounding notes, for their note-offs */
    struct { uint32_t off; uint8_t note, ch, live; } act[16];
    uint8_t nlive;                   /* how many of them sound (0: the frame has nothing to end) */
    /* notes held while recording, for their lengths */
    struct { uint32_t on; uint8_t note, ch, used; } open[16];
    /* the lanes, live: [0] the mod wheel, [1] the pitch bend (KREC_L_*) */
    uint8_t live[2];                 /* the controller's value as the keys last sent it */
    uint8_t touch[2];                /* writing the lane this cycle (moved while recording) */
    uint8_t hold[2];                 /* the live controller took over (moved while only playing): until the wrap */
    uint8_t lout[2];                 /* the value last sent (0xFF: none) */
    uint8_t out_ch;                  /* the lanes' channel */
    uint32_t frame_now;              /* the render's frame after its last pass: where the UI's calls act */
    uint8_t wait;                    /* a fixed loop started, its top (org) still ahead: the count-in */
    uint8_t click_hit;               /* a click due: 1 a beat, 2 a bar's first (the render takes it, sets 0) */
    uint32_t click_frame;            /* .. on this frame */
    uint32_t tick_frame;             /* (krec_frame's frame, for the click) */
    KrecOut out;
    /* CAPTURE: kept through CLEAR / UNDO (krec_clear stops before it) */
    KrecCap cap[KREC_CAP];           /* a ring, oldest first from cap_h - cap_n */
    uint16_t cap_h, cap_n;
    uint32_t cap_last;               /* the newest one's absolute tick */
} Krec;

void krec_init(Krec *k, KrecOut out);

/* the render's frame (from seq_frame): runs the ticks up to `frame`. 1: idle (nothing to do until an input) */
uint8_t krec_frame(Krec *k, uint32_t frame);
/* playing or recording: the next frame something is due on (a tick), 1; else 0 */
uint8_t krec_next_due(const Krec *k, uint32_t *due);
/* the sequencers' transport, for a start on their bar (as maq_set_sync) */
void krec_set_sync(Krec *k, uint8_t running, uint32_t origin_pulse);

/* the keys' MIDI (VSRC_KEYS only: the panel and MIDI in) as it is applied, at frame */
void krec_input(Krec *k, uint8_t status, uint8_t data1, uint8_t data2, uint32_t frame);

/* the UI (inside syn_lock), at the render's current frame */
void krec_tap(Krec *k, uint32_t frame);      /* REC: EMPTY -> ARMED -> (first note) REC -> close: ODUB <-> PLAY */
void krec_arm(Krec *k);                      /* EMPTY -> ARMED (the REC page opening) */
uint8_t krec_undo(Krec *k);                  /* the newest pass back; 1 if anything went */
uint8_t krec_double(Krec *k);                /* the loop twice as long, copied (a pass of its own); 0: no room */
void krec_clear(Krec *k);                    /* everything, back to EMPTY */
void krec_set_playing(Krec *k, uint8_t on, uint32_t frame);   /* the loop's transport (PLAY) */
uint8_t krec_can_double(const Krec *k);
uint8_t krec_capture(Krec *k, uint32_t frame);   /* no loop: one of the keys' last phrase, playing; 1 if made */
uint8_t krec_can_capture(const Krec *k, uint32_t frame);
uint8_t krec_countin(const Krec *k, uint32_t frame);   /* the count-in's beat now (1..4), 0 when none */

/* for the page */
uint16_t krec_pos(const Krec *k, uint32_t frame);   /* the playhead, ticks from the top (while recording the first
                                                     * pass: from its start) */
uint8_t krec_wheel_now(const Krec *k);       /* the lane's value now, KREC_LANE_NONE when it plays none */
uint8_t krec_note_kept(const Krec *k, const KrecNote *n);   /* DENSITY lets it sound */

#endif
