#include "hardware/synth_libc.h"
#include "fx.h"
#include "hardware/mem_section.h"
#include "amplifier.h"                           /* amp_exp2f (DELAY HP) */

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
#define TAPE_LEN 16384u                   /* power of 2: 32 KB of int16 (was a 128 KB full-rate line) */
#define TAPE_MASK (TAPE_LEN - 1u)
#define TAPE_GAP_MAX (TAPE_LEN - 64u)     /* room for the wow + flutter excursion (< 56 cells) and the neighbour */
/* gap / speed slew: tau = 1024 samples, about 23 ms, settled in ~0.1 s (2026-10-26: was 93 ms, ~0.5 s to settle, so
 * a note played soon after a DELAY TIME change had its first echo at an in-between length and the repeats at another) */
#define TAPE_SLEW 10
#define WOW_INC 48696u                    /* 0.5 Hz phase increment at 44.1 kHz (2^32 / 44100 * f) */
#define FLUTTER_INC 613567u               /* 6.3 Hz */
#define WOW_STEP_Q8 96                    /* per wow step (tape_degrade): 127 = +-47.6 cells at full speed, 0.34 % pitch */
#define FLUTTER_STEP_Q8 16                /* per flutter step: 127 = +-7.9 cells, 0.71 % pitch */

#define PRE_LEN 4448u                     /* predelay: 127 * 35 = 4445 samples at most */
#define REV_COMB_TOTAL (1116u + 1188u + 1277u + 1356u)
#define REV_AP_TOTAL (556u + 441u)

static const uint16_t SFX_REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t SFX_REV_AP[2] = {556, 441};
/* sync divisions of the 120 BPM whole note (88200 samples), shortest to longest like unsynced DELAY TIME:
 * 1/24, 1/16, 1/12, 1/8, 1/6, 1/4, 1/3, 1/2 */
static const uint16_t DELAY_SYNC_DEN[8] = {24, 16, 12, 8, 6, 4, 3, 2};

static int16_t tape[TAPE_LEN] SYNTH_POOL;
static int16_t pre_buf[PRE_LEN] SYNTH_POOL;
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
    uint8_t last_bbd;                     /* the delay type the targets were computed for */
    /* BBD (DELAYTYPE_BBD): the input (anti-alias) and output (reconstruction) filters, each head's; the compander's
     * envelope; the noise generator */
    int32_t bb_aa1, bb_aa2, bb_rc1, bb_rc2, bb_hp, bb_rc1b, bb_rc2b, bb_hpb, bb_env;
    uint32_t bb_rng;
    int32_t run_lp, bb_run_lp;            /* the runaway's low-pass in the feedback (delay_fb), tape's and BBD's */
    float scoop, bb_scoop;                /* DELAY HP's low-pass in the feedback (delay_fb), tape's and BBD's */
    uint32_t seed_rng;                    /* the runaway's hiss seed (the tape's) */
} TapeState;

static TapeState tp;
static uint32_t pre_w;
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
/* The sends' limiters (2026-10-16): the summed sends of a few hot voices pass full scale (8 driven voices: +8 dB), and
 * the hard clamps that stood here clipped the chord before the reverb and the delay, the "digital buzz with reverb and
 * several voices" (measured: up to 21 % of the samples clipped). The master limiter's law instead: the envelope jumps
 * to a peak over the ceiling (no sample passes it) and falls back to the ceiling with tau ~0.1 s; the gain is ceiling
 * / envelope. Float: a divide only while it limits (8 cycles on the FM1, the integer one 24). */
#define SEND_LIM_CEIL 32767.f
#define SEND_LIM_REL 0.99977f                              /* per sample: tau 0.1 s at 44.1 kHz */
static float rv_lim_env = SEND_LIM_CEIL, dl_lim_env = SEND_LIM_CEIL;

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

void fx_init(void)
{
    memset(tape, 0, sizeof tape);
    memset(pre_buf, 0, sizeof pre_buf);
    memset(sfx_rev_comb, 0, sizeof sfx_rev_comb);
    memset(sfx_rev_ap, 0, sizeof sfx_rev_ap);
    memset(comb_lp, 0, sizeof comb_lp);
    memset(&tp, 0, sizeof tp);
    tp.gap_q16 = tp.gap_tgt_q16 = 64 << 16;
    tp.speed_q24 = tp.speed_tgt_q24 = 1 << 24;
    tp.last_len = 64u;
    tp.bb_rng = 0x2545F491u;
    tp.seed_rng = 0x6C8E9CF5u;
    pre_w = 0;
    memset(comb_i, 0, sizeof comb_i);
    memset(ap_i, 0, sizeof ap_i);
    memset(rv_ring, 0, sizeof rv_ring);
    rv_lim_env = dl_lim_env = SEND_LIM_CEIL;
    rv_w = 0;
    rv_r = RV_RING - FX_REV_LAG;                       /* the back reads the lag behind the front */
    tank_init();
    spring_init();
}

/* Delay time in samples, always at least 64 and at most 44133 (1.0 s; 2026-10-26, user: "reduce the maximum delay
 * time so that the range is more useful on the knob": was 64 + 512 a step, 1.47 s, ~11.6 ms a step, now ~7.9 ms;
 * the factory bank's times were rescaled to keep them). Sync picks a division of the 120 BPM grid. */
#define DELAY_TIME_STEP 347u
static uint32_t delay_len(const MasterPerformancePreset *p)
{
    if (p->delay_sync)
        return 88200u / DELAY_SYNC_DEN[((uint32_t)p->delay_time * 8u) >> 7];
    return 64u + (uint32_t)p->delay_time * DELAY_TIME_STEP;
}

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
static inline int32_t mr_mulq15(int32_t a, int32_t b)
{
    int32_t t = a * b;
    return (t + ((t >> 31) & 32767)) >> 15;
}
#define MR_L(n) ((uint32_t)((n) * 13u + 5u) / 10u)        /* Dattorro's length at 29.761 kHz, scaled 1.3x */
#define MR_EXC 12                                         /* modulation excursion, samples either side */
/* Every line lives in one ring of MR_RING samples with one pointer that falls by one a sample (Keith Barr's FV-1
 * layout): line k writes at p + its start and reads d samples back at p + start + d, so an access is one add, one
 * mask and the load, the offsets constants (2026-10-15: per-line pointers, lengths and write heads cost ~1000 cycles
 * a sample on the FM1 in loads alone). A line's region is its longest delay + 1. */
