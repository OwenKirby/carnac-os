/* SEQ scale mode (2026-10-16): a NOTE lane's notes held to a scale (PresetStructures.h SEQSCALE_*). A scale is a
 * 12-bit mask of the pitch classes it allows, C = bit 0, already turned to its root; 0 means no scale (every note).
 * The MAQ snaps each note as it plays (maq_seq.c), the UI as it is entered: the knob steps the scale's degrees and the
 * keyboard's notes snap. SEQ_NOTE_TIE and SEQ_NOTE_NULL pass through untouched. Integer only. */
#pragma once
#include <stdint.h>
#include "PresetStructures.h"

/* the mask of a lane's scale: genre is the ARP's GENRE BANK (for SEQSCALE_GENRE); 0 = OFF */
uint16_t seq_scale_mask(uint8_t scale, uint8_t root, uint8_t genre);
static inline uint16_t seq_lane_scale_mask(const SequencerLane *lane, uint8_t genre)
{
    return lane->event_type == SEQEV_NOTE ? seq_scale_mask(lane->scale, lane->scale_root, genre) : (uint16_t)0u;
}
/* the nearest note in the scale (a tie: the lower), within 0..VAL_MAX - 1 (VAL_MAX reads as TIE in a step's first
 * slot); a note above VAL_MAX - 1 (TIE, NULL) and any note under mask 0 come back as they are */
uint8_t seq_scale_snap(uint8_t note, uint16_t mask);
/* note moved by degrees steps of the scale (from a note outside it, the first step lands on its neighbour in that
 * direction), held within 0 .. top; mask 0: by semitones */
uint8_t seq_scale_step(uint8_t note, int16_t degrees, uint16_t mask, uint8_t top);
