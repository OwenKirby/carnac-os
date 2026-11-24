#include "hardware/synth_libc.h"
#include "fx.h"
#include "hardware/mem_section.h"
#include "amplifier.h"                           /* amp_exp2f (DELAY HP) */
#include "dsp_cold.h"
#include "synth_prof.h"                /* the profile build: the delay loop's parts timed (2026-11-24) */

/* Delay: a mono tape echo. The reverb's ROOM network and its tuning are the Felucca firmware's
 * (firmware/src/fx.c rev_room): 4 damped combs into 2 allpasses, Freeverb-style.
 *
 * Every buffer here lives in .pool (SYNTH_POOL): the 96 KiB .data + .bss region cannot hold them.
 *
 * TAPE ECHO MODEL
 * The tape is a loop of TAPE_LEN cells. The record head writes one cell each time it crosses a cell boundary,
 * and the play head trails it by the head gap. Delay = gap / speed:
 *   - delays up to TAPE_GAP_MAX samples run the tape at full speed (1 cell per sample) with gap = delay;
 *   - longer delays keep the gap at its maximum and slow the tape down (speed = TAPE_GAP_MAX / delay), which
 *     lowers the bandwidth like a real slowed tape. At the 1.0 s maximum the tape runs at ~0.37 speed.
 * Gap and speed slew toward their targets, so moving DELAY TIME (or a sync tempo change) bends the pitch of
 * the echoes instead of clicking. Wow and flutter wobble the play head (from TAPE DEGRADE); the record
 * path saturates softly; playback loses top end (head loss) and low end (a high-pass), both set by TAPE DEGRADE,
 * so each repeat is darker and thinner. TAPE DEGRADE drives all three (tape_degrade); its default 36 gives the old
 * fixed character (32, 10, 36). PING PONG adds a second play head (tape_delay_block). */
/* THE TAPE'S MEMORY (2026-11-14, 2026-11-23, 2026-11-24). TAPE_LEN 12-bit cells (each held as its value, tape12_q),
 * 1.0 s at the half rate (22.05 kHz cells). The 8-bit mu-law cells of 2026-11-14 gave the bass buzz (a 55 Hz sine's 5th
 * and 7th harmonics +13 dB against -25 dB at 16 bits) and the hiss (the 1..2 kHz band +15 dB against -17): the cell's
 * resolution, not a filter, was the floor. (The BBD that shared it was removed 2026-11-14.) */
/* TAPE_ROUND, TAPE_EMPH (2026-11-19, the user: the delay added "a bottom layer of noise" under low sounds): a cell is
 * rounded to the nearer mu-law step (it was rounded toward zero everywhere, an error that followed the signal's sign: a
 * buzz under a bass, 0..200 Hz noise -32 -> -54 dB under a dark bass, host measure), toward zero only in the bottom
 * segment, so a dying loop still reaches silence. And an emphasis pair round the cells: the record side tilts up
 * (x - 0.5 x[-1]), each head tilts back (y + 0.5 y[-1]), exactly inverse, so the echo's tone is unchanged and the cells'
 * noise is tilted down where a bass leaves the spectrum empty (4..11 kHz -43 -> -49 dB; under bright sounds the low end
 * about as before). TAPE_EMPH 1 on, 0 none; the tilt is 0.5 exactly, so a shift (bit-identical to the Q15 multiply). */
#ifndef TAPE_ROUND
#define TAPE_ROUND 1
#endif
#ifndef TAPE_EMPH
#define TAPE_EMPH 1
#endif
/* TAPE_HALF 1 (2026-11-14): the tape runs at half the sample rate, as the affordable 80s delays did (~10 kHz wide, every
 * repeat through the decimator and the interpolator again: darker each pass), and never slows: no slowed tape, none of
 * its filters, no aliasing at any DELAY TIME. A delay longer than the tape holds stops at its end. */
#ifndef TAPE_HALF
#define TAPE_HALF 1                       /* (the user's pick, 2026-11-14: E, 32 KB: 1.49 s, PING PONG to 0.74 s a side) */
#endif
#define TAPE_LEN 22200u                    /* mono: 22200 cells, 1.0 s (2026-11-23: the sync floor's quarter note fits) */
#define ST_LEN 11100u                      /* stereo: each line, cells (the right line starts at ST_LEN) */
#define TAPE_MASK (TAPE_LEN - 1u)
#define TAPE_GAP_MAX (TAPE_LEN - 64u)     /* room for the wow + flutter excursion (< 56 cells) and the neighbour */
/* gap / speed slew: tau = 1024 samples, about 23 ms, settled in ~0.1 s (2026-10-26: was 93 ms, ~0.5 s to settle, so
 * a note played soon after a DELAY TIME change had its first echo at an in-between length and the repeats at another) */
#define TAPE_SLEW 10
#define WOW_INC 48696u                    /* 0.5 Hz phase increment at 44.1 kHz (2^32 / 44100 * f) */
#define FLUTTER_INC 613567u               /* 6.3 Hz */
#define WOW_INC2 55026u                   /* the second head's (PING PONG's near head, the BBD's right line, 2026-11-03):
                                           * 0.565 Hz ... */
#define FLUTTER_INC2 570617u              /* ... 5.86 Hz: each side wobbles on its own, the repeats drift apart */
/* Wow and flutter move a head at a few Hz: the pull is computed every MOD_EVERY samples (two sine lookups and two
 * multiplies a head) and interpolated between (2026-11-06: ping pong's second head cost ~11 ticks a sample on core 0). */
#define MOD_EVERY 8u
#define WOW_STEP_Q8 96                    /* per wow step (tape_degrade): 127 = +-47.6 cells at full speed, 0.34 % pitch */
#define FLUTTER_STEP_Q8 16                /* per flutter step: 127 = +-7.9 cells, 0.71 % pitch */

#define REV_COMB_TOTAL (1116u + 1188u + 1277u + 1356u)
#define REV_AP_TOTAL (556u + 441u)

static const uint16_t SFX_REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t SFX_REV_AP[2] = {556, 441};
/* sync divisions, shortest first (2026-11-24: down to 1/64T): each is NUM / DEN of the whole note, and its name. A whole
 * note is 96 pulses (fx_pulse_q8), so the length is exact in samples at any tempo. */
static const uint8_t DELAY_SYNC_NUM[13] = {1, 1, 1, 1, 1, 3, 1, 1, 3, 1, 1, 3, 1};
static const uint8_t DELAY_SYNC_DEN[13] = {96, 64, 48, 32, 24, 64, 16, 12, 32, 8, 6, 16, 4};
static const char *const DELAY_SYNC_NAME[13] = {"1/64T", "1/64", "1/32T", "1/32", "1/16T", "1/32.", "1/16", "1/8T",
                                                "1/16.", "1/8", "1/4T", "1/8.", "1/4"};

/* The 12-bit cell (2026-11-23): sign, a 4-bit exponent, a 7-bit mantissa. Exponent 0 is linear (0..127, one step each);
 * exponent e >= 1 is (128 + m) << (e - 1), so each octave has 128 steps and the step doubles with the level, as a
 * compander's does. Values past 32640 clip; a write rounds to the nearest step.
 * Each cell is held as the value its 12-bit code stands for (2026-11-24: an int16, not the code): the same values, so
 * the same echo bit for bit, but a read is one load and a write one store. Packed two codes to three bytes, each read
 * unpacked and decoded through a table and each write read back two bytes and merged its nibbles: the tape loop is
 * load-bound on the FM1 (~8 cycles a load). 44.4 KB of .pool for the 33.3 KB packed and the 8 KB decode table. */
static struct {
    int16_t c[TAPE_LEN];
} tape_mem SYNTH_POOL;
static uint8_t tape12_exp[256] SYNTH_POOL;                /* 1 + floor(log2(q)): the exponent of mag >> 7 = q */
static void tape12_tables(void)
{
    tape12_exp[0] = 0;
    for (uint32_t q = 1; q < 256u; q++) {
        uint32_t e = 1, t = q;
        while (t >>= 1) e++;
        tape12_exp[q] = (uint8_t)e;
    }
}
/* x rounded to the nearest 12-bit step (toward the next octave's first step when the mantissa rounds over; 32640 at
 * the top): the value of the code the packed cells stored */
static inline int32_t tape12_q(int32_t x)
{
    int32_t mag = x < 0 ? -x : x;
    if (mag > 32640) mag = 32640;
    if (mag >= 128) {
        int32_t e = tape12_exp[mag >> 7];                     /* (mag 128 .. 32640: mag >> 7 is 1 .. 255) */
        int32_t sh = e - 1, m = (mag + ((1 << sh) >> 1)) >> sh;   /* the rounded mantissa, 128 .. 256 */
        if (m >= 256) {                                       /* rounded into the next octave */
            if (e < 8) { m = 128; sh = e; } else m = 255;
        }
        mag = m << sh;
    }
    return x < 0 ? -mag : mag;
}
static inline int32_t tape12_rd(uint32_t i) { return tape_mem.c[i]; }
static inline void tape12_wr(uint32_t i, int32_t v) { tape_mem.c[i] = (int16_t)tape12_q(v); }
PS_STATIC_ASSERT(TAPE_LEN == 2u * ST_LEN, "the tape: the mono line, or the two stereo lines end to end");
#define TAPE_RD(i) tape12_rd(tbase + (i))
#define TAPE_WR(i, v) tape12_wr(tbase + (i), (v))
/* Core 1's memory (2026-10-16): what core 1 writes (the reverb's back below, through fx_reverb_job) sits on cache
 * lines of its own, 64 bytes, as X0X keeps its mailbox (fm1_cpu1.h: each core's writes on lines of their own). A
 * precaution, added while hunting the "digital buzz with reverb" (which turned out to be a miscompiled clear of the mix
 * bus: synth_runtime.c render_pass), kept because the two cores share no line it writes now. Scalars: in structs
 * aligned (so padded) to a line, their old names kept by #defines. Arrays: aligned, lengths padded to whole lines. */
#define FX_LINE __attribute__((aligned(64)))
#define FX_PAD16(n) (((n) + 31u) & ~31u)                 /* int16 elements to whole 64-byte lines */
static int16_t sfx_rev_comb[FX_PAD16(REV_COMB_TOTAL)] SYNTH_POOL FX_LINE;   /* core 1: the ROOM network */
static int16_t sfx_rev_ap[FX_PAD16(REV_AP_TOTAL)] SYNTH_POOL FX_LINE;

typedef struct {
    uint32_t wpos_q16;                    /* record head, cells in Q16; wraps with the uint32 (65536 cells) */
    int32_t gap_q16, gap_tgt_q16;         /* head gap, cells in Q16 */
    int32_t speed_q24, speed_tgt_q24;     /* cells per sample in Q24, at most 1.0 */
    uint32_t last_len;                    /* the delay length the targets were computed for */
    uint32_t wow_ph, flutter_ph;
    int32_t aa_lp;                        /* anti-alias low-pass on the record signal (slowed tape) */
    int32_t loss_lp, hp_lp;               /* playback head loss and high-pass states */
    int32_t loss2_lp, hp2_lp;             /* PING PONG: the near head's */
    int32_t run_lp;                       /* the runaway's low-pass in the tape's feedback (delay_fb) */
    float scoop;                          /* DELAY HP's low-pass in the tape's feedback (delay_fb) */
    uint32_t seed_rng;                    /* the runaway's hiss seed (the tape's) */
    /* the slowed tape's anti-aliasing (tape_delay_block, 2026-11-03): the record filter's second pole, the last
     * sample it gave (the cell's value is read between it and the next at the crossing), each head's reconstruction */
    float aa_z[6], rc_z[4], rcb_z[4];     /* (the biquads' states: the record filter's, each head's; together) */
    int32_t aa_h[3];                      /* the record filter's last three samples, oldest first (the crossings) */
    int32_t hz[3];                        /* TAPE_HALF: the decimator's last three inputs, oldest first */
    int32_t ho_l[2], ho_r[2];             /* TAPE_HALF: the heads' last two half-rate outputs, [1] the newest */
    uint32_t hstep;                       /* TAPE_HALF: half-rate steps (the wobble's update clock) */
    int32_t hm[4];                        /* TAPE_HALF: the heads' pull and its step (m1, d1, m2, d2), across blocks */
    uint8_t hph;                          /* TAPE_HALF: 1 when the next input completes a step's pair */
    uint32_t wow_ph2, flutter_ph2;        /* the second head's wow and flutter (WOW_INC2) */
    int32_t mt[2][2];                     /* the wow + flutter pull of each head, at the last 8-sample step (MOD_EVERY): [head][tape / BBD] */
    int32_t em_w, em_r[2];                /* TAPE_EMPH: the record side's last input, each head's last output */
    uint32_t brk_z;                       /* TAPE_BREAK: its random stream */
    int32_t brk_g, brk_tgt, brk_left;     /* the record level now and its target (Q15), the dropout's steps left */
    int32_t brk_snag;                     /* the heads' jolt, Q16 cells */
    uint32_t tw_ph, tw_cnt, tw_z;         /* TAPE_WEAR: the breath's spin, the drift's block count, its random stream */
    uint32_t rc_i; int32_t rc_v;          /* the read cache: the last cell decoded (the next step's first), its value (2026-11-24) */
    float tw_tgt, tw_v, tw_d, tw_dc;      /* TAPE_WEAR: the drift's target, its two smoothing stages (Q16 cells), the DC */
} TapeState;

static TapeState tpl[2];                 /* the lines: one (mono), or two (stereo: left, then right) */
static TapeState *tcur = &tpl[0];        /* the line being run (tp is its state) */
#define tp (*tcur)
static uint32_t tbase = 0u, tlen = TAPE_LEN;     /* the line's cells: the first, the count (its wrap) */
static uint32_t tgapmax = TAPE_GAP_MAX;          /* the line's longest delay, in cells */
static uint32_t tlen_q8 = 256u;                  /* the line's length, Q8 of the mode's (stereo's right line: 3/4) */
static int tpp = 0;                              /* PING PONG inside the line (stereo runs its cross-feed instead) */
static struct FX_LINE {                               /* core 1: the ROOM network's state, the back's read head */
    uint32_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint32_t rv_r;
} fx_room_c1;
#define comb_i (fx_room_c1.comb_i)
#define ap_i (fx_room_c1.ap_i)
#define comb_lp (fx_room_c1.comb_lp)
#define rv_r (fx_room_c1.rv_r)

static void tank_init(void);                       /* the MOD / DOLMEN / TUMULUS tank, below */
static void spring_init(void);                     /* SPRING, below */
static void rv_half_init(void);                    /* the tank's half rate, below */
static int32_t clamp16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
/* 32-bit Q15 multiply (no int64 on the 32-bit core). Every call here has |a| <= 65535 and 0 <= b <= 32767,
 * so the product stays below 2^31: tape states and comb low-passes track int16 values, so their differences
 * are at most 65535, and every coefficient is a Q15 value at most 32767. */
static int32_t sfx_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }

#define FX_REV_LAG FX_BLOCK_MAX                   /* the reverb's front runs this far ahead (fx_reverb_front) */
#define RV_RING 256u
PS_STATIC_ASSERT(RV_RING >= FX_REV_LAG + FX_BLOCK_MAX && (RV_RING & (RV_RING - 1u)) == 0u, "the front's ring");
static int32_t rv_ring[RV_RING];
static uint32_t rv_w;
/* The send's side (2026-11-03): DLY > REV's (L - R) / 2 (xf_way2, below), the reverb's only stereo input. It skips the
 * front's diffusers and limiter (it is the loop's, clipped on XF_H) and rides its own ring beside rv_ring; the back
 * adds it to the tank's left half and takes it from the right (the springs: one each). 0 while DLY > REV is off */
static int32_t rv_sring[RV_RING];
static float rv_side_in[FX_BLOCK_MAX];             /* this pass's, for the front (fx_cross_to_rev writes it) */
static uint32_t rv_side_n;                         /* its length; 0: none this pass */
/* The sends' limiters (2026-10-16): the summed sends of a few hot voices pass full scale (8 driven voices: +8 dB), and
 * the hard clamps that stood here clipped the chord before the reverb and the delay, the "digital buzz with reverb and
 * several voices" (measured: up to 21 % of the samples clipped). The master limiter's law instead: the envelope jumps
 * to a peak over the ceiling (no sample passes it) and falls back to the ceiling with tau ~0.1 s; the gain is ceiling
 * / envelope. Float: a divide only while it limits (8 cycles on the FM1, the integer one 24). */
#define SEND_LIM_CEIL 32767.f
#define SEND_LIM_REL 0.99977f                              /* per sample: tau 0.1 s at 44.1 kHz */
static float rv_lim_env = SEND_LIM_CEIL, dl_lim_env = SEND_LIM_CEIL, dl_lim_env_r = SEND_LIM_CEIL;

/* in (any int32 level) -> out (Q15), limited; *envp the limiter's envelope */
static void send_limit(float *envp, const int32_t *in, int32_t *out, uint32_t n)
{
    float env = *envp;
    for (uint32_t s = 0; s < n; s++) {
        int32_t v = in[s];
        float a = (float)(v < 0 ? -v : v);
        if (env > SEND_LIM_CEIL)
            env = SEND_LIM_CEIL + (env - SEND_LIM_CEIL) * SEND_LIM_REL;
        if (a > env)
            env = a;
        if (env > SEND_LIM_CEIL)
            v = (int32_t)((float)v * (SEND_LIM_CEIL / env));
        out[s] = clamp16(v);                               /* (in case a float rounding lands a hair over) */
    }
    *envp = env;
}

static void xf_init(void);                         /* the cross-feeds, below */

void fx_init(void)
{
    xf_init();
    tape12_tables();
    memset(&tape_mem, 0, sizeof tape_mem);
    memset(sfx_rev_comb, 0, sizeof sfx_rev_comb);
    memset(sfx_rev_ap, 0, sizeof sfx_rev_ap);
    memset(comb_lp, 0, sizeof comb_lp);
    memset(tpl, 0, sizeof tpl);
    for (int k = 0; k < 2; k++) {
        tcur = &tpl[k];
        tp.brk_g = tp.brk_tgt = 32767;                        /* TAPE_BREAK: the record level whole */
        tp.tw_z = 0x2545F491u;                                /* TAPE_WEAR: its own random stream */
    tp.rc_i = 0xFFFFFFFFu;                                /* (no cell cached) */
        tp.gap_q16 = tp.gap_tgt_q16 = 64 << 16;
        tp.speed_q24 = tp.speed_tgt_q24 = 1 << 24;
        tp.last_len = 64u;
        tp.seed_rng = 0x6C8E9CF5u;
        tp.wow_ph2 = 0x6A000000u;                          /* (the second head starts out of step with the first) */
        tp.flutter_ph2 = 0x35000000u;
    }
    tcur = &tpl[0];
    tbase = 0u; tlen = TAPE_LEN; tgapmax = TAPE_GAP_MAX; tlen_q8 = 256u; tpp = 0;
    memset(comb_i, 0, sizeof comb_i);
    memset(ap_i, 0, sizeof ap_i);
    memset(rv_ring, 0, sizeof rv_ring);
    memset(rv_sring, 0, sizeof rv_sring);
    rv_side_n = 0;
    rv_lim_env = dl_lim_env = dl_lim_env_r = SEND_LIM_CEIL;
    rv_w = 0;
    rv_r = RV_RING - FX_REV_LAG;                       /* the back reads the lag behind the front */
    tank_init();
    spring_init();
    rv_half_init();
#if defined(FX_LAYOUT_PAD) && FX_LAYOUT_PAD > 0
    extern void (*volatile fx_layout_pad_ref)(void);
    (void)fx_layout_pad_ref;                       /* (a read: the layout pad stays through --gc-sections) */
#endif
}

/* Delay time in samples, always at least 64 and at most 32576 (0.74 s; 2026-11-23: was 1.0 s at a step of 347; the knob
 * now has 256 a step, ~5.8 ms, and the factory bank's times were rescaled to keep them). Sync picks a division of the
 * 120 BPM grid, halved until it fits. */
#define DELAY_TIME_STEP 256u
uint32_t fx_pulse_q8 = 235200u;                   /* 918.75 frames: 120 BPM */
#define DELAY_LEN_MAX (64u + 127u * DELAY_TIME_STEP)  /* 0.74 s, DELAY TIME's top: mono (2026-11-23) */
#define DELAY_TIME_STEP_ST 173u                       /* stereo: each line 11100 cells, the knob's top ~0.4996 s */
#define DELAY_LEN_ST (2u * (ST_LEN - 64u))            /* the longest stereo delay: the line less its wow margin (22072) */
#define DELAY_SYNC_LEN_MAX (2u * (TAPE_LEN - 64u))    /* the longest mono sync delay (~1.0 s) */
/* The sync divisions are fixed (2026-11-23, the user: the division must not change with the tempo): the knob plays the
 * first N of the list, shortest first, at any tempo. N is what fits the line at SYNC_FLOOR_BPM: a quarter note at 80 BPM
 * is 0.75 s (mono's line, 1.0 s), a triplet quarter 0.5 s (stereo's line). The list starts at 1/64T. Below the floor a division can be longer than
 * its line: it then plays the line's length and its label carries a * (fx_sync_capped). */
