
#include "lfo.h"
#include "dsp_cold.h"
#include "hardware/mem_section.h"
#include "midi_clock.h"

static int16_t fast_lfo_rand_fixed(uint32_t *seed) {
    *seed = *seed * 1103515245 + 12345;
    return (int16_t)(*seed >> 16);
}

/* rate knob plus the speed modulation, 0..127. A synced LFO quantizes the speed mod to the rate's own steps:
 * each whole step is 16 knob units, which is one musical division, and a source below one step does nothing.
 * A free-running LFO takes the mod in single knob units, so a moving source sweeps the rate smoothly. */
static int32_t lfo_final_rate(const LfoParams *cfg, int32_t speed_mod_q15) {
    int32_t offset = speed_mod_q15 >> 15;           /* the source times SPEED MOD AMT (-64..+63), plus the MOD SENDS */
    if (cfg->clock_sync) offset = (offset / 16) * 16;                             /* whole rate steps, -4..+3 */
    int32_t final_rate = (int32_t)cfg->rate + offset;

    if (final_rate > 127) final_rate = 127;
    if (final_rate < 0)   final_rate = 0;
    return final_rate;
}

/* the clock grid division of a synced LFO: 0 = slowest (one bar), 7 = fastest (see midi_clock.c) */
static uint8_t lfo_division(const LfoParams *cfg, int32_t speed_mod_q15) {
    return (uint8_t)(lfo_final_rate(cfg, speed_mod_q15) / 16);
}

/* Free-running step, used when the LFO is not clock synced or no clock is running. A synced LFO with no clock
 * falls back to the fixed Hz grid, so it keeps running. */
uint32_t calculate_lfo_step_increment_fixed(const LfoParams *cfg, int32_t speed_mod_q15) {
    int32_t final_rate = lfo_final_rate(cfg, speed_mod_q15);

    if (cfg->clock_sync == 0) {
        /* Free-running rate, exponential: Hz = 0.05 * 800^(knob / 127), so 0 is 0.05 Hz (20 s per cycle),
         * 32 about 0.27 Hz, 64 about 1.45 Hz, 96 about 7.8 Hz and 127 is 40 Hz (low audio rate). Stored as the phase
         * step per sample = Hz * 2^32 / 44100. */
        static const uint32_t rate_step[128] = {
              4870,      5133,      5410,      5703,      6011,      6336,      6678,      7039,
              7419,      7820,      8243,      8688,      9158,      9653,     10175,     10724,
             11304,     11915,     12559,     13238,     13953,     14707,     15502,     16340,
             17223,     18154,     19135,     20169,     21259,     22408,     23619,     24895,
             26241,     27659,     29154,     30729,     32390,     34141,     35986,     37931,
             39981,     42141,     44419,     46820,     49350,     52017,     54828,     57791,
             60915,     64207,     67677,     71334,     75190,     79253,     83536,     88051,
             92810,     97826,    103113,    108685,    114559,    120750,    127276,    134155,
            141405,    149048,    157103,    165593,    174543,    183976,    193919,    204399,
            215446,    227089,    239362,    252298,    265934,    280306,    295455,    311423,
            328254,    345994,    364693,    384403,    405177,    427075,    450156,    474485,
            500128,    527157,    555647,    585677,    617330,    650693,    685859,    722926,
            761997,    803178,    846586,    892339,    940565,    991398,   1044977,   1101453,
           1160980,   1223725,   1289861,   1359571,   1433048,   1510496,   1592131,   1678177,
           1768873,   1864471,   1965235,   2071446,   2183396,   2301397,   2425775,   2556875,
           2695060,   2840713,   2994238,   3156061,   3326629,   3506415,   3695918,   3895662,
        };
        return rate_step[final_rate];
    }

    int division_index = final_rate / 16;
    uint32_t sync_rates_hz[] = { 12, 25, 50, 100, 200, 400, 800, 1600 };
    /* Phase step per sample = Hz * 2^32 / 44100 = Hz * 97391.5. */
    return sync_rates_hz[division_index % 8] * 97391u;
}

