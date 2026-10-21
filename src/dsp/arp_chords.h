/* The ARP page's Chord Genre Bank (mvave_fm1_master_specification.txt 8.2, 8.3), generated into arp_chords.c by
 * gen_arp_chords.py. A chord key plays a 4-voice chord whose bass is the key itself: arp_chord_iv[genre][key % 12]
 * holds the 4 voices as semitones above the key (voice 0 is always 0). */
#pragma once
#include <stdint.h>
#include "PresetStructures.h"            /* SEQSCALE_* */

#define ARP_GENRE_COUNT 84u          /* 8.2's list, in its order, then Phrygian (2026-10-17) */
#define ARP_GENRE_PHRYGIAN 83u
PS_STATIC_ASSERT(ARP_GENRE_DEFAULT == ARP_GENRE_PHRYGIAN, "arp_defaults' genre is Phrygian");
#define ARP_CHORD_NOTES 4u           /* 8.1: a 4-voice chord array */

extern const char *const arp_genre_names[ARP_GENRE_COUNT];
extern const uint8_t arp_chord_iv[ARP_GENRE_COUNT][12][ARP_CHORD_NOTES];
/* each genre's scale, for a SEQ NOTE lane on SCALE = GENRE: SEQSCALE_* in bits 0..3, its root (pitch class) in 4..7.
 * Fitted to the genre's chords (the tables are in C), by hand for the genres of one chord type on every key */
extern const uint8_t arp_genre_scale[ARP_GENRE_COUNT];
