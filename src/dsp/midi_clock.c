#include "midi_clock.h"
#include "preset.h"
#include "dsp_cold.h"
#include "hardware/mem_section.h"

/* Musical divisions, slowest first. Ticks per cycle at 24 per quarter note:
 * 1/1 = 96, 1/2 = 48, 1/4 = 24, 1/8 = 12, 1/8T = 8, 1/16 = 6, 1/16T = 4, 1/32 = 3 */
static const uint8_t div_ticks[MIDI_CLOCK_NDIV] = { 96, 48, 24, 12, 8, 6, 4, 3 };

/* frames per tick, Q8: 44100 * 60 / (BPM * 24) */
#define FRAMES_PER_MIN_Q8 (44100u * 60u * 256u)
/* a MIDI clock silent for this long (0.5 s) is treated as stopped */
#define EXTERNAL_TIMEOUT_FRAMES 22050u
#define PERIOD_MIN_Q8 256u           /* at least one frame per tick, so a zero-gap burst cannot divide by zero */
/* The MIDI clock's lock (2026-11-12; replaces 2026-10-24's DLL, which re-measured the tempo on any two pulses past
 * half a period: a DAW that sends its clock on audio-buffer boundaries, a USB hiccup or one late audio half set it off,
 * and its 1/16 steps landed up to 26 ms off; src/hardware/fm1_clock.py measures it). Every pulse plays at the frame
 * the lock predicts for it; the pulses that come in only steer the prediction:
 *  - a run's first EXT_ACQ pulses play as they come, and a straight line through them gives the period and phase (or,
 *    when a run before left a period, the lock flies on it from the first pulse, its phase the mean of the run's
 *    first EXT_ACQ pulses' errors: the first alone may have come in a stall);
 *  - then an alpha-beta tracker: each error moves the next expected pulse by a and the period by b = a^2 / (2 - a)
 *    (critically damped). a starts as a growing-memory straight-line fit (2 (2k - 1) / (k (k + 1))) and settles to a
 *    value set by the jitter seen (1/8 on a clean clock, down to 1/32 on a DAW's buffer-grid clock), raised toward 1/10
 *    while the errors lean one way (a tempo ramp);
 *  - an error is clipped to 3x the jitter around the lean, so one late burst barely moves it;
 *  - a tempo jump = 4 big same-sign errors in a row AND the median of the last 6 gaps off the period by 4 %: a line
 *    through the last 5 pulses takes over (a buffer grid beating against the clock only looks like a jump);
 *  - up to 2 pulses play ahead of those received (a USB stall), 1 after a big late error, none after two; pulses
 *    received and not yet played are due at once;
 *  - after Start / Continue / Stop nothing plays ahead until a pulse comes: the run's first plays as it comes;
 *  - an audio half the FM1 lost (midi_clock_slip) moves every prediction with it.
 * Pulses played always equal pulses received, after a Start. */
#define EXT_ACQ 12u                  /* pulses that play as they come at a run's start, then the line through them */
#define EXT_LEAD_MAX 2               /* pulses played ahead of those received */
#define EXT_HIST 12u                 /* the latest pulses' frames kept (a tempo jump's fit, the gaps' median) */
#define EXT_JUMP_N 4u                /* big same-sign errors in a row that may be a tempo jump */
#define EXT_MS_Q8 11294              /* 1 ms in frames, Q8 (44.1176 frames) */
#define EXT_JREF_Q8 6776             /* 0.6 ms: the jitter up to which the tracker runs at EXT_A_MAX */
#define EXT_A_MAX 8192u              /* gains Q16: 1/8 */
#define EXT_A_LO 2048u               /*  1/32: the floor on a jittery clock */
#define EXT_A_HI 6554u               /*  1/10: the floor while the errors lean one way */
enum { EXT_IDLE, EXT_ACQUIRE, EXT_TRACK };

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
static uint8_t ext_state;            /* MIDI: EXT_IDLE / EXT_ACQUIRE / EXT_TRACK */
static uint8_t ext_hold;             /* MIDI: after Start / Continue / Stop, nothing ahead until a pulse comes */
static uint32_t ext_next_q8;         /* MIDI, tracking: the frame (Q8) the next pulse is expected to come in */
static int8_t ext_lead;              /* MIDI: pulses played minus received, < 0 = owed, up to EXT_LEAD_MAX */
static uint32_t ext_k;               /* MIDI: pulses since the run's start or a tempo jump (the fit's memory) */
static int32_t ext_jit_q8;           /* MIDI: the mean |error| around the lean, Q8 frames */
static int32_t ext_lean_q8;          /* MIDI: the mean error over ~32 pulses, Q8 frames */
static uint8_t ext_big;              /* MIDI: big errors in a row, all of sign ext_big_sign */
static int8_t ext_big_sign;
static uint32_t ext_hist[EXT_HIST];  /* MIDI: the latest pulses' frames, oldest first */
static uint8_t ext_nhist;
static uint32_t ext_period_mem_q8;   /* MIDI: the period the last run left (0 = none): the next run flies on it */
static uint8_t ext_mem_n;            /* MIDI: flying on it, this pulse's index in the run (1 .. EXT_ACQ); 0 = not */
static uint32_t base_q32[MIDI_CLOCK_NDIV];   /* cycle phase at the latest pulse */
static uint32_t step_q32[MIDI_CLOCK_NDIV];   /* cycle phase advance per frame */