#define SYNC_FLOOR_BPM 80u
#define SYNC_COUNT_MONO 13u                           /* 1/64T .. 1/4 */
#define SYNC_COUNT_STEREO 11u                         /* 1/64T .. 1/4T */
/* Synced (2026-11-03, user: "tempo fix is necessary"; until then the divisions were of a fixed 120 BPM bar): the
 * division at the tempo. Past the 1.0 s the tape is made for (a half note under 120 BPM) it halves, so the echo
 * stays on the grid an octave of time shorter. A MIDI clock's tempo wobbles a little pulse to pulse: a change under
 * ~0.4 % keeps the length it had (delay_targets), so a steady clock gives a steady echo, not a warble. */
static inline uint32_t sync_len(uint8_t div) { return ((fx_pulse_q8 * 96u) >> 8) * DELAY_SYNC_NUM[div] / DELAY_SYNC_DEN[div]; }
const char *fx_sync_name(uint8_t div) { return DELAY_SYNC_NAME[div < 13u ? div : 0u]; }
uint8_t fx_sync_count(int stereo) { return stereo ? SYNC_COUNT_STEREO : SYNC_COUNT_MONO; }
uint8_t fx_sync_div(uint8_t v, int stereo)
{
    return (uint8_t)(((uint32_t)v * fx_sync_count(stereo)) >> 7);   /* the list's index: the same at any tempo */
}
int fx_sync_capped(uint8_t v, int stereo)
{
    return sync_len(fx_sync_div(v, stereo)) > (stereo ? DELAY_LEN_ST : DELAY_SYNC_LEN_MAX);
}
static uint32_t delay_len(const MasterPerformancePreset *p)
{
    if (!DELAY_SYNC(p))
        return fx_delay_free_len_m(p->delay_time, DELAY_STEREO(p));
    uint32_t len = sync_len(fx_sync_div(p->delay_time, DELAY_STEREO(p)));
    const uint32_t cap = DELAY_STEREO(p) ? DELAY_LEN_ST : DELAY_SYNC_LEN_MAX;
    if (len > cap) len = cap;                                      /* (below the tempo floor: the line's length) */
    return len < 64u ? 64u : len;
}
uint32_t fx_delay_free_len(uint8_t v) { return 64u + (uint32_t)v * DELAY_TIME_STEP; }
uint32_t fx_delay_free_len_m(uint8_t v, int stereo) { return 64u + (uint32_t)v * (stereo ? DELAY_TIME_STEP_ST : DELAY_TIME_STEP); }

/* Parabolic sine from a phase accumulator, Q15: 4x(1 - |x|) with x the phase in [-1, 1). */
static int32_t para_sine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16;
    int32_t ax = x < 0 ? -x : x;
    return (x * (32767 - ax)) >> 13;
}

/* the playback filtering (TAPE DEGRADE, tape_degrade) -> one-pole coefficients, Q15. Head loss low-pass: 0 = open (bypassed), 36 = about
 * 5.6 kHz, 127 = about 450 Hz, on a square law so the dark end is not crowded. High-pass: 0 = about 9 Hz (it still
 * keeps DC out of the feedback loop), 36 = about 55 Hz, 127 = about 170 Hz. */
static int32_t head_loss_q15(uint32_t amt)
{
    int32_t inv = 128 - (int32_t)(amt > 127u ? 127u : amt);
    int32_t c = 2048 + ((inv * inv * 30720) >> 14);
    return c > 32767 ? 32767 : c;
}
static int32_t tape_hp_q15(uint32_t amt) { return 40 + 6 * (int32_t)(amt > 127u ? 127u : amt); }

/* Tape record saturation, Q15: y = 1.5u - 0.5u^3 with u = 2x/3, so small signals pass at unity and the
 * output reaches full scale smoothly at 1.5x full-scale input. */
static int32_t tape_sat(int32_t x)
{
    int32_t u = clamp16((x * 21845) >> 15);
    int32_t u2 = (u * u) >> 15;
    return (3 * u - ((u * u2) >> 15)) >> 1;
}

/* ---- The tank (2026-10-15): MOD, DOLMEN and TUMULUS. Dattorro's figure-8 tank (J. Audio Eng. Soc. 45, 1997, "Effect
 * Design Part 1", itself after Griesinger's Lexicon designs) at 1.3x its 29.761 kHz lengths: the send through 4 input
 * allpasses into two halves, each a swept allpass, a delay, a damping low-pass, the decay gain, a second allpass and a
 * second delay, the end of each half feeding the other. Seven taps a side: the tank's own decorrelated stereo. Every
 * line can be read short of its length (size), so one set of lines makes three spaces:
 *   MOD (Lexicon-inspired): full size; the two swept allpasses move +-12 samples on a slow "spin" (0.40 / 0.55 Hz) plus
 *     a random "wander" (a smoothed random walk, a new target every 0.1 s), as the 224 / 480L chorus their tails: the
 *     tail detunes against itself and never rings on one pitch.
 *   DOLMEN (a cave: a small chamber of standing stones): 0.4 size, held still, short and dense, the stone bright.
 *   TUMULUS (a cavern, after the Tumulus Saint-Michel at Carnac: chambers under a great mound): full size, held
 *     still, its decay pushed long and its damping dark.
 *   CELESTIAL (for floating ambient music): MOD's moving tank with an octave-up pitch shifter in its feedback, so
 *     each pass of the tail rises an octave and the sound drifts upward (the "shimmer" reverb).
 * Integer, int16 lines in one 64 KB ring of .pool; REVERB TIME sets the decay, REVERB DAMP the damping, as for ROOM. */
/* the reverbs' Q15 multiply rounds toward zero (magnitude truncation): with a feedback gain under 1 every pass then
 * makes a value strictly smaller, so a tail always dies to silence. sfx_mulq15's floor biased the loops (a DC residue)
 * and rounding to nearest let a quiet tail circulate for ever at its pitch, where g x rounds back to x (measured
 * 2026-10-15: ROOM ~55 LSB, DOLMEN ~2 LSB, never decaying: the classic fixed-point limit cycle). */
#ifndef MR_TAIL_LOSS
#define MR_TAIL_LOSS 27853                       /* 0.85 a pass (Q15): every tank tail reaches silence (2026-11-24) */
#endif
static inline int32_t mr_mulq15(int32_t a, int32_t b)
{
    /* (64-bit: the tank's values are 256 x the Q15, 2026-11-24.) The product is negative exactly when the signs differ
     * and neither is 0; when one is 0 the bias < 2^15 still floors to 0. So the bias comes from the operands: one
     * multiply-add, where t >> 63 needed the product first and the compiler multiplied twice. Same values. */
    int32_t bias = ((a ^ b) >> 31) & 32767;
    return (int32_t)(((int64_t)a * b + bias) >> 15);
}
/* HALF RATE (2026-11-11, after S1Vave's REV_DIV 2): the tank runs at 22.05 kHz. The front takes the send's frames in
 * pairs through a [1 3 3 1] / 8 decimator (a double null at 22 kHz, -9 dB at the half rate's Nyquist) and diffuses
 * them at half rate; the back runs the tank once a pair and brings its output back up by linear interpolation, a
 * frame late. Every length, the sweep, the spin, the damping and the filters keep their times and corners, so the
 * space is the same; above ~8 kHz it is darker. Half the work on both cores and half the ring (32 KB of .pool back).
 * Measured on the FM1 (same layout, FX_LAYOUT_PAD): PERF TEST median +0.1, no row better than -0.8: the tank's work
 * is core 1's, hidden behind core 0's. So it stayed off then. 2026-11-24, with the 24-bit tank: ON (the user's
 * choice), PERF mean floor -4.1 / avg -4.8 against the full-rate 24-bit tank, its rows -8..-12 floor; host: wet level
 * within 0.1 dB, the same decay, every tail exact silence. MR_HALF 0 is the full-rate tank, as before. */
#ifndef MR_HALF
#define MR_HALF 1                                         /* (2026-11-11: off, it saved ~0 then: the tank ran on core 1
                                                           * beside core 0's delay and front, which set the pace.
                                                           * 2026-11-24: ON, the user's choice. The 24-bit tank (a7318df)
                                                           * is no longer hidden: it cost 13-17 pts of floor at full rate;
                                                           * half rate halves it, and its ring fits a power of two) */