#define MR_RING 32768u
#define MR_MASK (MR_RING - 1u)
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
PS_STATIC_ASSERT(MR_S_END <= MR_RING, "the tank's lines fit its ring");

static int16_t mr_ring[MR_RING] SYNTH_POOL FX_LINE;      /* core 1, as everything of the tank's but its front */
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
#define MR_SH_LEN 2048u
#define MR_SHIMMER 19661                                  /* 0.6: how much of it goes back into the tank, Q15 */
static int16_t mr_sh[MR_SH_LEN] SYNTH_POOL FX_LINE;     /* (its state: in fx_tank_c1, above) */

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
    memset(mr_ring, 0, sizeof mr_ring);
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
    mr_sh_w = mr_sh_c = 0;
    mr_sh_lp = mr_sh_hp = mr_sh_aa1 = mr_sh_aa2 = 0;
}

/* one sample through the octave-up shifter. Head A is MR_SH_LEN - 1 - c samples behind the write (c counts up), so
 * it reads two samples on each sample: an octave up; head B is half a window round. Each head's gain is a triangle
 * of its own c, 0 where it jumps back, and the two always sum to 1. */
/* the shifter's state while a block runs: locals of tank_run (on this core a load or store of a static costs ~8
 * cycles, and the shifter touched six of them a sample) */
typedef struct { uint32_t w, c; int32_t aa1, aa2, hp, lp; } MrShift;

static inline __attribute__((always_inline)) int32_t mr_shift(MrShift *st, int32_t x)
{
    const uint32_t mask = MR_SH_LEN - 1u, half = MR_SH_LEN / 2u;
    uint32_t w = st->w, c = st->c;
    /* two poles at about 6 kHz before it: an octave up folds whatever is above 11 kHz back down, and at 14.7 kHz (a
     * third of the rate) onto itself, where the loop held a faint tone forever (measured) */
    st->aa1 += mr_mulq15(x - st->aa1, 18000);
    st->aa2 += mr_mulq15(st->aa1 - st->aa2, 18000);
    mr_sh[w] = (int16_t)clamp16(st->aa2);
    int32_t a = mr_sh[(w - (mask - c)) & mask], b = mr_sh[(w - (mask - ((c + half) & mask))) & mask];
    /* head A's triangle; head B's is its complement (B is half a window round, so ga + gb = 32768 always), and
     * a ga + b (32768 - ga) = 32768 b + (a - b) ga: one multiply, the same bits */
    int32_t ga = (int32_t)(c < half ? c : MR_SH_LEN - c) * (32768 / (int32_t)half);
    st->w = (w + 1u) & mask;
    st->c = (c + 1u) & mask;
    int32_t y = b + (((a - b) * ga) >> 15);              /* |a - b| <= 65535, ga <= 32768: within 2^31 */
    /* a high-pass first: an octave up leaves DC and the lowest notes where they were, so fed back they would only
     * add to the decay and the loop would run away (measured); only what really rises goes back */
    st->hp += mr_mulq15(y - st->hp, 900);                /* about 200 Hz */
    st->lp += mr_mulq15(y - st->hp - st->lp, 14000);     /* about 5 kHz */
    return st->lp;
}

/* a one-pole high-pass at about 20 Hz in each half of the loop: without it, rounding offsets build up by 1 / (1 -
 * decay) and wander at a few hertz (measured: a subsonic residue that never died at long decays). Its state is Q8:
 * at this small a coefficient a plain Q0 one would not move for differences under ~176 LSB. */
static inline int32_t mr_dc_block(int32_t *s, int32_t x)
{
    *s += (((x << 8) - *s) * 93) >> 15;                   /* |x << 8 - s| < 2^24: times 93 inside int32 */
    return x - (*s >> 8);
}

/* ring access, at the sample's pointer p: the line starting at s, d samples back; and its write */
#define MR_RD(s, d) ((int32_t)mr_ring[(p + (s) + (d)) & MR_MASK])
#define MR_WR(s, v) (mr_ring[(p + (s)) & MR_MASK] = (int16_t)clamp16(v))

/* allpass on the line at s with delay d, coefficient g (Q15, either sign): v = x + g dl, out = dl - g v (|H| = 1) */
static inline __attribute__((always_inline)) int32_t mr_ap(uint32_t p, uint32_t s, uint32_t d, int32_t x, int32_t g)
{
    int32_t dl = MR_RD(s, d), v = clamp16(x + mr_mulq15(dl, g));
    MR_WR(s, v);
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
    int32_t dl = a + (((b - a) * fr) >> 15);
    int32_t v = clamp16(x + mr_mulq15(dl, g));
    MR_WR(s, v);
    return dl - mr_mulq15(v, g);
}

/* The send into the tank, Q15, and the output's matching shift (the output gain MR_OUT_GAIN x 2^(13 - MR_OUT_SH)).
 * 2026-10-16: 1/4 (was 1/2) and 1.2 (was 0.6), the tank 6 dB lower inside: a full-scale send of a hot chord at long
 * decays drove its int16 lines into the guard (ducking 50-60 % of the time) and their clamps, a buzz. Measured on the
 * hottest sends (6-8 driven voices at full send, decay 110): the guard now rests except TUMULUS ~19 % on the worst.
 * 12 dB lower kept even that at rest, but quiet tails lost ~2 dB to the int16 lines' rounding (6 dB: <= 0.7 dB). */
#ifndef MR_IN_GAIN
#define MR_IN_GAIN 8192
#endif
#ifndef MR_OUT_SH
#define MR_OUT_SH 12
#endif
#define MR_SZ(b, size) ((((b) * (size)) >> 8) ? (((b) * (size)) >> 8) : 1u)   /* a delay at size (Q8), at least 1 */
/* the input diffusers' allpass, on their ring (v = x + g dl, out = dl - g v) */
static inline __attribute__((always_inline)) int32_t mr_ap_in(uint32_t p, uint32_t s, uint32_t d, int32_t x, int32_t g)
{
    int32_t dl = mr_in_ring[(p + s + d) & (MR_IN_RING - 1u)], v = clamp16(x + mr_mulq15(dl, g));
    mr_in_ring[(p + s) & (MR_IN_RING - 1u)] = (int16_t)v;
    return dl - mr_mulq15(v, g);
}