MidiClockLog midi_clock_log SYNTH_POOL;

void midi_clock_log_arm(uint8_t force_ext)
{
    MidiClockLog *g = &midi_clock_log;
    g->armed = 0;
    g->nrx = g->nplay = 0;
    g->nev = 0;
    g->relocks = g->clipped = g->owed = g->acquires = 0;
    g->force_ext = force_ext;
    g->armed = 1;
}

static void log_rx(uint32_t frame)
{
    MidiClockLog *g = &midi_clock_log;
    if (!g->armed) return;
    if (g->nrx < MIDI_CLOCK_LOG_N) g->rx[g->nrx++] = frame;
    else g->armed = 0;                                /* full: what it holds stays for the reader */
}

static void log_ev(uint8_t status, uint32_t frame)
{
    MidiClockLog *g = &midi_clock_log;
    if (!g->armed || g->nev >= MIDI_CLOCK_LOG_EV) return;
    g->ev_frame[g->nev] = frame;
    g->ev_rx[g->nev] = g->nrx;
    g->ev_status[g->nev++] = status;
}

static uint8_t clock_source(void)
{
    if (midi_clock_log.force_ext)
        return midi_clock_log.force_ext == 2u ? CLKSRC_INTERNAL : CLKSRC_MIDI;
    return perf_state.clock_source == CLKSRC_MIDI ? CLKSRC_MIDI : CLKSRC_INTERNAL;
}

static uint8_t internal_bpm(void)
{
    /* int_tempo 0..215 -> 40..255 BPM. Clamped: a stored byte past 215 (a damaged record) would wrap the sum to 0 at
     * 256 and internal_period_q8 would divide by zero */
    uint8_t t = perf_state.int_tempo > MIDI_CLOCK_TEMPO_MAX ? (uint8_t)MIDI_CLOCK_TEMPO_MAX : perf_state.int_tempo;
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
    ext_state = EXT_IDLE;
    ext_hold = 0;
    ext_period_mem_q8 = 0;
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
    if (source_seen == CLKSRC_MIDI && midi_clock_log.armed && midi_clock_log.nplay < MIDI_CLOCK_LOG_N)
        midi_clock_log.play[midi_clock_log.nplay++] = frame;
}

static int32_t ext_abs(int32_t v) { return v < 0 ? -v : v; }

/* MIDI: the frame (Q8) the next pulse to play is due at: a pulse owed (received, not played) at its predicted frame,
 * which has passed; one ahead at its own, while the lead allows it. 0 = none until a MIDI event. */
static uint8_t ext_next_due(uint32_t *due_q8)
{
    int32_t lim = EXT_LEAD_MAX;
    if (ext_state != EXT_TRACK)
        return 0;
    if (ext_big && ext_big_sign > 0)                  /* pulses coming in late: the tempo may have dropped */
        lim = ext_big >= 2u ? 0 : EXT_LEAD_MAX - 1;
    if (ext_lead >= 0 && (ext_hold || ext_lead >= lim))
        return 0;
    *due_q8 = ext_next_q8 + (uint32_t)((int32_t)ext_lead * (int32_t)period_q8);
    return 1;
}