#endif
#define MR_DS (MR_HALF ? 2u : 1u)                         /* frames a tank sample */
/* MR_MELLOW 1: CELESTIAL's mellowing (below; on hold 2026-11-11, the user undecided): needs MR_HALF */
#ifndef MR_MELLOW
#define MR_MELLOW 0
#endif
PS_STATIC_ASSERT(!MR_MELLOW || MR_HALF, "the mellowing is the half-rate tank's");
#define MR_L(n) ((uint32_t)((n) * 13u + 5u * MR_DS) / (10u * MR_DS))   /* Dattorro's length at 29.761 kHz, scaled
                                                                         * 1.3x (at the tank's rate) */
#define MR_EXC (12 / (int)MR_DS)                          /* modulation excursion, samples either side (0.27 ms) */
/* Every line lives in one ring of MR_RING samples with one pointer that falls by one a sample (Keith Barr's FV-1
 * layout): line k writes at p + its start and reads d samples back at p + start + d, so an access is one add, one
 * compare and the load, the offsets constants (2026-10-15: per-line pointers, lengths and write heads cost ~1000 cycles
 * a sample on the FM1 in loads alone). A line's region is its longest delay + 1. */
#define MR_B_IN1 MR_L(142)                                /* each line's delay at full size */
#define MR_B_IN2 MR_L(107)
#define MR_B_IN3 MR_L(379)
#define MR_B_IN4 MR_L(277)
#define MR_B_AP1L MR_L(672)
#define MR_B_D1L MR_L(4453)
#define MR_B_AP2L MR_L(1800)
#define MR_B_D2L MR_L(3720)
#define MR_B_AP1R MR_L(908)
#define MR_B_D1R MR_L(4217)
#define MR_B_AP2R MR_L(2656)
#define MR_B_D2R MR_L(3163)
#define MR_SWEEP (2u * MR_EXC + 2u)                       /* the swept allpasses' extra reach */
#define MR_S_IN1 0u                                       /* each line's region start */
#define MR_S_IN2 (MR_S_IN1 + MR_B_IN1 + 1u)
#define MR_S_IN3 (MR_S_IN2 + MR_B_IN2 + 1u)
#define MR_S_IN4 (MR_S_IN3 + MR_B_IN3 + 1u)
#define MR_S_AP1L (MR_S_IN4 + MR_B_IN4 + 1u)
#define MR_S_D1L (MR_S_AP1L + MR_B_AP1L + MR_SWEEP + 1u)
#define MR_S_AP2L (MR_S_D1L + MR_B_D1L + 1u)
#define MR_S_D2L (MR_S_AP2L + MR_B_AP2L + 1u)
#define MR_S_AP1R (MR_S_D2L + MR_B_D2L + 1u)
#define MR_S_D1R (MR_S_AP1R + MR_B_AP1R + MR_SWEEP + 1u)
#define MR_S_AP2R (MR_S_D1R + MR_B_D1R + 1u)
#define MR_S_D2R (MR_S_AP2R + MR_B_AP2R + 1u)
#define MR_S_END (MR_S_D2R + MR_B_D2R + 1u)
/* the ring is exactly the lines' length (2026-11-24: it was 32768 cells, a power of two for the mask, 9 KB the lines
 * never use: the pool needed it). A read or write wraps once by a compare: every offset is below the ring's length. */
/* Half rate (2026-11-24): the lines (~14,650 cells) fit 16384, a power of two, so an access wraps by one mask again (the
 * compare cost ~4 instructions an access, ~32 accesses a sample), in 48 KB. */
#if MR_HALF
#define MR_RING 16384u
#define MR_WRAP(i) ((i) & (MR_RING - 1u))
#else
#define MR_RING MR_S_END
#define MR_WRAP(i) ((i) >= MR_RING ? (i) - MR_RING : (i))
#endif
PS_STATIC_ASSERT(MR_S_END <= MR_RING, "the tank's lines fit its ring");

/* THE TANK'S 24-BIT CELLS (2026-11-24): the tank's values run at 256 x the Q15, so every rounding in its loops is 48 dB
 * lower against the signal. The ring holds 24-bit cells (3 bytes, little-endian, sign in the top bit). The hiss was the
 * 16-bit rounding of the feedback: measured, 2..5 kHz floor -38 -> -49 dB with this scale (scratch, 2026-11-24). */
#define MR_CLAMP24 8388607
/* one add and one compare on the common path (2026-11-24: the two-sided clamp cost two compares and two 6-byte
 * constants at every write; the same values) */
static inline int32_t clamp24(int32_t v)
{
    if (__builtin_expect((uint32_t)v + (uint32_t)MR_CLAMP24 > 2u * (uint32_t)MR_CLAMP24, 0))
        v = v < 0 ? -MR_CLAMP24 : MR_CLAMP24;
    return v;
}
/* A cell is split in two arrays (2026-11-24, the same 3 bytes a cell, the same values): its top 16 bits (signed) and
 * its low 8. A read is a halfword and a byte load and a shift-add; the packed 3-byte cell was a x3, three byte loads,
 * two inserts and a sign fix, and its write three shifted byte stores (the 24-bit tank cost the FM1 ~13 pts of floor,
 * PERF 2026-11-23 22:04). */
static struct FX_LINE {
    int16_t hi[MR_RING];
    uint8_t lo[MR_RING];
} mr_ring SYNTH_POOL;                                     /* core 1, as everything of the tank's but its front */
static inline int32_t r24_rd(uint32_t i) { return (int32_t)mr_ring.hi[i] * 256 + (int32_t)mr_ring.lo[i]; }
static inline void r24_wr(uint32_t i, int32_t v)         /* v within +-MR_CLAMP24 */
{
    mr_ring.hi[i] = (int16_t)(v >> 8);
    mr_ring.lo[i] = (uint8_t)v;
}
static int32_t mr_dco[2];                                 /* the outputs' DC blockers' states (2026-11-24) */
static uint32_t mr_qz[2];                                 /* the output gates' quiet counts (2026-11-24) */
static struct FX_LINE {
    uint32_t p;                                           /* the ring's pointer */
    int32_t lp_l, lp_r;
    int32_t dc_l, dc_r;                                   /* each half's DC blocker, Q8 */
    uint32_t ph_l, ph_r;
    int32_t wander[2], wander_tgt[2];                     /* Q15, the random wander of each side */
    uint32_t rng, wander_n;
    int32_t mod_l, mod_r;                                 /* the swept allpasses' excursion, held for MR_CHUNK samples */
    int32_t duck, duck_g, duck_step;                      /* the guard (below) */
    uint32_t sh_w, sh_c;                                  /* CELESTIAL's shifter (below) */
    int32_t sh_lp, sh_hp, sh_aa1, sh_aa2;
} fx_tank_c1;
#define mr_p (fx_tank_c1.p)
#define mr_lp_l (fx_tank_c1.lp_l)
#define mr_lp_r (fx_tank_c1.lp_r)
#define mr_dc_l (fx_tank_c1.dc_l)
#define mr_dc_r (fx_tank_c1.dc_r)
#define mr_ph_l (fx_tank_c1.ph_l)
#define mr_ph_r (fx_tank_c1.ph_r)
#define mr_wander (fx_tank_c1.wander)
#define mr_wander_tgt (fx_tank_c1.wander_tgt)
#define mr_rng (fx_tank_c1.rng)
#define mr_wander_n (fx_tank_c1.wander_n)
#define mr_mod_l (fx_tank_c1.mod_l)
#define mr_mod_r (fx_tank_c1.mod_r)
#define mr_duck (fx_tank_c1.duck)
#define mr_duck_g (fx_tank_c1.duck_g)
#define mr_duck_step (fx_tank_c1.duck_step)
#define mr_sh_w (fx_tank_c1.sh_w)
#define mr_sh_c (fx_tank_c1.sh_c)
#define mr_sh_lp (fx_tank_c1.sh_lp)
#define mr_sh_hp (fx_tank_c1.sh_hp)
#define mr_sh_aa1 (fx_tank_c1.sh_aa1)
#define mr_sh_aa2 (fx_tank_c1.sh_aa2)
/* The tank's guard: near full scale its feedback (and its input) duck, so a loud, long tail levels off below the
 * lines' int16 ceiling instead of clipping in the loop (a hard clip there buzzes). 32767: no duck. mr_duck is its
 * target, set once a chunk; mr_duck_g the gain in use, which glides to it over the chunk (2026-10-16: it stepped,
 * -0.5 dB at once every 16 samples while ducking, a buzz of its own on a loud chord) */
/* (mr_duck, mr_duck_g, mr_duck_step: in fx_tank_c1, above) */
#define MR_CHUNK 16u                                      /* the modulation and the guard run once in this many samples */
PS_STATIC_ASSERT(sizeof fx_tank_c1 % 64u == 0u && sizeof fx_room_c1 % 64u == 0u, "core 1's lines");
PS_STATIC_ASSERT(MR_CHUNK == 16u, "the guard glides with >> 4");
#define MR_DUCK_AT 16000                                  /* a line this loud (of 32767) starts the duck */
/* CELESTIAL's pitch shifter: a window of MR_SH_LEN samples read by two heads at twice the writing speed (an octave
 * up, whole-sample steps: no interpolation), half a window apart, each faded in and out by a triangle so the other
 * covers its jump back; then a one-pole low-pass, as each octave up would pile up harsh highs */
#define MR_SH_LEN (MR_MELLOW ? 2048u : 2048u / MR_DS)  /* (46 ms; the mellowing's 93: a smoother window) */
#define MR_SHIMMER 19661                                  /* 0.6: how much of it goes back into the tank, Q15 */
#if MR_HALF                                               /* its one-poles at the tank's rate: */
#define MR_SH_AA 22300                                    /* ~4 kHz (an octave up must stay under 11 kHz) */
#define MR_SH_HP 1800                                     /* ~200 Hz */
#define MR_SH_LP 22000                                    /* ~3.9 kHz (MR_MELLOW: it glides, below) */
#else
#define MR_SH_AA 18000
#define MR_SH_HP 900
#define MR_SH_LP 14000
#endif
static int16_t mr_sh[MR_SH_LEN] SYNTH_POOL FX_LINE;     /* (its state: in fx_tank_c1, above) */
/* CELESTIAL's mellowing (2026-11-11, the user: "dreamier, not as annoying when the octave shift rings out ... having it
 * mellow out and blur"): the octaves keep blooming through the whole tail, but the older the tail, the softer and the
 * more smeared each pass up. An envelope of the tank's input (its peak a chunk, Q8: fast up, tau ~1.5 s down) is the
 * tail's youth: fresh (notes going in, from MR_BLOOM_FULL) the shimmer's low-pass sits at ~3.5 kHz and its blur
 * allpass is light; as the envelope falls the low-pass glides down to ~900 Hz and the blur's feedback rises, so the
 * octave stack turns into a warm haze instead of a whistle. The blur: one allpass of MR_BLUR_D samples (9.6 ms) on its
 * own small ring after the shifter. MR_HALF only: MR_HALF 0 keeps the old CELESTIAL to the bit. */
#if MR_MELLOW
#define MR_BLOOM_FULL 512                                 /* the input's level (Q15 into the tank) at a fresh tail */
#define MR_SH_LP_FRESH 20700                              /* ~3.5 kHz */
#define MR_SH_LP_AGED 7400                                /* ~900 Hz */
#define MR_BLUR_FRESH 9830                                /* the blur allpass's coefficient, 0.3 .. */
#define MR_BLUR_AGED 22938                                /* .. 0.7 */
#define MR_BLUR_LEN 256u
#define MR_BLUR_D 211u
static int16_t mr_blur[MR_BLUR_LEN] SYNTH_POOL;
static int32_t mr_bloom_env, mr_bloom_g, mr_bloom_pk;     /* the envelope (Q8), the youth (Q15) and its peak so far */
PS_STATIC_ASSERT(((MR_BLOOM_FULL << 8) >> 2) == 32768, "the youth is the envelope >> 2");
PS_STATIC_ASSERT(MR_BLUR_D < MR_BLUR_LEN && (MR_SH_LEN & (MR_BLUR_LEN - 1u)) == 0u, "the blur's ring");
#endif

/* The tank's input diffusion runs in the reverb's front (fx_reverb_front, core 0 on the FM1), so its four allpasses
 * have a ring of their own (the same FV-1 layout, MR_S_IN1 .. MR_S_AP1L of it), apart from the tank's. */
#define MR_IN_RING 2048u
PS_STATIC_ASSERT(MR_S_AP1L <= MR_IN_RING, "the input diffusers fit their ring");
static int16_t mr_in_ring[MR_IN_RING] SYNTH_POOL;
static uint32_t mr_in_p;

static void tank_init(void)
{
    memset(mr_in_ring, 0, sizeof mr_in_ring);
    mr_in_p = 0;
    memset(&mr_ring, 0, sizeof mr_ring);
    memset(mr_dco, 0, sizeof mr_dco);
    memset(mr_qz, 0, sizeof mr_qz);
    mr_p = 0;
    mr_lp_l = mr_lp_r = 0;
    mr_dc_l = mr_dc_r = 0;
    mr_ph_l = 0;
    mr_ph_r = 0x40000000u;                                /* the two sides' spin a quarter cycle apart */
    mr_wander[0] = mr_wander[1] = mr_wander_tgt[0] = mr_wander_tgt[1] = 0;
    mr_wander_n = 0;
    mr_rng = 0x1F3D5B79u;
    mr_mod_l = mr_mod_r = 0;
    mr_duck = mr_duck_g = 32767;
    mr_duck_step = 0;
    memset(mr_sh, 0, sizeof mr_sh);
#if MR_MELLOW
    memset(mr_blur, 0, sizeof mr_blur);
    mr_bloom_env = mr_bloom_g = mr_bloom_pk = 0;
#endif
    mr_sh_w = mr_sh_c = 0;
    mr_sh_lp = mr_sh_hp = mr_sh_aa1 = mr_sh_aa2 = 0;
}

/* one sample through the octave-up shifter. Head A is MR_SH_LEN - 1 - c samples behind the write (c counts up), so
 * it reads two samples on each sample: an octave up; head B is half a window round. Each head's gain is a triangle
 * of its own c, 0 where it jumps back, and the two always sum to 1. */
/* the shifter's state while a block runs: locals of tank_run (on this core a load or store of a static costs ~8
 * cycles, and the shifter touched six of them a sample) */
typedef struct {
    uint32_t w, c;
    int32_t aa1, aa2, hp, lp;
#if MR_MELLOW
    int32_t lp_c, blur_g;                                 /* the mellowing's low-pass and blur, set once a chunk */
#endif
} MrShift;

static inline __attribute__((always_inline)) int32_t mr_shift(MrShift *st, int32_t x)
{
    const uint32_t mask = MR_SH_LEN - 1u, half = MR_SH_LEN / 2u;
    uint32_t w = st->w, c = st->c;
    /* two poles at about 6 kHz before it: an octave up folds whatever is above 11 kHz back down, and at 14.7 kHz (a
     * third of the rate) onto itself, where the loop held a faint tone forever (measured) */
    st->aa1 += mr_mulq15(x - st->aa1, MR_SH_AA);
#if MR_MELLOW
    mr_sh[w] = (int16_t)clamp16(st->aa1 >> 8);
#else
    st->aa2 += mr_mulq15(st->aa1 - st->aa2, MR_SH_AA);
    mr_sh[w] = (int16_t)clamp16(st->aa2 >> 8);
#endif
    int32_t a = mr_sh[(w - (mask - c)) & mask] * 256, b = mr_sh[(w - (mask - ((c + half) & mask))) & mask] * 256;
    /* head A's triangle; head B's is its complement (B is half a window round, so ga + gb = 32768 always), and
     * a ga + b (32768 - ga) = 32768 b + (a - b) ga: one multiply, the same bits */
    int32_t ga = (int32_t)(c < half ? c : MR_SH_LEN - c) * (32768 / (int32_t)half);
    st->w = (w + 1u) & mask;
    st->c = (c + 1u) & mask;
    int32_t y = b + (int32_t)((((int64_t)a - b) * ga) >> 15);              /* |a - b| <= 65535, ga <= 32768: within 2^31 */
    /* a high-pass first: an octave up leaves DC and the lowest notes where they were, so fed back they would only
     * add to the decay and the loop would run away (measured); only what really rises goes back */
    st->hp += mr_mulq15(y - st->hp, MR_SH_HP);           /* about 200 Hz */
#if MR_MELLOW
    st->lp += mr_mulq15(y - st->hp - st->lp, st->lp_c);  /* ~3.5 kHz fresh .. ~900 Hz aged */
    /* the blur: an allpass on its own ring (v = x + g dl, out = dl - g v), the shifter's write count its index */
    const uint32_t bm = MR_BLUR_LEN - 1u;
    int32_t dl = mr_blur[(w - MR_BLUR_D) & bm], v = clamp16(st->lp + mr_mulq15(dl, st->blur_g));
    mr_blur[w & bm] = (int16_t)v;
    return dl - mr_mulq15(v, st->blur_g);
#else
    st->lp += mr_mulq15(y - st->hp - st->lp, MR_SH_LP);  /* about 4..5 kHz */
    return st->lp;
#endif
}

/* a one-pole high-pass at about 20 Hz in each half of the loop: without it, rounding offsets build up by 1 / (1 -
 * decay) and wander at a few hertz (measured: a subsonic residue that never died at long decays). Its state is Q8:
 * at this small a coefficient a plain Q0 one would not move for differences under ~176 LSB. */
static inline int32_t mr_dc_block(int32_t *s, int32_t x)
{
    /* 64-bit: the tank's values are 256 x Q15 (2026-11-24), so x << 8 overflowed int32 and the DC state went wild */
    int64_t d = ((int64_t)x << 8) - *s;
    *s += (int32_t)(((d >> (MR_DS - 1u)) * 93) >> (16u - MR_DS));
    return x - (*s >> 8);
}

/* ring access, at the sample's pointer p: the line starting at s, d samples back; and its write */
#define MR_RD(s, d) r24_rd(MR_WRAP(p + (s) + (d)))
#define MR_WR(s, v) r24_wr(MR_WRAP(p + (s)), clamp24(v))
#define MR_WR_C(s, v) r24_wr(MR_WRAP(p + (s)), (v))      /* v clamped already (the compiler kept both clamps) */

/* allpass on the line at s with delay d, coefficient g (Q15, either sign): v = x + g dl, out = dl - g v (|H| = 1) */
static inline __attribute__((always_inline)) int32_t mr_ap(uint32_t p, uint32_t s, uint32_t d, int32_t x, int32_t g)
{
    int32_t dl = MR_RD(s, d), v = clamp24(x + mr_mulq15(dl, g));
    MR_WR_C(s, v);
    return dl - mr_mulq15(v, g);
}
/* the same with its delay swept: d + MR_EXC + the excursion e (-32767 .. 32767 = -MR_EXC .. +MR_EXC), read between
 * two samples */
static inline __attribute__((always_inline)) int32_t mr_ap_swept(uint32_t p, uint32_t s, uint32_t d, int32_t e,
                                                                 int32_t x, int32_t g)
{
    int32_t pos_q15 = (int32_t)((d + MR_EXC) << 15) + e * MR_EXC;
    uint32_t di = (uint32_t)pos_q15 >> 15;
    int32_t fr = pos_q15 & 0x7FFF;
    int32_t a = MR_RD(s, di), b = MR_RD(s, di + 1u);
    int32_t dl = a + (int32_t)((((int64_t)b - a) * fr) >> 15);   /* (64-bit: the tank's values are 256 x Q15) */
    int32_t v = clamp24(x + mr_mulq15(dl, g));
    MR_WR_C(s, v);
    return dl - mr_mulq15(v, g);
}

/* The send into the tank, Q15, and the output's matching shift (the output gain MR_OUT_GAIN x 2^(13 - MR_OUT_SH)).
 * 2026-10-16: 1/4 (was 1/2) and 1.2 (was 0.6), the tank 6 dB lower inside: a full-scale send of a hot chord at long
 * decays drove its int16 lines into the guard (ducking 50-60 % of the time) and their clamps, a buzz. Measured on the
 * hottest sends (6-8 driven voices at full send, decay 110): the guard now rests except TUMULUS ~19 % on the worst.
 * 12 dB lower kept even that at rest, but quiet tails lost ~2 dB to the int16 lines' rounding (6 dB: <= 0.7 dB). */
#ifndef MR_IN_GAIN
#define MR_IN_GAIN 16384                             /* (2026-11-24: +6 dB, the tank's headroom: its int16 rounding was the hiss) */
#endif
#ifndef MR_OUT_SH
#define MR_OUT_SH 13                                 /* (x 2 with MR_IN_GAIN: the same output level) */
#endif
#define MR_SZ(b, size) ((((b) * (size)) >> 8) ? (((b) * (size)) >> 8) : 1u)   /* a delay at size (Q8), at least 1 */
/* the input diffusers' allpass, on their ring (v = x + g dl, out = dl - g v) */
static inline __attribute__((always_inline)) int32_t mr_ap_in(uint32_t p, uint32_t s, uint32_t d, int32_t x, int32_t g)
{
    int32_t dl = (int32_t)mr_in_ring[(p + s + d) & (MR_IN_RING - 1u)] * 256, v = clamp24(x + mr_mulq15(dl, g));
    mr_in_ring[(p + s) & (MR_IN_RING - 1u)] = (int16_t)clamp16(v >> 8);   /* (16 bits at 1/256: the diffusers' ring) */
    return dl - mr_mulq15(v, g);
}

/* the tank's input: the send into it, then four allpasses diffusing it (0.75, 0.75, 0.625, 0.625), in place. Half
 * rate: odd 1 on a pair's second frame (rv_odd_f), which gets the pair's tank sample; its first frame gets 0 */
#if MR_HALF
static int32_t mr_in_h[3];                                /* the send's last three frames, the newest first */
#endif
static inline __attribute__((always_inline)) void tank_front(int32_t *x, uint32_t n, const uint32_t size, uint32_t odd)
{
    uint32_t p = mr_in_p;
#if MR_HALF
    int32_t h0 = mr_in_h[0], h1 = mr_in_h[1], h2 = mr_in_h[2];
#else
    (void)odd;
#endif
    for (uint32_t s = 0; s < n; s++) {
#if MR_HALF
        int32_t v = x[s];
        x[s] = 0;
        /* the decimator, [1 3 3 1] / 8, straight into the tank's 256 x Q15 (x 256 / 8 = x 32: no rounding; 2026-11-24,
         * the 24-bit tank's half-rate input) */
        int32_t dec = (v + 3 * (h0 + h1) + h2) * 32;
        h2 = h1;
        h1 = h0;
        h0 = v;
        uint32_t second = odd;
        odd ^= 1u;
        if (!second)                                      /* (the pair's first frame: no tank sample) */
            continue;
        int32_t in = mr_mulq15(dec, MR_IN_GAIN);
#else
        int32_t in = mr_mulq15(x[s] * 256, MR_IN_GAIN);
#endif
        in = mr_ap_in(p, MR_S_IN1, MR_SZ(MR_B_IN1, size), in, 24576);
        in = mr_ap_in(p, MR_S_IN2, MR_SZ(MR_B_IN2, size), in, 24576);
        in = mr_ap_in(p, MR_S_IN3, MR_SZ(MR_B_IN3, size), in, 20480);
        x[s] = mr_ap_in(p, MR_S_IN4, MR_SZ(MR_B_IN4, size), in, 20480);
        p = (p - 1u) & (MR_IN_RING - 1u);
    }
    mr_in_p = p;
#if MR_HALF
    mr_in_h[0] = h0; mr_in_h[1] = h1; mr_in_h[2] = h2;
#endif
}

#define MR_RATE_L 38956u                                  /* spin: 0.40 Hz */
#define MR_RATE_R 53565u                                  /* 0.55 Hz */
#define MR_OUT_GAIN 19661                                 /* 0.6, Dattorro's */

/* The output's silence (2026-11-24): within 8 LSB for 2048 samples (46 ms) it reads as exact 0, so a tail ends in
 * silence, not in the rounding limit cycle (+-1 LSB, peaks past 4) the DC blockers leave. Under -72 dBFS; it lifts the moment the output rises. */
static inline int32_t mr_gate(uint32_t *q, int32_t v)
{
    if (v >= -8 && v <= 8) {                              /* (2048 / MR_DS tank samples: 46 ms at either rate) */
        if (*q < 2048u / MR_DS) (*q)++;
        return *q >= 2048u / MR_DS ? 0 : v;
    }
    *q = 0;
    return v;
}

/* The tank over a block, its size a constant at each call (inlined twice: every offset folds to a constant). x: the
 * reverb send in, the left output out; r: the right output. */
static inline __attribute__((always_inline)) void tank_run(int32_t *x, const int32_t *sd, int32_t *r, uint32_t n,
                                                           int32_t decay,
                                                           int32_t damp, int moving, int celestial, const uint32_t size)
{
    uint32_t p = mr_p;
    int32_t lp_l = mr_lp_l, lp_r = mr_lp_r, mod_l = mr_mod_l, mod_r = mr_mod_r, duck = mr_duck;
    int32_t dg = mr_duck_g, dg_step = mr_duck_step;
    int32_t dec = mr_mulq15(decay, dg);
    int32_t dc_l = mr_dc_l, dc_r = mr_dc_r;
    uint32_t chunk = mr_wander_n;
#if MR_MELLOW
    int32_t bl_env = mr_bloom_env, bl_g = mr_bloom_g, bl_pk = mr_bloom_pk;
    MrShift sh = {mr_sh_w, mr_sh_c, mr_sh_aa1, mr_sh_aa2, mr_sh_hp, mr_sh_lp,
                  MR_SH_LP_AGED + (((MR_SH_LP_FRESH - MR_SH_LP_AGED) * bl_g) >> 15),
                  MR_BLUR_AGED + (((MR_BLUR_FRESH - MR_BLUR_AGED) * bl_g) >> 15)};
#else
    MrShift sh = {mr_sh_w, mr_sh_c, mr_sh_aa1, mr_sh_aa2, mr_sh_hp, mr_sh_lp};
#endif
    if (!moving)
        mod_l = mod_r = 0;                                /* (a still tank: none left from MOD or CELESTIAL) */
    for (uint32_t s = 0; s < n; s++) {
        if (dg_step) {                                    /* the guard gliding */
            dg += dg_step;
            dec = mr_mulq15(decay, dg);
        }
        int32_t in = mr_mulq15(x[s], dg);                 /* the front's diffused send (fx_reverb_front), ducked */
        int32_t si = mr_mulq15(sd[s], dg);                /* its side (rv_sring), + left half, - right */

        int32_t from_r = MR_RD(MR_S_D2R, MR_SZ(MR_B_D2R, size)), from_l = MR_RD(MR_S_D2L, MR_SZ(MR_B_D2L, size));
        if ((chunk++ & (MR_CHUNK - 1u)) == 0u) {
            /* once a chunk: the modulation (its step, ~0.01 sample of delay a chunk, is far below hearing) and the
             * guard, which reads the lines' ends: louder than MR_DUCK_AT ducks fast (-0.5 dB a chunk), else it
             * comes back slowly (tau ~0.1 s); the gain glides to the new target over the chunk */
            int32_t pk = from_l < 0 ? -from_l : from_l, pr = from_r < 0 ? -from_r : from_r;
            if (pr > pk) pk = pr;
            if (pk > MR_DUCK_AT * 256)                     /* (the lines are 256 x Q15, 2026-11-24) */
                duck -= duck >> 4;
            else if (duck < 32767)
                duck += ((32767 - duck) >> (9u - MR_DS)) + 1;   /* (half rate: chunks twice as long, the same tau) */
            if (duck > 32767) duck = 32767;
            dg_step = (duck - dg) >> 4;                   /* MR_CHUNK samples to the target */
            if (dg_step == 0 && dg != duck) {             /* (the last few LSB: 0.004 dB at most) */
                dg = duck;
                dec = mr_mulq15(decay, dg);
            }
#if MR_MELLOW
            if (celestial) {                              /* the mellowing: the last chunk's peak, the youth from it */
                int32_t pk8 = bl_pk << 8;
                if (pk8 > bl_env)
                    bl_env += (pk8 - bl_env) >> 2;        /* (up in ~3 ms) */
                else
                    bl_env -= (bl_env >> 11) + (bl_env > 0);   /* (down: tau 2048 chunks, 1.5 s) */
                bl_pk = 0;
                bl_g = bl_env >= (MR_BLOOM_FULL << 8) ? 32767 : bl_env >> 2;
                sh.lp_c = MR_SH_LP_AGED + (((MR_SH_LP_FRESH - MR_SH_LP_AGED) * bl_g) >> 15);
                sh.blur_g = MR_BLUR_AGED + (((MR_BLUR_FRESH - MR_BLUR_AGED) * bl_g) >> 15);
            }
#endif
            if (moving) {
                mr_ph_l += MR_RATE_L * MR_CHUNK * MR_DS;
                mr_ph_r += MR_RATE_R * MR_CHUNK * MR_DS;
                if (chunk >= 4410u / MR_DS) {             /* wander: a new random target every 0.1 s ... */
                    chunk = 1;                    /* (as it is just after a chunk's count) */
                    for (int k = 0; k < 2; k++) {
                        mr_rng = mr_rng * 1103515245u + 12345u;
                        mr_wander_tgt[k] = (int16_t)(mr_rng >> 16);
                    }
                }
                for (int k = 0; k < 2; k++)               /* ... approached slowly (tau ~ 0.19 s), never stepped */
                    mr_wander[k] += (mr_wander_tgt[k] - mr_wander[k]) >> (10u - MR_DS);
                mod_l = (para_sine(mr_ph_l) + mr_wander[0]) >> 1;   /* spin + wander, each half the excursion */
                mod_r = (para_sine(mr_ph_r) + mr_wander[1]) >> 1;
            }
        }
        int32_t shim = 0;                                 /* CELESTIAL: both ends an octave up, back into both halves */
        if (celestial) {
#if MR_MELLOW
            int32_t ai = in < 0 ? -in : in;               /* (the mellowing's peak) */
            if (ai > bl_pk)
                bl_pk = ai;
#endif
            shim = mr_mulq15(mr_shift(&sh, (from_l + from_r) >> 1), MR_SHIMMER);
        }

        /* left half */
        int32_t a = mr_ap_swept(p, MR_S_AP1L, MR_SZ(MR_B_AP1L, size), mod_l, in + si + shim + mr_mulq15(from_r, dec),
                                -22938);                  /* -0.7 */
        int32_t o = MR_RD(MR_S_D1L, MR_SZ(MR_B_D1L, size));
        MR_WR(MR_S_D1L, a);
        lp_l = o + mr_mulq15(lp_l - o, damp);
        MR_WR(MR_S_D2L, mr_ap(p, MR_S_AP2L, MR_SZ(MR_B_AP2L, size),
                              mr_mulq15(mr_dc_block(&dc_l, lp_l), dec), 16384));   /* 0.5 */

        /* right half */
        int32_t b = mr_ap_swept(p, MR_S_AP1R, MR_SZ(MR_B_AP1R, size), mod_r, in - si + shim + mr_mulq15(from_l, dec),
                                -22938);
        o = MR_RD(MR_S_D1R, MR_SZ(MR_B_D1R, size));
        MR_WR(MR_S_D1R, b);
        lp_r = o + mr_mulq15(lp_r - o, damp);
        MR_WR(MR_S_D2R, mr_ap(p, MR_S_AP2R, MR_SZ(MR_B_AP2R, size),
                              mr_mulq15(mr_dc_block(&dc_r, lp_r), dec), 16384));

        /* Dattorro's output taps (at 29.761 kHz), each side mostly from the other half's lines; read after this
         * sample's writes, so a tap t back is t - 1 from p */
#define MR_TAP(S, B, t) MR_RD(S, MR_SZ(MR_L(t), size) - 1u)
        int32_t yl = MR_TAP(MR_S_D1R, 0, 266) + MR_TAP(MR_S_D1R, 0, 2974) - MR_TAP(MR_S_AP2R, 0, 1913)
                     + MR_TAP(MR_S_D2R, 0, 1996) - MR_TAP(MR_S_D1L, 0, 1990) - MR_TAP(MR_S_AP2L, 0, 187)
                     - MR_TAP(MR_S_D2L, 0, 1066);
        int32_t yr = MR_TAP(MR_S_D1L, 0, 353) + MR_TAP(MR_S_D1L, 0, 3627) - MR_TAP(MR_S_AP2L, 0, 1228)
                     + MR_TAP(MR_S_D2L, 0, 2673) - MR_TAP(MR_S_D1R, 0, 2111) - MR_TAP(MR_S_AP2R, 0, 335)
                     - MR_TAP(MR_S_D2R, 0, 121);
#undef MR_TAP
        /* x 0.6 x 2 (MR_IN_GAIN); |taps| <= 7 x 32768, so / 4 first: int32. Not clamped: the wet sum goes through
         * the master limiter (a clamp here clipped a loud tail: buzz) */
        x[s] = mr_gate(&mr_qz[0], mr_dc_block(&mr_dco[0], (int32_t)((((int64_t)(yl >> 2)) * MR_OUT_GAIN) >> (MR_OUT_SH + 8))));   /* (2026-11-24: no DC out) */
        r[s] = mr_gate(&mr_qz[1], mr_dc_block(&mr_dco[1], (int32_t)((((int64_t)(yr >> 2)) * MR_OUT_GAIN) >> (MR_OUT_SH + 8))));
        p = MR_WRAP(p + MR_RING - 1u);
    }
    mr_p = p;
    mr_lp_l = lp_l;
    mr_lp_r = lp_r;
    mr_mod_l = mod_l;
    mr_mod_r = mod_r;
    mr_duck = duck;
    mr_duck_g = dg;
    mr_duck_step = dg_step;
    mr_dc_l = dc_l;
    mr_dc_r = dc_r;
    mr_wander_n = chunk;
    mr_sh_w = sh.w; mr_sh_c = sh.c; mr_sh_aa1 = sh.aa1; mr_sh_aa2 = sh.aa2; mr_sh_hp = sh.hp; mr_sh_lp = sh.lp;
#if MR_MELLOW
    mr_bloom_env = bl_env;
    mr_bloom_g = bl_g;
    mr_bloom_pk = bl_pk;
#endif
}

/* x: the reverb send in, the left output out; r: the right output. type: REVTYPE_MOD / _CELESTIAL / _DOLMEN /
 * _TUMULUS */
static void tank_reverb_block(const MasterPerformancePreset *p, int32_t *x, const int32_t *sd, int32_t *r, uint32_t n,
                              uint8_t type)
{
    int32_t knob_t = p->reverb_decay > 127u ? 127 : p->reverb_decay, knob_d = p->reverb_damp > 127u ? 127 : p->reverb_damp;
    int32_t decay, damp;
    int celestial = type == REVTYPE_CELESTIAL, moving = type == REVTYPE_MOD || celestial;
    if (type == REVTYPE_TUMULUS) {                        /* the cavern: long and dark */
        decay = 16000 + knob_t * 128;                     /* 0.49 .. 0.99 */
        damp = 6000 + knob_d * 150;                       /* 0.18 .. 0.76 */
    } else if (type == REVTYPE_DOLMEN) {                  /* the cave: small, dense, the stone bright */
        /* 0.45 .. 0.82: a chamber, never a hall. 2026-10-16 (user: "hard to hear"): was 0.18 .. 0.65, RT60 0.36 ..
         * 1.0 s, a tail the dry note covered; now ~0.6 s at TIME 0, ~1 s at 64, ~1.7 s at 110, ~2.2 s at 127, its
         * tail 7-8 dB up (~2 dB over PLATE's), the level going in unchanged */
        decay = 14800 + knob_t * 95;
        damp = knob_d * 120;                              /* 0 .. 0.47 */
    } else if (celestial) {                               /* the shimmer feeds the loop too: a shorter decay */
        decay = 8000 + knob_t * 185;                      /* 0.24 .. 0.96 */
        damp = knob_d * 200;                              /* 0 .. 0.78 */
    } else {                                              /* MOD */
        decay = 8000 + knob_t * 190;                      /* 0.24 .. 0.98 */
        damp = knob_d * 200;                              /* 0 .. 0.78 */
    }
    /* the per-pass loss (2026-11-24): every pass keeps a little less than the knob's gain, so each tail reaches silence */
    decay = mr_mulq15(decay, MR_TAIL_LOSS);
    if (MR_HALF)                                          /* the damping's pole squared: its corner at half rate */
        damp = mr_mulq15(damp, damp);
    if (type == REVTYPE_DOLMEN)
        tank_run(x, sd, r, n, decay, damp, 0, 0, 102u);       /* 0.4 size */
    else if (celestial)                                   /* one copy each: the type's branches fold away */
        tank_run(x, sd, r, n, decay, damp, 1, 1, 256u);
    else if (moving)
        tank_run(x, sd, r, n, decay, damp, 1, 0, 256u);
    else
        tank_run(x, sd, r, n, decay, damp, 0, 0, 256u);
}

/* ---- SPRING (2026-10-16): two springs, after the dispersive models of spring tanks (Valimaki, Parker and Abel,
 * "Parametric spring reverberation effect", JAES 2010): a wave along a helical spring travels slower the lower its
 * pitch, so every echo of the spring comes back as a falling chirp (the "drip", the laser "pew"), smeared further on
 * each trip. Each spring: a loop of a delay (one trip), a low-pass (REVERB DAMP), the decay (REVERB TIME) and
 * SP_STAGES stretched first-order allpasses, (a + z^-K) / (1 + a z^-K) with a < 0, which delay the lows more than the
 * highs: the dispersion. The springs' trips (34.7 and 43.2 ms) differ, so their flutter echoes do not line up; each
 * feeds one side, a little of the other mixed in. TIME 0 .. 100: RT60 ~0.3 .. ~4.5 s; past 100 the dub feedback
 * (below); level as before.
 * 2026-11-02, more sproing (the user: "it should sproing a bit more"): a real tank's chirp spreads over tens of ms a
 * trip (the paper's ~100 stages), this one's was ~6.6 ms (150 Hz against 4.5 kHz, the first echo). A spring carries
 * nothing much above ~4.5 kHz, so its loops now run at half rate (22.05 kHz, the MFX brigades' trick): each stage's
 * sample twice as long, twice the stages for the same work, 24 at K = 2 (no mirrored chirp under the half rate's
 * Nyquist): ~15 ms a trip, 30 by the second echo. The front, as a tank's driver: the send high-passed (~120 Hz: a
 * tank has little bass, but the lowest notes chirp the longest, the "dwoing"), the driver's soft headroom (a hard hit
 * crashes the springs a little), the transducer's top (~4.5 kHz, the half rate's anti-alias filter too).
 * The loops run block-wise: the delay's reads for the block, the filters, the allpass stages two at a time with their
 * histories in registers, then the delay's writes. Lines int16 in one ring (FV-1 layout, as the tank). Half rate: the
 * back takes the send's frames in pairs (across blocks: a pair may straddle two), averaged; its echoes come back up by
 * linear interpolation, a frame late. */
#define SP_RING 2048u
#define SP_MASK (SP_RING - 1u)
#define SP_D1 765u                                        /* one trip, half-rate samples (34.7 ms) */
#define SP_D2 953u                                        /* (43.2 ms) */
#define SP_S1 0u                                          /* each line's region start */
#define SP_S2 (SP_D1 + 1u)
#define SP_HB (FX_BLOCK_MAX / 2u + 1u)                    /* a block's half-rate samples, at most */
PS_STATIC_ASSERT(SP_S2 + SP_D2 + 1u <= SP_RING && SP_D1 >= SP_HB && SP_D2 >= SP_HB,
                 "the springs fit their ring, and a trip is at least a block");
#ifndef SP_STAGES
#define SP_STAGES 24                                      /* allpass stages a spring (even: two a pass) */
#endif
#define SP_K 2                                            /* the stretch: z^-2 at half rate */
#ifndef SP_A
#define SP_A (-0.78f)                                     /* per stage 16 half-rate samples at DC (1.5 ms), 0.25 at 5.5 kHz */
#endif
#define SP_IN_GAIN 0.2f                                   /* the send into the loops (headroom in the lines) */
#ifndef SP_OUT_GAIN
#define SP_OUT_GAIN 0.49f                                 /* x 4 / 4: each side 3/4 its spring + 1/4 the other's
                                                           * (2026-11-02: -3 dB, the lower high-pass's bass: pink
                                                           * noise's wet level as before) */
#endif
#define SP_HP_C 0.0170f                                   /* the front's high-pass, ~120 Hz */
#define SP_TOP_C 0.473f                                   /* the transducer's top, ~4.5 kHz (a 1-pole at the full rate) */
#define SP_DRV (1.f / 24000.f)                            /* the driver's headroom: flat at 24000 (the send's units) */
#define SP_TINY 1e-4f                                     /* a state this small (LSB) is flushed to 0 at a block's end */
PS_STATIC_ASSERT((SP_STAGES & 1) == 0, "the stages run in pairs");
/* The dub feedback (2026-11-02, the user's: TIME past 100, as DELAY FEEDBACK past 100): the tank's return patched back
 * into its send on the desk, through the channel's EQ (the boom and the fizz off: 250 Hz .. 2.5 kHz, 1-poles at half
 * rate) and into the driver again, its headroom holding the howl (the return's alone: the send itself unchanged). One trip late, as on the desk. Over TIME 100 .. 127
 * the feedback rises (self-oscillating from ~SP_FB_ON) while the springs' own decay backs off (their ring's headroom:
 * the howl is the desk's loop, not the tank's) */
#ifndef SP_FB_G
#define SP_FB_G 0.5f                                      /* the return's gain into the send at TIME 127 (x k^2: on
                                                           * the edge at ~113, a held tail; ~2 s to howl at 116, ~1 at
                                                           * 120, at once at 127) */
#endif
#ifndef SP_FBH
#define SP_FBH 700.f                                      /* the driver's headroom in the loops' units: the howl's
                                                           * cap, -14 .. -20 dBFS out (a loud tail, not the mix) */
#endif
#define SP_FB_HP 0.0688f                                  /* the desk EQ: 250 Hz high-pass, 2.5 kHz low-pass (half rate) */
#define SP_FB_LP 0.51f

/* The loop is float (2026-10-16): in int, each stage's rounding (up to 1 LSB, 12 stages) went round the loop, and
 * the tail never died (measured: ~200 LSB for ever at TIME 127, a faint tone). In float the stages' error is ~2^-16
 * LSB; the line writes truncate toward zero to int16, a dead zone, so a tail ends in exact silence; and states under
 * SP_TINY are flushed at each block's end, so the FPU never meets a denormal. (The FM1's single-precision multiply
 * costs ~7 cycles, the integer one ~10.) */
static int16_t sp_ring[SP_RING] SYNTH_POOL FX_LINE;     /* core 1 (sp_p, sp_sp: fx_spring_c1, below) */
static float sp_hp, sp_top;                               /* the front's high-pass and top (core 0); float: an integer
                                                           * one stuck a few LSB off zero and fed the loops a DC */
typedef struct {
    float lp;                                             /* the loop's low-pass (no DC blocker: the send comes in
                                                           * high-passed and nothing in the loop adds a DC) */
    float h[SP_STAGES / 2][3][SP_K];                      /* each pair's last K inputs, middles and outputs */
    float prev;                                           /* the last echo out (the interpolation's) */
} SpSpring;
static struct FX_LINE {                               /* core 1: the springs' state */
    SpSpring sp[2];
    uint32_t p;
    float pend, pend_s;                                   /* a pair's first frame (and its side), when a block ended
                                                           * between */
    float fb_hp, fb_lp;                                   /* the dub feedback's desk EQ */
    uint32_t odd;                                         /* 1: pend holds it */
} fx_spring_c1;
#define sp_sp (fx_spring_c1.sp)
#define sp_p (fx_spring_c1.p)

static void spring_init(void)
{
    memset(sp_ring, 0, sizeof sp_ring);
    memset(&fx_spring_c1, 0, sizeof fx_spring_c1);
    sp_hp = sp_top = 0.f;
}

static inline float sp_flush(float v) { return v < SP_TINY && v > -SP_TINY ? 0.f : v; }

/* two stretched allpasses over a block, in place: y[n] = x[n - K] + a (x[n] - y[n - K]) twice. h: the histories of
 * the pair's input, middle and output, newest first. Each pair keeps its own input history: a pair's output history
 * holds the same values, but the pairs run one after the other over the block, so by the time the next pair starts
 * this one has already moved its history to the block's end */
static inline __attribute__((always_inline)) void sp_pair(float *b, uint32_t n, float (*h)[SP_K])
{
    float x1 = h[0][0], x2 = h[0][1];
    float m1 = h[1][0], m2 = h[1][1];
    float y1 = h[2][0], y2 = h[2][1];
    for (uint32_t s = 0; s < n; s++) {
        float x = b[s];
        float m = x2 + SP_A * (x - m2);
        float y = m2 + SP_A * (m - y2);
        x2 = x1; x1 = x;
        m2 = m1; m1 = m;
        y2 = y1; y1 = y;
        b[s] = y;
    }
    h[0][0] = sp_flush(x1); h[0][1] = sp_flush(x2);
    h[1][0] = sp_flush(m1); h[1][1] = sp_flush(m2);
    h[2][0] = sp_flush(y1); h[2][1] = sp_flush(y2);
}
PS_STATIC_ASSERT(SP_K == 2, "sp_pair keeps two samples of history");

/* one spring over a block of n half-rate samples: in, the send; out, its echoes (the trip's end, as read) */
static inline __attribute__((always_inline)) void spring_run(SpSpring *sp, const float *in, float *out, uint32_t n,
                                                             const uint32_t start, const uint32_t d, float decay,
                                                             float lp_c)
{
    float b[SP_HB];
    uint32_t p = sp_p;
    float lp = sp->lp;
    for (uint32_t s = 0; s < n; s++) {                    /* the trip's end, filtered, decayed, plus the send */
        float o = (float)sp_ring[(p - s + start + d) & SP_MASK];
        out[s] = o;
        lp += lp_c * (o - lp);
        b[s] = in[s] + decay * lp;
    }
    sp->lp = sp_flush(lp);
    for (uint32_t k = 0; k < SP_STAGES; k += 2)           /* the dispersion */
        sp_pair(b, n, sp->h[k / 2u]);
    for (uint32_t s = 0; s < n; s++)                      /* into the trip (read above at least a block later); the cast
                                                           * truncates toward zero */
        sp_ring[(p - s + start) & SP_MASK] = (int16_t)clamp16((int32_t)b[s]);
}

/* the front's part, the tank's driver: the send high-passed, through the driver's headroom and the transducer's top,
 * scaled into the loops, in place */
static void spring_front(int32_t *x, uint32_t n)
{
    float hp = sp_hp, top = sp_top;
    for (uint32_t s = 0; s < n; s++) {
        float v = (float)x[s];
        hp += SP_HP_C * (v - hp);
        float u = (v - hp) * SP_DRV;
        u = u > 1.f ? 1.f : u < -1.f ? -1.f : u * (1.5f - 0.5f * u * u);
        top += SP_TOP_C * (u * (1.f / SP_DRV) - top);
        x[s] = (int32_t)(top * SP_IN_GAIN);
    }
    sp_hp = sp_flush(hp);
    sp_top = sp_flush(top);
}

/* the back: x the front's send in, the left output out; r the right */
static void spring_block(const MasterPerformancePreset *p, int32_t *x, const int32_t *sd, int32_t *r, uint32_t n)
{
    int32_t knob_t = p->reverb_decay > 127u ? 127 : p->reverb_decay, knob_d = p->reverb_damp > 127u ? 127 : p->reverb_damp;
    float decay, fb = 0.f;
    if (knob_t <= 100)
        decay = 0.52f + (float)knob_t * (0.44f / 100.f);    /* a trip's gain 0.52 .. 0.96 */
    else {                                                  /* the dub feedback (above) */
        float k = (float)(knob_t - 100) * (1.f / 27.f);
        decay = 0.96f - 0.26f * k * k * k;                 /* (late: the tail only grows as TIME turns up) */
        fb = SP_FB_G * k * k;                               /* (rising on a curve: the top builds fastest) */
    }
    float lp_c = 0.76f - (float)knob_d * (0.41f / 127.f);    /* the loop's low-pass ~5 kHz .. ~1.5 kHz (half rate) */
    static float us[SP_HB] FX_LINE;                       /* (core 1's, off its stack) */
    float u[SP_HB], o1[SP_HB], o2[SP_HB];
    uint32_t m = 0, odd = fx_spring_c1.odd, side = 0u;
    float pend = fx_spring_c1.pend, pend_s = fx_spring_c1.pend_s;
    for (uint32_t s = 0; s < n; s++) {                    /* the frames in pairs, averaged (the side's too) */
        side |= (uint32_t)sd[s];
        if (odd) {
            us[m] = 0.5f * (pend_s + (float)sd[s]);
            u[m++] = 0.5f * (pend + (float)x[s]);
        } else {
            pend = (float)x[s];
            pend_s = (float)sd[s];
        }
        odd ^= 1u;
    }
    fx_spring_c1.pend_s = pend_s;
    if (fb > 0.f) {                                       /* the return (the trips' ends, read ahead), the desk EQ, back
                                                           * into the driver with the send */
        float hp = fx_spring_c1.fb_hp, lp = fx_spring_c1.fb_lp;
        for (uint32_t s = 0; s < m; s++) {
            float v = 0.5f * ((float)sp_ring[(sp_p - s + SP_S1 + SP_D1) & SP_MASK]
                              + (float)sp_ring[(sp_p - s + SP_S2 + SP_D2) & SP_MASK]);
            hp += SP_FB_HP * (v - hp);
            lp += SP_FB_LP * (v - hp - lp);
            float z = fb * lp * (1.f / SP_FBH);             /* (the return alone: the send as below TIME 100) */
            z = z > 1.f ? 1.f : z < -1.f ? -1.f : z * (1.5f - 0.5f * z * z);
            u[s] += z * SP_FBH;
        }
        fx_spring_c1.fb_hp = sp_flush(hp);
        fx_spring_c1.fb_lp = sp_flush(lp);
    }
    const float *u2 = u;
    if (side || pend_s != 0.f) {                          /* DLY > REV's side: the first spring + it, the second - it */
        for (uint32_t s = 0; s < m; s++) {
            float v = us[s] * SP_IN_GAIN;
            us[s] = u[s] - v;
            u[s] += v;
        }
        u2 = us;
    }
    spring_run(&sp_sp[0], u, o1, m, SP_S1, SP_D1, decay, lp_c);
    spring_run(&sp_sp[1], u2, o2, m, SP_S2, SP_D2, decay, lp_c);
    sp_p = (sp_p - m) & SP_MASK;
    /* back up: a pair's first frame the last echo, its second halfway to the new one (a frame late) */
    float a = sp_sp[0].prev, b = sp_sp[1].prev;
    odd = fx_spring_c1.odd;
    m = 0;
    for (uint32_t s = 0; s < n; s++) {                    /* each side its spring, a quarter of the other */
        float e1 = a, e2 = b;
        if (odd) {
            e1 = 0.5f * (a + o1[m]);
            e2 = 0.5f * (b + o2[m]);
            a = o1[m];
            b = o2[m++];
        }
        odd ^= 1u;
        x[s] = (int32_t)((e1 * 3.f + e2) * SP_OUT_GAIN);
        r[s] = (int32_t)((e2 * 3.f + e1) * SP_OUT_GAIN);
    }
    sp_sp[0].prev = a;
    sp_sp[1].prev = b;
    fx_spring_c1.odd = odd;
    fx_spring_c1.pend = pend;
}

/* ---- blocks (2026-10-15): the same integer arithmetic as the per-sample code before, in the same order, with the
 * knobs read and the state loaded once per block instead of every sample (the FM1 pays ~8 cycles a load). Integer on
 * purpose: these are 32-bit multiplies among many adds, which the FM1 does faster in int than in float (float adds
 * cost ~6 cycles), and the buffers stay int16. p holds still over a block (the UI writes it outside the render). */

/* TAPE DEGRADE (2026-10-15): one macro over the tape's wear, from new (0) to worn out (127): wow, flutter and the
 * playback filtering together. At 36 (the default) it gives the fixed character these had before they were controls
 * (wow 32, flutter 10, filter 36); flutter rises on a square law, so a worn tape warbles fast as well as slow. */
static void tape_degrade(uint32_t d, int32_t *wow, int32_t *flutter, uint32_t *filter)
{
    if (d > 127u) d = 127u;
    *wow = (int32_t)(d * 32u / 36u);                      /* 0 .. 112 */
    *flutter = (int32_t)(d * d * 10u / 1296u);            /* 0 .. 124 */
    /* 2026-11-19 (user: past the default it should not just go dark): the playback filtering as before up to 36
     * (~5.6 kHz), then only gently further, to ~3 kHz at 127 (was ~450 Hz); the wear above that is the tape breaking
     * up (TAPE_BREAK) */
    *filter = d <= 36u ? d : 36u + (d - 36u) * 21u / 91u;
}

/* TAPE_BREAK (2026-11-19): a worn tape breaks up, past DEGRADE 64 and more as it rises: oxide dropouts and head snags,
 * at random. A dropout is written onto the tape (the record level dips to 15..70 % for 10..60 ms, eased in and out
 * over ~2 ms), so every repeat carries the holes of the passes before it and the echoes crumble as they circulate; a
 * snag jolts the heads by up to +-24 cells, settling over ~50 ms (a short lurch in pitch). At 127 about 1.5 dropouts
 * and 0.7 snags a second; below 65 none, and nothing else changes (its own random stream). */
#ifndef TAPE_BREAK
#define TAPE_BREAK 1
#endif
static inline uint32_t brk_rand(uint32_t *z) { *z = *z * 1664525u + 1013904223u; return *z; }

/* TAPE_WEAR (2026-11-20, user: LOOP TONE should grunge up the delay, and its length and feedback move so a held loop
 * evolves): away from LOOP TONE 64 the wear of the cross-feeds (xf_tone) enters the tape's own feedback, more the
 * further the knob turns (64: none, to the bit):
 *   - the grunge: the repeats driven into a soft clip, lopsided on the darker (tape) side (even harmonics, its DC taken
 *     out), even on the thinner (desk) side, the make-up 1 / drive (a quiet echo passes as it was; a hot loop crunches
 *     and holds lower);
 *   - the drift: the delay's length walks slowly at random (a new target every ~1.5 s, two smoothing stages of ~1 s, so
 *     the pitch glides without kinks), up to +-TW_DRIFT of the delay at full wear (the desk a third of it): each pass
 *     smears a little further from the last;
 *   - the breath: the feedback dips and returns on a slow spin (~0.07 Hz), up to TW_BREATH at full wear (never above
 *     the knob: a breath cannot push the loop into a howl).
 * The half-rate tape only (TAPE_HALF). */
#ifndef TAPE_WEAR
#define TAPE_WEAR 1
#endif
#ifndef TW_DRIFT
#define TW_DRIFT 0.004f                                    /* the drift's reach, a share of the delay, at full wear (0.015
                                                           * detuned the loop's resonances: a held drone died) */
#endif
#ifndef TW_BREATH
#define TW_BREATH 0.06f                                    /* the breath's dip, a share of the feedback (~-0.5 dB) */
#endif
#ifndef TW_PUSH
#define TW_PUSH 0.08f                                      /* the feedback's lift at full wear, past the knob */
#endif
#ifndef TW_DRIVE
#define TW_DRIVE 3.f                                       /* the tape side's drive at full wear (1 + this) */
#endif
#ifndef TW_DRIVE_DESK
#define TW_DRIVE_DESK 3.5f                                 /* the desk side's */
#endif
#define TW_BREATH_INC 6800u                                /* its spin a sample (0.07 Hz) */
#define TW_DRIFT_BLOCKS 512u                               /* blocks between the drift's targets (~1.5 s at 128) */

/* The slowed tape's anti-aliasing (2026-11-03, user: PING PONG "sounds very digital ... a bitcrushed sound ... in the
 * high end when tone is neutral ... definitely aliasing"). PING PONG runs the tape as for a delay twice as long, so
 * past ~185 ms it slows (at 0.5 s to ~0.37 speed: cells at ~16 kHz; at 1 s ~0.18, ~8 kHz), and the single one-pole at
 * 1.5 x speed before the record head let the top through as aliases (measured, a 5 kHz sine: aliases -8 .. -14 dB
 * under the input; the mono echo past ~370 ms the same). Now, while the tape is slowed (TapeAA):
 *   - before the record head a 6th-order Butterworth low-pass at TAPE_AA_FC x the cell rate;
 *   - each cell's value is read at the head's crossing, by 4-point (Catmull-Rom) interpolation of the filter's
 *     samples (the cells no longer land on the output's sample grid: that jitter was grit of its own, spurs at
 *     44.1 kHz - the cell rate - the note);
 *   - each play head reads 4-point too, then a 2nd-order Butterworth at TAPE_RC_FC x the cell rate (the
 *     reconstruction: the linear interpolation passed the cell rate's images, folded into the top octave).
 * The one-pole stays in front of them (the slowed tape's tone, each repeat as dark as before); a slowed tape is darker
 * still at its top, as a real one is. Float (as DELAY HP), the coefficients once a block from the speed;
 * the outputs truncate toward zero into the integer path, so a tail still ends in silence. At full speed none of it
 * runs: the echo as before, bit for bit. */
#ifndef TAPE_AA_N
#define TAPE_AA_N 3
#endif
#ifndef TAPE_RC_N
#define TAPE_RC_N 1
#endif
#ifndef TAPE_AA_FC
#define TAPE_AA_FC 0.40f
#endif
#ifndef TAPE_RC_FC
#define TAPE_RC_FC 0.42f
#endif
typedef struct { float b0, a1, a2; } TapeBq;              /* a low-pass biquad's coefficients (b1 = 2 b0, b2 = b0) */
typedef struct { TapeBq aa[3], rc[2]; } TapeAA;
/* fc_n: the cutoff as a share of the sample rate (< 0.5); q the Butterworth pole's */
#if !TAPE_HALF
static SYNTH_COLD TapeBq tape_bq(float fc_n, float q)
{
    float x = 3.14159265f * fc_n;
    float k = x * (15.f - x * x) / (15.f - 6.f * x * x);  /* tan(x), Pade (x <= ~1.3 here: within 0.5 %) */
    float k2 = k * k, nm = 1.f / (1.f + k / q + k2);
    TapeBq c = {k2 * nm, 2.f * (k2 - 1.f) * nm, (1.f - k / q + k2) * nm};
    return c;
}
static SYNTH_COLD TapeAA tape_aa(int32_t speed_q24)   /* (once a block) */
{
    float r = (float)speed_q24 * (1.f / 16777216.f);      /* the cell rate, a share of the sample rate */
    TapeAA a = {{tape_bq(TAPE_AA_FC * r, 0.5176f), tape_bq(TAPE_AA_FC * r, 0.7071f), tape_bq(TAPE_AA_FC * r, 1.9319f)},
                 {tape_bq(TAPE_RC_FC * r, 0.5412f), tape_bq(TAPE_RC_FC * r, 1.3066f)}};
    return a;
}
/* Catmull-Rom between y1 and y2 (t 0 .. 1), y0 and y3 their outer neighbours: the slowed heads' interpolation (the
 * linear one's error, beating with the cell rate, was -30 dB spurs on a 5 kHz note) */
static inline float tape_cr(float y0, float y1, float y2, float y3, float t)
{
    float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
/* (2026-11-14) the tape echo's biquad, 4-point read and crossing, inlined into its loop with the filter states in locals for
 * the whole block: the same sums in the same order (bit for bit: fx_hash), without a call and four memory trips a
 * biquad. The slowed tape (PING PONG at a dotted eighth: every factory performance) cost ~64 ticks a sample more than
 * the full-speed one on the FM1 (PROF, f.DLY 86 vs 22), on core 0's critical path. */
#if TAPE_AA_N != 3 || TAPE_RC_N != 1
#error "tape_delay_block's inlined filters are written for TAPE_AA_N 3, TAPE_RC_N 1"
#endif
static inline __attribute__((always_inline)) float tbq(const TapeBq *c, float *z0, float *z1, float x)
{
    float y = c->b0 * x + *z0;
    *z0 = 2.f * c->b0 * x - c->a1 * y + *z1;
    *z1 = c->b0 * x - c->a2 * y;
    return y;
}
static inline __attribute__((always_inline)) float tape_cr_at(uint32_t r)
{
    uint32_t i = r >> 16;
    return tape_cr((float)TAPE_RD(i - 1u), (float)TAPE_RD(i), (float)TAPE_RD(i + 1u), (float)TAPE_RD(i + 2u),
                   (float)(r & 0xFFFFu) * (1.f / 65536.f));
}
static inline __attribute__((always_inline)) int32_t tape_cross_i(int32_t h0, int32_t h1, int32_t h2, int32_t h3,
                                                                  uint32_t frac, float inv_sp)
{
    float t = (float)frac * inv_sp * (1.f / 65536.f);
    return (int32_t)tape_cr((float)h0, (float)h1, (float)h2, (float)h3, 1.f - (t < 1.f ? t : 1.f));
}
static void tape_bq_flush(float *z, int n)                 /* (no denormals; silence stays silence) */
{
    for (int i = 0; i < n; i++)
        if (z[i] < 1e-3f && z[i] > -1e-3f) z[i] = 0.f;
}

#endif

/* the heads' targets for p's delay: the gap and the speed. Up to gap_max cells (the tape's whole loop, or the BBD's
 * stages) at full speed; longer delays slow down. Only a change of DELAY TIME / SYNC / PING PONG / TYPE pays for the
 * division. */
static void delay_targets(const MasterPerformancePreset *p, int pp)
{
    uint32_t len = ((delay_len(p) * tlen_q8) >> 8) << pp, gap_max = tgapmax;
    if (DELAY_SYNC(p) && len - tp.last_len + (tp.last_len >> 8) <= (tp.last_len >> 7))
        len = tp.last_len;                                 /* (a clock's wobble, within ~0.4 %: as it was) */
    if (len != tp.last_len) {
        tp.last_len = len;
        if (len <= gap_max) {
            tp.gap_tgt_q16 = (int32_t)(len << 16);
            tp.speed_tgt_q24 = 1 << 24;
        } else {
            tp.gap_tgt_q16 = (int32_t)(gap_max << 16);
            tp.speed_tgt_q24 = (int32_t)(((uint64_t)gap_max << 24) / len);
        }
    }
}

/* FEEDBACK (2026-10-16: both delays self-oscillate). 0 .. 100 as before, knob x 250 (0 .. 0.76, Q15); from 100 the
 * loop gain climbs past unity (at ~114) to 1.22 at 127 (1.62 at DEGRADE 127), so the echoes stop dying and build into a howl that the
 * record saturation (tape_sat) holds at a steady level, as a cranked Space Echo or a BBD pedal does. Musical rather
 * than a white roar: past 100 a one-pole low-pass in the feedback closes from open to ~2 kHz at 127, so the runaway
 * sits in the low mids and turns darker as it is pushed; and a faint hiss seed (the tape's ~-78 dBFS, the BBD's more)
 * lets it take off by itself. Measured (0.4 s, a 440 Hz note): 100 dies as before; 110 sustains; ~114 rings on;
 * 118 holds (tape -9, BBD -22 dBFS); 127 settles at -4 / -6 dBFS, on the note that went in. Moving DELAY TIME while it howls bends its pitch (the heads slew). The gain is
 * split in two multiplies (lo <= 32767, hi the rest) so a Q15 multiply never passes 2^31; at 100 and below hi is 0:
 * the same sums as before. */
/* DELAY HP (2026-10-26, user: "scooped in the low end during high feedback, like a dub delay ... a reasonably tame
 * high pass"; then "the range is too narrow"): a fixed one-pole high-pass in the feedback loop only (the first echo
 * keeps its body), off at 0, then exponential from ~30 Hz at 1 to ~1 kHz at 127 (~165 Hz at 70). Every pass goes
 * through it again, so each repeat is thinner than the last and the more FEEDBACK, the more the tail is scooped.
 * Float (an int one-pole's state sticks at a low cutoff and feeds DC round the loop), flushed once a block, its
 * output truncated toward zero, so a tail still ends in silence; off, the state is zeroed (bit-exact with before, a
 * clean start when turned on). It takes gain out of the loop, so the runaway's top rises with it (scoop_c x
 * DELAY_HP_TOP): the howl's onset stays about where it was. */
#define DELAY_HP_C0 0.00427f              /* 2 pi 30 / 44100: the one-pole's coefficient at 1 (c ~ w below ~1 kHz) */
#define DELAY_HP_OCT (5.06f / 126.f)      /* octaves an amount step: 1 .. 127 = 30 Hz .. 1 kHz */
/* the runaway top's rise per unit of coefficient, tuned so the howl's onset stays (a 440 Hz note, 40 s): the tape
 * rings at FEEDBACK 112 and howls by 116 at every DELAY HP; the BBD, whose dark filters leave little above a high
 * cutoff, howls from 116 up to DELAY HP 100 and only at 127 at DELAY HP 127 (the top's 3.0 cap) */
#define DELAY_HP_TOP_TAPE 200000.f
/* LOOP TONE darker (2026-11-02, fx.h): the runaway's low-pass, in the loop at any FEEDBACK, each repeat through it
 * again: from ~7.6 kHz just under the middle to ~400 Hz at 0 (the lower of it and the runaway's own past 100). At
 * the middle and below FEEDBACK 101 the loop is as before, bit for bit */
/* LOOP TONE (fx.h): how dark, 0 .. 1 below the middle; DELAY HP's amount (0 off, 1..127) above it */
static inline float tone_dark(uint8_t v) { return v < 64u ? (float)(64u - v) * (1.f / 64.f) : 0.f; }
static inline uint32_t tone_hp(uint8_t v) { return v > 64u ? ((uint32_t)(v > 127u ? 127u : v) - 64u) * 127u / 63u : 0u; }
#define TONE_DARK_HZ 8000.f
#define TONE_DARK_OCT 4.3f
typedef struct { int32_t lo, hi, hi2, run_c; float scoop_c; int runaway, lp_on; } DelayFb;
static DelayFb delay_fb(uint32_t knob, uint32_t degrade, uint32_t hp, uint8_t tone)
{
    DelayFb f;
    /* the top: 1.22 at DEGRADE 0 .. 1.62 at 127 (a worn tape's or BBD's filters lose more each pass: without this,
     * DEGRADE 127 never howled) */
    f.scoop_c = hp ? DELAY_HP_C0 * amp_exp2f((float)(int32_t)(hp - 1u) * DELAY_HP_OCT) : 0.f;
    int32_t top = 40000 + (int32_t)(degrade > 127u ? 127u : degrade) * 104 +
                  (int32_t)(f.scoop_c * DELAY_HP_TOP_TAPE);
    if (top > 98301) top = 98301;                          /* (lo, hi, hi2 <= 32767: each Q15 multiply < 2^31) */
    int32_t k = knob > 127u ? 127 : (int32_t)knob, g = k <= 100 ? k * 250 : 25000 + (k - 100) * (top - 25000) / 27;
    f.lo = g > 32767 ? 32767 : g;
    f.hi = g - f.lo > 32767 ? 32767 : g - f.lo;
    f.hi2 = g - f.lo - f.hi;                               /* (only DELAY HP's top reaches it) */
    f.runaway = k > 100;
    f.run_c = 32767 - (k > 100 ? (k - 100) * 900 : 0);     /* 127: 8467, ~2 kHz */
    f.lp_on = f.runaway;
    if (tone < 64u) {                                      /* LOOP TONE darker */
        float fc = TONE_DARK_HZ * amp_exp2f(-TONE_DARK_OCT * tone_dark(tone));
        int32_t c = (int32_t)(32767.f * (1.f - amp_exp2f(-fc * (2.f * 3.14159265f * 1.442695f / 44100.f))));
        f.run_c = f.runaway && f.run_c < c ? f.run_c : c;
        f.lp_on = 1;
    }
    return f;
}

#if !TAPE_HALF
/* The tape echo over a block. PING PONG: a second play head halfway along the gap, the first echo from it on the
 * left, the next from the far head on the right, which also feeds the record head (so each side's echoes are a whole
 * delay apart and the pair repeats every 2 delays: the tape runs as for a delay twice as long). Each head has its own
 * head-loss and high-pass filters. Off: one head, the same echo both sides. */
static void tape_delay_block(const MasterPerformancePreset *p, const int32_t *in, int32_t *out_l, int32_t *out_r,
                             uint32_t n)
{
    const int pp = tpp;
    delay_targets(p, pp);
    int32_t wow, flutter;
    uint32_t filter;
    tape_degrade(p->delay_degrade, &wow, &flutter, &filter);
    const int32_t gap_tgt = tp.gap_tgt_q16, speed_tgt = tp.speed_tgt_q24;
    const int32_t wow_q8 = wow * WOW_STEP_Q8, flutter_q8 = flutter * FLUTTER_STEP_Q8;
    const int32_t loss_c = head_loss_q15(filter), hp_c = tape_hp_q15(filter);
    const DelayFb fb = delay_fb(p->delay_feedback, p->delay_degrade, DELAY_HP(p), fx_live[FX_L_TONE]);             /* (delay_fb: past 100 it self-oscillates) */
    int32_t run_lp = tp.run_lp;
    float scoop = fb.scoop_c != 0.f ? tp.scoop : 0.f;
    uint32_t seed = tp.seed_rng;
    uint32_t wpos = tp.wpos_q16, wow_ph = tp.wow_ph, flutter_ph = tp.flutter_ph;
    uint32_t wow_ph2 = tp.wow_ph2, flutter_ph2 = tp.flutter_ph2;
    int32_t gap = tp.gap_q16, speed = tp.speed_q24, aa_lp = tp.aa_lp, loss_lp = tp.loss_lp, hp_lp = tp.hp_lp;
    int32_t loss2 = tp.loss2_lp, hp2 = tp.hp2_lp;
    int32_t h0 = tp.aa_h[0], h1 = tp.aa_h[1], h2 = tp.aa_h[2];
    int32_t m1 = 0, d1 = 0, m2 = 0, d2 = 0;                /* the heads' pull, Q16, and its step */
    /* slowed (or slowing): the anti-aliasing (TapeAA) and 1 / speed (the crossings' times), once a block from the speed
     * at its start */
    const int slow = tp.speed_q24 < (1 << 24) || speed_tgt < (1 << 24);
    TapeAA aac;
    float inv_sp = 1.f;
    if (slow) {
        aac = tape_aa(tp.speed_q24);
        inv_sp = 65536.f / (float)((tp.speed_q24 >> 8) | 1);
    } else {
        memset(tp.aa_z, 0, sizeof tp.aa_z + sizeof tp.rc_z + sizeof tp.rcb_z);
    }
    float az0 = tp.aa_z[0], az1 = tp.aa_z[1], az2 = tp.aa_z[2], az3 = tp.aa_z[3], az4 = tp.aa_z[4], az5 = tp.aa_z[5];
    float rz0 = tp.rc_z[0], rz1 = tp.rc_z[1], qz0 = tp.rcb_z[0], qz1 = tp.rcb_z[1];

    for (uint32_t s = 0; s < n; s++) {
        gap += (gap_tgt - gap) >> TAPE_SLEW;
        speed += (speed_tgt - speed) >> TAPE_SLEW;
        if (speed - speed_tgt < (1 << TAPE_SLEW) && speed_tgt - speed < (1 << TAPE_SLEW))
            speed = speed_tgt;                            /* (the slew alone stops short: a tape back from slow ran a
                                                           * hair under full speed and dropped a cell every ~1.5 s) */
        int32_t speed_q16 = speed >> 8;

        /* wow and flutter move the play head; scaled by speed so the pitch wobble is the same at any tape speed */
        wow_ph += WOW_INC;
        flutter_ph += FLUTTER_INC;
        if (!(s & (MOD_EVERY - 1u))) {
            int32_t nt = (para_sine(wow_ph + MOD_EVERY * WOW_INC) * wow_q8 + para_sine(flutter_ph + MOD_EVERY * FLUTTER_INC) * flutter_q8) >> 15;
            m1 = tp.mt[0][0] << 8;
            d1 = (nt - tp.mt[0][0]) << 5;
            tp.mt[0][0] = nt;
        }
        m1 += d1;
        int32_t mod_q8 = m1 >> 8;
        int32_t mod_q16 = (mod_q8 * speed_q16) >> 8;

        /* play head: linear interpolation between the two cells around it (slowed: tape_cr_at, then the reconstruction) */
        uint32_t r = wpos - (uint32_t)gap - (uint32_t)mod_q16;
        int32_t play;
        if (slow) {
            play = (int32_t)tbq(&aac.rc[0], &rz0, &rz1, tape_cr_at(r));
        } else {
            uint32_t i = r >> 16;
            int32_t frac = (int32_t)((r >> 1) & 0x7FFFu);
            int32_t a = TAPE_RD(i);
            int32_t b = TAPE_RD(i + 1u);
            play = a + (((b - a) * frac) >> 15);
        }

        /* playback head loss and high-pass: this is both the echo output and the feedback source */
        loss_lp += sfx_mulq15(play - loss_lp, loss_c);
        hp_lp += sfx_mulq15(loss_lp - hp_lp, hp_c);
        int32_t o = loss_lp - hp_lp;

        if (pp) {                                         /* the near head, halfway: the left side's echoes, its own
                                                           * wow and flutter (2026-11-03: the two sides drift apart) */
            wow_ph2 += WOW_INC2;
            flutter_ph2 += FLUTTER_INC2;
            if (!(s & (MOD_EVERY - 1u))) {
                int32_t nt = (para_sine(wow_ph2 + MOD_EVERY * WOW_INC2) * wow_q8 + para_sine(flutter_ph2 + MOD_EVERY * FLUTTER_INC2) * flutter_q8) >> 15;
                m2 = tp.mt[1][0] << 8;
                d2 = (nt - tp.mt[1][0]) << 5;
                tp.mt[1][0] = nt;
            }
            m2 += d2;
            int32_t mod2_q8 = m2 >> 8;
            uint32_t r2 = wpos - ((uint32_t)gap >> 1) - (uint32_t)((mod2_q8 * speed_q16) >> 8);
            int32_t play2;
            if (slow) {
                play2 = (int32_t)tbq(&aac.rc[0], &qz0, &qz1, tape_cr_at(r2));
            } else {
                uint32_t i2 = r2 >> 16;
                int32_t f2 = (int32_t)((r2 >> 1) & 0x7FFFu);
                int32_t a2 = TAPE_RD(i2);
                int32_t b2 = TAPE_RD(i2 + 1u);
                play2 = a2 + (((b2 - a2) * f2) >> 15);
            }
            loss2 += sfx_mulq15(play2 - loss2, loss_c);
            hp2 += sfx_mulq15(loss2 - hp2, hp_c);
            out_l[s] = loss2 - hp2;
            out_r[s] = o;
        } else {
            out_l[s] = out_r[s] = o;
        }

        /* record: input plus feedback, saturated, then band-limited for the tape speed */
        int32_t fbs = o, hiss = 0;
        if (fb.lp_on) {                                   /* past 100 or LOOP TONE darker: the darkening low-pass */
            run_lp += mr_mulq15(o - run_lp, fb.run_c);
            fbs = run_lp;
            if (fb.runaway) {                             /* past 100: the hiss seed */
                seed = seed * 1664525u + 1013904223u;
                hiss = (int32_t)(int16_t)(seed >> 16) >> 12;
            }
        }
        if (fb.scoop_c != 0.f) {                          /* DELAY HP */
            scoop += ((float)fbs - scoop) * fb.scoop_c;
            fbs -= (int32_t)scoop;
        }
        int32_t rec = tape_sat(in[s] + sfx_mulq15(fbs, fb.lo) + sfx_mulq15(fbs, fb.hi) + sfx_mulq15(fbs, fb.hi2) + hiss);
        int32_t aa_q15 = (speed_q16 >> 1) + (speed_q16 >> 2);    /* 0.75 x speed: the slowed tape's Nyquist */
        if (aa_q15 > 32767) aa_q15 = 32767;
        aa_lp += sfx_mulq15(rec - aa_lp, aa_q15);         /* (slowed, its tone stays: TapeAA only takes the top off) */
        int32_t cell = aa_lp;
        if (slow) {                                       /* the 6th-order low-pass (three biquads in a row) */
            int32_t c4 = (int32_t)tbq(&aac.aa[1], &az2, &az3, tbq(&aac.aa[0], &az0, &az1, (float)aa_lp));
            cell = (int32_t)tbq(&aac.aa[2], &az4, &az5, (float)c4);
        }

        /* the record head moves at most one cell per sample, so it writes at most one cell; slowed, its value at
         * the crossing, t samples ago, read by Catmull-Rom a sample late (between h1 and h2: the tape one sample
         * behind, which nothing hears) */
        uint32_t old_cell = wpos >> 16;
        wpos += (uint32_t)speed_q16;
        if ((wpos >> 16) != old_cell) {
            int32_t v = cell;
            if (slow) v = tape_cross_i(h0, h1, h2, cell, wpos & 0xFFFFu, inv_sp);
            TAPE_WR(wpos >> 16, clamp16(v));
        }
        h0 = h1; h1 = h2; h2 = cell;
    }
    tp.aa_h[0] = h0; tp.aa_h[1] = h1; tp.aa_h[2] = h2;
    tp.aa_z[0] = az0; tp.aa_z[1] = az1; tp.aa_z[2] = az2; tp.aa_z[3] = az3; tp.aa_z[4] = az4; tp.aa_z[5] = az5;
    tp.rc_z[0] = rz0; tp.rc_z[1] = rz1; tp.rcb_z[0] = qz0; tp.rcb_z[1] = qz1;
    tape_bq_flush(tp.aa_z, 14);                           /* (aa_z, rc_z, rcb_z) */
    tp.wpos_q16 = wpos;
    tp.wow_ph = wow_ph;
    tp.flutter_ph = flutter_ph;
    tp.gap_q16 = gap;
    tp.speed_q24 = speed;
    tp.aa_lp = aa_lp;
    tp.loss_lp = loss_lp;
    tp.hp_lp = hp_lp;
    tp.loss2_lp = loss2;
    tp.hp2_lp = hp2;
    tp.run_lp = run_lp;
    tp.scoop = scoop > 1e-3f || scoop < -1e-3f ? scoop : 0.f;   /* (no denormals) */
    tp.seed_rng = seed;
    tp.wow_ph2 = wow_ph2;
    tp.flutter_ph2 = flutter_ph2;
}

#endif
#if TAPE_HALF
/* a one-pole's coefficient (Q15) for half the rate: the same pole in time (1 - (1 - c)^2) */
static inline int32_t half_c(int32_t c) { return 2 * c - (int32_t)(((int64_t)c * c) >> 15); }

/* The half-rate tape (TAPE_HALF): the input pairs through a [1 3 3 1] / 8 decimator, one tape step each pair, the
 * heads' outputs back up by the [1 3 3 1] / 4 interpolator (a quarter and three quarters of the way, a step late). The
 * step itself is tape_delay_block's at full speed: heads read linearly, wow and flutter (their depth halved: a cell is
 * two samples now), head loss and high-pass, the feedback's filters, the record saturation; each one-pole moved to the
 * half rate (half_c), so the tone and the decay times stay. */
/* a head's position, Q16 cells, w less back (either sign), wrapped into the line [0, lq) (2026-11-23: the lines are
 * counts of cells, not powers of two, so the wrap is explicit) */
static inline uint32_t tape_pos(uint32_t w, int32_t back, uint32_t lq)
{
    int32_t d = (int32_t)w - back;
    while (d < 0) d += (int32_t)lq;
    while (d >= (int32_t)lq) d -= (int32_t)lq;
    return (uint32_t)d;
}
/* The loop's parts (2026-11-24), each compiled in only where tape_delay_block_half's variant `tv` holds it: PING PONG's
 * near head, DELAY HP, the runaway's / LOOP TONE's low-pass, LOOP TONE's wear, the break-up. The wrapper picks a
 * variant only when every part it leaves out would do nothing this block, so all of them give the same values. */
#define TV_PP 1u
#define TV_HP 2u
#define TV_LP 4u
#define TV_WEAR 8u
#define TV_BRK 16u
#define TV_ALL 31u
static inline __attribute__((always_inline)) void tape_half_run(const MasterPerformancePreset *p, const int32_t *in,
                                                                int32_t *out_l, int32_t *out_r, uint32_t n, DelayFb fb,
                                                                const unsigned tv)
{
    const int pp = (tv & TV_PP) ? tpp : 0;
    const uint32_t lq = tlen << 16;
    delay_targets(p, pp);                               /* (its length; the speed it sets is not used here) */
    uint32_t cells = (tp.last_len + 1u) >> 1;
    if (cells > tgapmax) cells = tgapmax;
    const int32_t gap_tgt = (int32_t)(cells << 16);
    int32_t wow, flutter;
    uint32_t filter;
    tape_degrade(p->delay_degrade, &wow, &flutter, &filter);
    const int32_t wow_q8 = wow * WOW_STEP_Q8 / 2, flutter_q8 = flutter * FLUTTER_STEP_Q8 / 2;
    const int32_t loss_c = half_c(head_loss_q15(filter)), hp_c = half_c(tape_hp_q15(filter));
    fb.run_c = half_c(fb.run_c);
    fb.scoop_c = fb.scoop_c != 0.f ? 2.f * fb.scoop_c - fb.scoop_c * fb.scoop_c : 0.f;
    int32_t run_lp = tp.run_lp;
    float scoop = fb.scoop_c != 0.f ? tp.scoop : 0.f;
    uint32_t seed = tp.seed_rng, step = tp.hstep;
    uint32_t wpos = tp.wpos_q16, wow_ph = tp.wow_ph, flutter_ph = tp.flutter_ph;
    uint32_t wow_ph2 = tp.wow_ph2, flutter_ph2 = tp.flutter_ph2;
    int32_t gap = tp.gap_q16, aa_lp = tp.aa_lp, loss_lp = tp.loss_lp, hp_lp = tp.hp_lp;
    int32_t loss2 = tp.loss2_lp, hp2 = tp.hp2_lp;
    int32_t z0 = tp.hz[0], z1 = tp.hz[1], z2 = tp.hz[2];
    int32_t l0 = tp.ho_l[0], l1 = tp.ho_l[1], r0 = tp.ho_r[0], r1 = tp.ho_r[1];
    int32_t m1 = tp.hm[0], d1 = tp.hm[1], m2 = tp.hm[2], d2 = tp.hm[3];
    uint8_t ph = tp.hph;
    uint32_t rc_i = tp.rc_i;
    int32_t rc_v = tp.rc_v;
    /* the line and the per-step state in locals (2026-11-24, the same values): the cells' byte stores may alias any
     * global, so every tp. field and tbase / tlen was loaded again after each write */
    const uint32_t tb = tbase, tl = tlen;
    int32_t mt0 = tp.mt[0][0], mt1 = tp.mt[1][0];
    int32_t em_w = tp.em_w, em_r0 = tp.em_r[0], em_r1 = tp.em_r[1];
    uint32_t brk_z = tp.brk_z;
    int32_t brk_g = tp.brk_g, brk_tgt = tp.brk_tgt, brk_left = tp.brk_left, brk_snag = tp.brk_snag;

#define TAPE_RD_L(i) tape12_rd(tb + (i))
    tp.speed_q24 = 1 << 24;                                /* (the full-rate tape's, should the type change back) */
#if TAPE_BREAK
    const int32_t brk_k = p->delay_degrade > 64u ? (int32_t)(p->delay_degrade > 127u ? 127u : p->delay_degrade) - 64 : 0;
    const uint32_t brk_drop = (uint32_t)(brk_k * brk_k) * 74u, brk_snagp = (uint32_t)(brk_k * brk_k) * 34u;
#endif
#if TAPE_WEAR
    const uint8_t tw_v = fx_live[FX_L_TONE];
    const float tw = tw_v < 64u ? tone_dark(tw_v) : (float)tone_hp(tw_v) * (1.f / 127.f);
    const int tw_on = (tv & TV_WEAR) && (tw > 0.f || tp.tw_d != 0.f || tp.tw_v != 0.f || tp.tw_dc != 0.f), tw_tape = tw_v < 64u;
    float tw_drv = 1.f, tw_make = 32768.f, tw_asym = 0.f, tw_dc = tp.tw_dc;
    int32_t tw_fbg = 32767, tw_pos = 0, tw_step = 0;
    if (tw_on) {
        tw_drv = (1.f + (tw_tape ? TW_DRIVE : TW_DRIVE_DESK) * tw) * (2.f / 3.f / 32768.f);   /* (u = 2x/3: unity when quiet) */
        tw_make = 32768.f / (1.f + (tw_tape ? TW_DRIVE : TW_DRIVE_DESK) * tw);
        tw_asym = tw_tape ? 0.35f * tw : 0.f;
        tp.tw_ph += TW_BREATH_INC * n;
        float br = 0.5f + 0.5f * (float)para_sine(tp.tw_ph) * (1.f / 32768.f);
        tw_fbg = (int32_t)(32767.f * (1.f - TW_BREATH * tw * br));
        tw_make *= 1.f + TW_PUSH * tw;                     /* (the push: the worn tape leans towards a howl, its clip holds it) */
        if (++tp.tw_cnt >= TW_DRIFT_BLOCKS * 128u / (n ? n : 1u)) {   /* the drift's next target */
            tp.tw_cnt = 0;
            float u = (float)(int16_t)(brk_rand(&tp.tw_z) >> 16) * (1.f / 32768.f);
            tp.tw_tgt = u * TW_DRIFT * (tw_tape ? 1.f : 0.33f) * tw * (float)gap_tgt;
        }
        if (tw == 0.f) tp.tw_tgt = 0.f;                    /* (turned back to 64: the length comes home) */
        const float k = (float)n * (1.f / 44100.f);        /* (~1 s each stage) */
        float v1 = tp.tw_v + (tp.tw_tgt - tp.tw_v) * k, d1 = tp.tw_d + (v1 - tp.tw_d) * k;
        if (tw == 0.f && d1 < 64.f && d1 > -64.f && v1 < 64.f && v1 > -64.f) v1 = d1 = 0.f;
        tw_pos = (int32_t)tp.tw_d;
        tw_step = (int32_t)((d1 - tp.tw_d) * (2.f / (float)(n ? n : 1u)));   /* a half-rate step's share */
        tp.tw_v = v1;
        tp.tw_d = d1;
    }
#endif

    SPROF_DECL;
    SPROF_START();
    for (uint32_t s = 0; s < n; s++) {
        int32_t x = in[s];
        if (!ph) {                                         /* the pair's first: the interpolator's late quarter */
            z0 = z1; z1 = z2; z2 = x;
            ph = 1;
            out_l[s] = (3 * l1 + l0) >> 2;
            out_r[s] = (3 * r1 + r0) >> 2;
            continue;
        }
        ph = 0;
        int32_t xin = (z0 + 3 * z1 + 3 * z2 + x) >> 3;     /* the decimator, a step */
        z0 = z1; z1 = z2; z2 = x;

        gap += (gap_tgt - gap) >> (TAPE_SLEW - 1);
        wow_ph += 2u * WOW_INC;
        flutter_ph += 2u * FLUTTER_INC;
        if (!(step & (MOD_EVERY - 1u))) {
            int32_t nt = (para_sine(wow_ph + MOD_EVERY * 2u * WOW_INC) * wow_q8 +
                          para_sine(flutter_ph + MOD_EVERY * 2u * FLUTTER_INC) * flutter_q8) >> 15;
            m1 = mt0 << 8;
            d1 = (nt - mt0) << 5;
            mt0 = nt;
        }
        m1 += d1;
#if TAPE_BREAK
        if ((tv & TV_BRK) && (brk_k || brk_snag || brk_g != 32767)) {
            if (brk_k) {
                uint32_t z = brk_rand(&brk_z);
                if (z < brk_drop && brk_left <= 0) {             /* a dropout: its depth and length */
                    uint32_t y = brk_rand(&brk_z);
                    brk_tgt = 4915 + (int32_t)((y >> 16) % 18022u);            /* 15 .. 70 % */
                    brk_left = 220 + (int32_t)((y & 0xFFFFu) % 1100u);         /* 10 .. 60 ms */
                } else if (z - brk_drop < brk_snagp) {           /* a snag */
                    int32_t y = (int32_t)(brk_rand(&brk_z) >> 16) - 32768;
                    brk_snag += y * 48 * brk_k / 63;             /* (+-24 cells at 127) */
                }
            }
            if (brk_left > 0 && --brk_left == 0) brk_tgt = 32767;
            if (!brk_k) { brk_tgt = 32767; brk_left = 0; }
            brk_g += (brk_tgt - brk_g + (brk_tgt > brk_g ? 31 : -31)) >> 5;   /* ~1.5 ms ease */
            brk_snag -= brk_snag >> 10;                          /* settles over ~50 ms */
            if (brk_snag > -64 && brk_snag < 64) brk_snag = 0;
        }
        int32_t back = (int32_t)gap + (m1 >> 8) + ((tv & TV_BRK) ? brk_snag : 0);
#else
        int32_t back = (int32_t)gap + (m1 >> 8);
#endif
#if TAPE_WEAR
        if (tv & TV_WEAR) {                                /* (without: no wear, tw_pos 0) */
            tw_pos += tw_step;
            back += tw_pos;                                /* the drift */
        }
#endif
        /* (one wrap of the whole distance: the same cell as wrapping each part in turn, every wrap a full reduction
         * into [0, lq) and every sum inside 32 bits) */
        uint32_t r = tape_pos(wpos, back, lq);
        uint32_t i = r >> 16, i1 = i + 1u < tl ? i + 1u : 0u;
        /* the read cache (2026-11-24): the cell this step reads first was the last one read, unless the head moved
         * back over it; only wide delays (128 cells: wow, flutter and the snags stay behind the write head) cache */
        const int wide = gap >= (int32_t)(128u << 16);
        SPROF_LAP(SPROF_D_MOD);
        int32_t a = (wide && i == rc_i) ? rc_v : TAPE_RD_L(i), b = TAPE_RD_L(i1);
        SPROF_LAP(SPROF_D_RD);
        rc_i = wide ? i1 : 0xFFFFFFFFu;
        rc_v = b;
        int32_t play = a + (((b - a) * (int32_t)((r >> 1) & 0x7FFFu)) >> 15);
#if TAPE_EMPH
        play = em_r0 = play + (em_r0 >> 1);
#endif
        loss_lp += sfx_mulq15(play - loss_lp, loss_c);
        hp_lp += sfx_mulq15(loss_lp - hp_lp, hp_c);
        int32_t o = loss_lp - hp_lp, ol = o;
        if (pp) {                                          /* the near head, halfway */
            wow_ph2 += 2u * WOW_INC2;
            flutter_ph2 += 2u * FLUTTER_INC2;
            if (!(step & (MOD_EVERY - 1u))) {
                int32_t nt = (para_sine(wow_ph2 + MOD_EVERY * 2u * WOW_INC2) * wow_q8 +
                              para_sine(flutter_ph2 + MOD_EVERY * 2u * FLUTTER_INC2) * flutter_q8) >> 15;
                m2 = mt1 << 8;
                d2 = (nt - mt1) << 5;
                mt1 = nt;
            }
            m2 += d2;
            int32_t back2 = (int32_t)(gap >> 1) + (m2 >> 8);
#if TAPE_BREAK
            if (tv & TV_BRK) back2 += brk_snag;
#endif
#if TAPE_WEAR
            if (tv & TV_WEAR) back2 += tw_pos >> 1;
#endif
            uint32_t r2 = tape_pos(wpos, back2, lq);
            uint32_t i2 = r2 >> 16, i21 = i2 + 1u < tl ? i2 + 1u : 0u;
            int32_t a2 = TAPE_RD_L(i2), b2 = TAPE_RD_L(i21);
            int32_t play2 = a2 + (((b2 - a2) * (int32_t)((r2 >> 1) & 0x7FFFu)) >> 15);
#if TAPE_EMPH
            play2 = em_r1 = play2 + (em_r1 >> 1);
#endif
            loss2 += sfx_mulq15(play2 - loss2, loss_c);
            hp2 += sfx_mulq15(loss2 - hp2, hp_c);
            ol = loss2 - hp2;
        }
        int32_t fbs = o, hiss = 0;
        if ((tv & TV_LP) && fb.lp_on) {
            run_lp += mr_mulq15(o - run_lp, fb.run_c);
            fbs = run_lp;
            if (fb.runaway) {
                seed = seed * 1664525u + 1013904223u;
                hiss = (int32_t)(int16_t)(seed >> 16) >> 12;
            }
        }
        if ((tv & TV_HP) && fb.scoop_c != 0.f) {
            scoop += ((float)fbs - scoop) * fb.scoop_c;
            fbs -= (int32_t)scoop;
        }
#if TAPE_WEAR
        if ((tv & TV_WEAR) && tw_on) {                     /* the grunge and the breath */
            float u = (float)fbs * tw_drv;
            if (tw_tape) {
                float t = tw_asym * u * u;
                tw_dc += 0.001f * (t - tw_dc);
                u += t - tw_dc;
            }
            u = u > 1.f ? 1.f : u < -1.f ? -1.f : u * (1.5f - 0.5f * u * u);
            fbs = sfx_mulq15((int32_t)(u * tw_make), tw_fbg);
        }
#endif
        SPROF_LAP(SPROF_D_FLT);
        int32_t rec = tape_sat(xin + sfx_mulq15(fbs, fb.lo) + sfx_mulq15(fbs, fb.hi) + sfx_mulq15(fbs, fb.hi2) + hiss);
        aa_lp += sfx_mulq15(rec - aa_lp, 24575);           /* (the full-speed tape's record low-pass, 0.75) */
        wpos += 1u << 16;
        if (wpos >= lq) wpos -= lq;
#if TAPE_EMPH
        {
#if TAPE_BREAK
            int32_t e0 = !(tv & TV_BRK) || brk_g == 32767 ? aa_lp : (int32_t)(((int64_t)aa_lp * brk_g) >> 15);   /* the dropout */
#else
            int32_t e0 = aa_lp;
#endif
            int32_t e = e0 - (em_w >> 1);
            em_w = e0;
            tape12_wr(tb + (wpos >> 16), clamp16(e));
        }
#else
        tape12_wr(tb + (wpos >> 16), clamp16(aa_lp));
        SPROF_LAP(SPROF_D_WR);
#endif
        step++;
        l0 = l1; l1 = ol;                                  /* the interpolator's step: a quarter of the newest */
        r0 = r1; r1 = o;
        out_l[s] = (l1 + 3 * l0) >> 2;
        SPROF_LAP(SPROF_D_STEP);
        out_r[s] = (r1 + 3 * r0) >> 2;
    }
#undef TAPE_RD_L
    tp.mt[0][0] = mt0; tp.mt[1][0] = mt1;
    tp.em_w = em_w; tp.em_r[0] = em_r0; tp.em_r[1] = em_r1;
    tp.brk_z = brk_z; tp.brk_g = brk_g; tp.brk_tgt = brk_tgt; tp.brk_left = brk_left; tp.brk_snag = brk_snag;
    tp.hz[0] = z0; tp.hz[1] = z1; tp.hz[2] = z2;
    tp.ho_l[0] = l0; tp.ho_l[1] = l1; tp.ho_r[0] = r0; tp.ho_r[1] = r1;
    tp.hph = ph;
    tp.rc_i = rc_i;
    tp.rc_v = rc_v;
    tp.hstep = step;
    tp.hm[0] = m1; tp.hm[1] = d1; tp.hm[2] = m2; tp.hm[3] = d2;
    tp.wpos_q16 = wpos;
    tp.wow_ph = wow_ph;
    tp.flutter_ph = flutter_ph;
    tp.gap_q16 = gap;
    tp.aa_lp = aa_lp;
    tp.loss_lp = loss_lp;
    tp.hp_lp = hp_lp;
    tp.loss2_lp = loss2;
    tp.hp2_lp = hp2;
    tp.run_lp = run_lp;
    tp.scoop = scoop > 1e-3f || scoop < -1e-3f ? scoop : 0.f;
    tp.seed_rng = seed;
    tp.wow_ph2 = wow_ph2;
    tp.flutter_ph2 = flutter_ph2;
#if TAPE_WEAR
    tp.tw_dc = tw_dc < 1e-6f && tw_dc > -1e-6f ? 0.f : tw_dc;
#endif
}
/* The plain tape's own loop (2026-11-24): on the FM1 every load and store costs ~8 cycles, and the full loop carries so
 * much state (the near head, the break-up, the wear, the runaway, DELAY HP) that ~130 spills a step were most of its
 * time (the delay-only profile: 37 ticks a sample a line, ~3 cycles an instruction). When none of those would do
 * anything this block (each test below is the one the loop makes), the loop without them gives the same values with
 * far fewer live registers. Stereo's lines never run PING PONG inside, so with DEGRADE <= 64, LOOP TONE 64, FEEDBACK
 * <= 100 and DELAY HP off both take it. */
static void tape_delay_block_half(const MasterPerformancePreset *p, const int32_t *in, int32_t *out_l, int32_t *out_r,
                                  uint32_t n)
{
    DelayFb fb = delay_fb(p->delay_feedback, p->delay_degrade, DELAY_HP(p), fx_live[FX_L_TONE]);
    unsigned need = (tpp ? TV_PP : 0u) | (fb.lp_on ? TV_LP : 0u) | (fb.scoop_c != 0.f ? TV_HP : 0u);
#if TAPE_BREAK
    if (p->delay_degrade > 64u || tp.brk_snag != 0 || tp.brk_g != 32767) need |= TV_BRK;
#endif
#if TAPE_WEAR
    if (fx_live[FX_L_TONE] != 64u || tp.tw_d != 0.f || tp.tw_v != 0.f || tp.tw_dc != 0.f) need |= TV_WEAR;
#endif
    /* (2026-11-24) the factory performances: PING PONG with DELAY HP, whose LOOP TONE above the middle wears the tape
     * (16 of 20), or with LOOP TONE dark (the 4 dub ones); a stereo line with DELAY HP takes the first too */
    if (need == 0u)
        tape_half_run(p, in, out_l, out_r, n, fb, 0u);
    else if ((need & ~(TV_PP | TV_HP | TV_WEAR)) == 0u)
        tape_half_run(p, in, out_l, out_r, n, fb, TV_PP | TV_HP | TV_WEAR);
    else if ((need & ~(TV_PP | TV_LP | TV_WEAR)) == 0u)
        tape_half_run(p, in, out_l, out_r, n, fb, TV_PP | TV_LP | TV_WEAR);
    else
        tape_half_run(p, in, out_l, out_r, n, fb, TV_ALL);
}
#endif


/* ROOM's multiplies round toward zero (mr_mulq15, 2026-10-15): with sfx_mulq15's floor the combs held a residue for
 * ever, up to ~90 LSB at the top of REVERB TIME (measured). */
/* the reverb of p's type: x is the reverb send, replaced by the left output; r gets the
 * right. MOD, DOLMEN and TUMULUS: the tank, stereo. The others: the ROOM network, mono (decay sets the comb feedback,
 * damping the comb low-pass, spec 4.6 EFFECTS) */
/* The reverb in two halves (2026-10-15): the front (the send clamped, a tank's input diffusion), which
 * feeds nothing back, and the back (the tank, or the ROOM network). The front runs FX_REV_LAG samples ahead through
 * rv_ring: on the FM1 core 0 runs this pass's front beside the delay while core 1 runs the back on the front's output
 * of a pass ago; they touch different places of the ring (n <= the lag), so one core or two give the same sums.
 * The reverb starts the lag (2.9 ms) late. REV PREDELAY was removed with the performance's v11 (2026-11-03: 8.9 KB of
 * RAM; DLY > REV stands in). */

static int reverb_is_tank(uint8_t t)
{
    return t == REVTYPE_MOD || t == REVTYPE_CELESTIAL || t == REVTYPE_DOLMEN || t == REVTYPE_TUMULUS;
}

/* the half-rate tank's frame parity, front and back: 1 when the next frame is a pair's second. Both step on every
 * frame whatever the type, so they stay in step (the back reads FX_REV_LAG frames behind, an even number) */
static uint32_t rv_odd_f, rv_odd_b;
PS_STATIC_ASSERT((FX_REV_LAG & 1u) == 0u, "the back reads whole pairs behind the front");

void fx_reverb_front(const MasterPerformancePreset *p, const int32_t *send, uint32_t n)
{
    int32_t x[FX_BLOCK_MAX];
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    uint32_t odd = rv_odd_f;
    rv_odd_f ^= n & 1u;
    send_limit(&rv_lim_env, send, x, n);
    if (p->reverb_type == REVTYPE_DOLMEN)
        tank_front(x, n, 102u, odd);
    else if (reverb_is_tank(p->reverb_type))
        tank_front(x, n, 256u, odd);
    else if (p->reverb_type == REVTYPE_SPRING)
        spring_front(x, n);
    for (uint32_t s = 0; s < n; s++) {
        rv_ring[(rv_w + s) & (RV_RING - 1u)] = x[s];
        rv_sring[(rv_w + s) & (RV_RING - 1u)] = s < rv_side_n ? (int32_t)rv_side_in[s] : 0;
    }
    rv_side_n = 0u;
    rv_w = (rv_w + n) & (RV_RING - 1u);
}

/* the side's block for the back (core 1's: static, not on its 2.8 KB stack) */
static int32_t rv_sd[FX_BLOCK_MAX] FX_LINE;

#if MR_HALF
#define MR_HB (FX_BLOCK_MAX / 2u + 1u)                    /* a block's tank samples, at most */
static int32_t mr_hx[MR_HB] FX_LINE, mr_hs[MR_HB] FX_LINE, mr_hr[MR_HB] FX_LINE;   /* (core 1's) */
static struct FX_LINE {
    int32_t pend_s;                                       /* a pair's first side frame, when a block ended between */
    int32_t prev_l, prev_r;                               /* the tank's last output (the interpolation's) */
} mr_half_c1;
/* the tank at half rate over a block of n frames, odd as rv_odd_b: the front's pair samples (on the pairs' second
 * frames) and the side's pairs averaged in, the output back up a frame late (a pair's first frame the last tank
 * sample, its second halfway to the new one), as the springs */
static void tank_half_block(const MasterPerformancePreset *p, int32_t *x, const int32_t *sd, int32_t *r, uint32_t n,
                            uint32_t odd)
{
    uint32_t m = 0, o = odd;
    int32_t pend_s = mr_half_c1.pend_s;
    for (uint32_t s = 0; s < n; s++) {
        if (o) {
            mr_hx[m] = x[s];
            mr_hs[m++] = (pend_s + sd[s]) / 2;
        } else
            pend_s = sd[s];
        o ^= 1u;
    }
    mr_half_c1.pend_s = pend_s;
    if (m)
        tank_reverb_block(p, mr_hx, mr_hs, mr_hr, m, p->reverb_type);
    int32_t al = mr_half_c1.prev_l, ar = mr_half_c1.prev_r;
    m = 0;
    o = odd;
    for (uint32_t s = 0; s < n; s++) {
        int32_t el = al, er = ar;
        if (o) {
            el = (al + mr_hx[m]) / 2;                     /* (toward zero: a dead tail ends at 0) */
            er = (ar + mr_hr[m]) / 2;
            al = mr_hx[m];
            ar = mr_hr[m++];
        }
        o ^= 1u;
        x[s] = el;
        r[s] = er;
    }
    mr_half_c1.prev_l = al;
    mr_half_c1.prev_r = ar;
}
#endif

static void rv_half_init(void)
{
    rv_odd_f = rv_odd_b = 0u;
#if MR_HALF
    memset(mr_in_h, 0, sizeof mr_in_h);
    memset(&mr_half_c1, 0, sizeof mr_half_c1);
#endif
}

void fx_reverb_back(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n)
{
    int32_t *const sd = rv_sd;
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    uint32_t odd = rv_odd_b;
    rv_odd_b ^= n & 1u;
    (void)odd;
    const int tank = reverb_is_tank(p->reverb_type);
    for (uint32_t s = 0; s < n; s++) {
        x[s] = rv_ring[(rv_r + s) & (RV_RING - 1u)];
        int32_t v = rv_sring[(rv_r + s) & (RV_RING - 1u)];
        sd[s] = tank ? mr_mulq15(v * 256, MR_IN_GAIN) : v;   /* (the tank's: at its send's level, as tank_front's) */
    }
    rv_r = (rv_r + n) & (RV_RING - 1u);
    if (tank) {
#if MR_HALF
        tank_half_block(p, x, sd, r, n, odd);
#else
        tank_reverb_block(p, x, sd, r, n, p->reverb_type);
#endif
        return;
    }
    if (p->reverb_type == REVTYPE_SPRING) {
        spring_block(p, x, sd, r, n);
        return;
    }                                                     /* (TOMB, mono: no side) */
    const int32_t size = 25000 + (int32_t)p->reverb_decay * 50;
    const int32_t damp_q15 = (int32_t)p->reverb_damp * 200;     /* comb low-pass coefficient, Q15 */
    int16_t *const c0 = sfx_rev_comb, *const c1 = c0 + SFX_REV_COMB[0], *const c2 = c1 + SFX_REV_COMB[1],
            *const c3 = c2 + SFX_REV_COMB[2];
    int16_t *const a0 = sfx_rev_ap, *const a1 = a0 + SFX_REV_AP[0];
    uint32_t i0 = comb_i[0], i1 = comb_i[1], i2 = comb_i[2], i3 = comb_i[3], j0 = ap_i[0], j1 = ap_i[1];
    int32_t l0 = comb_lp[0], l1 = comb_lp[1], l2 = comb_lp[2], l3 = comb_lp[3];

    for (uint32_t s = 0; s < n; s++) {
        int32_t in = mr_mulq15(x[s], 2580 * 2);                /* 1/8 at -4 dB, as the Felucca ROOM, x4 (2026-11-24: the
                                                                * int16 lines' rounding was the hiss: 6 dB more inside) */
        int32_t a = 0, o;
#define SFX_COMB(c, i, lp, len)                                                     \
        o = c[i];                                                                   \
        lp = o + mr_mulq15(lp - o, damp_q15);                                      \
        c[i] = (int16_t)clamp16(in + mr_mulq15(lp, size));                         \
        if (++i >= (len)) i = 0;                                                    \
        a += o;
        SFX_COMB(c0, i0, l0, 1116u)
        SFX_COMB(c1, i1, l1, 1188u)
        SFX_COMB(c2, i2, l2, 1277u)
        SFX_COMB(c3, i3, l3, 1356u)
#undef SFX_COMB
        /* 2026-10-16: the allpasses at half level (their int16 lines clipped the four combs' sum of a hot chord:
         * crackle), the output doubled after them; halved toward zero (a floor would leave a DC residue) */
        a = (a + ((a >> 31) & 1)) >> 1;
/* the allpasses' feedback halves toward zero too (2026-10-16): o >> 1 floored, so a -1 fed back stayed -1 and a few
 * LSB circulated in the two loops for ever after any sound, a high whine through PLATE / SPRING / ROOM / HALL that
 * never stopped (measured 30 s after release: 0 .. -4 LSB, ~7000 value changes a second; now exactly 0) */
#define SFX_AP(c, j, len)                                                           \
        o = c[j];                                                                   \
        c[j] = (int16_t)clamp16(a + ((o + ((o >> 31) & 1)) >> 1));                  \
        a = o - a;                                          /* Freeverb: out = buf - in */ \
        if (++j >= (len)) j = 0;
        SFX_AP(a0, j0, 556u)
        SFX_AP(a1, j1, 441u)
#undef SFX_AP
        x[s] = a;                                           /* x2 (as before) over the x2 in: unity */
        r[s] = a;
    }
    comb_i[0] = i0; comb_i[1] = i1; comb_i[2] = i2; comb_i[3] = i3;
    ap_i[0] = j0; ap_i[1] = j1;
    comb_lp[0] = l0; comb_lp[1] = l1; comb_lp[2] = l2; comb_lp[3] = l3;
}

void fx_reverb_block(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n)
{
    fx_reverb_front(p, x, n);
    fx_reverb_back(p, x, r, n);
}

/* FX_LAYOUT_PAD (2026-11-11, a layout tool): the FM1 runs the DSP from XIP flash through a 32 KB 8-way I-cache (4 KB a
 * way), so a function's cache sets are its address mod 4 KB. Every DSP function after the reverb (the MFX, the
 * oscillators, the voices' control tick and render) moves when the reverb's code grows, onto other sets: +-2..5 PERF
 * points from changes that touch no voice code (fm1-cpu-profile, the layout lottery; the half-rate tank, +536 B: +4.7
 * on rows it never runs in). FX_LAYOUT_PAD N: a never-run function of N volatile stores here, so the code after it
 * moves by a whole way and keeps its sets. (A 4 KB-aligned function would do it exactly, but it raises .text's
 * alignment and moves _start; inline asm is X0X's hal/ only.) */
#if defined(FX_LAYOUT_PAD) && FX_LAYOUT_PAD > 0
volatile uint8_t fx_pad_sink;
#define FXP1 fx_pad_sink = 0u;
#define FXP2 FXP1 FXP1
#define FXP4 FXP2 FXP2
#define FXP8 FXP4 FXP4
#define FXP16 FXP8 FXP8
#define FXP32 FXP16 FXP16
#define FXP64 FXP32 FXP32
#define FXP128 FXP64 FXP64
#define FXP256 FXP128 FXP128
#define FXP512 FXP256 FXP256
#define FXP1024 FXP512 FXP512
__attribute__((noinline, used)) void fx_layout_pad(void)
{
#if FX_LAYOUT_PAD & 1
    FXP1
#endif
#if FX_LAYOUT_PAD & 2
    FXP2
#endif
#if FX_LAYOUT_PAD & 4
    FXP4
#endif
#if FX_LAYOUT_PAD & 8
    FXP8
#endif
#if FX_LAYOUT_PAD & 16
    FXP16
#endif
#if FX_LAYOUT_PAD & 32
    FXP32
#endif
#if FX_LAYOUT_PAD & 64
    FXP64
#endif
#if FX_LAYOUT_PAD & 128
    FXP128
#endif
#if FX_LAYOUT_PAD & 256
    FXP256
#endif
#if FX_LAYOUT_PAD & 512
    FXP512
#endif
#if FX_LAYOUT_PAD & 1024
    FXP1024
#endif
}
void (*volatile fx_layout_pad_ref)(void) = fx_layout_pad;   /* (keeps it through --gc-sections) */
#endif

void fx_delay_block(const MasterPerformancePreset *p, const int32_t *in_l, const int32_t *in_r, int32_t *out_l,
                    int32_t *out_r, uint32_t n)
{
    /* in_l is the mono send (in_r NULL), or the left channel of a stereo send (2026-11-24: each side feeds its own line) */
    int32_t dl[FX_BLOCK_MAX], dr[FX_BLOCK_MAX];
    int32_t din[FX_BLOCK_MAX];
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    if (n == 0u) return;
    send_limit(&dl_lim_env, in_l, dl, n);
    if (in_r) send_limit(&dl_lim_env_r, in_r, dr, n);
    else memcpy(dr, dl, n * sizeof dl[0]);
    memcpy(din, dl, n * sizeof din[0]);
#if TAPE_HALF
    if (!DELAY_STEREO(p)) {                          /* mono: one line of 22200 cells (1.0 s), both ears alike */
        tcur = &tpl[0]; tbase = 0u; tlen = TAPE_LEN; tgapmax = TAPE_GAP_MAX; tlen_q8 = 256u;
        tpp = DELAY_PINGPONG(p) != 0u;
        tape_delay_block_half(p, din, out_l, out_r, n);
    } else {                                         /* stereo: two lines of 8192 cells, the right at 3/4 of the left */
        static int32_t xl[FX_BLOCK_MAX], xr[FX_BLOCK_MAX];   /* last block's outputs: the PING PONG cross-feed */
        static int32_t sl[FX_BLOCK_MAX], sr[FX_BLOCK_MAX];
        int pp = DELAY_PINGPONG(p) != 0u;
        int32_t g = (int32_t)(p->delay_feedback > 100u ? 100u : p->delay_feedback) * 250;   /* the feedback, Q15 */
        int32_t il[FX_BLOCK_MAX], ir[FX_BLOCK_MAX];
        for (uint32_t k = 0; k < n; k++) {
            il[k] = dl[k] + (pp ? (int32_t)(((int64_t)xr[k] * g) >> 15) : 0);
            ir[k] = dr[k] + (pp ? (int32_t)(((int64_t)xl[k] * g) >> 15) : 0);
        }
        tcur = &tpl[0]; tbase = 0u; tlen = ST_LEN; tgapmax = ST_LEN - 64u; tlen_q8 = 256u; tpp = 0;
        tape_delay_block_half(p, il, out_l, sl, n);
        tcur = &tpl[1]; tbase = ST_LEN; tlen = ST_LEN; tgapmax = ST_LEN - 64u; tlen_q8 = 192u; tpp = 0;
        tape_delay_block_half(p, ir, sr, out_r, n);
        for (uint32_t k = 0; k < n; k++) { xl[k] = out_l[k]; xr[k] = out_r[k]; }
    }
#else
    tape_delay_block(p, din, out_l, out_r, n);
#endif
}

void fx_process_block(const MasterPerformancePreset *p, const int32_t *delay_in, int32_t *reverb_io, int32_t *wet_r,
                      uint32_t n)
{
    /* Two effects, each on its own send, and the cross-feeds between them in the render's order (fx.h) */
    int32_t din[FX_BLOCK_MAX], dl[FX_BLOCK_MAX], dr[FX_BLOCK_MAX];
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    memcpy(din, delay_in, n * sizeof din[0]);
    fx_cross_begin(n);
    fx_cross_to_dly(din, 0, n);
    fx_delay_block(p, din, 0, dl, dr, n);
    fx_cross_to_rev(dl, dr, reverb_io, n);
    fx_reverb_block(p, reverb_io, wet_r, n);
    fx_cross_rev_job(reverb_io, wet_r, n);
    fx_mix_block(dl, dr, reverb_io, wet_r, n);
}

/* ---- the cross-feeds (2026-11-02, fx.h) ----
 * Each way: the source band-limited by a 1-pole high-pass at ~150 Hz and low-pass at ~4.5 kHz (the loop darkens and
 * thins a little each time round, as through old desks and tape: never mud, never fizz; LOOP TONE moves them, as it
 * does the delay's own feedback: the loop follows the repeats), times the amount (XF_MAX (k / 127)^2: fine at the
 * bottom; ramped over the block, no zipper), soft-clipped alone on XF_H (a return holds at its headroom: a loop blooms
 * up to it and cannot run away) and added to the other's send.
 * 2026-11-03, stereo: DLY > REV runs each side of the echoes on its own; their mean goes into the reverb's send (as
 * the mono way did, to the bit while the sides match), half their difference is the send's side (rv_side_in: the
 * tank's two halves, the two springs), so a PING PONG echo enters the reverb where it was. REV > DLY stays mono (the
 * delay has one tape).
 * 2026-11-03, the wear: away from LOOP TONE 64 the loop wears, more the further it turns (64: none, to the bit).
 * Darker: tape. Each side's time wanders (XF_WOW: a slow spin and a random drift, each side its own rates: the
 * repeats drift in pitch and apart, more each pass), a lopsided saturation pushed harder (even harmonics, its DC
 * taken out), and the hotter the loop the darker its low-pass (a pushed machine sinking back). Thinner: an old desk,
 * an even saturation pushed harder still, a little wander, a little darkening. */
#ifndef XF_MAX
#define XF_MAX 0.7f                                       /* the amount at 127 (both at 127, FEEDBACK 80, TIME 90: every
                                                           * reverb type blooms and holds, -15 .. -22 dBFS) */
#endif
#ifndef XF_H
#define XF_H 1500.f                                       /* a return's headroom, the sends' units (the bloom's cap: a
                                                           * hot send settles where a quiet one does) */
