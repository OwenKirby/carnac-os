/* Host test for the MIDI clock's lock (midi_clock.c, 2026-10-24) and the timed MIDI in (synth_midi_at).
 * Drives midi_clock frame by frame with 0xF8 pulses as the FM1 sees them: on the old 11.6 ms block grid (every pulse on
 * a 512-frame boundary), or stamped (0..66 frames late: USB's 1 ms frames and the 0.5 ms read). Checks the pulses the
 * clock plays: as many as came in, evenly spaced, through tempo jumps, ramps, Stop / Continue and a silent sender.
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

enum { GRID, STAMPED };
/* n pulses from `bpm`, `ramp` BPM added per pulse, each arriving per `how`; returns the pulses received */
static int feed(int n, double bpm, double ramp, int how, double *t)
{
    int k;
    for (k = 0; k < n; k++) {
        double arr = *t + (how == STAMPED ? urand() * 66.0 : 0.0);
        uint32_t at = how == GRID ? ((uint32_t)arr / 512u + 1u) * 512u : (uint32_t)arr;
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
    double t, sp[3];
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
    CHECK(nplayed - got >= 0 && nplayed - got <= 1, "silent sender: played %d of %d (one ahead at most)", nplayed, got);
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

    /* ---- a burst (two pulses on one frame, mid-run): both played, none owed ---- */
    start();
    t = now + 100.0;
    got = feed(24 * 2, 120.0, 0.0, STAMPED, &t);
    pulse_in();
    pulse_in();
    got += 2;
    frame();
    CHECK(abs(nplayed - got) <= 1, "burst: played %d of %d", nplayed, got);

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
    printf("midi_clock: pulses within %.2f ms (512 grid), %.2f / %.2f ms (stamped 90 / 140 BPM); timed MIDI in\n",
           sp[0], sp[1], sp[2]);
    return 0;
}
