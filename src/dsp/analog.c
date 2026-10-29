#include "analog.h"
#include "synth_voice.h"     /* SYNTH_CTRL_SHIFT */
#include "dsp_cold.h"

/* the most each part adds, at ANALOG 127 */
#define AN_PITCH_TOL   4.0f      /* cents, the slot's fixed OSC offsets */
#define AN_PITCH_DRIFT 4.0f      /* cents per unit of drift (a walk: standard deviation 0.5, peaks ~2) */
#define AN_THERMAL     3.0f      /* cents per unit of the shared warm-up drift (the same spread) */
#define AN_NOTE_PITCH  1.5f      /* cents, each note's scatter */
#define AN_CUT_TOL     300.0f    /* Q8 cutoff steps (a step is ~0.9 semitone: about +-1 semitone) */
#define AN_CUT_DRIFT   200.0f
#define AN_RES_TOL     2.5f      /* resonance steps */
#define AN_ENV_TOL     0.10f     /* envelope speed, +-10 % */
#define AN_NOTE_TIME   0.04f
#define AN_LEVEL_TOL   0.06f     /* level, about +-0.5 dB */
#define AN_DRIFT_FROM  40        /* the knob value drift starts at */

#define AN_CENT 5.7762265e-4f    /* ln 2 / 1200: a ratio of 1 + c * AN_CENT is c cents (to 0.01 c at 12 c) */
#define AN_TICK_HZ (44100.f / (float)SYNTH_CTRL_PERIOD)
#define AN_TWO_PI 6.2831853f
/* two one-poles at the walk's corner, driven by uniform noise times the gain: the gains give a standard deviation of 0.5
 * (measured on the host over 4 simulated hours: unit gain 0.0069 at 0.25 Hz, 0.0031 at 0.05 Hz; peaks ~4.4 deviations) */
#define AN_DRIFT_A   (4.f * AN_TWO_PI * 0.25f / AN_TICK_HZ)   /* the voices' walks, ~0.25 Hz, a step every 4 ticks */
#define AN_THERMAL_A (AN_TWO_PI * 0.05f / AN_TICK_HZ)    /* the warm-up drift, ~0.05 Hz */
#define AN_DRIFT_GAIN   36.2f     /* the walks step every 4th tick (4 a, so half the gain; measured the same) */
#define AN_THERMAL_GAIN 161.0f

static float an_th[2];
static uint32_t an_th_rng = 0x2545F491u;

static inline float an_noise(uint32_t *s)    /* -1..1 */
{
    *s = *s * 1664525u + 1013904223u;
    return (float)(int32_t)*s * (1.f / 2147483648.f);
}

static inline uint32_t an_hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}

static inline int an_round(float x) { return (int)(x + (x >= 0.f ? 0.5f : -0.5f)); }

SYNTH_COLD void analog_voice_init(AnalogVoice *a, unsigned slot)
{
    unsigned i;
    for (i = 0; i < ANT_COUNT; i++)
        a->tol[i] = (float)(int32_t)an_hash(slot * 131u + i * 7919u + 1u) * (1.f / 2147483648.f);
    for (i = 0; i < 6u; i++)
        a->drift[i] = 0.f;
    a->note_pitch = a->note_time = 0.f;
    a->rng = an_hash(slot + 0x9E3779B9u) | 1u;
    a->amount = 0u;
    a->phase = 0u;
}

void analog_note_on(AnalogVoice *a)
{
    a->note_pitch = an_noise(&a->rng);
    a->note_time = an_noise(&a->rng);
    a->amount = 0u;
}

void analog_begin_tick(void)
{
    an_th[0] += AN_THERMAL_A * (AN_THERMAL_GAIN * an_noise(&an_th_rng) - an_th[0]);
    an_th[1] += AN_THERMAL_A * (an_th[0] - an_th[1]);
}

/* the parts that hold while the amount and the note do */
static void analog_fixed(AnalogVoice *a, uint8_t amount)
{
    AnalogTick *t = &a->out;
    float tamt = (float)amount * (1.f / 127.f), note = tamt * AN_NOTE_PITCH * a->note_pitch;
    a->damt = 0.f;
    if (amount > AN_DRIFT_FROM) {                          /* eases in: 90 ~0.45, 127 1 */
        float d = (float)(amount - AN_DRIFT_FROM) * (1.f / (127.f - AN_DRIFT_FROM));
        a->damt = d * (0.5f + 0.5f * d);
    }
    a->base[0] = tamt * AN_PITCH_TOL * a->tol[ANT_PITCH1] + note;
    a->base[1] = tamt * AN_PITCH_TOL * a->tol[ANT_PITCH2] + note;
    a->base[2] = tamt * AN_CUT_TOL * a->tol[ANT_CUT1];
    a->base[3] = tamt * AN_CUT_TOL * a->tol[ANT_CUT2];
    t->pitch1 = 1.f + a->base[0] * AN_CENT;
    t->pitch2 = 1.f + a->base[1] * AN_CENT;
    t->cut1_q8 = an_round(a->base[2]);
    t->cut2_q8 = an_round(a->base[3]);
    t->res1 = an_round(tamt * AN_RES_TOL * a->tol[ANT_RES]);
    t->res2 = an_round(tamt * AN_RES_TOL * a->tol[ANT_RES2]);
    t->time_k_q16 = (int32_t)(65536.f * (1.f + tamt * (AN_ENV_TOL * a->tol[ANT_TIME] + AN_NOTE_TIME * a->note_time)));
    t->level_q15 = (int32_t)(32768.f * (1.f + tamt * AN_LEVEL_TOL * a->tol[ANT_LEVEL]));
    a->amount = amount;
}

const AnalogTick *analog_voice_tick(AnalogVoice *a, uint8_t amount)
{
    AnalogTick *t = &a->out;
    if (amount != a->amount)
        analog_fixed(a, amount);
    if (a->damt != 0.f) {
        float damt = a->damt, th = AN_THERMAL * an_th[1], dc;
        if ((++a->phase & 3u) == 0u) {
            int i;
            for (i = 0; i < 6; i += 2) {
                a->drift[i] += AN_DRIFT_A * (AN_DRIFT_GAIN * an_noise(&a->rng) - a->drift[i]);
                a->drift[i + 1] += AN_DRIFT_A * (a->drift[i] - a->drift[i + 1]);
            }
        }
        t->pitch1 = 1.f + (a->base[0] + damt * (AN_PITCH_DRIFT * a->drift[1] + th)) * AN_CENT;
        t->pitch2 = 1.f + (a->base[1] + damt * (AN_PITCH_DRIFT * a->drift[3] + th)) * AN_CENT;
        dc = damt * AN_CUT_DRIFT * a->drift[5];
        t->cut1_q8 = an_round(a->base[2] + dc);
        t->cut2_q8 = an_round(a->base[3] + dc);
    }
    return t;
}
