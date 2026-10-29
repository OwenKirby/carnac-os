#include "midi_clock.h"
#include "preset.h"
#include "dsp_cold.h"

/* Musical divisions, slowest first. Ticks per cycle at 24 per quarter note:
 * 1/1 = 96, 1/2 = 48, 1/4 = 24, 1/8 = 12, 1/8T = 8, 1/16 = 6, 1/16T = 4, 1/32 = 3 */
static const uint8_t div_ticks[MIDI_CLOCK_NDIV] = { 96, 48, 24, 12, 8, 6, 4, 3 };

/* frames per tick, Q8: 44100 * 60 / (BPM * 24) */
#define FRAMES_PER_MIN_Q8 (44100u * 60u * 256u)
/* a MIDI clock silent for this long (0.5 s) is treated as stopped */
#define EXTERNAL_TIMEOUT_FRAMES 22050u
#define PERIOD_MIN_Q8 256u           /* at least one frame per tick, so a zero-gap burst cannot divide by zero */
/* The MIDI clock's lock (2026-10-24). Pulses reached the render on the FM1's 11.6 ms block grid, and each re-based the
 * grid and the period where it landed: steps up to ~6 ms off, the tempo wobbling 10-30 %. Now the pulses that come in
 * steer a prediction (a delay-locked loop: each one's error moves the next expected pulse by 1/8 of it and the period
 * by 1/64), and every pulse plays at its predicted frame: one that comes early waits for it, one that is late is
 * played by the prediction (at most one ahead of those received). A run's first beat (and the beat after a tempo jump:
 * two errors in a row past half a period; one alone is jitter and counts as half a period) plays the pulses as they
 * come and takes the period as the average over all of them, which one gap on a coarse grid cannot give. Pulses
 * played always equal pulses received. */
#define DLL_PHASE_SHIFT 3            /* 1/8 */
#define DLL_PERIOD_SHIFT 6           /* 1/64 */
#define ACQ_PULSES 24u               /* a beat: the period measured over the run before the loop takes over */

static uint32_t now_frame;           /* the frame the render loop is on */
static uint32_t last_tick_frame;     /* the frame of the latest pulse */
static uint32_t period_q8;           /* frames per tick, Q8 */
static uint32_t pulses;              /* pulse index since the last MIDI Start; the latest pulse is pulses - 1 */
static uint32_t pulse_total;         /* every pulse since boot, never reset (wraps): the MAQ sequencer counts these */
static uint32_t start_total;         /* MIDI Start messages since boot (wraps) */
static uint8_t ext_running;          /* MIDI transport: 1 after Start / Continue, 0 after Stop */
static uint32_t internal_acc_q8;     /* engine clock accumulator */
static uint8_t internal_bpm_seen;    /* INT TEMPO the internal period was computed for */
static uint8_t ticks_seen;           /* 0 / 1 / 2: a period is only measured from the second pulse on */
static uint8_t source_seen;          /* CLKSRC_* the state above was gathered for */
static uint32_t ext_rx_frame;        /* MIDI: the frame of the latest pulse received */
static uint32_t ext_next_q8;         /* MIDI, locked: the frame (Q8) the next pulse is expected to come in */
static int8_t ext_lead;              /* MIDI, locked: pulses played minus received, -1 .. 1 */
static uint8_t ext_miss;             /* MIDI, locked: pulses in a row past half a period from the prediction */
static uint32_t ext_miss_frame;      /* MIDI: the frame of the pulse before the first of those */
static uint8_t ext_acq;              /* MIDI: gaps measured since ext_acq_frame, while under ACQ_PULSES; 0 = the loop */
static uint32_t ext_acq_frame;       /* MIDI: the frame the period is measured from */
static uint32_t base_q32[MIDI_CLOCK_NDIV];   /* cycle phase at the latest pulse */
static uint32_t step_q32[MIDI_CLOCK_NDIV];   /* cycle phase advance per frame */

static uint8_t clock_source(void)
{
    return perf_state.clock_source == CLKSRC_MIDI ? CLKSRC_MIDI : CLKSRC_INTERNAL;
}

static uint8_t internal_bpm(void)
{
    /* int_tempo 0..127 -> 40..167 BPM. Clamped: a stored byte past 127 (a damaged record) would wrap the sum to 0 at
     * 216 and internal_period_q8 would divide by zero */
    uint8_t t = perf_state.int_tempo > VAL_MAX ? (uint8_t)VAL_MAX : perf_state.int_tempo;
    return (uint8_t)(MIDI_CLOCK_BPM_BASE + t);
}

