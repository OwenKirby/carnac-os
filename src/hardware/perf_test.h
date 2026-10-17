/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/* PERF TEST: the synth measures its own audio load on a fixed list of scenarios (docs/fm1-measure-checklist.md), in
 * turn: each is set up from the factory patch, settles 1 s, then is measured 3 s. Afterwards the live patches, the
 * performance, the sequencer and the arp are put back as they were and every voice restarts silent; the patch slots
 * are never touched.
 *
 * Portable: it calls only the synth (src/dsp) and the clock it is given, so the host runs the same code
 * (src/host/perf_test_host.c) as the FM1 (src/hardware/synth_main_fm1.c, SysEx ED_PERFT).
 *
 * Who does what:
 *   the audio side    per rendered half: pt_audio_half() with that half's load; renders silence while it returns 0
 *   the main loop     pt_start / pt_stop / pt_tick, all under the synth lock (they reset voices and patches);
 *                     pt_ui_frame() per screen frame, outside it
 * pt_tick's return value is the page the scenario shows (UIPAGE_*), or PT_PAGE_KEEP once the test is over. */
#include <stdint.h>
#include "dsp/synth_prof.h"

#define PT_SETTLE_MS 1000u
#define PT_MEASURE_MS 3000u
#define PT_OVER_HALVES 8u            /* this many halves in a row past their deadline: the scenario is cut (silence) */
#define PT_PAGE_KEEP 0xFFu

enum { PT_IDLE, PT_RUN, PT_DONE, PT_STOPPED };

typedef struct {
    uint16_t avg_pm;                 /* audio load, per mille of the half's deadline: the mean over the 3 s */
    uint16_t peak_pm;                /* .. the worst half */
    uint16_t late;                   /* halves the DMA overtook (heard as a click) */
    uint16_t ui_fps10;               /* screen frames drawn per second x 10 */
    uint16_t ui_max_ms;              /* the slowest screen frame (draw only) */
    uint8_t voices;                  /* most voices sounding at once */
    uint8_t over;                    /* cut by the guard after PT_OVER_HALVES late halves: the figures run to the cut */
    uint16_t cyc[SPROF_N];           /* the profile build (SYNTH_PROF): each stage's counter ticks a sample, else 0 */
} PtResult;
#define PT_HALF_FRAMES 512u          /* frames the audio side renders between two pt_audio_half calls */

uint8_t pt_count(void);
const char *pt_name(uint8_t i);
uint8_t pt_state(void);              /* PT_* */
uint8_t pt_scenario(void);           /* the one running (or the count, once done) */
const PtResult *pt_result(uint8_t i);

#define PT_WT_RAM 1u                 /* pt_start mode: the scenarios' wavetable banks read from a RAM copy, not XIP flash */
void pt_start(uint32_t now_ms, uint8_t mode);   /* ignored while running */
uint32_t pt_lap_cost(void);          /* the profile build: ticks one counter read costs (subtract one a lap) */
uint8_t pt_mode(void);               /* the mode of the run going on, or of the last one */
void pt_stop(void);                  /* abandon: restore, PT_STOPPED */
uint8_t pt_tick(uint32_t now_ms);

/* the audio side: one rendered half, its load in per mille of its deadline, and whether the DMA overtook it.
 * Returns 0 when the next half must be silence instead of a render (the guard cut the scenario). */
uint8_t pt_audio_half(uint32_t load_pm, uint8_t late);
uint8_t pt_audio_ok(void);           /* 0: skip the render (silence) */
void pt_ui_frame(uint32_t draw_us);
