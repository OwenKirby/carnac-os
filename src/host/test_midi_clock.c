/* Host test for the MIDI clock's lock (midi_clock.c, 2026-10-24, reworked 2026-11-12) and the timed MIDI in
 * (synth_midi_at). Drives midi_clock frame by frame with 0xF8 pulses as the FM1 sees them: on the old 11.6 ms block
 * grid (every pulse on a 512-frame boundary), stamped (0..66 frames late: USB's 1 ms frames and the 0.5 ms read), sent
 * on a DAW's 512-sample audio-buffer grid, or with 40 ms USB stalls. Checks the pulses the clock plays: as many as came
 * in, evenly spaced, through tempo jumps, ramps, Stop / Continue, Start with the clock already running, a lost audio
 * half (midi_clock_slip) and a silent sender. src/hardware/fm1_clock.py measures the same on the FM1.
 * Exit code 0 = pass. Test scaffolding only.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/test_midi_clock.c -lm -o test_midi_clock */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "preset.h"
#include "dsp/midi_clock.h"
#include "dsp/synth_runtime.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define FS 44100.0
#define NPLAY 8192
static uint32_t now, played[NPLAY];
static int nplayed;
static uint32_t rng = 12345u;
static double urand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; }

/* the pulses played since the last look, recorded at `now` */
static uint32_t seen;
static void record(void)
{
    while (seen != midi_clock_pulse_count()) {
        seen++;
        if (nplayed < NPLAY) played[nplayed++] = now;
    }
}

/* one frame of the clock */
static void frame(void)
{
    midi_clock_frame(now);
    record();
    now++;
}

static void pulse_in(void)   /* a 0xF8 on frame `now`, after the clock's own frame: as the render applies it */
{
    midi_clock_realtime(0xF8);
    record();
}

static void start(void)
{
    preset_defaults();
    perf_state.clock_source = CLKSRC_MIDI;
    midi_clock_init();
    now = 1000u;
    nplayed = 0;
    seen = midi_clock_pulse_count();
}

enum { GRID, STAMPED, DAW512, STALL };
#define DAW_BUF (512.0 * FS / 48000.0)   /* a 512-sample buffer at 48 kHz, in our frames */
/* n pulses from `bpm`, `ramp` BPM added per pulse, each arriving per `how`; returns the pulses received */
static int feed(int n, double bpm, double ramp, int how, double *t)
{
    int k;
    for (k = 0; k < n; k++) {
        double arr = *t + (how == GRID ? 0.0 : urand() * 66.0);
        uint32_t at;
        if (how == DAW512)                     /* sent at the next buffer boundary */
            arr = (floor(*t / DAW_BUF) + 1.0) * DAW_BUF + urand() * 66.0;
        if (how == STALL && fmod(*t, 2.0 * FS) < 0.040 * FS)   /* 40 ms every 2 s: held, then sent at once */
            arr = floor(*t / (2.0 * FS)) * 2.0 * FS + 0.040 * FS + urand() * 66.0;
        at = how == GRID ? ((uint32_t)arr / 512u + 1u) * 512u : (uint32_t)arr;
        while (now < at) frame();
        midi_clock_frame(now);
        record();
        pulse_in();
        now++;
        *t += FS * 60.0 / (bpm * 24.0);
        bpm += ramp;
    }
    return n;
}

/* the worst distance of played pulses i0.. from an even grid fitted to them, in ms */
static double spread_ms(int i0)
{
    int i, n = nplayed - i0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0, a, b, worst = 0;
    for (i = 0; i < n; i++) { sx += i; sy += played[i0 + i]; sxx += (double)i * i; sxy += (double)i * played[i0 + i]; }
    b = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    a = (sy - b * sx) / n;
    for (i = 0; i < n; i++) { double d = fabs(played[i0 + i] - (a + b * i)); if (d > worst) worst = d; }
    return worst * 1000.0 / FS;
}

