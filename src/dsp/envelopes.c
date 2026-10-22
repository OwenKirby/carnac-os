
#include "envelopes.h"
#include "dsp_clamp.h"
#include "midi_clock.h"
#include <stdint.h>

/* Stage times. Knob k sets T = 1 ms * 20000^((k / 127)^0.75): 0 is 1 ms, 16 about 8 ms, 32 about 34 ms, 48 about
 * 120 ms, 64 about 0.37 s, 80 about 1.1 s, 96 about 3 s, 112 about 8 s and 127 is 20 s. The curve spends more of the
 * knob on the short and middle times, where most patches live.
 *
 * The stages are one-pole curves, stored as the per-sample coefficient 1 - exp(-1 / (tau * 44100)) in Q31,
 * generated offline (no runtime float):
 *   attack: rises toward 1.5x full scale and stops at full scale, so T is the time from 0 to the peak (tau = T / ln 3)
 *   decay and release: T is the time to fall 60 dB (tau = T / ln 1000)
 * Like the Roland envelope chips: the attack overshoots its target (a steeper, snappier start than a plain RC), the
 * decay runs toward 0 and is held at the sustain level, and a retrigger starts from the current level. */
static const uint32_t env_attack_coef_q31[128] = {
     52836911,  40783965,  34170824,  29255328,  25362756,  22178909,  19521277,  17271080,
     15345421,  13683631,  12239765,  10978115,   9870360,   8893666,   8029379,   7262085,
      6578927,   5969089,   5423412,   4934087,   4494420,   4098645,   3741774,   3419470,
      3127955,   2863919,   2624460,   2407023,   2209352,   2029452,   1865554,   1716087,
      1579650,   1454994,   1341005,   1236682,   1141131,   1053546,    973206,    899459,
       831717,    769452,    712185,    659482,    610950,    566235,    525013,    486991,
       451903,    419505,    389578,    361919,    336344,    312687,    290793,    270523,
       251748,    234351,    218225,    203270,    189397,    176523,    164572,    153472,
       143161,    133579,    124671,    116388,    108683,    101513,     94840,     88627,
        82841,     77451,     72428,     67746,     63380,     59309,     55512,     51969,
        48662,     45574,     42691,     39999,     37484,     35133,     32937,     30883,
        28963,     27167,     25487,     23915,     22444,     21067,     19778,     18571,
        17441,     16382,     15390,     14460,     13588,     12771,     12005,     11287,
        10613,      9981,      9388,      8831,      8309,      7818,      7358,      6926,
         6520,      6138,      5780,      5443,      5127,      4829,      4550,      4287,
         4040,      3807,      3589,      3383,      3190,      3008,      2836,      2675,
};
static const uint32_t env_fall_coef_q31[128] = {
    311356911, 243905877, 206020313, 177451207, 154577309, 135702423, 119832202, 106312827,
     94683181,  84602381,  75809792,  68101048,  61312729,  55312106,  49989991,  45255606,
     41032795,  37257180,  33873970,  30836265,  28103707,  25641406,  23419077,  21410329,
     19592089,  17944119,  16448616,  15089881,  13854031,  12728764,  11703157,  10767486,
      9913084,   9132211,   8417941,   7764069,   7165025,   6615806,   6111909,   5649276,
      5224246,   4833513,   4474088,   4143264,   3838589,   3557840,   3298996,   3060224,
      2839854,   2636366,   2448378,   2274626,   2113959,   1965327,   1827767,   1700401,
      1582426,   1473105,   1371764,   1277784,   1190598,   1109686,   1034569,    964807,
       899998,    839769,    783778,    731710,    683276,    638207,    596258,    557202,
       520827,    486940,    455363,    425929,    398487,    372893,    349019,    326742,
       305951,    286541,    268417,    251490,    235676,    220898,    207087,    194176,
       182103,    170812,    160250,    150367,    141119,    132462,    124357,    116768,
       109660,    103002,     96763,     90917,     85437,     80300,     75484,     70967,
        66731,     62756,     59027,     55528,     52243,     49160,     46265,     43546,
        40993,     38595,     36342,     34225,     32235,     30365,     28608,     26955,
        25401,     23939,     22565,     21272,     20055,     18910,     17833,     16819,
};

