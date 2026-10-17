/* Sequencer step data shared by the MAQ engine, the voice input adapters and PresetStructures.h.
 * mvave_fm1_master_specification.txt 7.3 (SequencerStep) and 7.5 (event registry).
 * Kept free of the PresetStructures.h parameter types (FilterParams, LfoParams) so the voice can
 * include it without a name clash. */
#pragma once
#include <stdint.h>

#define SEQ_NNOTE 4u
#define SEQ_NOTE_TIE 127u            /* note_buffer[0] full CW: PAUSE / TIE */
#define SEQ_NOTE_NULL 0xFFu          /* note_buffer[1..3]: empty slot */
#define SEQ_FLAG_MUTE (1u << 0)
#define SEQ_FLAG_TIE (1u << 1)       /* tie / legato */
#define SEQ_FLAG_SKIP (1u << 2)

enum {                               /* 7.5 event registry (spec numbers 01..14) */
    SEQEV_NOTE, SEQEV_DYNAMICS, SEQEV_TRANSPOSE, SEQEV_TIMING, SEQEV_GATE, SEQEV_RATCHET,
    SEQEV_PROB_SKIP, SEQEV_PROB_MUTE, SEQEV_MODWHEEL, SEQEV_AFTERTOUCH, SEQEV_CUSTOMCC1,
    SEQEV_CUSTOMCC2, SEQEV_PROG_CHANGE, SEQEV_LAYER, SEQEV_COUNT
};

/* 7.3: one step of one MAQ lane. 6 bytes, no padding (flash layout). */
typedef struct {
    uint8_t note_buffer[SEQ_NNOTE];  /* 4 pitch slots (0..127); slots 2..4 SEQ_NOTE_NULL when unused */
    uint8_t value;                   /* absolute data byte (0..127) for non-note events */
    uint8_t flags;                   /* SEQ_FLAG_* */
} SequencerStep;
#ifdef __cplusplus
static_assert(sizeof(SequencerStep) == 6u, "SequencerStep flash layout");
#else
_Static_assert(sizeof(SequencerStep) == 6u, "SequencerStep flash layout");
#endif