void lfo_key_reset(const LfoParams *cfg, LfoRuntimeState *state) {
    if (cfg->key_reset == 0) return;
    state->phase_accumulator = (uint32_t)cfg->phase << 25;     /* 0..127 across the cycle */
    state->delay_counter = 0;
    state->fade_counter = 0;
}

void lfo_advance(const LfoParams *cfg, LfoRuntimeState *state, int32_t speed_mod_q15, int key_held, uint8_t shift,
                 uint32_t frame) {
    if (cfg->delay > LFO_REL_DELAY && key_held) {
        state->phase_accumulator = (uint32_t)cfg->phase << 25;    /* REL: wait at the phase start for the release */
        state->envelope_amp_q15 = 0;
        state->delay_counter = 0;
        state->fade_counter = 0;
        return;
    }
    if (cfg->clock_sync) {
        uint8_t division = lfo_division(cfg, speed_mod_q15);
        uint32_t locked, step;
        if (!cfg->key_reset && midi_clock_phase_at(division, frame, &locked)) {
            /* grid-locked: every cycle starts on the clock grid, whatever the keys do */
            state->phase_accumulator = locked + ((uint32_t)cfg->phase << 25);
            run_poly_lfo_envelope_fixed(cfg, state, shift);
            return;
        }
        if (cfg->key_reset && midi_clock_step_at(division, frame, &step)) {
            /* KEY RESET: lfo_key_reset put the playhead at its phase start on note-on. From there it runs at the
             * clock's rate, so each cycle starts at the note and keeps the tempo. */
            state->phase_accumulator += step << shift;
            run_poly_lfo_envelope_fixed(cfg, state, shift);
            return;
        }
    }
    /* free-running, or synced with no clock: the fixed rate curve (or the Hz grid for a synced LFO) */
    state->phase_accumulator += calculate_lfo_step_increment_fixed(cfg, speed_mod_q15) << shift;
    run_poly_lfo_envelope_fixed(cfg, state, shift);
}

void run_poly_lfo_envelope_fixed(const LfoParams *cfg, LfoRuntimeState *state, uint8_t shift) {
    /* REL (delay above 100) has no delay of its own: the playhead starts at the release. */
    uint32_t delay_index = cfg->delay > LFO_REL_DELAY ? 0u : (uint32_t)cfg->delay;
    uint32_t target_delay = delay_index * 350u;
    if (state->delay_counter < target_delay) {
        state->delay_counter += 1u << shift;
        state->envelope_amp_q15 = 0;
        return;
    }

    /* FADE: bipolar. Positive ramps the depth up to full, negative ramps it down to zero, 0 leaves it full. */
    int fade = (int)cfg->fade - (int)BIPOLAR_CENTER;              /* -64..+63 */
    uint32_t target_fade = (uint32_t)(fade < 0 ? -fade : fade) * 600u;
    if (fade == 0 || state->fade_counter >= target_fade) {
        state->envelope_amp_q15 = fade < 0 ? 0 : 32767;
    } else {
        state->fade_counter += 1u << shift;
        if (state->fade_counter > target_fade) state->fade_counter = target_fade;
        int32_t ramp = (int32_t)((state->fade_counter * 32767u) / target_fade);
        state->envelope_amp_q15 = fade < 0 ? 32767 - ramp : ramp;
    }
}

/* The LFO shapes, from gen_lfo_wavetable.py: 256 steps a cycle, each step its start value and its rise across the
 * step. Only the sine is a table in flash (lfo_sine); the straight shapes (triangle, saw, square, which rise along their own
 * slope so the saw and square edges stay hard) are worked out from the same formulas at start-up into RAM
 * (lfo_tables_init, lfo_straight), value for value the flash table they were (2026-11-03: 3 KB of flash). lfo_morph_stage / lfo_morph_w: for each SHAPE knob value, the ring stage and the
 * Q15 weight of the next stage. */