#endif
#define XF_HP 0.0211f                                     /* ~150 Hz (LOOP TONE thinner: DELAY HP's, when higher) */
#define XF_LP_HZ 4500.f                                   /* the low-pass at LOOP TONE 64 (darker: down to ~400 Hz) */
#define XF_WOW_LEN 256u                                   /* each side's wander line (power of 2) */
#define XF_WOW_BASE 40.f                                  /* its time at full wear, samples (0.9 ms) ... */
#define XF_WOW_EXC 24.f                                   /* ... +- this (0.55 ms: ~3 cents at the spin's rate; 1.8
                                                           * ms detuned the loop's own resonances and it no longer held) */
#define XF_WOW_THIN 0.25f                                 /* the thinner side's share of it */
PS_STATIC_ASSERT((int)XF_WOW_BASE + (int)XF_WOW_EXC + 2 < (int)XF_WOW_LEN, "the wander fits its line");
enum { XF_CL, XF_CR, XF_CM, XF_CM2, XF_NCH };            /* DLY > REV's left and right; REV > DLY's mono (CM2: the
                                                           * removed BBD's right, unused) */
/* 2026-11-20, the cores: REV > DLY's chain runs on core 1, at the end of the reverb's job (fx_cross_rev_job), on the
 * tank's output it has just made; its result (xf_c1.pend) goes into the next pass's delay send, as the reverb's last
 * pass always did (the same lag). Core 0 does everything the two ways share before core 1 starts (fx_cross_begin:
 * LOOP TONE's coefficients, the gains, the drifts' new targets) and hands each way its block's settings (XfP), so
 * core 1 only reads those and writes its own channel, level and result, each on lines of their own (FX_LINE). A
 * knob turned reaches REV > DLY a pass (2.9 ms) later than before; held, the loop is the same. */
