/* The ARP page's Chord Genre Bank, generated into arp_chords.c by gen_arp_chords.py: the Roland J-6's 100 chord sets (its
 * manual pp. 25-33) in the J-6's order, then Phrygian. A chord key plays the J-6's chord for its pitch class, 1..4 notes:
 * arp_chord_off(genre, key % 12, off) gives them as semitones from the J-6's bass for that key (C3 .. E3, F2 .. B2), so a
 * key at note N plays N + off[i]: the bass is the key for the root-position chords, below or above it for the inversions
 * and slash chords. Stored in 3 bytes a chord (21 bits): the lowest offset + 20 in 6 bits, then 3 gaps of 5 bits. */
#pragma once
#include <stdint.h>
#include "PresetStructures.h"            /* SEQSCALE_* */

#define ARP_GENRE_COUNT 101u         /* the J-6's 100 sets in its order, then Phrygian (2026-10-17, the default) */
#define ARP_GENRE_PHRYGIAN 100u
PS_STATIC_ASSERT(ARP_GENRE_DEFAULT == ARP_GENRE_PHRYGIAN, "arp_defaults' genre is Phrygian");
#define ARP_CHORD_NOTES 4u           /* 8.1: a 4-voice chord array */

extern const char *const arp_genre_names[ARP_GENRE_COUNT];
extern const uint8_t arp_chord_pk[ARP_GENRE_COUNT][12][3];
/* the chord's notes, ascending, as semitones from the J-6's bass for the key; returns how many (1..4) */
static inline uint8_t arp_chord_off(uint8_t genre, uint8_t pc, int8_t off[ARP_CHORD_NOTES])
{
    const uint8_t *b = arp_chord_pk[genre][pc];
    uint32_t p = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16;
    uint8_t n = 1u, i;
    off[0] = (int8_t)((int)(p & 63u) - 20);
    for (i = 1u; i < ARP_CHORD_NOTES; i++) {
        uint8_t g = (uint8_t)(p >> (6u + 5u * (i - 1u)) & 31u);
        if (g == 0u) break;
        off[i] = (int8_t)(off[i - 1u] + g);
        n++;
    }
    return n;
}
/* each genre's scale, for a SEQ NOTE lane on SCALE = GENRE: SEQSCALE_* in bits 0..3, its root (pitch class) in 4..7.
 * Fitted to the genre's chords (the tables are in C), by hand for the genres of one chord type on every key */
extern const uint8_t arp_genre_scale[ARP_GENRE_COUNT];
