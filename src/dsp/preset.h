#pragma once
#include <stdint.h>
#include "PresetStructures.h"

/* The one parameter store. mvave_fm1_master_specification.txt 4 (per-layer registry), 5.3 and 6 (performance
 * and patch links). Every value is read by the DSP from here and written by UIStateMachine. */

/* the live patch of each layer: what that layer's voices play */
extern EngineState engine_state[NLAYER];

/* the live performance: layer routing, voice modes, split, effects, GLOBAL */
extern MasterPerformancePreset perf_state;

/* the live MAQ sequencer slot (spec 7): what the sequencer plays and the SEQ page edits. SEQ_SETTINGS saves it and
 * the performance's seq_link names the stored slot it came from */
extern SequencerLane seq_lanes[SEQ_NLANE];

/* the live arp / chord pattern (spec 8): what the ARP engine plays and the ARP page edits. ARP_SEQ saves it and the
 * performance's arp_link names the stored slot it came from */
extern ArpState arp_state;
extern InstrumentSettings instrument;   /* GLOBAL's instrument settings (master tune, velocity curve, clock out) */
uint8_t instrument_velocity(uint8_t v);  /* VEL CURVE on an incoming note-on velocity */
int32_t instrument_tune_cents(void);     /* MASTER TUNE, -50..+50 */

/* stored patches. A layer plays whichever slot its perf_state.layer_patch names; patches are saved and
 * loaded per layer, so the two layers can hold different sounds */
extern EngineState patch_bank[PATCH_SLOTS];   /* in .pool on the FM1 (preset.c) */

/* the factory sequencer slot into lanes (preset_defaults uses it for seq_lanes) */
void seq_defaults(SequencerLane lanes[SEQ_NLANE]);

/* factory defaults for the live state and every patch slot (spec default values) */
void preset_defaults(void);
/* the same for the live state only (both layers, the performance, the sequencer and arp slots); the patch slots
 * keep what was saved (the FM1's PERF TEST) */
void live_defaults(void);
void engine_defaults(EngineState *e);   /* one layer's factory patch (the HOME browser measures a patch against it) */

/* live layer -> patch slot. The layer keeps playing its live sound; the slot holds a copy */
void patch_store(uint8_t layer, uint8_t slot);

/* patch slot -> live layer. The layer's perf_state.layer_patch records the slot it now plays */
void patch_load(uint8_t layer, uint8_t slot);