typedef struct FX_LINE {                                  /* (a line each: core 1 runs XF_CM while core 0 runs CL, CR) */
    float hp, lp, dc;                                     /* the band-limit; the lopsided saturation's DC */
    float d;                                              /* the wander's time at the last block's end, samples */
    float wan, wan_tgt;                                   /* its random drift, -1 .. 1 */
    uint32_t ph, inc;                                     /* its spin */
    uint32_t wp;                                          /* the line's write position */
    float *buf;                                           /* its line (xf_wow: the band-limited source) */
    float prev;                                           /* (XF_HALF) the last half-rate output, for the upsampling */
} XfCh;
typedef struct {                                          /* a way's block: what core 0 worked out for it */
    float g, dg;                                          /* the gain at the start and its step a sample */
    float hp_c, lp_c;                                     /* the band-limit (lp_c heated) */
    float drive, asym, make, wow;                         /* the wear */
    float hp_h, lp_h;                                     /* (XF_HALF) the band-limit at the half rate */
    int on;                                               /* 0: the way is off this pass */
} XfP;
static float xf_wow[XF_NCH][XF_WOW_LEN] SYNTH_POOL FX_LINE;   /* (4 KB: .pool, as every buffer here) */
static struct {
    XfCh ch[XF_NCH];
    float g[2];                                           /* each way's gain at the last block's end */
    float hp_c, lp_c;                                     /* the band-limit's coefficients at LOOP TONE tone_key */
    float drive, asym, make, heat, wow;                   /* the wear at tone_key (64: 1, 0, 1, 0, 0) */
    int tone_key;
    float lvl_dr;                                         /* DLY > REV's return as a share of XF_H, held and falling */
    uint32_t rng, nblk;                                   /* the drifts' */
    XfP pp[2];                                            /* this pass's settings, each way (fx_cross_begin) */
} xf;
static struct FX_LINE {                                   /* core 1's: REV > DLY's level and result */
    float lvl;                                            /* its return as a share of XF_H, held and falling */
    uint32_t npend;                                       /* the result's length (0: none) */
    int32_t pend[FX_BLOCK_MAX] FX_LINE;                   /* for the next pass's delay send */
} xf_c1;
PS_STATIC_ASSERT(sizeof(XfCh) % 64u == 0u && sizeof xf_c1 % 64u == 0u, "core 1's lines");

