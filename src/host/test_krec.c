/* The REC page's recorder (src/dsp/krec.c) on its own, on a stand-in clock: recording and closing a loop, its
 * playback, overdub, UNDO, DOUBLE, the lanes, GATE, DENSITY, QUANTIZE, the 16-bar limit, a full loop; and the same
 * run frame by frame and only on the frames krec_next_due names (the render's skip rule) giving the same events.
 *
 *   gcc -O2 -Isrc -Isrc/dsp src/dsp/krec.c src/host/test_krec.c -o test_krec */
#include <stdio.h>
#include <string.h>
#include "dsp/krec.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- the stand-in clock: a pulse every P frames (120 BPM at 44.1 kHz: 918.75; 918 here) */
#define P 918u
static uint32_t pc_now;
uint32_t midi_clock_pulse_count(void) { return pc_now; }
uint32_t midi_clock_period_q8(void) { return P << 8; }

/* ---- what the recorder sends */
typedef struct { uint32_t frame; uint8_t st, d1, d2; } Ev;
static Ev ev[8192];
static int nev;
static uint32_t cur_frame;
static void out(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (nev < 8192) { ev[nev].frame = cur_frame; ev[nev].st = st; ev[nev].d1 = d1; ev[nev].d2 = d2; nev++; }
}

static Krec k;
static uint32_t frame;
static int skip_mode;                /* 1: run krec_frame only on pulse frames and krec_next_due's */
static uint32_t wake;
static uint8_t have_wake;

static void step_frame(void)
{
    uint8_t pulse = frame % P == 0u;
    if (pulse) pc_now++;
    cur_frame = frame;
    if (!skip_mode || pulse || (have_wake && (int32_t)(frame - wake) >= 0)) {
        krec_frame(&k, frame);
        have_wake = krec_next_due(&k, &wake);
    }
    frame++;
}
static void run(uint32_t frames) { while (frames--) step_frame(); }
/* frames for t ticks */
#define TICKS(t) ((uint32_t)((t) * P / KREC_SUB))

static void key(uint8_t note, uint8_t vel)
{
    cur_frame = frame;
    krec_input(&k, vel ? 0x90u : 0x80u, note, vel, frame);
    if (skip_mode) {                                 /* an event wakes the render's sequencer frame */
        krec_frame(&k, frame);
        have_wake = krec_next_due(&k, &wake);
    }
}
static void ctl(uint8_t st, uint8_t d1, uint8_t d2)
{
    cur_frame = frame;
    krec_input(&k, st, d1, d2, frame);
    if (skip_mode) { krec_frame(&k, frame); have_wake = krec_next_due(&k, &wake); }
}

static int count(uint8_t st, uint8_t d1, uint32_t from, uint32_t to)
{
    int c = 0;
    for (int i = 0; i < nev; i++)
        if (ev[i].st == st && (d1 == 0xFFu || ev[i].d1 == d1) && ev[i].frame >= from && ev[i].frame < to) c++;
    return c;
}

static void reset(void)
{
    krec_init(&k, out);
    frame = 0;
    pc_now = 0;
    nev = 0;
    have_wake = 0;
}

/* the scenario both runs must agree on; returns the events' count */
static void scenario(void)
{
    reset();
    run(P * 3u + 100u);
    krec_arm(&k);
    CHECK(k.state == KREC_ARMED, "armed");
    key(60, 100); run(TICKS(48)); key(60, 0);        /* beat 1: C, an eighth */
    run(TICKS(48));
    key(64, 90); run(TICKS(24)); key(64, 0);         /* beat 2: E, a sixteenth */
    run(TICKS(96 + 72 - 24));
    key(67, 80); run(TICKS(96)); key(67, 0);         /* beat 3 and a half: G */
    run(TICKS(384 - 72 - 96 - 192 - 6));             /* ~6 ticks before the bar */
    cur_frame = frame;
    krec_tap(&k, frame);                             /* close: one bar */
}

