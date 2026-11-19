/* The live sequence over MIDI (2026-11-19, the web editor's SEQ tab): one lane or one step as 7-bit SysEx bytes, in a
 * format of its own (not the flash layout), so the editor keeps working when FREC_VER_SEQ moves.
 *
 *   lane   = footer (SEQW_FOOT_N) + SEQ_NSTEP steps (SEQW_STEP_N each) = SEQW_LANE_N bytes
 *   footer = EVENT, TARGET, MD, PRESCALE, LOOP START, LOOP END, SCALE, ROOT
 *   step   = note 1..4, VALUE, FLAGS (SEQ_FLAG_*), EMPTY (bit i: note slot i is SEQ_NOTE_NULL, its byte 0)
 *
 * Unpacking checks and clamps as the panel's own controls would: an unknown EVENT refuses the whole lane, the rest is
 * held to its range (TARGET to seq_target_max of the EVENT, PRESCALE 1..32, loops 0..15, ...). Header only: the
 * firmware (synth_panel.c) and the host test (test_seq_wire.c) include it. */
#pragma once
#include <stdint.h>
#include "PresetStructures.h"

#define SEQW_FOOT_N 8u
#define SEQW_STEP_N 7u
#define SEQW_LANE_N (SEQW_FOOT_N + SEQ_NSTEP * SEQW_STEP_N)   /* 120 */

static inline void seqw_step_pack(const SequencerStep *s, uint8_t *o)
{
    uint8_t i, empty = 0u;
    for (i = 0u; i < SEQ_NNOTE; i++) {
        if (s->note_buffer[i] == SEQ_NOTE_NULL) {
            empty |= (uint8_t)(1u << i);
            o[i] = 0u;
        } else {
            o[i] = (uint8_t)(s->note_buffer[i] & 0x7Fu);
        }
    }
    o[4] = (uint8_t)(s->value & 0x7Fu);
    o[5] = (uint8_t)(s->flags & (SEQ_FLAG_MUTE | SEQ_FLAG_TIE | SEQ_FLAG_SKIP));
    o[6] = empty;
}

static inline void seqw_step_unpack(SequencerStep *s, const uint8_t *in)
{
    uint8_t i;
    for (i = 0u; i < SEQ_NNOTE; i++)
        s->note_buffer[i] = (in[6] >> i) & 1u ? (uint8_t)SEQ_NOTE_NULL : (uint8_t)(in[i] & 0x7Fu);
    s->value = (uint8_t)(in[4] & 0x7Fu);
    s->flags = (uint8_t)(in[5] & (SEQ_FLAG_MUTE | SEQ_FLAG_TIE | SEQ_FLAG_SKIP));
}

static inline void seqw_foot_pack(const SequencerLane *l, uint8_t *o)
{
    o[0] = l->event_type;
    o[1] = (uint8_t)(l->target_mask & 0x7Fu);
    o[2] = l->playback_mode;
    o[3] = l->prescale;
    o[4] = l->loop_start;
    o[5] = l->loop_end;
    o[6] = l->scale;
    o[7] = l->scale_root;
}

static inline uint8_t seqw_clamp(uint8_t v, uint8_t lo, uint8_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* 0: taken; 1: refused (an unknown EVENT), the lane untouched */
static inline uint8_t seqw_foot_unpack(SequencerLane *l, const uint8_t *in)
{
    if (in[0] >= SEQEV_COUNT)
        return 1u;
    l->event_type = in[0];
    l->target_mask = seqw_clamp(in[1], 0u, seq_target_max(in[0]));
    l->playback_mode = in[2] < SEQ_MODE_COUNT ? in[2] : (uint8_t)SEQ_MODE_FORWARD;
    l->prescale = seqw_clamp(in[3], SEQ_PRESCALE_MIN, SEQ_PRESCALE_MAX);
    l->loop_start = seqw_clamp(in[4], 0u, SEQ_NSTEP - 1u);
    l->loop_end = seqw_clamp(in[5], 0u, SEQ_NSTEP - 1u);
    l->scale = in[6] < SEQSCALE_COUNT ? in[6] : (uint8_t)SEQSCALE_OFF;
    l->scale_root = in[7] < 12u ? in[7] : 0u;
    return 0u;
}

static inline void seqw_lane_pack(const SequencerLane *l, uint8_t *o)
{
    uint8_t s;
    seqw_foot_pack(l, o);
    for (s = 0u; s < SEQ_NSTEP; s++)
        seqw_step_pack(&l->steps[s], o + SEQW_FOOT_N + s * SEQW_STEP_N);
}

static inline uint8_t seqw_lane_unpack(SequencerLane *l, const uint8_t *in)
{
    uint8_t s;
    if (in[0] >= SEQEV_COUNT)
        return 1u;
    (void)seqw_foot_unpack(l, in);
    for (s = 0u; s < SEQ_NSTEP; s++)
        seqw_step_unpack(&l->steps[s], in + SEQW_FOOT_N + s * SEQW_STEP_N);
    return 0u;
}
