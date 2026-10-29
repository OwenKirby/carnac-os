/* GLOBAL > TUNING and ROOT (2026-10-29): the performance's tuning, a cents offset from equal temperament per key.
 * The built-in tunings are 12-note tables, turned to ROOT; the four USER tunings are whole 128-key tables that arrive
 * over MIDI Tuning Standard SysEx (tuning_mts: a bulk dump, a single-note change or a scale/octave tuning; the web
 * installer turns a Scala .scl file into a bulk dump). The performance keeps TUNING and ROOT (PERF_TUNING /
 * PERF_TUNING_ROOT, PresetStructures.h); the USER tables are the instrument's, kept in the store's names chunk
 * (preset_store_fm1.c). Integer only. */
#pragma once
#include <stdint.h>
#include "PresetStructures.h"

#define TUNING_NKEY 128u
/* the USER tables, cents from equal temperament per key (0: in tune) */
extern int16_t tuning_user[TUNING_NUSER][TUNING_NKEY];
/* set when a non-realtime MTS message changed a USER table: the store saves the tables and clears it */
extern volatile uint8_t tuning_unsaved;

/* key's offset in cents from equal temperament under tuning (TUNING_*) turned to root (0..11, C = 0) */
int32_t tuning_key_cents(uint8_t tuning, uint8_t root, uint8_t key);

/* a MIDI Tuning Standard message, the bytes between F0 and F7 (universal 7E / 7F, sub-ID 08): bulk dump (01, bank +
 * 04), single-note change (02, bank + 07), scale/octave 1-byte (08) and 2-byte (09). A dump or a note change goes to
 * USER (program & 3) + 1; a scale/octave tuning to the USER tuning the performance plays (cur_tuning), else USER 1.
 * Non-realtime (7E) ones set tuning_unsaved. The USER slot it changed (0..TUNING_NUSER - 1), or -1: not one of these */
int tuning_mts(const uint8_t *p, uint32_t n, uint8_t cur_tuning);