#define ENV_PEAK_Q30     (1 << 30)              /* full scale; level_q15 = level_q30 >> 15 */
#define ENV_DECAY_SNAP   (ENV_PEAK_Q30 >> 10)   /* decay snaps to sustain within -60 dB of full scale */
#define ENV_RELEASE_OFF  (1 << 16)              /* release ends below about -84 dB */
/* The decay runs toward 0 and is caught at sustain, so it crosses the sustain level several times sooner than a curve
 * aimed at the sustain level. Its coefficient is scaled (Q10) to lengthen the time constant, so the knob keeps about
 * the old settling time and body after the attack. */
#ifndef ENV_DECAY_SLOWING_Q10
#define ENV_DECAY_SLOWING_Q10 410               /* 0.4: time constant 2.5x the 60 dB fall time of the table */
#endif

/* one one-pole step toward target. The product is 64-bit: a long stage has a coefficient of a few thousand in Q31. */
static int32_t env_step(int32_t level, int32_t target, uint32_t coef_q31) {
    return level + (int32_t)(((int64_t)(target - level) * (int64_t)coef_q31) >> 31);
}


void adsr_init(ADSR *env) {
    env->state = STATE_OFF;
    env->level_q15 = 0;
    env->level_q30 = 0;
    env->is_key_down = 0;
    env->retrigger_timer = 0;
    env->retrig_phase = 0;
    env->time_dk_q16 = 0;
}

void adsr_note_on_legato(ADSR *env, const EnvParams *p, int is_any_other_voice_held) {
    env->is_key_down = 1;
    env->retrigger_timer = 0;
    if (p->legato == 1 && is_any_other_voice_held) {
        if (env->state == STATE_OFF || env->state == STATE_RELEASE) {
            env->state = STATE_ATTACK;
        }
    } else {
        /* Roland style: a retrigger charges on from wherever the level is (the capacitor is not dumped), so a
         * struck-again or stolen voice does not click down to zero first. */
        env->state = STATE_ATTACK;
    }
}

void adsr_note_off(ADSR *env) {
    env->state = STATE_RELEASE;
    env->is_key_down = 0;
}

/* AUTO-RETRIGGER period in samples (spec 4.3). Frequency 0 = off. Unsynced: 1 Hz at 1, about 1 kHz at 127.
 * Synced: the clock's musical grid (midi_clock.c), with the fixed Hz grid as
 * the fallback when no clock is running. */
static uint32_t retrig_period_samples(const EnvParams *p) {
    static const uint32_t sync_rates_hz[] = { 12, 25, 50, 100, 200, 400, 800, 1600 };
    uint32_t r = p->retrig_rate;
    uint32_t hz = p->retrig_sync ? sync_rates_hz[(r >> 4) & 7u] : 1u + ((r * r) >> 4);
    return 44100u / hz;
}

/* Whether the auto-retrigger fires within the last n samples. Synced: at each cycle start of the clock grid, a
 * crossing of the phase origin (the top of the phase wraps to the bottom) since the previous control tick.
 * Unsynced or no clock: the period counter, advanced n samples at a time. */
static int retrig_due(ADSR *env, const EnvParams *p, uint32_t n, uint32_t frame) {
    uint32_t phase;
    if (p->retrig_sync && midi_clock_phase_at((uint8_t)(p->retrig_rate >> 4), frame, &phase)) {
        int due = env->retrig_phase >= 0x80000000u && phase < 0x80000000u;
        env->retrig_phase = phase;
        return due;
    }
    env->retrigger_timer += n;
    uint32_t period = retrig_period_samples(p);
    if (env->retrigger_timer >= period) {
        env->retrigger_timer -= period;
        if (env->retrigger_timer >= period) env->retrigger_timer = 0;   /* a period shorter than the tick */
        return 1;
    }
    return 0;
}