/* the tank's input: the send into it, then four allpasses diffusing it (0.75, 0.75, 0.625, 0.625), in place */
static inline __attribute__((always_inline)) void tank_front(int32_t *x, uint32_t n, const uint32_t size)
{
    uint32_t p = mr_in_p;
    for (uint32_t s = 0; s < n; s++) {
        int32_t in = mr_mulq15(x[s], MR_IN_GAIN);
        in = mr_ap_in(p, MR_S_IN1, MR_SZ(MR_B_IN1, size), in, 24576);
        in = mr_ap_in(p, MR_S_IN2, MR_SZ(MR_B_IN2, size), in, 24576);
        in = mr_ap_in(p, MR_S_IN3, MR_SZ(MR_B_IN3, size), in, 20480);
        x[s] = mr_ap_in(p, MR_S_IN4, MR_SZ(MR_B_IN4, size), in, 20480);
        p = (p - 1u) & (MR_IN_RING - 1u);
    }
    mr_in_p = p;
}

#define MR_RATE_L 38956u                                  /* spin: 0.40 Hz */
#define MR_RATE_R 53565u                                  /* 0.55 Hz */
#define MR_OUT_GAIN 19661                                 /* 0.6, Dattorro's */

/* The tank over a block, its size a constant at each call (inlined twice: every offset folds to a constant). x: the
 * reverb send in, the left output out; r: the right output. */