static void xf_init(void)                                 /* (the amounts, fx_live, are the UI's) */
{
    static const uint32_t inc[XF_NCH] = {36035u, 51618u, 28244u, 39941u};   /* the spins: 0.37, 0.53, 0.29, 0.41 Hz */
    memset(&xf, 0, sizeof xf);
    memset(&xf_c1, 0, sizeof xf_c1);
    xf.tone_key = -1;
    xf.rng = 0x1F2E3D4Cu;
    memset(xf_wow, 0, sizeof xf_wow);
    for (int c = 0; c < XF_NCH; c++) {
        xf.ch[c].buf = xf_wow[c];
        xf.ch[c].inc = inc[c];
        xf.ch[c].ph = (uint32_t)c * 0x55555555u;
    }
}

/* 1 if way j is on, or still ramping down to off */
static inline int xf_on(int j) { return fx_cross[j] != 0u || xf.g[j] != 0.f; }
int fx_cross_to_rev_on(void) { return xf_on(FX_X_DLY_REV); }

/* the band-limit and the wear for LOOP TONE: the high-pass DELAY HP's where above ~150 Hz, the low-pass 4.5 kHz falling
 * to ~400 Hz; the wear from none at 64 to full at either end */
static void xf_tone(void)
{
    uint8_t v = fx_live[FX_L_TONE];
    if ((int)v == xf.tone_key)
        return;
    uint32_t hp = tone_hp(v);
    float c = hp ? DELAY_HP_C0 * amp_exp2f((float)(int32_t)(hp - 1u) * DELAY_HP_OCT) : 0.f;
    xf.hp_c = c > XF_HP ? c : XF_HP;
    xf.lp_c = 1.f - amp_exp2f(-XF_LP_HZ * amp_exp2f(-3.5f * tone_dark(v)) * (2.f * 3.14159265f * 1.442695f / 44100.f));
    float w = v < 64u ? tone_dark(v) : (float)hp * (1.f / 127.f);
    if (v < 64u) {                                        /* tape */
        xf.drive = 1.f + 1.5f * w;
        xf.asym = 0.3f * w;
        xf.heat = 0.7f * w;
        xf.wow = w;
    } else {                                              /* the desk (and 64: none) */
        xf.drive = 1.f + 2.f * w;
        xf.asym = 0.f;
        xf.heat = 0.4f * w;
        xf.wow = XF_WOW_THIN * w;
    }
#ifdef XF_NO_WEAR                                         /* (host A/B: LOOP TONE's filters alone, as before) */
    xf.drive = 1.f; xf.asym = 0.f; xf.heat = 0.f; xf.wow = 0.f;
#endif
#ifdef XF_NO_HEAT
    xf.heat = 0.f;
#endif
#ifdef XF_NO_WOW
    xf.wow = 0.f;
#endif
#ifdef XF_NO_ASYM
    xf.asym = 0.f;
#endif
#ifdef XF_NO_DRIVE
    xf.drive = 1.f;
#endif
    xf.make = 1.f / xf.drive;                             /* (a quiet pass as it was: the wear colours the loop, it
                                                           * does not lengthen it; a held loop holds lower) */
    xf.tone_key = (int)v;
}