int main(void)
{
    double t, sp[7];
    int got;

    /* ---- the old block grid: pulses every 512 or 1024 frames, played evenly ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 32, 120.0, 0.0, GRID, &t);
    CHECK(abs(nplayed - got) <= 1, "grid 120 BPM: played %d of %d received", nplayed, got);
    sp[0] = spread_ms(24 * 8);
    CHECK(sp[0] < 1.5, "grid 120 BPM: pulses within %.2f ms of even (want < 1.5)", sp[0]);
    { double bpm = FS * 60.0 * 256.0 / (24.0 * midi_clock_period_q8());
      CHECK(fabs(bpm - 120.0) < 0.5, "grid 120 BPM: tempo read as %.2f", bpm); }

    /* ---- stamped arrivals at 90 / 140 BPM ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 32, 90.0, 0.0, STAMPED, &t);
    CHECK(abs(nplayed - got) <= 1, "stamped 90 BPM: played %d of %d", nplayed, got);
    sp[1] = spread_ms(24 * 8);
    CHECK(sp[1] < 0.8, "stamped 90 BPM: within %.2f ms (want < 0.8)", sp[1]);
    start();
    t = now + 100.0;
    got = feed(24 * 32, 140.0, 0.0, STAMPED, &t);
    CHECK(abs(nplayed - got) <= 1, "stamped 140 BPM: played %d of %d", nplayed, got);
    sp[2] = spread_ms(24 * 8);
    CHECK(sp[2] < 0.8, "stamped 140 BPM: within %.2f ms (want < 0.8)", sp[2]);

    /* ---- a ramp 120 -> 128 over 8 beats: no pulse lost or doubled, the tempo followed ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 4, 120.0, 0.0, STAMPED, &t);
    got += feed(24 * 8, 120.0, 8.0 / (24 * 8), STAMPED, &t);
    got += feed(24 * 4, 128.0, 0.0, STAMPED, &t);
    CHECK(abs(nplayed - got) <= 1, "ramp: played %d of %d", nplayed, got);
    { double bpm = FS * 60.0 * 256.0 / (24.0 * midi_clock_period_q8());
      CHECK(fabs(bpm - 128.0) < 1.0, "ramp: tempo read as %.2f after (want 128)", bpm); }

    /* ---- jumps 120 -> 160 -> 80: lock again, the count kept ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 4, 120.0, 0.0, STAMPED, &t);
    got += feed(24 * 4, 160.0, 0.0, STAMPED, &t);
    got += feed(24 * 4, 80.0, 0.0, STAMPED, &t);
    CHECK(abs(nplayed - got) <= 1, "jumps: played %d of %d", nplayed, got);
    { double bpm = FS * 60.0 * 256.0 / (24.0 * midi_clock_period_q8());
      CHECK(fabs(bpm - 80.0) < 1.0, "jumps: tempo read as %.2f after (want 80)", bpm); }

    /* ---- the sender goes quiet with no Stop: at most one pulse played ahead, then nothing; the clock lets go ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 4, 120.0, 0.0, STAMPED, &t);
    { int i; for (i = 0; i < 44100; i++) frame(); }
    CHECK(nplayed - got >= 0 && nplayed - got <= 2, "silent sender: played %d of %d (two ahead at most)", nplayed, got);
    { uint32_t ph; CHECK(!midi_clock_phase(0, &ph), "silent sender: no clock to follow after 1 s"); }

    /* ---- Stop, then Continue: no pulses while stopped, the count kept after ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 2, 120.0, 0.0, STAMPED, &t);
    midi_clock_realtime(0xFC);
    { int i, n0 = nplayed; for (i = 0; i < 20000; i++) frame(); CHECK(nplayed == n0, "stopped: %d pulses played", nplayed - n0); }
    midi_clock_realtime(0xFB);
    t = now + 50.0;
    got += feed(24 * 2, 120.0, 0.0, STAMPED, &t);
    CHECK(abs(nplayed - got) <= 1, "stop / continue: played %d of %d", nplayed, got);

    /* ---- a burst (two pulses on one frame, mid-run): both played on their beats, none owed after ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 2, 120.0, 0.0, STAMPED, &t);
    pulse_in();
    pulse_in();
    got += 2;
    { int i; for (i = 0; i < 2 * 919; i++) frame(); }
    CHECK(abs(nplayed - got) <= 2, "burst: played %d of %d", nplayed, got);

    /* ---- a DAW's buffer-grid clock (512 samples at 48 kHz, 10.7 ms): 1/16 steps even (the old lock: 4.6 ms) ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 32, 120.0, 0.0, DAW512, &t);
    CHECK(abs(nplayed - got) <= 2, "DAW grid: played %d of %d", nplayed, got);
    sp[3] = spread_ms(24 * 4);
    CHECK(sp[3] < 2.5, "DAW grid 120 BPM: within %.2f ms (want < 2.5; the input +-5.3)", sp[3]);

    /* ---- 40 ms USB stalls every 2 s: ridden through (the old lock: 19 ms) ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 40, 120.0, 0.0, STALL, &t);
    CHECK(abs(nplayed - got) <= 2, "stalls: played %d of %d", nplayed, got);
    sp[4] = spread_ms(24 * 4);
    CHECK(sp[4] < 2.0, "stalls 120 BPM: within %.2f ms (want < 2)", sp[4]);

    /* ---- an audio half lost mid-run (the FM1 overloaded): told, the pulses stay on the beat ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 8, 120.0, 0.0, STAMPED, &t);
    { int i0 = nplayed, i; double sh;
      midi_clock_slip(512u);                   /* the frame count falls 512 behind real time: everything after is 512 less */
      t -= 512.0;
      got += feed(24 * 8, 120.0, 0.0, STAMPED, &t);
      for (i = i0; i < nplayed; i++) played[i] += 512u;   /* (back to real time to measure) */
      sh = spread_ms(24 * 4);
      CHECK(sh < 1.0, "slip: within %.2f ms of even in real time (want < 1)", sh);
      CHECK(abs(nplayed - got) <= 2, "slip: played %d of %d", nplayed, got); }

    /* ---- Start with the clock already running: the run's first pulse plays as it comes, the count from there ---- */
    start();
    t = now + 100.0;
    feed(24 * 2, 120.0, 0.0, STAMPED, &t);
    { int n0, k; uint32_t first_rx;
      while (now < (uint32_t)t - 200u) frame();
      midi_clock_realtime(0xFA);
      n0 = nplayed;
      first_rx = (uint32_t)(t + urand() * 66.0);
      while (now < first_rx) frame();
      CHECK(nplayed == n0, "start: %d pulses played before the run's first came", nplayed - n0);
      midi_clock_frame(now); record(); pulse_in(); now++;
      { int i; for (i = 0; i < 70 && nplayed == n0; i++) frame(); }   /* (one that came early waits for its beat) */
      CHECK(nplayed == n0 + 1 && played[n0] + 1u >= first_rx && played[n0] <= first_rx + 70u,
            "start: the first pulse at %u, came at %u", nplayed > n0 ? played[n0] : 0u, first_rx);
      t += FS * 60.0 / (120.0 * 24.0);
      k = feed(24 * 4 - 1, 120.0, 0.0, STAMPED, &t) + 1;
      CHECK(nplayed - n0 >= k && nplayed - n0 <= k + 2, "start: %d played of the run's %d", nplayed - n0, k);
      sp[5] = spread_ms(n0);
      CHECK(sp[5] < 0.8, "start, clock running: the run within %.2f ms (want < 0.8)", sp[5]); }

    /* ---- the next run at the same tempo flies on the last one's: its first beat already even ---- */
    { int i; for (i = 0; i < 30000; i++) frame(); }   /* (silent past the timeout) */
    midi_clock_realtime(0xFA);
    { int n0 = nplayed; t = now + 100.0;
      feed(24, 120.0, 0.0, STAMPED, &t);
      sp[6] = spread_ms(n0);
      CHECK(sp[6] < 1.0, "second run: its first beat within %.2f ms (want < 1)", sp[6]); }

    /* ---- a tempo jump 150 -> 90: the new tempo within a beat ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 8, 150.0, 0.0, STAMPED, &t);
    got += feed(24, 90.0, 0.0, STAMPED, &t);
    { double bpm = FS * 60.0 * 256.0 / (24.0 * midi_clock_period_q8());
      CHECK(fabs(bpm - 90.0) < 2.0, "jump 150 -> 90: tempo read as %.2f a beat later", bpm); }
    got += feed(24 * 4, 90.0, 0.0, STAMPED, &t);
    CHECK(abs(nplayed - got) <= 2, "jump 150 -> 90: played %d of %d", nplayed, got);

    /* ---- synth_midi_at: a pulse timed into the next render plays on its frame ---- */
    synth_init();
    perf_state.clock_source = CLKSRC_MIDI;
    { static int32_t buf[2 * 512]; uint32_t c0;
      synth_render_q15(buf, 512);
      c0 = midi_clock_pulse_count();
      synth_midi_at(300, 0xF8, 0, 0);
      synth_render_q15(buf, 256);
      CHECK(midi_clock_pulse_count() == c0, "synth_midi_at: not yet at frame 256 (%u)", midi_clock_pulse_count() - c0);
      synth_render_q15(buf, 256);
      CHECK(midi_clock_pulse_count() == c0 + 1u, "synth_midi_at: played by frame 512 (%u)", midi_clock_pulse_count() - c0); }

    if (fails) { printf("midi_clock: %d failures\n", fails); return 1; }
    printf("midi_clock: within %.2f ms (512 grid), %.2f / %.2f (stamped 90 / 140 BPM), %.2f (DAW grid), %.2f (stalls), "
           "%.2f (Start, clock running), %.2f (next run); timed MIDI in\n", sp[0], sp[1], sp[2], sp[3], sp[4], sp[5], sp[6]);
    return 0;
}
