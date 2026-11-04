#include "tuning.h"
#include "dsp_cold.h"
#include "hardware/mem_section.h"

int16_t tuning_user[TUNING_NUSER][TUNING_NKEY] SYNTH_POOL;
volatile uint8_t tuning_unsaved;

/* each built-in tuning's degrees from its root, in cents from equal temperament, rounded (1200 log2 of the ratio
 * - 100 x the degree) */
static const int8_t TUNING_TABLE[TUNING_USER1 - 1][12] = {
    {0, 5, 4, -2, -14, -29, -49, 2, 41, 6, -31, -12},              /* OVERTONE 16 17 18 19 20 21 22 24 26 27 28 30 /16 */
    {0, 12, 4, 16, -14, -2, -10, 2, 14, -16, 18, -12},             /* JUST 1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8 */
    {0, 14, 4, -6, 8, -2, 12, 2, -8, 6, -4, 10},                   /* PYTHAGOREAN: fifths 3/2, Eb..G# */
    {0, 19, 31, 16, -14, -2, -17, 2, 14, -16, -31, -12},           /* SEPTIMAL 1 15/14 8/7 6/5 5/4 4/3 7/5 3/2 8/5 5/3 7/4 15/8 */
    {0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50},                      /* RAST */
    {0, -24, -7, 10, -14, 3, -21, -3, -27, -10, 7, -17},           /* MEANTONE: fifths 1/4 comma narrow, Eb..G# */
    {0, -10, -8, -6, -10, -2, -12, -4, -8, -12, -4, -8},           /* WERCKMEISTER III */
};

int32_t tuning_key_cents(uint8_t tuning, uint8_t root, uint8_t key)
{
    if (tuning >= TUNING_USER1 && tuning < TUNING_COUNT)
        return tuning_user[tuning - TUNING_USER1][key & 127u];
    if (tuning == TUNING_EQUAL || tuning >= TUNING_COUNT)
        return 0;
    return TUNING_TABLE[tuning - 1u][(key + 12u * 11u - root % 12u) % 12u];
}

static int16_t tuning_clamp(int32_t c) { return (int16_t)(c > 9999 ? 9999 : c < -9999 ? -9999 : c); }

/* an MTS frequency word (xx yy zz: the semitone, then 14 bits of it) as key's offset in cents; 7F 7F 7F: no change */
static void tuning_put(int16_t *t, uint8_t key, const uint8_t *f)
{
    uint32_t frac = (uint32_t)f[1] << 7 | f[2];
    if (key > 127u || (f[0] == 0x7Fu && frac == 0x3FFFu))
        return;
    t[key] = tuning_clamp(((int32_t)f[0] - key) * 100 + (int32_t)((frac * 100u + 8192u) >> 14));
}

SYNTH_COLD int tuning_mts(const uint8_t *p, uint32_t n, uint8_t cur_tuning)
{
    uint32_t i, k, nn, o;
    int slot = -1;
    int16_t pc[12];
    if (n < 5u || (p[0] != 0x7Eu && p[0] != 0x7Fu) || p[2] != 0x08u)
        return -1;
    switch (p[3]) {
    case 0x01: case 0x04:            /* bulk dump [bank] program name[16] (xx yy zz) x 128 checksum */
        o = p[3] == 0x04u ? 5u : 4u;
        if (p[0] != 0x7Eu || n < o + 1u + 16u + 3u * TUNING_NKEY)
            return -1;
        slot = p[o] & (TUNING_NUSER - 1u);
        for (k = 0; k < TUNING_NKEY; k++)
            tuning_put(tuning_user[slot], (uint8_t)k, p + o + 17u + 3u * k);
        break;
    case 0x02: case 0x07:            /* single note [bank] program count (key xx yy zz) x count */
        o = p[3] == 0x07u ? 5u : 4u;
        if (n < o + 2u)
            return -1;
        slot = p[o] & (TUNING_NUSER - 1u);
        nn = p[o + 1u];
        for (i = 0, k = o + 2u; i < nn && k + 4u <= n; i++, k += 4u)
            tuning_put(tuning_user[slot], p[k], p + k + 1u);
        break;
    case 0x08: case 0x09:            /* scale/octave: 3 channel-mask bytes, then 12 pitch classes from C */
        if (n < 7u + (p[3] == 0x09u ? 24u : 12u))
            return -1;
        for (i = 0; i < 12u; i++)
            pc[i] = p[3] == 0x08u ? (int16_t)(p[7u + i] - 64)            /* -64..+63 cents */
                                  : (int16_t)((((int32_t)p[7u + 2u * i] << 7 | p[8u + 2u * i]) - 8192) * 100 / 8192);
        slot = cur_tuning >= TUNING_USER1 && cur_tuning < TUNING_COUNT ? cur_tuning - TUNING_USER1 : 0;
        for (k = 0; k < TUNING_NKEY; k++)
            tuning_user[slot][k] = pc[k % 12u];
        break;
    default:
        return -1;
    }
    if (p[0] == 0x7Eu)
        tuning_unsaved = 1u;
    return slot;
}