/* channel c's wander time at this block's end (spin and drift), samples; 0 with no wear */
static float xf_wow_d(XfCh *c, uint32_t n, float wow)
{
    c->ph += c->inc * n;
    c->wan += (c->wan_tgt - c->wan) * 0.03f;              /* (tau ~0.1 s) */
    float lfo = 0.6f * (float)para_sine(c->ph) * (1.f / 32768.f) + 0.4f * c->wan;
    return wow * (XF_WOW_BASE + XF_WOW_EXC * lfo);
}

/* 2026-11-20, half rate (XF_HALF 1): each chain runs at 22.05 kHz, a pair of samples a step (averaged in, linearly
 * interpolated out, ~1.5 samples late); its band-limit is <= 4.5 kHz, so the pair average and the interpolation's
 * images (> 17 kHz, falling) sit far above it. The half-rate saturator's harmonics fold below 11 kHz: a little
 * grit, the loop's own. Coefficients rescaled (the poles squared), the wander halved in samples (the same time).
 * Odd passes (none on the FM1: 128 frames): the last sample holds the last output. */
#ifndef XF_HALF
#define XF_HALF 1
#endif
/* What a chain reads and where its result goes (one loop, no copies in or out, 2026-11-20):
 *  XS_L  DLY > REV's left: src a, the result kept in z (shares of XF_H) for the right's pass;
 *  XS_R  its right: src a; the mean with z into the send o (+=), half the difference into the side so;
 *  XS_M  REV > DLY: src the mean of a and b, the result into o (=, the units of the send). */