static const int16_t lfo_sine[256][2] = {
        {     0,  804}, {   804,  804}, {  1608,  802}, {  2410,  802}, {  3212,  799}, {  4011,  797}, {  4808,  794}, {  5602,  791},
        {  6393,  786}, {  7179,  783}, {  7962,  777}, {  8739,  773}, {  9512,  766}, { 10278,  761}, { 11039,  754}, { 11793,  746},
        { 12539,  740}, { 13279,  731}, { 14010,  722}, { 14732,  714}, { 15446,  705}, { 16151,  695}, { 16846,  684}, { 17530,  674},
        { 18204,  664}, { 18868,  651}, { 19519,  640}, { 20159,  628}, { 20787,  616}, { 21403,  602}, { 22005,  589}, { 22594,  576},
        { 23170,  561}, { 23731,  548}, { 24279,  532}, { 24811,  518}, { 25329,  503}, { 25832,  487}, { 26319,  471}, { 26790,  455},
        { 27245,  438}, { 27683,  422}, { 28105,  405}, { 28510,  388}, { 28898,  370}, { 29268,  353}, { 29621,  335}, { 29956,  317},
        { 30273,  298}, { 30571,  281}, { 30852,  261}, { 31113,  243}, { 31356,  224}, { 31580,  205}, { 31785,  186}, { 31971,  166},
        { 32137,  148}, { 32285,  127}, { 32412,  109}, { 32521,   88}, { 32609,   69}, { 32678,   50}, { 32728,   29}, { 32757,   10},
        { 32767,  -10}, { 32757,  -29}, { 32728,  -50}, { 32678,  -69}, { 32609,  -88}, { 32521, -109}, { 32412, -127}, { 32285, -148},
        { 32137, -166}, { 31971, -186}, { 31785, -205}, { 31580, -224}, { 31356, -243}, { 31113, -261}, { 30852, -281}, { 30571, -298},
        { 30273, -317}, { 29956, -335}, { 29621, -353}, { 29268, -370}, { 28898, -388}, { 28510, -405}, { 28105, -422}, { 27683, -438},
        { 27245, -455}, { 26790, -471}, { 26319, -487}, { 25832, -503}, { 25329, -518}, { 24811, -532}, { 24279, -548}, { 23731, -561},
        { 23170, -576}, { 22594, -589}, { 22005, -602}, { 21403, -616}, { 20787, -628}, { 20159, -640}, { 19519, -651}, { 18868, -664},
        { 18204, -674}, { 17530, -684}, { 16846, -695}, { 16151, -705}, { 15446, -714}, { 14732, -722}, { 14010, -731}, { 13279, -740},
        { 12539, -746}, { 11793, -754}, { 11039, -761}, { 10278, -766}, {  9512, -773}, {  8739, -777}, {  7962, -783}, {  7179, -786},
        {  6393, -791}, {  5602, -794}, {  4808, -797}, {  4011, -799}, {  3212, -802}, {  2410, -802}, {  1608, -804}, {   804, -804},
        {     0, -804}, {  -804, -804}, { -1608, -802}, { -2410, -802}, { -3212, -799}, { -4011, -797}, { -4808, -794}, { -5602, -791},
        { -6393, -786}, { -7179, -783}, { -7962, -777}, { -8739, -773}, { -9512, -766}, {-10278, -761}, {-11039, -754}, {-11793, -746},
        {-12539, -740}, {-13279, -731}, {-14010, -722}, {-14732, -714}, {-15446, -705}, {-16151, -695}, {-16846, -684}, {-17530, -674},
        {-18204, -664}, {-18868, -651}, {-19519, -640}, {-20159, -628}, {-20787, -616}, {-21403, -602}, {-22005, -589}, {-22594, -576},
        {-23170, -561}, {-23731, -548}, {-24279, -532}, {-24811, -518}, {-25329, -503}, {-25832, -487}, {-26319, -471}, {-26790, -455},
        {-27245, -438}, {-27683, -422}, {-28105, -405}, {-28510, -388}, {-28898, -370}, {-29268, -353}, {-29621, -335}, {-29956, -317},
        {-30273, -298}, {-30571, -281}, {-30852, -261}, {-31113, -243}, {-31356, -224}, {-31580, -205}, {-31785, -186}, {-31971, -166},
        {-32137, -148}, {-32285, -127}, {-32412, -109}, {-32521,  -88}, {-32609,  -69}, {-32678,  -50}, {-32728,  -29}, {-32757,  -10},
        {-32767,   10}, {-32757,   29}, {-32728,   50}, {-32678,   69}, {-32609,   88}, {-32521,  109}, {-32412,  127}, {-32285,  148},
        {-32137,  166}, {-31971,  186}, {-31785,  205}, {-31580,  224}, {-31356,  243}, {-31113,  261}, {-30852,  281}, {-30571,  298},
        {-30273,  317}, {-29956,  335}, {-29621,  353}, {-29268,  370}, {-28898,  388}, {-28510,  405}, {-28105,  422}, {-27683,  438},
        {-27245,  455}, {-26790,  471}, {-26319,  487}, {-25832,  503}, {-25329,  518}, {-24811,  532}, {-24279,  548}, {-23731,  561},
        {-23170,  576}, {-22594,  589}, {-22005,  602}, {-21403,  616}, {-20787,  628}, {-20159,  640}, {-19519,  651}, {-18868,  664},
        {-18204,  674}, {-17530,  684}, {-16846,  695}, {-16151,  705}, {-15446,  714}, {-14732,  722}, {-14010,  731}, {-13279,  740},
        {-12539,  746}, {-11793,  754}, {-11039,  761}, {-10278,  766}, { -9512,  773}, { -8739,  777}, { -7962,  783}, { -7179,  786},
        { -6393,  791}, { -5602,  794}, { -4808,  797}, { -4011,  799}, { -3212,  802}, { -2410,  802}, { -1608,  804}, {  -804,  804},
};
static const uint8_t lfo_morph_stage[128] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,0,
};
static const uint16_t lfo_morph_w[128] = {
        0,  1548,  3096,  4644,  6192,  7740,  9289, 10837, 12385, 13933, 15481, 17029, 18577, 20125, 21673, 23221,
    24770, 26318, 27866, 29414, 30962, 32510,  1290,  2838,  4386,  5934,  7482,  9031, 10579, 12127, 13675, 15223,
    16771, 18319, 19867, 21415, 22963, 24511, 26060, 27608, 29156, 30704, 32252,  1032,  2580,  4128,  5676,  7224,
     8773, 10321, 11869, 13417, 14965, 16513, 18061, 19609, 21157, 22705, 24253, 25802, 27350, 28898, 30446, 31994,
      774,  2322,  3870,  5418,  6966,  8515, 10063, 11611, 13159, 14707, 16255, 17803, 19351, 20899, 22447, 23995,
    25544, 27092, 28640, 30188, 31736,   516,  2064,  3612,  5160,  6708,  8257,  9805, 11353, 12901, 14449, 15997,
    17545, 19093, 20641, 22189, 23737, 25286, 26834, 28382, 29930, 31478,   258,  1806,  3354,  4902,  6450,  7998,
     9547, 11095, 12643, 14191, 15739, 17287, 18835, 20383, 21931, 23479, 25028, 26576, 28124, 29672, 31220,     0,
};