static uint8_t ext_due(uint32_t frame)
{
    uint32_t due;
    if (frame - ext_rx_frame > EXTERNAL_TIMEOUT_FRAMES || !ext_next_due(&due))
        return 0;
    return (int32_t)((frame << 8) - due) >= 0;
}

static void ext_play(uint32_t frame)
{
    if (ext_lead < 0)
        midi_clock_log.owed++;
    ext_lead++;
    clock_pulse(frame);
}

/* MIDI: a straight line through the last n pulses (2 <= n <= EXT_HIST): the period, and the next expected pulse */
static void ext_fit(uint32_t n)
{
    int64_t sxy = 0, sy = 0;
    uint32_t i, h0 = ext_hist[ext_nhist - n];
    int32_t den = (int32_t)(n * (n * n - 1u) / 6u), slope;
    for (i = 0; i < n; i++) {
        int32_t y = (int32_t)(ext_hist[ext_nhist - n + i] - h0);
        sxy += (int64_t)(2 * (int32_t)i - (int32_t)n + 1) * y;
        sy += y;
    }
    slope = (int32_t)((sxy * 256) / den);             /* Q8 frames a pulse */
    period_q8 = slope < (int32_t)PERIOD_MIN_Q8 ? PERIOD_MIN_Q8 : (uint32_t)slope;
    /* the line's value at index n: its mean (at (n - 1) / 2) plus (n + 1) / 2 periods */
    ext_next_q8 = (h0 << 8) + (uint32_t)((sy * 256) / (int32_t)n) + (uint32_t)(((int32_t)n + 1) * slope / 2);
}

static void ext_hist_add(uint32_t frame)
{
    uint32_t i;
    if (ext_nhist == EXT_HIST) {
        for (i = 1; i < EXT_HIST; i++)
            ext_hist[i - 1u] = ext_hist[i];
        ext_nhist--;
    }
    ext_hist[ext_nhist++] = frame;
}

/* the median of the last 6 gaps, Q8 */
static uint32_t ext_gap_median_q8(void)
{
    uint32_t g[6], i, j, t;
    for (i = 0; i < 6u; i++)
        g[i] = ext_hist[ext_nhist - 6u + i] - ext_hist[ext_nhist - 7u + i];
    for (i = 1; i < 6u; i++)                          /* (six values: insertion sort) */
        for (j = i; j > 0 && g[j - 1u] > g[j]; j--) {
            t = g[j]; g[j] = g[j - 1u]; g[j - 1u] = t;
        }
    return (g[2] + g[3]) << 7;
}

/* the tracker's gains for this pulse (Q16): the growing fit's while it is young, else the jitter's with the lean's
 * floor; b critically damped */
static void ext_gains(uint32_t *a, uint32_t *b)
{
    uint32_t k = ext_k > 4096u ? 4096u : ext_k, a_ls, b_ls, a_ss, floor, den, lean;
    a_ls = (uint32_t)(((uint64_t)(2u * (2u * k - 1u)) << 16) / ((uint64_t)k * (k + 1u)));
    b_ls = (uint32_t)(((uint64_t)6u << 16) / ((uint64_t)k * (k + 1u)));
    if (ext_mem_n) {                                  /* flying on a known period: the phase the errors' mean */
        a_ls = 65536u / (ext_mem_n + 1u);
        b_ls = 0;
    }
    a_ss = ext_jit_q8 <= EXT_JREF_Q8 ? EXT_A_MAX : (EXT_A_MAX * (uint32_t)EXT_JREF_Q8) / (uint32_t)ext_jit_q8;
    den = ext_jit_q8 > 3 * EXT_MS_Q8 / 10 ? (uint32_t)ext_jit_q8 : (uint32_t)(3 * EXT_MS_Q8 / 10);
    lean = (uint32_t)ext_abs(ext_lean_q8);
    if (lean > den) lean = den;
    floor = EXT_A_LO + (uint32_t)(((uint64_t)(EXT_A_HI - EXT_A_LO) * lean) / den);
    if (a_ss < floor) a_ss = floor;
    if (a_ss > EXT_A_MAX) a_ss = EXT_A_MAX;
    *a = a_ls > a_ss ? a_ls : a_ss;
    a_ss = (uint32_t)(((uint64_t)a_ss * a_ss) / (131072u - a_ss));
    *b = b_ls > a_ss ? b_ls : a_ss;
}