static uint32_t internal_period_q8(void)
{
    return FRAMES_PER_MIN_Q8 / (24u * internal_bpm());
}

/* the period the engine clock runs this frame: a moved INT TEMPO takes effect at once (midi_clock_frame) */
static uint32_t internal_period_now(void)
{
    return internal_bpm() != internal_bpm_seen ? internal_period_q8() : period_q8;
}

/* the latest pulse index sets each division's cycle phase; the period sets how fast it advances */
static void recompute_grid(uint32_t latest)
{
    for (uint8_t d = 0; d < MIDI_CLOCK_NDIV; d++) {
        uint32_t cyc = div_ticks[d];
        uint64_t step = ((uint64_t)1 << 40) / ((uint64_t)cyc * period_q8);   /* 2^32 per cycle, / frames per cycle */
        base_q32[d] = (uint32_t)((((uint64_t)(latest % cyc)) << 32) / cyc);
        step_q32[d] = step > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)step;
    }
}

static void clock_reset(uint8_t src)
{
    source_seen = src;
    ticks_seen = 0;
    pulses = 0;
    ext_lead = 0;
    internal_acc_q8 = internal_period_q8() - 256u;   /* the engine clock's first pulse lands on the first frame: pulse 0 */
    internal_bpm_seen = internal_bpm();
    period_q8 = internal_period_q8();
}

/* one pulse of the clock, at `frame`: the engine clock's, or one the MIDI clock's lock plays */
static void clock_pulse(uint32_t frame)
{
    uint32_t latest = pulses++;
    pulse_total++;
    ticks_seen = ticks_seen < 2 ? (uint8_t)(ticks_seen + 1u) : 2;
    last_tick_frame = frame;
    recompute_grid(latest);
}

/* MIDI, locked, with no pulse played ahead: whether the next one is due at `frame`. With one received and not yet
 * played (ext_lead -1) it is due at its own predicted frame, one period before the next expected. */
static uint8_t ext_due(uint32_t frame)
{
    uint32_t due;
    if (ticks_seen != 2 || ext_lead > 0 || frame - ext_rx_frame > EXTERNAL_TIMEOUT_FRAMES)
        return 0;
    due = ext_lead == 0 ? ext_next_q8 : ext_next_q8 - period_q8;
    return (int32_t)((frame << 8) - due) >= 0;
}

/* MIDI, measuring: the period over every gap since ext_acq_frame (n of them), the next pulse a period on, and the
 * pulses owed played now */
static void ext_measure(uint32_t frame, uint32_t n)
{
    uint32_t p = ((frame - ext_acq_frame) << 8) / n;
    period_q8 = p < PERIOD_MIN_Q8 ? PERIOD_MIN_Q8 : p;
    ext_next_q8 = (frame << 8) + period_q8;
    while (ext_lead < 0) {
        ext_lead++;
        clock_pulse(frame);
    }
    ext_lead = 0;
    ext_miss = 0;
}

/* a 0xF8 received, at `frame` */
static void ext_pulse(uint32_t frame)
{
    int32_t e, half;
    uint32_t prev = ext_rx_frame;
    if (ticks_seen == 2 && frame - prev > EXTERNAL_TIMEOUT_FRAMES)
        ticks_seen = 0;                               /* a long gap is a new run, not a period */
    ext_rx_frame = frame;
    if (ticks_seen == 0) {                            /* a run's first pulse: plays as it comes */
        ext_lead = 0;
        ext_miss = 0;
        ext_acq = 0;
        ext_acq_frame = frame;
        clock_pulse(frame);
        return;
    }
    ext_lead--;
    if (ticks_seen == 1 || ext_acq) {                 /* the first beat: as they come, the period averaged */
        ext_acq = ext_acq + 1u >= ACQ_PULSES ? (uint8_t)0u : (uint8_t)(ext_acq + 1u);
        ext_measure(frame, ext_acq ? ext_acq : ACQ_PULSES);
        return;
    }
    e = (int32_t)((frame << 8) - ext_next_q8);
    half = (int32_t)(period_q8 >> 1);
    if (e > half || e < -half) {
        if (++ext_miss >= 2u) {                       /* a tempo jump: measure again from before the first miss */
            ext_acq_frame = ext_miss_frame;
            ext_acq = 2u;
            ext_measure(frame, 2u);
            return;
        }
        ext_miss_frame = prev;
        e = e > 0 ? half : -half;                     /* one alone: jitter, at most half a period */
    } else {
        ext_miss = 0;
    }
    ext_next_q8 += period_q8 + (uint32_t)(e >> DLL_PHASE_SHIFT);
    period_q8 = (uint32_t)((int32_t)period_q8 + (e >> DLL_PERIOD_SHIFT));
    if (period_q8 < PERIOD_MIN_Q8) period_q8 = PERIOD_MIN_Q8;
    while (ext_lead < -1) {                           /* two owed (a burst): the older one now */
        ext_lead++;
        clock_pulse(frame);
    }
}