/* Six shapes on a ring: sine, triangle, saw, square, stepped, slewed, then back to sine (LFO_SHAPE_*). */
enum { LFO_SHAPE_STEP = 4, LFO_SHAPE_SLEW = 5 };

/* triangle (1), saw (2) or square (3) at phase ph, as gen_lfo_wavetable.py tri / saw / sqr */
static int32_t lfo_straight_at(uint32_t shape, uint32_t ph) {
    if (shape == 1u) return ph < 0x80000000u ? -32768 + (int32_t)(ph >> 15) : 32767 - (int32_t)((ph - 0x80000000u) >> 15);
    if (shape == 2u) return 32767 - (int32_t)(ph >> 16);
    return ph < 0x80000000u ? 32767 : -32768;
}

static int16_t lfo_straight[3][256][2] SYNTH_POOL;   /* triangle, saw, square: start, rise (lfo_tables_init) */

SYNTH_COLD void lfo_tables_init(void) {
    for (uint32_t sh = 1u; sh < 4u; sh++)
        for (uint32_t i = 0u; i < 256u; i++) {         /* as gen_lfo_wavetable.py frame() */
            int32_t s0 = lfo_straight_at(sh, i << 24), rise = 2 * (lfo_straight_at(sh, (i << 24) + (1u << 23)) - s0);
            while (s0 + ((rise * 32767) >> 15) < -32768) rise++;
            while (s0 + ((rise * 32767) >> 15) > 32767) rise--;
            lfo_straight[sh - 1u][i][0] = (int16_t)s0;
            lfo_straight[sh - 1u][i][1] = (int16_t)rise;
        }
}

