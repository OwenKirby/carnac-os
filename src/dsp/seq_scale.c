#include "seq_scale.h"
#include "arp_chords.h"

/* each SEQSCALE_* from SEQSCALE_MAJOR on C: bit n = pitch class n (C = 0) */
static const uint16_t SEQ_SCALE_C[SEQSCALE_COUNT - SEQSCALE_MAJOR] = {
    0xAB5u,                          /* MAJOR       C D E F G A B */
    0x5ADu,                          /* MINOR       C D Eb F G Ab Bb */
    0x6ADu,                          /* DORIAN      C D Eb F G A Bb */
    0x5ABu,                          /* PHRYGIAN    C Db Eb F G Ab Bb */
    0xAD5u,                          /* LYDIAN      C D E F# G A B */
    0x6B5u,                          /* MIXOLYDIAN  C D E F G A Bb */
    0x9ADu,                          /* HARM_MINOR  C D Eb F G Ab B */
    0x295u,                          /* MAJ_PENTA   C D E G A */
    0x4A9u,                          /* MIN_PENTA   C Eb F G Bb */
    0x4E9u,                          /* BLUES       C Eb F F# G Bb */
};

uint16_t seq_scale_mask(uint8_t scale, uint8_t root, uint8_t genre)
{
    uint32_t m;
    if (scale == SEQSCALE_GENRE) {
        uint8_t g = arp_genre_scale[genre < ARP_GENRE_COUNT ? genre : 0u];
        scale = g & 0x0Fu;
        root = g >> 4;
    }
    if (scale < SEQSCALE_MAJOR || scale >= SEQSCALE_COUNT)
        return 0u;
    root %= 12u;
    m = (uint32_t)SEQ_SCALE_C[scale - SEQSCALE_MAJOR] << root;   /* turned to the root: wrap the top round */
    return (uint16_t)((m | m >> 12) & 0xFFFu);
}

static int seq_in(int n, uint16_t mask) { return (mask >> (n % 12)) & 1u; }

uint8_t seq_scale_snap(uint8_t note, uint16_t mask)
{
    int d, top = VAL_MAX - 1;
    if (mask == 0u || note > top)
        return note;
    for (d = 0; d < 12; d++) {                          /* every scale has a note within 6 semitones */
        if (note - d >= 0 && seq_in(note - d, mask))
            return (uint8_t)(note - d);
        if (note + d <= top && seq_in(note + d, mask))
            return (uint8_t)(note + d);
    }
    return note;
}

uint8_t seq_scale_step(uint8_t note, int16_t degrees, uint16_t mask, uint8_t top)
{
    int n = note, dir = degrees < 0 ? -1 : 1, k = degrees < 0 ? -degrees : degrees;
    if (mask == 0u) {
        n += degrees;
        return (uint8_t)(n < 0 ? 0 : n > top ? top : n);
    }
    while (k-- > 0) {
        int m = n + dir;
        while (m >= 0 && m <= top && !seq_in(m, mask))
            m += dir;
        if (m < 0 || m > top)
            break;                                      /* no further degree that way: stay */
        n = m;
    }
    return (uint8_t)n;
}