void midi_clock_init(void)
{
    now_frame = 0;
    last_tick_frame = 0;
    clock_reset(clock_source());
}

void midi_clock_frame(uint32_t frame)
{
    uint8_t src = clock_source();
    now_frame = frame;
    if (src != source_seen)
        clock_reset(src);                             /* CLOCK SOURCE changed: measure the new clock from scratch */
    if (src == CLKSRC_INTERNAL) {
        if (internal_bpm() != internal_bpm_seen) {    /* INT TEMPO moved: the next pulse uses the new period (internal_period_now) */
            internal_bpm_seen = internal_bpm();
            period_q8 = internal_period_q8();
        }
        internal_acc_q8 += 256u;
        if (internal_acc_q8 >= period_q8) {
            internal_acc_q8 -= period_q8;
            clock_pulse(frame);
        }
    } else if (ext_due(frame)) {                      /* MIDI: the lock plays the pulse at its predicted frame */
        ext_lead++;
        clock_pulse(frame);
    }
}

uint8_t midi_clock_frame_changes(void)
{
    uint8_t src = clock_source();
    if (src != source_seen)
        return 1;                                     /* clock_reset */
    if (src == CLKSRC_MIDI)
        return ext_due(now_frame + 1u);               /* the render's next frame */
    return internal_acc_q8 + 256u >= internal_period_now();   /* clock_pulse */
}

SYNTH_COLD void midi_clock_realtime(uint8_t status)
{
    if (clock_source() != CLKSRC_MIDI)
        return;
    switch (status) {
    case 0xF8:                                        /* timing clock */
        ext_pulse(now_frame);
        break;
    case 0xFA:                                        /* start: the next pulse is beat 1, tick 0 */
        pulses = 0;
        ticks_seen = 0;
        start_total++;
        ext_running = 1;
        break;
    case 0xFB:                                        /* continue: keep the position, measure again */
        ticks_seen = 0;
        ext_running = 1;
        break;
    case 0xFC:                                        /* stop: no lock until the clock runs again */
        ticks_seen = 0;
        ext_running = 0;
        break;
    default:
        break;
    }
}

static uint8_t clock_valid(uint32_t frame)
{
    return ticks_seen >= 2 && !(source_seen == CLKSRC_MIDI && frame - ext_rx_frame > EXTERNAL_TIMEOUT_FRAMES);
}

uint8_t midi_clock_phase_at(uint8_t division, uint32_t frame, uint32_t *phase_q32)
{
    if (!clock_valid(frame))
        return 0;
    if (division >= MIDI_CLOCK_NDIV)
        division = MIDI_CLOCK_NDIV - 1u;
    /* wraps mod 2^32, which is exactly the cycle phase */
    *phase_q32 = base_q32[division] + (frame - last_tick_frame) * step_q32[division];
    return 1;
}

uint8_t midi_clock_step_at(uint8_t division, uint32_t frame, uint32_t *step_q32_out)
{
    if (!clock_valid(frame))
        return 0;
    if (division >= MIDI_CLOCK_NDIV)
        division = MIDI_CLOCK_NDIV - 1u;
    *step_q32_out = step_q32[division];
    return 1;
}

uint8_t midi_clock_phase(uint8_t division, uint32_t *phase_q32) { return midi_clock_phase_at(division, now_frame, phase_q32); }

uint8_t midi_clock_step(uint8_t division, uint32_t *step_q32_out) { return midi_clock_step_at(division, now_frame, step_q32_out); }

uint32_t midi_clock_pulse_count(void) { return pulse_total; }

uint32_t midi_clock_period_q8(void) { return period_q8; }

uint8_t midi_clock_is_external(void) { return source_seen == CLKSRC_MIDI; }

uint32_t midi_clock_start_count(void) { return start_total; }

uint8_t midi_clock_ext_running(void) { return ext_running; }