/* one shape of the ring at this phase. The table shapes read one step and interpolate within it with the 15
 * phase bits below the step; the random shapes read the values drawn at the last wrap. */
static int32_t lfo_shape_at(const LfoRuntimeState *state, uint32_t shape, uint32_t phase) {
    if (shape < LFO_SHAPE_STEP) {
        const int16_t *s = shape ? lfo_straight[shape - 1u][phase >> 24] : lfo_sine[phase >> 24];
        return s[0] + ((s[1] * (int32_t)((phase >> 9) & 0x7FFFu)) >> 15);
    }
    if (shape == LFO_SHAPE_STEP) return state->current_random;
    int32_t norm_q15 = (int32_t)(phase >> 17);   /* 0..32767 across the cycle */
    return ((state->current_random * (32768 - norm_q15)) + (state->next_random * norm_q15)) >> 15;
}

int16_t compute_morphed_lfo_fixed(LfoRuntimeState *state, int raw_morph) {
    uint32_t phase = state->phase_accumulator;

    /* Stepped and slewed random values are redrawn once per cycle, when the phase wraps, whatever the shape,
     * so they stay in step when the knob moves. The slew carries on from the last target, so it stays
     * continuous. */
    if (phase < state->last_phase) {
        state->current_random = state->next_random;
        state->next_random = fast_lfo_rand_fixed(&state->rand_seed);
    }
    state->last_phase = phase;

    /* The morph knob walks the ring once over 0..127. Past the slewed shape it wraps to the sine start instead
     * of clipping (spec 4.4), so the top of the knob is the sine again. Only the two shapes the knob sits
     * between are read, and only one on a whole stage. */
    uint32_t morph = raw_morph > 127 ? 127u : raw_morph < 0 ? 0u : (uint32_t)raw_morph;
    uint32_t stage = lfo_morph_stage[morph];
    int32_t w = lfo_morph_w[morph];
    int32_t a = lfo_shape_at(state, stage, phase);
    if (w == 0) return (int16_t)a;
    int32_t b = lfo_shape_at(state, stage == LFO_SHAPE_SLEW ? 0u : stage + 1u, phase);
    return (int16_t)(a + (((b - a) * w) >> 15));
}

int16_t lfo_peek_fixed(const LfoRuntimeState *state, int raw_morph, uint32_t phase_offset) {
    /* The table shapes read at the shifted phase. The random shapes keep the values the playhead drew this cycle,
     * so the stepped shape holds the same value in both ears and the slewed one glides along the shifted phase. */
    uint32_t phase = state->phase_accumulator + phase_offset;
    uint32_t morph = raw_morph > 127 ? 127u : raw_morph < 0 ? 0u : (uint32_t)raw_morph;
    uint32_t stage = lfo_morph_stage[morph];
    int32_t w = lfo_morph_w[morph];
    int32_t a = lfo_shape_at(state, stage, phase);
    if (w == 0) return (int16_t)a;
    int32_t b = lfo_shape_at(state, stage == LFO_SHAPE_SLEW ? 0u : stage + 1u, phase);
    return (int16_t)(a + (((b - a) * w) >> 15));
}