int main(void)
{
    /* ---- record a bar, close it, it plays */
    scenario();
    CHECK(k.state == KREC_ODUB && k.len == KREC_BAR && k.nn == 3, "closed to one bar of 3 notes (state %u len %u n %u)",
          k.state, k.len, k.nn);
    CHECK(k.n[0].note == 60 && k.n[0].t == 0 && k.n[0].len == 48 && k.n[1].note == 64 && k.n[1].t == 96 &&
          k.n[1].len == 24, "first notes: C at 0 len %u, E at %u len %u", k.n[0].len, k.n[1].t, k.n[1].len);
    {
        uint32_t f0 = frame;
        nev = 0;
        run(TICKS(KREC_BAR * 2u));
        CHECK(count(0x90, 60, f0, frame) == 2 && count(0x90, 64, f0, frame) == 2 && count(0x90, 67, f0, frame) == 2,
              "two bars: each note twice (C %d E %d G %d)", count(0x90, 60, f0, frame), count(0x90, 64, f0, frame),
              count(0x90, 67, f0, frame));
        CHECK(count(0x80, 0xFF, f0, frame) >= 5, "and their note-offs (%d)", count(0x80, 0xFF, f0, frame));
        /* C's on to off: 48 ticks */
        {
            uint32_t on = 0, off = 0;
            for (int i = 0; i < nev; i++) {
                if (ev[i].st == 0x90 && ev[i].d1 == 60 && !on) on = ev[i].frame;
                if (ev[i].st == 0x80 && ev[i].d1 == 60 && on && !off) off = ev[i].frame;
            }
            CHECK(off - on >= TICKS(47) && off - on <= TICKS(49), "C lasts 48 ticks (%u frames, ~%u)", off - on, TICKS(48));
        }
    }

    /* ---- the same with the render's skip rule: the same events, the same frames */
    {
        static Ev ref[8192];
        int nref;
        skip_mode = 0;
        scenario();
        run(TICKS(KREC_BAR * 2u));
        nref = nev;
        memcpy(ref, ev, sizeof ev);
        skip_mode = 1;
        scenario();
        run(TICKS(KREC_BAR * 2u));
        CHECK(nev == nref && memcmp(ref, ev, (size_t)nev * sizeof(Ev)) == 0, "skip rule: %d events vs %d, or frames differ",
              nev, nref);
        skip_mode = 0;
    }

    /* ---- overdub: a note added in cycle 1 sounds once (live) there, then every cycle; UNDO takes it back. Under the
     * skip rule too, where the recorder's playhead lags the key by up to a pulse (it sounded live: not again) */
    for (int sm = 1; sm >= 0; sm--) {
    skip_mode = sm;
    scenario();
    run(TICKS(192 + 20) + P / 2u);                   /* mid bar, overdubbing, mid pulse */
    nev = 0;
    {
        uint32_t f0 = frame;
        key(72, 70); run(TICKS(12)); key(72, 0);
        CHECK(k.nn == 4 && k.pass_mask == 3u, "the overdub's note (n %u mask %x)", k.nn, k.pass_mask);
        run(TICKS(KREC_BAR - 212 - 12));             /* to the top */
        CHECK(count(0x90, 72, f0, frame) == 0, "not replayed in its own cycle (skip %d: %d)", sm,
              count(0x90, 72, f0, frame));
        f0 = frame;
        run(TICKS(KREC_BAR));
        CHECK(count(0x90, 72, f0, frame) == 1, "replayed the next cycle (%d)", count(0x90, 72, f0, frame));
    }
    krec_tap(&k, frame);                             /* ODUB -> PLAY */
    CHECK(k.state == KREC_PLAY, "REC: overdub off");
    CHECK(krec_undo(&k) && k.nn == 3 && k.pass_mask == 1u, "UNDO: the overdub's note gone (n %u)", k.nn);
    CHECK(krec_undo(&k) && k.state == KREC_ARMED && k.nn == 0 && k.len == 0, "UNDO again: the first pass, armed");
    }
    skip_mode = 0;

    /* ---- DOUBLE: twice the length, the notes copied (a pass); UNDO halves it again */
    scenario();
    krec_tap(&k, frame);                             /* PLAY */
    CHECK(krec_double(&k) && k.len == 2u * KREC_BAR && k.nn == 6 && k.n[3].t == KREC_BAR && k.n[3].note == 60,
          "DOUBLE: 2 bars, 6 notes, the copy from bar 2 (len %u n %u t3 %u)", k.len, k.nn, k.n[3].t);
    {
        uint32_t f0 = frame;
        nev = 0;
        run(TICKS(2u * KREC_BAR));
        CHECK(count(0x90, 60, f0, frame) == 2, "doubled: C twice in two bars (%d)", count(0x90, 60, f0, frame));
    }
    CHECK(krec_undo(&k) && k.len == KREC_BAR && k.nn == 3, "UNDO the DOUBLE: one bar again (len %u n %u)", k.len, k.nn);
    k.len = 8u * KREC_BAR;
    CHECK(krec_double(&k) && k.len == KREC_MAX, "8 bars double to 16");
    CHECK(!krec_can_double(&k) && !krec_double(&k), "16 bars: no DOUBLE");

    /* ---- GATE, DENSITY, TRANSPOSE */
    scenario();
    krec_tap(&k, frame);
    k.set.gate = 0;
    {
        uint32_t f0 = frame, on = 0, off = 0;
        nev = 0;
        run(TICKS(KREC_BAR));
        for (int i = 0; i < nev; i++) {
            if (ev[i].st == 0x90 && ev[i].d1 == 60 && !on) on = ev[i].frame + 1u;
            if (ev[i].st == 0x80 && ev[i].d1 == 60 && on && !off) off = ev[i].frame + 1u;
        }
        CHECK(on && off && off - on <= TICKS(2), "GATE 0: a tick long (%u frames)", off - on);
        (void)f0;
    }
    k.set.gate = 64;
    k.set.transpose = 64 + 5;
    {
        uint32_t f0 = frame;
        nev = 0;
        run(TICKS(KREC_BAR));
        CHECK(count(0x90, 65, f0, frame) == 1 && count(0x90, 60, f0, frame) == 0, "TRANSPOSE +5: C plays as F");
    }
    k.set.transpose = 64;
    {
        int prev = 1000;
        for (int d = 127; d >= 0; d -= 8) {
            int kept = 0;
            k.set.density = (uint8_t)d;
            for (uint32_t i = 0; i < k.nn; i++) kept += krec_note_kept(&k, &k.n[i]);
            CHECK(kept <= prev, "DENSITY thins as it falls (%d at %d, %d before)", kept, d, prev);
            prev = kept;
        }
        k.set.density = 0;
        CHECK(!krec_note_kept(&k, &k.n[0]), "DENSITY 0: nothing");
        k.set.density = 127;
    }

    /* ---- QUANTIZE while overdubbing: a note 5 ticks early lands on the eighth ahead of the playhead; it sounded live,
     * so the playhead reaching it plays nothing more, and the next cycle plays it */
    scenario();
    k.set.quantize = KQ_8;
    run(TICKS(240 - 5));                             /* 5 ticks before the eighth at 240 */
    nev = 0;
    {
        uint32_t f0 = frame;
        key(74, 90); run(TICKS(2)); key(74, 0);         /* (let go before the playhead reaches 240) */
        CHECK(k.n[k.nn - 1u].t == 240 || k.n[2].t == 240 || k.n[3].t == 240, "quantized forward to 240");
        run(TICKS(KREC_BAR - 240 - 2 + 5 - 12));     /* to just before the top */
        CHECK(count(0x90, 74, f0, frame) == 0, "the note ahead was not played again in its cycle (%d)",
              count(0x90, 74, f0, frame));
        f0 = frame;
        run(TICKS(KREC_BAR));
        CHECK(count(0x90, 74, f0, frame) == 1, "and plays the next (%d)", count(0x90, 74, f0, frame));
    }
    k.set.quantize = KQ_OFF;

    /* ---- QUANTIZE 1/16: a note 5 ticks late lands on the grid */
    reset();
    k.set.quantize = KQ_16;
    run(P * 2u);
    krec_arm(&k);
    key(60, 100); run(TICKS(10)); key(60, 0);
    run(TICKS(24 + 5 - 10));
    key(62, 100); run(TICKS(10)); key(62, 0);
    CHECK(k.nn == 2 && k.n[1].t == 24, "QUANTIZE 1/16: at 24, not %u", k.n[1].t);

    /* ---- the lanes: the wheel moved while overdubbing plays back; moved while only playing, it takes over */
    scenario();
    {
        int i;
        for (i = 0; i < 16; i++) {                   /* a ramp over the first half of the next cycle */
            run(TICKS(12));
            ctl(0xB0, 1, (uint8_t)(i * 8));
        }
        run(TICKS(KREC_BAR));                        /* past the top: the touch ends */
        krec_tap(&k, frame);                         /* PLAY */
        uint32_t f0 = frame;
        nev = 0;
        run(TICKS(KREC_BAR));
        CHECK(count(0xB0, 1, f0, frame) >= 8, "the wheel lane plays (%d CCs)", count(0xB0, 1, f0, frame));
        ctl(0xB0, 1, 127);                           /* a live move: the lane steps aside until the top */
        CHECK(k.hold[0] == 1 && krec_wheel_now(&k) == KREC_LANE_NONE, "the live wheel takes over");
        CHECK(krec_undo(&k) && k.wheel[0] == KREC_LANE_NONE, "UNDO: the lane as it was");
    }

    /* ---- 16 bars: the first pass closes itself; a full loop says so */
    reset();
    run(P);
    krec_arm(&k);
    key(60, 100); key(60, 0);
    run(TICKS(KREC_MAX + 8u));
    CHECK(k.state == KREC_ODUB && k.len == KREC_MAX, "16 bars: closed itself (state %u len %u)", k.state, k.len);
    for (int i = 0; i < 300; i++) { key((uint8_t)(40 + i % 40), 90); run(TICKS(3)); key((uint8_t)(40 + i % 40), 0); }
    CHECK(k.nn == KREC_NOTES && k.full, "full at %u notes", k.nn);
    for (uint32_t i = 1; i < k.nn; i++)
        if (k.n[i].t < k.n[i - 1u].t) { CHECK(0, "notes out of order at %u", i); break; }
    krec_clear(&k);
    CHECK(k.state == KREC_EMPTY && k.nn == 0 && !k.full, "CLEAR");

    /* ---- PLAY stop and start: all notes off, then from the top */
    scenario();
    run(TICKS(10));
    krec_set_playing(&k, 0, frame);
    CHECK(!k.playing && k.state == KREC_PLAY, "stopped");
    {
        int live = 0;
        for (int i = 0; i < 16; i++) live += k.act[i].live;
        CHECK(live == 0, "stopped: nothing left sounding");
    }
    krec_set_playing(&k, 1, frame);
    {
        uint32_t f0 = frame;
        nev = 0;
        run(TICKS(4));
        CHECK(count(0x90, 60, f0, frame) == 1, "started: from the top (C at once)");
    }

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("all recorder checks passed\n");
    return 0;
}