/* A one-pole coefficient for 2^shift samples at once: 1 - (1 - c)^(2^shift), Q31, by repeated squaring of the
 * per-sample decay factor (rounded at each step). The stage curve lands on the same values at every tick as the
 * per-sample curve would. */
static uint32_t coef_pow2(uint32_t coef_q31, uint8_t shift) {
    uint64_t keep = (1ull << 31) - coef_q31;                     /* (1 - c), Q31 */
    for (uint8_t i = 0; i < shift; i++)
        keep = (keep * keep + (1ull << 30)) >> 31;
    return (uint32_t)((1ull << 31) - keep);
}

/* ANALOG's speed change on the running stage's coefficient (a faster stage: a larger coefficient), held below 1 */
static uint32_t coef_analog(uint32_t coef_q31, int32_t dk_q16) {
    int64_t c;
    if (dk_q16 == 0) return coef_q31;
    c = (int64_t)coef_q31 + (((int64_t)coef_q31 * dk_q16) >> 16);
    return c >= (int64_t)(1u << 31) ? (1u << 31) - 1u : (c < 1 ? 1u : (uint32_t)c);
}

int32_t adsr_advance(ADSR *env, const EnvParams *p, int time_mod, uint16_t release_boost_q15, uint8_t shift,
                     uint32_t frame) {
    int attack_i = clampi(p->attack + time_mod, 0, 127);
    int decay_i = clampi(p->decay + time_mod, 0, 127);
    int release_i = clampi(p->release + time_mod, 0, 127);
    // AUTO DECAY: rapid repeats speed up the release. Factor 1.0 + amount * boost, up to 2x at full amount.
    uint32_t boost_q7 = 128u + (((uint32_t)p->auto_decay * (uint32_t)release_boost_q15) >> 15);
    uint32_t release_coef = (uint32_t)(((uint64_t)env_fall_coef_q31[release_i] * boost_q7) >> 7);

    int32_t sustain_target = ((int32_t)p->sustain * 258) << 15;

    // AUTO-RETRIGGER: while the key is held, the envelope loops back into its attack every period.
    if (p->retrig_rate != 0 && env->is_key_down) {
        if (retrig_due(env, p, 1u << shift, frame))
            env->state = STATE_ATTACK;
    }

    /* The level runs in Q30 so the slow stages keep moving: in Q15 a 20 s stage would round its step to 0. */
    int32_t level = env->level_q30;
    switch (env->state) {
        case STATE_ATTACK:
            level = env_step(level, ENV_PEAK_Q30 + (ENV_PEAK_Q30 >> 1),
                             coef_pow2(coef_analog(env_attack_coef_q31[attack_i], env->time_dk_q16), shift));
            if (level >= ENV_PEAK_Q30) {
                level = ENV_PEAK_Q30;
                env->state = STATE_DECAY;
            }
            break;
        case STATE_DECAY:
            /* Roland style: the decay discharges toward 0 and is caught at the sustain level, so a high sustain
             * is reached sooner and the curve keeps its full exponential slope into the sustain. */
            level = env_step(level, 0, coef_pow2(coef_analog((uint32_t)(((uint64_t)env_fall_coef_q31[decay_i] *
                                                                    ENV_DECAY_SLOWING_Q10) >> 10), env->time_dk_q16), shift));
            if (level - sustain_target < ENV_DECAY_SNAP) {
                level = sustain_target;
                env->state = STATE_SUSTAIN;
            }
            break;
        case STATE_SUSTAIN:
            level = sustain_target;
            break;
        case STATE_RELEASE:
            level = env_step(level, 0, coef_pow2(coef_analog(release_coef, env->time_dk_q16), shift));
            if (level < ENV_RELEASE_OFF) {
                level = 0;
                env->state = STATE_OFF;
            }
            break;
        case STATE_OFF:
            level = 0;
            break;
    }
    env->level_q30 = level;
    env->level_q15 = level >> 15;
    return env->level_q15;
}