/* a 0xF8 received, at `frame` */
static void ext_pulse(uint32_t frame)
{
    int32_t e, lim, ec, big_lim;
    uint32_t a, b;
    log_rx(frame);
    if (ext_state != EXT_IDLE && frame - ext_rx_frame > EXTERNAL_TIMEOUT_FRAMES)
        ext_state = EXT_IDLE;                         /* a long silence: a new run */
    ext_rx_frame = frame;
    ext_hold = 0;
    if (ext_state == EXT_IDLE) {                      /* a run's first pulse: plays as it comes */
        ext_lead = 0;
        ext_k = 1;
        ext_jit_q8 = ext_lean_q8 = 0;
        ext_big = 0;
        ext_nhist = 0;
        ext_hist_add(frame);
        midi_clock_log.acquires++;
        ext_mem_n = 0;
        if (ext_period_mem_q8) {                      /* the last run's tempo: fly on it from here */
            period_q8 = ext_period_mem_q8;
            ext_next_q8 = (frame << 8) + period_q8;
            ext_k = EXT_ACQ;
            ext_mem_n = 1;
            ext_state = EXT_TRACK;
        } else {
            ext_state = EXT_ACQUIRE;
        }
        clock_pulse(frame);
        return;
    }
    ext_hist_add(frame);
    if (ext_state == EXT_ACQUIRE) {                   /* as they come; the period so far the average gap */
        ext_k++;
        period_q8 = ((frame - ext_hist[0]) << 8) / (ext_nhist - 1u);
        if (period_q8 < PERIOD_MIN_Q8) period_q8 = PERIOD_MIN_Q8;
        clock_pulse(frame);
        ext_lead = 0;
        if (ext_k >= EXT_ACQ) {
            ext_fit(ext_nhist);
            ext_state = EXT_TRACK;
        }
        return;
    }
    e = (int32_t)((frame << 8) - ext_next_q8);
    ext_lead--;
    ext_k++;
    ext_lean_q8 += (e - ext_lean_q8) / 32;
    {
        int32_t dev = ext_abs(e - ext_lean_q8), half = (int32_t)(period_q8 >> 1);
        ext_jit_q8 += ((dev < half ? dev : half) - ext_jit_q8) / 16;
    }
    big_lim = 4 * ext_jit_q8 + EXT_MS_Q8;
    if (big_lim < (int32_t)(period_q8 >> 2)) big_lim = (int32_t)(period_q8 >> 2);
    if (ext_abs(e) > big_lim) {
        int8_t sg = e > 0 ? 1 : -1;
        ext_big = (ext_big && sg == ext_big_sign) ? (uint8_t)(ext_big < 255u ? ext_big + 1u : 255u) : 1u;
        ext_big_sign = sg;
    } else {
        ext_big = 0;
    }
    if (ext_big >= EXT_JUMP_N && ext_nhist >= 7u) {   /* a tempo jump, if every gap moved (not one double-pulse buffer) */
        int32_t d = (int32_t)(ext_gap_median_q8() - period_q8);
        if (ext_abs(d) > (int32_t)(period_q8 / 25u)) {
            midi_clock_log.relocks++;
            ext_fit(EXT_JUMP_N + 1u);
            ext_k = EXT_JUMP_N + 1u;
            ext_mem_n = 0;
            ext_big = 0;
            ext_lean_q8 = 0;
            ext_period_mem_q8 = period_q8;
            while (ext_due(frame))
                ext_play(frame);
            return;
        }
    }
    lim = 3 * ext_jit_q8;
    if (lim < EXT_MS_Q8) lim = EXT_MS_Q8;
    ec = e;
    if (ext_k > EXT_ACQ + 4u) {                       /* (a young fit takes the error whole) */
        int32_t d = e - ext_lean_q8;
        if (d > lim || d < -lim) {
            midi_clock_log.clipped++;
            ec = ext_lean_q8 + (d > 0 ? lim : -lim);
        }
    }
    ext_gains(&a, &b);
    ext_mem_n = ext_mem_n && ext_mem_n < EXT_ACQ ? (uint8_t)(ext_mem_n + 1u) : 0u;
    ext_next_q8 += period_q8 + (uint32_t)(int32_t)(((int64_t)ec * a) >> 16);
    {
        int32_t p = (int32_t)period_q8 + (int32_t)(((int64_t)ec * b) >> 16);
        period_q8 = p < (int32_t)PERIOD_MIN_Q8 ? PERIOD_MIN_Q8 : (uint32_t)p;
    }
    ext_period_mem_q8 = period_q8;
    while (ext_due(frame))                            /* owed: at once */
        ext_play(frame);
}

