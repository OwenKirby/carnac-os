#pragma once
/* Where the render's time goes, by stage: compiled in only with -DSYNTH_PROF (build_fm1.py --prof, or the host's
 * perf_test_host); otherwise every macro below is empty and the render is unchanged.
 *
 * The platform supplies synth_prof_now(), a free-running counter (the FM1: its core's cycle counter, X0X plat_cycles;
 * the host: the CPU's time-stamp counter). Each stage adds the counter's difference across it to synth_prof_acc. The
 * stages nest: VOICE is the whole per-sample voice render, OSC .. AMP split it for SINGLE / SPREAD (render_single);
 * TOTAL is the whole render, so what the stages do not cover (the clock, the sequencers, the loops) is TOTAL minus the
 * rest. Reading the counter costs a few cycles a lap: the profile reads a little high. */
#include <stdint.h>

enum { SPROF_TOTAL, SPROF_CTRL, SPROF_VOICE, SPROF_OSC, SPROF_EXTRAS, SPROF_FILTER, SPROF_AMP, SPROF_MIX, SPROF_FX,
       SPROF_CLOCK, SPROF_SEQ, SPROF_LTICK,
       SPROF_C_LFO, SPROF_C_ENV, SPROF_C_OSC, SPROF_C_FILT, SPROF_C_AMP, SPROF_N };
                                                         /* CLOCK: the MIDI clock a frame; SEQ: the MAQ and the ARP a
                                                          * frame; LTICK: the layer-wide control tick (LFO1, OSCMIXER,
                                                          * the voice limits) every SYNTH_CTRL_PERIOD frames. C_*: CTRL
                                                          * split (voice_control_tick): LFO2/3, the 3 envelopes, amp
                                                          * level + pitch + OSCMIXER + osc_prepare, the filter
                                                          * prepare, the amp's pan and mix gains */

#if defined(SYNTH_PROF)
uint32_t synth_prof_now(void);
extern volatile uint32_t synth_prof_acc[SPROF_N];
#define SPROF_DECL uint32_t sprof_t_
#define SPROF_START() (sprof_t_ = synth_prof_now())
#define SPROF_LAP(i) do { uint32_t sprof_n_ = synth_prof_now(); synth_prof_acc[i] += sprof_n_ - sprof_t_; \
                          sprof_t_ = sprof_n_; } while (0)
#else
#define SPROF_DECL
#define SPROF_START() ((void)0)
#define SPROF_LAP(i) ((void)0)
#endif