static inline __attribute__((always_inline)) void tank_run(int32_t *x, int32_t *r, uint32_t n, int32_t decay,
                                                           int32_t damp, int moving, int celestial, const uint32_t size)
{
    uint32_t p = mr_p;
    int32_t lp_l = mr_lp_l, lp_r = mr_lp_r, mod_l = mr_mod_l, mod_r = mr_mod_r, duck = mr_duck;
    int32_t dg = mr_duck_g, dg_step = mr_duck_step;
    int32_t dec = mr_mulq15(decay, dg);
    int32_t dc_l = mr_dc_l, dc_r = mr_dc_r;
    uint32_t chunk = mr_wander_n;
    MrShift sh = {mr_sh_w, mr_sh_c, mr_sh_aa1, mr_sh_aa2, mr_sh_hp, mr_sh_lp};
    if (!moving)
        mod_l = mod_r = 0;                                /* (a still tank: none left from MOD or CELESTIAL) */
    for (uint32_t s = 0; s < n; s++) {
        if (dg_step) {                                    /* the guard gliding */
            dg += dg_step;
            dec = mr_mulq15(decay, dg);
        }
        int32_t in = mr_mulq15(x[s], dg);                 /* the front's diffused send (fx_reverb_front), ducked */

        int32_t from_r = MR_RD(MR_S_D2R, MR_SZ(MR_B_D2R, size)), from_l = MR_RD(MR_S_D2L, MR_SZ(MR_B_D2L, size));
        if ((chunk++ & (MR_CHUNK - 1u)) == 0u) {
            /* once a chunk: the modulation (its step, ~0.01 sample of delay a chunk, is far below hearing) and the
             * guard, which reads the lines' ends: louder than MR_DUCK_AT ducks fast (-0.5 dB a chunk), else it
             * comes back slowly (tau ~0.1 s); the gain glides to the new target over the chunk */
            int32_t pk = from_l < 0 ? -from_l : from_l, pr = from_r < 0 ? -from_r : from_r;
            if (pr > pk) pk = pr;
            if (pk > MR_DUCK_AT)
                duck -= duck >> 4;
            else if (duck < 32767)
                duck += ((32767 - duck) >> 8) + 1;
            if (duck > 32767) duck = 32767;
            dg_step = (duck - dg) >> 4;                   /* MR_CHUNK samples to the target */
            if (dg_step == 0 && dg != duck) {             /* (the last few LSB: 0.004 dB at most) */
                dg = duck;
                dec = mr_mulq15(decay, dg);
            }
            if (moving) {
                mr_ph_l += MR_RATE_L * MR_CHUNK;
                mr_ph_r += MR_RATE_R * MR_CHUNK;
                if (chunk >= 4410u) {                     /* wander: a new random target every 0.1 s ... */
                    chunk = 1;                    /* (as it is just after a chunk's count) */
                    for (int k = 0; k < 2; k++) {
                        mr_rng = mr_rng * 1103515245u + 12345u;
                        mr_wander_tgt[k] = (int16_t)(mr_rng >> 16);
                    }
                }
                for (int k = 0; k < 2; k++)               /* ... approached slowly (tau ~ 0.19 s), never stepped */
                    mr_wander[k] += (mr_wander_tgt[k] - mr_wander[k]) >> 9;
                mod_l = (para_sine(mr_ph_l) + mr_wander[0]) >> 1;   /* spin + wander, each half the excursion */
                mod_r = (para_sine(mr_ph_r) + mr_wander[1]) >> 1;
            }
        }
        int32_t shim = 0;                                 /* CELESTIAL: both ends an octave up, back into both halves */
        if (celestial)
            shim = mr_mulq15(mr_shift(&sh, (from_l + from_r) >> 1), MR_SHIMMER);

        /* left half */
        int32_t a = mr_ap_swept(p, MR_S_AP1L, MR_SZ(MR_B_AP1L, size), mod_l, in + shim + mr_mulq15(from_r, dec),
                                -22938);                  /* -0.7 */
        int32_t o = MR_RD(MR_S_D1L, MR_SZ(MR_B_D1L, size));
        MR_WR(MR_S_D1L, a);
        lp_l = o + mr_mulq15(lp_l - o, damp);
        MR_WR(MR_S_D2L, mr_ap(p, MR_S_AP2L, MR_SZ(MR_B_AP2L, size),
                              mr_mulq15(mr_dc_block(&dc_l, lp_l), dec), 16384));   /* 0.5 */

        /* right half */
        int32_t b = mr_ap_swept(p, MR_S_AP1R, MR_SZ(MR_B_AP1R, size), mod_r, in + shim + mr_mulq15(from_l, dec),
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
        x[s] = ((yl >> 2) * MR_OUT_GAIN) >> MR_OUT_SH;
        r[s] = ((yr >> 2) * MR_OUT_GAIN) >> MR_OUT_SH;
        p = (p - 1u) & MR_MASK;
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
}

/* x: the reverb send in, the left output out; r: the right output. type: REVTYPE_MOD / _CELESTIAL / _DOLMEN /
 * _TUMULUS */
static void tank_reverb_block(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n, uint8_t type)
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
    if (type == REVTYPE_DOLMEN)
        tank_run(x, r, n, decay, damp, 0, 0, 102u);       /* 0.4 size */
    else if (celestial)                                   /* one copy each: the type's branches fold away */
        tank_run(x, r, n, decay, damp, 1, 1, 256u);
    else if (moving)
        tank_run(x, r, n, decay, damp, 1, 0, 256u);
    else
        tank_run(x, r, n, decay, damp, 0, 0, 256u);
}

/* ---- SPRING (2026-10-16): two springs, after the dispersive models of spring tanks (Valimaki, Parker and Abel,
 * "Parametric spring reverberation effect", JAES 2010): a wave along a helical spring travels slower the lower its
 * pitch, so every echo of the spring comes back as a falling chirp (the "drip", the laser "pew"), smeared further on
 * each trip. Each spring: a loop of a delay (one trip), a low-pass (REVERB DAMP), the decay (REVERB TIME) and
 * SP_STAGES stretched first-order allpasses, (a + z^-K) / (1 + a z^-K) with a < 0, which delay the lows more than the
 * highs: the dispersion (measured: 170 Hz arrives 5.3 ms after 4 kHz in the first echo, twice that in the second).
 * The springs' trips (34.7 and 43.2 ms) differ, so their flutter echoes do not line up; each feeds one side, a little
 * of the other mixed in. The front high-passes the send (a spring tank has little bass) and scales it in. TIME 0 ..
 * 127: RT60 ~0.3 .. ~4.5 s; level as PLATE's.
 * Cost (2026-10-16): 12 stages at a = -0.78 (16 at -0.7 chirp alike, but cost the FM1 0.8 % more than CELESTIAL:
 * float adds cost it ~6 cycles); host 14.1 ns a sample on core 1 and 3.0 on core 0, CELESTIAL 18.7 and 6.8. One trip
 * is longer than a block, so the loop runs a whole block at a time: the delay's reads for the block, the filters,
 * the allpass stages two at a time with their histories in registers (a load and a store per sample per pair instead
 * of per stage), then the delay's writes. Lines int16 in one 4096 ring (FV-1 layout, as the tank). */
#define SP_RING 4096u
#define SP_MASK (SP_RING - 1u)
#define SP_D1 1531u                                       /* one trip, samples */
#define SP_D2 1907u
#define SP_S1 0u                                          /* each line's region start */
#define SP_S2 (SP_D1 + 1u)
PS_STATIC_ASSERT(SP_S2 + SP_D2 + 1u <= SP_RING && SP_D1 >= FX_BLOCK_MAX && SP_D2 >= FX_BLOCK_MAX,
                 "the springs fit their ring, and a trip is at least a block");
#ifndef SP_STAGES
#define SP_STAGES 12                                      /* allpass stages a spring (even: two a pass) */
#endif
#define SP_K 3                                            /* the stretch: z^-3 */
#ifndef SP_A
#define SP_A (-0.78f)                                     /* per stage 24 samples at DC, 0.4 at 7.35 kHz */
#endif
#define SP_IN_GAIN 0.2f                                   /* the send into the loops (headroom in the lines) */
#ifndef SP_OUT_GAIN
#define SP_OUT_GAIN 0.69f                                 /* x 4 / 4: each side 3/4 its spring + 1/4 the other's */
#endif
#define SP_HP_C 0.0233f                                   /* the front's high-pass, ~165 Hz */
#define SP_TINY 1e-4f                                     /* a state this small (LSB) is flushed to 0 at a block's end */
PS_STATIC_ASSERT((SP_STAGES & 1) == 0, "the stages run in pairs");

/* The loop is float (2026-10-16): in int, each stage's rounding (up to 1 LSB, 12 stages) went round the loop, and
 * the tail never died (measured: ~200 LSB for ever at TIME 127, a faint tone). In float the stages' error is ~2^-16
 * LSB; the line writes truncate toward zero to int16, a dead zone, so a tail ends in exact silence; and states under
 * SP_TINY are flushed at each block's end, so the FPU never meets a denormal. (The FM1's single-precision multiply
 * costs ~7 cycles, the integer one ~10.) */
static int16_t sp_ring[SP_RING] SYNTH_POOL FX_LINE;     /* core 1 (sp_p, sp_sp: fx_spring_c1, below) */
static float sp_hp;                                       /* the front's high-pass (core 0); float: an integer one
                                                           * stuck a few LSB off zero and fed the loops a DC */
typedef struct {
    float lp;                                             /* the loop's low-pass (no DC blocker: the send comes in
                                                           * high-passed and nothing in the loop adds a DC) */
    float h[SP_STAGES / 2][3][SP_K];                      /* each pair's last K inputs, middles and outputs */
} SpSpring;
static struct FX_LINE {                               /* core 1: the springs' state */
    SpSpring sp[2];
    uint32_t p;
} fx_spring_c1;
#define sp_sp (fx_spring_c1.sp)
#define sp_p (fx_spring_c1.p)

static void spring_init(void)
{
    memset(sp_ring, 0, sizeof sp_ring);
    memset(sp_sp, 0, sizeof sp_sp);
    sp_p = 0;
    sp_hp = 0.f;
}

static inline float sp_flush(float v) { return v < SP_TINY && v > -SP_TINY ? 0.f : v; }

/* two stretched allpasses over a block, in place: y[n] = x[n - K] + a (x[n] - y[n - K]) twice. h: the histories of
 * the pair's input, middle and output, newest first. Each pair keeps its own input history: a pair's output history
 * holds the same values, but the pairs run one after the other over the block, so by the time the next pair starts
 * this one has already moved its history to the block's end */
static inline __attribute__((always_inline)) void sp_pair(float *b, uint32_t n, float (*h)[SP_K])
{
    float x1 = h[0][0], x2 = h[0][1], x3 = h[0][2];
    float m1 = h[1][0], m2 = h[1][1], m3 = h[1][2];
    float y1 = h[2][0], y2 = h[2][1], y3 = h[2][2];
    for (uint32_t s = 0; s < n; s++) {
        float x = b[s];
        float m = x3 + SP_A * (x - m3);
        float y = m3 + SP_A * (m - y3);
        x3 = x2; x2 = x1; x1 = x;
        m3 = m2; m2 = m1; m1 = m;
        y3 = y2; y2 = y1; y1 = y;
        b[s] = y;
    }
    h[0][0] = sp_flush(x1); h[0][1] = sp_flush(x2); h[0][2] = sp_flush(x3);
    h[1][0] = sp_flush(m1); h[1][1] = sp_flush(m2); h[1][2] = sp_flush(m3);
    h[2][0] = sp_flush(y1); h[2][1] = sp_flush(y2); h[2][2] = sp_flush(y3);
}

/* one spring over a block: in, the front's send; out, its echoes (the trip's end, as read) */
static inline __attribute__((always_inline)) void spring_run(SpSpring *sp, const int32_t *in, float *out, uint32_t n,
                                                             const uint32_t start, const uint32_t d, float decay,
                                                             float lp_c)
{
    float b[FX_BLOCK_MAX];
    uint32_t p = sp_p;
    float lp = sp->lp;
    for (uint32_t s = 0; s < n; s++) {                    /* the trip's end, filtered, decayed, plus the send */
        float o = (float)sp_ring[(p - s + start + d) & SP_MASK];
        out[s] = o;
        lp += lp_c * (o - lp);
        b[s] = (float)in[s] + decay * lp;
    }
    sp->lp = sp_flush(lp);
    for (uint32_t k = 0; k < SP_STAGES; k += 2)           /* the dispersion */
        sp_pair(b, n, sp->h[k / 2u]);
    for (uint32_t s = 0; s < n; s++)                      /* into the trip (read above at least a block later); the cast
                                                           * truncates toward zero */
        sp_ring[(p - s + start) & SP_MASK] = (int16_t)clamp16((int32_t)b[s]);
}

/* the front's part: the send high-passed and scaled into the loops, in place */
static void spring_front(int32_t *x, uint32_t n)
{
    float hp = sp_hp;
    for (uint32_t s = 0; s < n; s++) {
        float v = (float)x[s];
        hp += SP_HP_C * (v - hp);
        x[s] = (int32_t)((v - hp) * SP_IN_GAIN);
    }
    sp_hp = sp_flush(hp);
}

/* the back: x the front's send in, the left output out; r the right */
static void spring_block(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n)
{
    int32_t knob_t = p->reverb_decay > 127u ? 127 : p->reverb_decay, knob_d = p->reverb_damp > 127u ? 127 : p->reverb_damp;
    float decay = 0.52f + (float)knob_t * (0.44f / 127.f);   /* a trip's gain 0.52 .. 0.96 */
    float lp_c = 0.51f - (float)knob_d * (0.32f / 127.f);    /* the loop's low-pass ~5 kHz .. ~1.5 kHz */
    float o1[FX_BLOCK_MAX], o2[FX_BLOCK_MAX];
    spring_run(&sp_sp[0], x, o1, n, SP_S1, SP_D1, decay, lp_c);
    spring_run(&sp_sp[1], x, o2, n, SP_S2, SP_D2, decay, lp_c);
    sp_p = (sp_p - n) & SP_MASK;
    for (uint32_t s = 0; s < n; s++) {                    /* each side its spring, a quarter of the other */
        x[s] = (int32_t)((o1[s] * 3.f + o2[s]) * SP_OUT_GAIN);
        r[s] = (int32_t)((o2[s] * 3.f + o1[s]) * SP_OUT_GAIN);
    }
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
    *filter = d;
}

#define BBD_N 8192u                       /* the BBD's stages (bbd_delay_block): four MN3008s in a chain */

/* the heads' targets for p's delay: the gap and the speed. Up to gap_max cells (the tape's whole loop, or the BBD's
 * stages) at full speed; longer delays slow down. Only a change of DELAY TIME / SYNC / PING PONG / TYPE pays for the
 * division. */
static void delay_targets(const MasterPerformancePreset *p, int pp, int bbd)
{
    uint32_t len = delay_len(p) << pp, gap_max = bbd ? BBD_N : TAPE_GAP_MAX;
    if (len != tp.last_len || (uint8_t)bbd != tp.last_bbd) {
        tp.last_len = len;
        tp.last_bbd = (uint8_t)bbd;
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
#define DELAY_HP_TOP_BBD 550000.f
typedef struct { int32_t lo, hi, hi2, run_c; float scoop_c; int runaway; } DelayFb;
static DelayFb delay_fb(uint32_t knob, uint32_t degrade, uint32_t hp, int bbd)
{
    DelayFb f;
    /* the top: 1.22 at DEGRADE 0 .. 1.62 at 127 (a worn tape's or BBD's filters lose more each pass: without this,
     * DEGRADE 127 never howled) */
    f.scoop_c = hp ? DELAY_HP_C0 * amp_exp2f((float)(int32_t)(hp - 1u) * DELAY_HP_OCT) : 0.f;
    int32_t top = 40000 + (int32_t)(degrade > 127u ? 127u : degrade) * 104 +
                  (int32_t)(f.scoop_c * (bbd ? DELAY_HP_TOP_BBD : DELAY_HP_TOP_TAPE));
    if (top > 98301) top = 98301;                          /* (lo, hi, hi2 <= 32767: each Q15 multiply < 2^31) */
    int32_t k = knob > 127u ? 127 : (int32_t)knob, g = k <= 100 ? k * 250 : 25000 + (k - 100) * (top - 25000) / 27;
    f.lo = g > 32767 ? 32767 : g;
    f.hi = g - f.lo > 32767 ? 32767 : g - f.lo;
    f.hi2 = g - f.lo - f.hi;                               /* (only DELAY HP's top reaches it) */
    f.runaway = k > 100;
    f.run_c = 32767 - (k > 100 ? (k - 100) * 900 : 0);     /* 127: 8467, ~2 kHz */
    return f;
}

/* The tape echo over a block. PING PONG: a second play head halfway along the gap, the first echo from it on the
 * left, the next from the far head on the right, which also feeds the record head (so each side's echoes are a whole
 * delay apart and the pair repeats every 2 delays: the tape runs as for a delay twice as long). Each head has its own
 * head-loss and high-pass filters. Off: one head, the same echo both sides. */
static void tape_delay_block(const MasterPerformancePreset *p, const int32_t *in, int32_t *out_l, int32_t *out_r,
                             uint32_t n)
{
    const int pp = DELAY_PINGPONG(p) != 0u;
    delay_targets(p, pp, 0);
    int32_t wow, flutter;
    uint32_t filter;
    tape_degrade(p->delay_degrade, &wow, &flutter, &filter);
    const int32_t gap_tgt = tp.gap_tgt_q16, speed_tgt = tp.speed_tgt_q24;
    const int32_t wow_q8 = wow * WOW_STEP_Q8, flutter_q8 = flutter * FLUTTER_STEP_Q8;
    const int32_t loss_c = head_loss_q15(filter), hp_c = tape_hp_q15(filter);
    const DelayFb fb = delay_fb(p->delay_feedback, p->delay_degrade, DELAY_HP(p), 0);             /* (delay_fb: past 100 it self-oscillates) */
    int32_t run_lp = tp.run_lp;
    float scoop = fb.scoop_c != 0.f ? tp.scoop : 0.f;
    uint32_t seed = tp.seed_rng;
    uint32_t wpos = tp.wpos_q16, wow_ph = tp.wow_ph, flutter_ph = tp.flutter_ph;
    int32_t gap = tp.gap_q16, speed = tp.speed_q24, aa_lp = tp.aa_lp, loss_lp = tp.loss_lp, hp_lp = tp.hp_lp;
    int32_t loss2 = tp.loss2_lp, hp2 = tp.hp2_lp;

    for (uint32_t s = 0; s < n; s++) {
        gap += (gap_tgt - gap) >> TAPE_SLEW;
        speed += (speed_tgt - speed) >> TAPE_SLEW;
        int32_t speed_q16 = speed >> 8;

        /* wow and flutter move the play head; scaled by speed so the pitch wobble is the same at any tape speed */
        wow_ph += WOW_INC;
        flutter_ph += FLUTTER_INC;
        int32_t mod_q8 = (para_sine(wow_ph) * wow_q8 + para_sine(flutter_ph) * flutter_q8) >> 15;
        int32_t mod_q16 = (mod_q8 * speed_q16) >> 8;

        /* play head: linear interpolation between the two cells around it */
        uint32_t r = wpos - (uint32_t)gap - (uint32_t)mod_q16;
        uint32_t i = r >> 16;
        int32_t frac = (int32_t)((r >> 1) & 0x7FFFu);
        int32_t a = tape[i & TAPE_MASK];
        int32_t b = tape[(i + 1u) & TAPE_MASK];
        int32_t play = a + (((b - a) * frac) >> 15);

        /* playback head loss and high-pass: this is both the echo output and the feedback source */
        loss_lp += sfx_mulq15(play - loss_lp, loss_c);
        hp_lp += sfx_mulq15(loss_lp - hp_lp, hp_c);
        int32_t o = loss_lp - hp_lp;

        if (pp) {                                         /* the near head, halfway: the left side's echoes */
            uint32_t r2 = wpos - ((uint32_t)gap >> 1) - (uint32_t)mod_q16;
            uint32_t i2 = r2 >> 16;
            int32_t f2 = (int32_t)((r2 >> 1) & 0x7FFFu);
            int32_t a2 = tape[i2 & TAPE_MASK];
            int32_t b2 = tape[(i2 + 1u) & TAPE_MASK];
            int32_t play2 = a2 + (((b2 - a2) * f2) >> 15);
            loss2 += sfx_mulq15(play2 - loss2, loss_c);
            hp2 += sfx_mulq15(loss2 - hp2, hp_c);
            out_l[s] = loss2 - hp2;
            out_r[s] = o;
        } else {
            out_l[s] = out_r[s] = o;
        }

        /* record: input plus feedback, saturated, then band-limited for the tape speed */
        int32_t fbs = o, hiss = 0;
        if (fb.runaway) {                                 /* past 100: the darkening low-pass and the hiss seed */
            run_lp += mr_mulq15(o - run_lp, fb.run_c);
            fbs = run_lp;
            seed = seed * 1664525u + 1013904223u;
            hiss = (int32_t)(int16_t)(seed >> 16) >> 12;
        }
        if (fb.scoop_c != 0.f) {                          /* DELAY HP */
            scoop += ((float)fbs - scoop) * fb.scoop_c;
            fbs -= (int32_t)scoop;
        }
        int32_t rec = tape_sat(in[s] + sfx_mulq15(fbs, fb.lo) + sfx_mulq15(fbs, fb.hi) + sfx_mulq15(fbs, fb.hi2) + hiss);
        int32_t aa_q15 = (speed_q16 >> 1) + (speed_q16 >> 2);    /* 0.75 x speed: the slowed tape's Nyquist */
        if (aa_q15 > 32767) aa_q15 = 32767;
        aa_lp += sfx_mulq15(rec - aa_lp, aa_q15);

        /* the record head moves at most one cell per sample, so it writes at most one cell */
        uint32_t old_cell = wpos >> 16;
        wpos += (uint32_t)speed_q16;
        if ((wpos >> 16) != old_cell)
            tape[(wpos >> 16) & TAPE_MASK] = (int16_t)clamp16(aa_lp);
    }
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
}

/* BBD (2026-10-16, DELAY TYPE): a bucket-brigade echo, dark. A BBD chip holds a fixed number of samples and is
 * clocked slower for a longer delay; here BBD_N = 8192 stages (2026-10-26, four MN3008s in a chain, as a long-delay
 * pedal's: with one chip's 2048 a 1 s echo clocked at ~2 kHz and sounded bitcrushed): up to 186 ms it runs at the
 * full rate, at the 1.0 s maximum its clock is ~8 kHz. The tape's mechanism is exactly that (the record head writes a
 * cell, a bucket, each clock tick; the gap is the stages), so it shares the tape, the heads and their slew, wow and
 * flutter (the clock's wobble). The BBD's own:
 *   - the buckets read with linear interpolation, as the tape's (2026-10-26; a held step let the clock's images
 *     through: grit);
 *   - 2-pole anti-alias and reconstruction low-passes, dark: from TAPE DEGRADE, ~5 kHz at 0, ~2.7 kHz at 36 (the
 *     default), ~0.9 kHz at 127, held below ~0.35 x the clock (a long echo at a low DEGRADE no longer aliases);
 *     every repeat goes through both again;
 *   - the compander's breath: noise that rises and falls with the echo (none in silence), more with DEGRADE;
 *   - its multiplies round toward zero (mr_mulq15), so a tail ends in silence.
 * Same cost class as the tape: two more one-poles a head and a noise draw a sample. */
static void bbd_delay_block(const MasterPerformancePreset *p, const int32_t *in, int32_t *out_l, int32_t *out_r,
                            uint32_t n)
{
    const int pp = DELAY_PINGPONG(p) != 0u;
    delay_targets(p, pp, 1);
    int32_t wow, flutter;
    uint32_t filter;
    tape_degrade(p->delay_degrade, &wow, &flutter, &filter);
    const int32_t inv = 128 - (int32_t)filter;
    const int32_t gap_tgt = tp.gap_tgt_q16, speed_tgt = tp.speed_tgt_q24;
    int32_t lp_c = 4000 + ((inv * inv * 12700) >> 14);         /* both filters' one-poles, Q15 */
    const int32_t lp_cap = speed_tgt >> 8;                       /* ~0.35 x the clock (2 x speed, Q15) */
    if (lp_c > lp_cap) lp_c = lp_cap;
    const int32_t hp_c = tape_hp_q15(filter), nz_k = 8 + (int32_t)filter;
    const int32_t wow_q8 = wow * WOW_STEP_Q8, flutter_q8 = flutter * FLUTTER_STEP_Q8;
    const DelayFb fb = delay_fb(p->delay_feedback, p->delay_degrade, DELAY_HP(p), 1);
    int32_t run_lp = tp.bb_run_lp;
    float scoop = fb.scoop_c != 0.f ? tp.bb_scoop : 0.f;
    uint32_t wpos = tp.wpos_q16, wow_ph = tp.wow_ph, flutter_ph = tp.flutter_ph, rng = tp.bb_rng;
    int32_t gap = tp.gap_q16, speed = tp.speed_q24;
    int32_t aa1 = tp.bb_aa1, aa2 = tp.bb_aa2, rc1 = tp.bb_rc1, rc2 = tp.bb_rc2, hp = tp.bb_hp, env = tp.bb_env;
    int32_t rc1b = tp.bb_rc1b, rc2b = tp.bb_rc2b, hpb = tp.bb_hpb;

    for (uint32_t s = 0; s < n; s++) {
        gap += (gap_tgt - gap) >> TAPE_SLEW;
        speed += (speed_tgt - speed) >> TAPE_SLEW;
        int32_t speed_q16 = speed >> 8;
        wow_ph += WOW_INC;
        flutter_ph += FLUTTER_INC;
        int32_t mod_q8 = (para_sine(wow_ph) * wow_q8 + para_sine(flutter_ph) * flutter_q8) >> 15;
        int32_t mod_q16 = (mod_q8 * speed_q16) >> 8;

        /* the last bucket, interpolated, then the reconstruction filter and the high-pass */
        uint32_t r = wpos - (uint32_t)gap - (uint32_t)mod_q16;
        int32_t a = tape[(r >> 16) & TAPE_MASK], fr = (int32_t)((r >> 1) & 0x7FFFu);
        int32_t play = a + (((tape[((r >> 16) + 1u) & TAPE_MASK] - a) * fr) >> 15);
        rc1 += mr_mulq15(play - rc1, lp_c);
        rc2 += mr_mulq15(rc1 - rc2, lp_c);
        hp += mr_mulq15(rc2 - hp, hp_c);
        int32_t o = rc2 - hp;
        if (pp) {                                         /* the near head, halfway: the left side's echoes */
            uint32_t r2 = wpos - ((uint32_t)gap >> 1) - (uint32_t)mod_q16;
            int32_t a2 = tape[(r2 >> 16) & TAPE_MASK], f2 = (int32_t)((r2 >> 1) & 0x7FFFu);
            int32_t play2 = a2 + (((tape[((r2 >> 16) + 1u) & TAPE_MASK] - a2) * f2) >> 15);
            rc1b += mr_mulq15(play2 - rc1b, lp_c);
            rc2b += mr_mulq15(rc1b - rc2b, lp_c);
            hpb += mr_mulq15(rc2b - hpb, hp_c);
            out_l[s] = rc2b - hpb;
            out_r[s] = o;
        } else {
            out_l[s] = out_r[s] = o;
        }

        /* the compander's breath: noise riding the echo's envelope (tau ~3 ms), into the buckets with the input */
        env += ((o < 0 ? -o : o) - env) >> 7;
        rng = rng * 1664525u + 1013904223u;
        int32_t noise = mr_mulq15((int32_t)(int16_t)(rng >> 16), (env * nz_k) >> 10);

        /* record: input plus feedback plus the noise, saturated, through the anti-alias filter; a bucket each tick */
        int32_t fbs = o;
        if (fb.runaway) {                                 /* past 100: the darkening low-pass and the hiss seed */
            run_lp += mr_mulq15(o - run_lp, fb.run_c);
            fbs = run_lp;
            noise += (int32_t)(int16_t)(rng >> 16) >> 9;   /* (8x the tape's: its filters round toward zero and
                                                           * swallowed a smaller seed; measured: from silence at 127 both
                                                           * reach their howl in ~20 s) */
        }
        if (fb.scoop_c != 0.f) {                          /* DELAY HP */
            scoop += ((float)fbs - scoop) * fb.scoop_c;
            fbs -= (int32_t)scoop;
        }
        int32_t rec = tape_sat(in[s] + mr_mulq15(fbs, fb.lo) + mr_mulq15(fbs, fb.hi) + mr_mulq15(fbs, fb.hi2) + noise);
        aa1 += mr_mulq15(rec - aa1, lp_c);
        aa2 += mr_mulq15(aa1 - aa2, lp_c);
        uint32_t old_cell = wpos >> 16;
        wpos += (uint32_t)speed_q16;
        if ((wpos >> 16) != old_cell)
            tape[(wpos >> 16) & TAPE_MASK] = (int16_t)clamp16(aa2);
    }
    tp.wpos_q16 = wpos;
    tp.wow_ph = wow_ph;
    tp.flutter_ph = flutter_ph;
    tp.gap_q16 = gap;
    tp.speed_q24 = speed;
    tp.bb_rng = rng;
    tp.bb_aa1 = aa1; tp.bb_aa2 = aa2; tp.bb_rc1 = rc1; tp.bb_rc2 = rc2; tp.bb_hp = hp; tp.bb_env = env;
    tp.bb_rc1b = rc1b; tp.bb_rc2b = rc2b; tp.bb_hpb = hpb;
    tp.bb_run_lp = run_lp;
    tp.bb_scoop = scoop > 1e-3f || scoop < -1e-3f ? scoop : 0.f;
}

/* ROOM's multiplies round toward zero (mr_mulq15, 2026-10-15): with sfx_mulq15's floor the combs held a residue for
 * ever, up to ~90 LSB at the top of REVERB TIME (measured). */
/* the predelay (in place), then the reverb of p's type: x is the reverb send, replaced by the left output; r gets the
 * right. MOD, DOLMEN and TUMULUS: the tank, stereo. The others: the ROOM network, mono (decay sets the comb feedback,
 * damping the comb low-pass, spec 4.6 EFFECTS) */
/* The reverb in two halves (2026-10-15): the front (the send clamped, the predelay, a tank's input diffusion), which
 * feeds nothing back, and the back (the tank, or the ROOM network). The front runs FX_REV_LAG samples ahead through
 * rv_ring: on the FM1 core 0 runs this pass's front beside the delay while core 1 runs the back on the front's output
 * of a pass ago; they touch different places of the ring (n <= the lag), so one core or two give the same sums.
 * The lag comes off the predelay: from PREDELAY 4 up its time is exact, below that the reverb starts up to 2.9 ms
 * later than before. */

static int reverb_is_tank(uint8_t t)
{
    return t == REVTYPE_MOD || t == REVTYPE_CELESTIAL || t == REVTYPE_DOLMEN || t == REVTYPE_TUMULUS;
}

void fx_reverb_front(const MasterPerformancePreset *p, const int32_t *send, uint32_t n)
{
    int32_t x[FX_BLOCK_MAX];
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    send_limit(&rv_lim_env, send, x, n);
    uint32_t pre = (uint32_t)p->reverb_predelay * 35u;
    pre = pre > FX_REV_LAG ? pre - FX_REV_LAG : 0u;
    if (pre != 0u) {
        uint32_t w = pre_w;
        for (uint32_t s = 0; s < n; s++) {
            uint32_t rd = w + PRE_LEN - pre;                /* the read index wraps by one subtraction, not a modulo */
            if (rd >= PRE_LEN) rd -= PRE_LEN;
            int32_t o = pre_buf[rd];
            pre_buf[w] = (int16_t)clamp16(x[s]);
            if (++w >= PRE_LEN) w = 0;
            x[s] = o;
        }
        pre_w = w;
    }
    if (p->reverb_type == REVTYPE_DOLMEN)
        tank_front(x, n, 102u);
    else if (reverb_is_tank(p->reverb_type))
        tank_front(x, n, 256u);
    else if (p->reverb_type == REVTYPE_SPRING)
        spring_front(x, n);
    for (uint32_t s = 0; s < n; s++)
        rv_ring[(rv_w + s) & (RV_RING - 1u)] = x[s];
    rv_w = (rv_w + n) & (RV_RING - 1u);
}

void fx_reverb_back(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n)
{
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    for (uint32_t s = 0; s < n; s++)
        x[s] = rv_ring[(rv_r + s) & (RV_RING - 1u)];
    rv_r = (rv_r + n) & (RV_RING - 1u);
    if (reverb_is_tank(p->reverb_type)) {
        tank_reverb_block(p, x, r, n, p->reverb_type);
        return;
    }
    if (p->reverb_type == REVTYPE_SPRING) {
        spring_block(p, x, r, n);
        return;
    }
    const int32_t size = 25000 + (int32_t)p->reverb_decay * 50;
    const int32_t damp_q15 = (int32_t)p->reverb_damp * 200;     /* comb low-pass coefficient, Q15 */
    int16_t *const c0 = sfx_rev_comb, *const c1 = c0 + SFX_REV_COMB[0], *const c2 = c1 + SFX_REV_COMB[1],
            *const c3 = c2 + SFX_REV_COMB[2];
    int16_t *const a0 = sfx_rev_ap, *const a1 = a0 + SFX_REV_AP[0];
    uint32_t i0 = comb_i[0], i1 = comb_i[1], i2 = comb_i[2], i3 = comb_i[3], j0 = ap_i[0], j1 = ap_i[1];
    int32_t l0 = comb_lp[0], l1 = comb_lp[1], l2 = comb_lp[2], l3 = comb_lp[3];

    for (uint32_t s = 0; s < n; s++) {
        int32_t in = mr_mulq15(x[s], 2580);                    /* 1/8 at -4 dB, as the Felucca ROOM */
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
        x[s] = a * 2;
        r[s] = a * 2;
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

void fx_delay_block(const MasterPerformancePreset *p, const int32_t *in, int32_t *out_l, int32_t *out_r, uint32_t n)
{
    int32_t din[FX_BLOCK_MAX];
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    if (n == 0u) return;
    send_limit(&dl_lim_env, in, din, n);
    if (p->delay_type == DELAYTYPE_BBD)
        bbd_delay_block(p, din, out_l, out_r, n);
    else
        tape_delay_block(p, din, out_l, out_r, n);
}

void fx_process_block(const MasterPerformancePreset *p, const int32_t *delay_in, int32_t *reverb_io, int32_t *wet_r,
                      uint32_t n)
{
    /* Two independent effects, each on its own send: the delay output is not fed into the reverb. */
    int32_t dl[FX_BLOCK_MAX], dr[FX_BLOCK_MAX];
    if (n > FX_BLOCK_MAX) n = FX_BLOCK_MAX;
    fx_delay_block(p, delay_in, dl, dr, n);
    fx_reverb_block(p, reverb_io, wet_r, n);
    fx_mix_block(dl, dr, reverb_io, wet_r, n);
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