enum { XS_L, XS_R, XS_M };
#define XF_EMIT(S, V)                                                                                                  \
    do {                                                                                                               \
        if (mode == XS_L)                                                                                              \
            z[S] = (V);                                                                                                \
        else if (mode == XS_R) {                                                                                       \
            o[S] += (int32_t)(0.5f * (z[S] + (V)) * XF_H);                                                             \
            so[S] = 0.5f * (z[S] - (V)) * XF_H;                                                                        \
        } else                                                                                                         \
            o[S] = (int32_t)((V) * XF_H);                                                                              \
    } while (0)

/* channel c over n samples: band-limited (the heated low-pass), wandered, times the gain, worn and clipped;
 * returns the peak before the make-up. worn 0: none of it (LOOP TONE 64); 1: the desk (drive, make-up, a little
 * wander, no lopsided part); 2: the tape (all of it). Each variant its own loop (xf_loop_*), so each keeps only what
 * it needs in registers */
static inline __attribute__((always_inline)) float xf_run(XfCh *c, const XfP *q, uint32_t n, float d1, const int mode,
                                                          const int worn, const int32_t *a, const int32_t *b,
                                                          float *z, int32_t *o, float *so)
{
    const int half = XF_HALF;
    const uint32_t m = half ? n >> 1 : n;
    const float hp_c = half ? q->hp_h : q->hp_c, lp_c = half ? q->lp_h : q->lp_c;
    const float asym = q->asym, mk = q->make, dck = half ? 0.001f : 0.0005f;
    const float sc = worn ? q->drive * (1.f / XF_H) : (1.f / XF_H);   /* (the drive and the headroom in the gain) */
    float g = q->g * sc, dg = q->dg * sc * (half ? 2.f : 1.f);
    float hp = c->hp, lp = c->lp, dc = c->dc, peak = 0.f, prev = c->prev;
    float d = c->d, dd = m ? (d1 - d) * (1.f / (float)m) : 0.f;
    uint32_t wp = c->wp;
    float *const buf = c->buf;
    for (uint32_t k = 0; k < m; k++) {
        float x;
        if (half) {
            const uint32_t s = 2u * k;
            x = mode == XS_M ? 0.25f * (((float)a[s] + (float)b[s]) + ((float)a[s + 1u] + (float)b[s + 1u]))
                             : 0.5f * ((float)a[s] + (float)a[s + 1u]);
        } else
            x = mode == XS_M ? 0.5f * ((float)a[k] + (float)b[k]) : (float)a[k];
        g += dg;
        hp += hp_c * (x - hp);
        lp += lp_c * (x - hp - lp);
        float y = lp;
        if (worn) {                                       /* the wander: d behind, linear (a fresh line: d <= k) */
            buf[wp & (XF_WOW_LEN - 1u)] = lp;
            d += dd;
            uint32_t i = (uint32_t)d;
            float f = d - (float)i, r = buf[(wp - i) & (XF_WOW_LEN - 1u)];
            y = r + f * (buf[(wp - i - 1u) & (XF_WOW_LEN - 1u)] - r);
            wp++;
        }
        float u = g * y;
        if (worn == 2) {                                  /* the lopsided part, its DC (tau ~50 ms) taken out */
            float t = asym * u * u;
            dc += dck * (t - dc);
            u += t - dc;
        }
        u = u > 1.f ? 1.f : u < -1.f ? -1.f : u * (1.5f - 0.5f * u * u);
        float am = u < 0.f ? -u : u;
        peak = am > peak ? am : peak;
        if (worn) u *= mk;
        if (half) {
            const uint32_t s = 2u * k;
            const float v0 = 0.5f * (prev + u);
            XF_EMIT(s, v0);
            XF_EMIT(s + 1u, u);
            prev = u;
        } else
            XF_EMIT(k, u);
    }
    if (half && (n & 1u))
        XF_EMIT(n - 1u, prev);
    c->wp = wp & (XF_WOW_LEN - 1u);
    c->d = worn ? d1 : 0.f;
    c->prev = prev;
    c->hp = hp < 1e-3f && hp > -1e-3f ? 0.f : hp;         /* (no denormals) */
    c->lp = lp < 1e-3f && lp > -1e-3f ? 0.f : lp;
    c->dc = dc < 1e-7f && dc > -1e-7f ? 0.f : dc;
    return peak;
}

/* the chain for a mode: its variants each a function of its own (noinline: one function calling xf_run twice let
 * the compiler merge them back into one loop testing the flag every sample, with spills: no gain on the FM1) */
#define XF_LOOP(MODE, WORN)                                                                                            \
    static __attribute__((noinline)) float xf_loop_##MODE##_##WORN(XfCh *c, const XfP *q, uint32_t n, float d1,         \
                                                                  const int32_t *a, const int32_t *b, float *z,        \
                                                                  int32_t *o, float *so)                               \
    {                                                                                                                  \
        return xf_run(c, q, n, d1, MODE, WORN, a, b, z, o, so);                                                        \
    }
#define XF_CHAIN(MODE)                                                                                                 \
    XF_LOOP(MODE, 0)                                                                                                   \
    XF_LOOP(MODE, 1)                                                                                                   \
    XF_LOOP(MODE, 2)                                                                                                   \
    static float xf_chain_##MODE(XfCh *c, const XfP *q, uint32_t n, const int32_t *a, const int32_t *b, float *z,     \
                                 int32_t *o, float *so)                                                                \
    {                                                                                                                  \
        float d1 = xf_wow_d(c, n, q->wow) * (XF_HALF ? 0.5f : 1.f);                                                    \
        if (q->asym != 0.f || c->dc != 0.f)                                                                            \
            return xf_loop_##MODE##_2(c, q, n, d1, a, b, z, o, so);                                                    \
        if (q->drive != 1.f || q->make != 1.f || d1 != 0.f || c->d != 0.f)                                             \
            return xf_loop_##MODE##_1(c, q, n, d1, a, b, z, o, so);                                                    \
        return xf_loop_##MODE##_0(c, q, n, d1, a, b, z, o, so);                                                        \
    }
XF_CHAIN(XS_L)
XF_CHAIN(XS_R)
XF_CHAIN(XS_M)

/* way j at a pass's start: its settings (xf.pp[j]: the gain and its step, the heated low-pass, the wear); now and
 * then the drifts' new targets. On core 0, before core 1 starts (lvl: the way's level) */
static void xf_begin(int j, uint32_t n, float lvl)
{
    XfP *q = &xf.pp[j];
    float k = (float)(fx_cross[j] > 127u ? 127u : fx_cross[j]) * (1.f / 127.f), g1 = XF_MAX * k * k, g = xf.g[j];
    q->g = g;
    q->dg = (g1 - g) * (1.f / (float)n);
    xf.g[j] = g1;
    q->hp_c = xf.hp_c;
    q->lp_c = xf.lp_c * (1.f - xf.heat * lvl);
    q->drive = xf.drive;
    q->asym = xf.asym;
    q->make = xf.make;
    q->wow = xf.wow;
    q->hp_h = 1.f - (1.f - q->hp_c) * (1.f - q->hp_c);    /* (the same pole at half the rate) */
    q->lp_h = 1.f - (1.f - q->lp_c) * (1.f - q->lp_c);
    q->on = 1;
    if ((++xf.nblk & 31u) == 0u)                          /* (~46 ms, either way on) */
        for (int c = 0; c < XF_NCH; c++) {
            xf.rng = xf.rng * 1103515245u + 12345u;
            xf.ch[c].wan_tgt = (float)(int16_t)(xf.rng >> 16) * (1.f / 32768.f);
        }
}

/* a level, held and falling ~0.3 s */
static inline float xf_level(float lvl, float peak) { return peak > lvl * 0.97f ? peak : lvl * 0.97f; }

float fx_cross_level(void) { return xf.lvl_dr > xf_c1.lvl ? xf.lvl_dr : xf_c1.lvl; }

void fx_cross_begin(uint32_t n)
{
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    xf.pp[FX_X_REV_DLY].on = xf.pp[FX_X_DLY_REV].on = 0;
    if (n == 0u)
        return;
    if (xf_on(FX_X_REV_DLY) || xf_on(FX_X_DLY_REV))
        xf_tone();
    if (xf_on(FX_X_REV_DLY))                              /* (the old order: REV > DLY's first) */
        xf_begin(FX_X_REV_DLY, n, xf_c1.lvl);
    else
        xf_c1.lvl *= 0.97f;
    if (xf_on(FX_X_DLY_REV))
        xf_begin(FX_X_DLY_REV, n, xf.lvl_dr);
    else
        xf.lvl_dr *= 0.97f;
}

void fx_tone_sync(MasterPerformancePreset *p)
{
    static uint8_t last_tone = 64u, last_hp = 0u;
    uint8_t hp = DELAY_HP(p), v = fx_live[FX_L_TONE];
    if (v != last_tone) {                                 /* turned: the thinner half is DELAY HP */
        hp = (uint8_t)tone_hp(v);
        p->delay_pingpong = (uint8_t)((p->delay_pingpong & 1u) | (uint8_t)(hp << 1));
    } else if (hp != last_hp) {                           /* loaded (or set elsewhere): the knob follows */
        v = hp ? (uint8_t)(64u + ((uint32_t)hp * 63u + 126u) / 127u) : 64u;
        fx_live[FX_L_TONE] = v;
    }
    last_tone = v;
    last_hp = hp;
}

void fx_cross_to_dly(int32_t *delay_in, int32_t *delay_in_r, uint32_t n)
{
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    uint32_t m = xf_c1.npend < n ? xf_c1.npend : n;       /* (core 1's, from the reverb's last pass) */
    for (uint32_t s = 0; s < m; s++) delay_in[s] += xf_c1.pend[s];
    if (delay_in_r) for (uint32_t s = 0; s < m; s++) delay_in_r[s] += xf_c1.pend[s];   /* (stereo: both sides) */
    xf_c1.npend = 0u;
}

void fx_cross_to_rev(const int32_t *delay_l, const int32_t *delay_r, int32_t *reverb_in, uint32_t n)
{
    float zl[FX_BLOCK_MAX];
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    const XfP *q = &xf.pp[FX_X_DLY_REV];
    if (n == 0u || !q->on)
        return;
    float pl = xf_chain_XS_L(&xf.ch[XF_CL], q, n, delay_l, 0, zl, 0, 0);
    float pr = xf_chain_XS_R(&xf.ch[XF_CR], q, n, delay_r, 0, zl, reverb_in, rv_side_in);
    xf.lvl_dr = xf_level(xf.lvl_dr, pl > pr ? pl : pr);
    rv_side_n = n;
#ifdef XF_NO_SIDE                                         /* (host A/B: the reverb's input mono again) */
    rv_side_n = 0u;
#endif
}

void fx_cross_rev_job(const int32_t *rev_l, const int32_t *rev_r, uint32_t n)
{
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    const XfP *q = &xf.pp[FX_X_REV_DLY];
    if (n == 0u || !q->on) {
        xf_c1.npend = 0u;
        return;
    }
    xf_c1.lvl = xf_level(xf_c1.lvl, xf_chain_XS_M(&xf.ch[XF_CM], q, n, rev_l, rev_r, 0, xf_c1.pend, 0));
    xf_c1.npend = n;
}

void fx_mix_block(const int32_t *delay_l, const int32_t *delay_r, int32_t *rev_l, int32_t *rev_r, uint32_t n)
{
    for (uint32_t s = 0; s < n; s++) {
        rev_l[s] = delay_l[s] + rev_l[s];
        rev_r[s] = delay_r[s] + rev_r[s];
    }
}

int32_t fx_process_sample(const MasterPerformancePreset *p, int32_t delay_in_q15, int32_t reverb_in_q15)
{
    int32_t wet_r;
    fx_process_block(p, &delay_in_q15, &reverb_in_q15, &wet_r, 1u);
    return reverb_in_q15;                       /* the left */
}