void midi_clock_slip(uint32_t n)
{
    uint32_t i;
    if (source_seen != CLKSRC_MIDI || ext_state == EXT_IDLE)
        return;
    ext_next_q8 -= n << 8;
    ext_rx_frame -= n;
    for (i = 0; i < ext_nhist; i++)
        ext_hist[i] -= n;
    if (midi_clock_log.armed && midi_clock_log.nev < MIDI_CLOCK_LOG_EV) {
        midi_clock_log.ev_frame[midi_clock_log.nev] = now_frame + 1u;   /* (the first frame it moves) */
        midi_clock_log.ev_rx[midi_clock_log.nev] = midi_clock_log.nrx;
        midi_clock_log.ev_status[midi_clock_log.nev++] = (uint8_t)(0x10u | ((n >> 9) > 15u ? 15u : (n >> 9)));
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
        ext_play(frame);
    }
}

/* Quiet frames (2026-11-07): after a midi_clock_frame, how many of the frames that follow change nothing but the engine
 * clock's accumulator and now_frame (no pulse, no reset). The render skips their calls and settles them in one go
 * (midi_clock_skipped) before anything can see the difference: the UI writes only between passes, and a MIDI event
 * settles it first. The MIDI lock too since 2026-11-12 (it only changes on a pulse it plays, at its due frame, or on a
 * MIDI event): ~5 load points with the sequencers playing on MIDI clock. */
#define QUIET_MAX 4096u                               /* (a pass is 256 frames: "until an event") */
uint32_t midi_clock_quiet(void)
{
    uint32_t need;
    uint8_t src = clock_source();
    if (src == CLKSRC_MIDI && source_seen == CLKSRC_MIDI) {
        uint32_t due;
        int32_t d;
        if (!ext_next_due(&due))
            return QUIET_MAX;
        d = (int32_t)(due - (now_frame << 8));        /* frames now + j, j = 1.., are quiet while 256 j < d */
        return d <= 0 ? 0u : (((uint32_t)d + 255u) >> 8) - 1u;
    }
    if (src != CLKSRC_INTERNAL || source_seen != CLKSRC_INTERNAL || internal_bpm() != internal_bpm_seen)
        return 0;
    need = period_q8 - internal_acc_q8;               /* acc < period after a frame */
    if (internal_acc_q8 >= period_q8)
        return 0;
    return ((need + 255u) >> 8) - 1u;                 /* frames j with acc + 256 j < period */
}

/* n quiet frames, the last of them `last`, as midi_clock_frame would have run them */
void midi_clock_skipped(uint32_t n, uint32_t last)
{
    internal_acc_q8 += 256u * n;
    now_frame = last;
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
    if (status != 0xF8u)
        log_ev(status, now_frame);
    switch (status) {
    case 0xF8:                                        /* timing clock */
        ext_pulse(now_frame);
        break;
    case 0xFA:                                        /* start: the next pulse is beat 1, tick 0, as it comes */
        pulses = 0;
        ticks_seen = 0;
        start_total++;
        ext_running = 1;
        if (ext_lead > 0) ext_lead = 0;               /* (ones played ahead were the stopped clock's) */
        ext_hold = 1;
        break;
    case 0xFB:                                        /* continue: keep the position and the lock */
        ticks_seen = 0;
        ext_running = 1;
        if (ext_lead > 0) ext_lead = 0;
        ext_hold = 1;
        break;
    case 0xFC:                                        /* stop: nothing ahead; the lock keeps the clock if it runs on */
        ticks_seen = 0;
        ext_running = 0;
        ext_hold = 1;
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
