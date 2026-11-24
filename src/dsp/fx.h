#pragma once
#include <stdint.h>
#include "PresetStructures.h"
#include "preset.h"                         /* perf_state: the MIX page's bytes (fx_live) */

/* The shared effects block (spec 4.6 EFFECTS): a delay and a reverb in parallel, mono, Q15. Each layer reaches
 * them through its AMP sends: the delay send feeds the delay, the reverb send feeds the reverb. The sum of both is the
 * wet signal, added to the dry mix. Since 2026-11-02 each may feed the other too (fx_cross, below; both up, a loop). */

void fx_init(void);
/* DELAY TIME's knob (also the UI's text, ui_sm_dtime_text): under DELAY SYNC the division's index, 0 (1/24) .. 11 (3/4, a dotted half);
 * unsynced the delay in samples */
uint8_t fx_sync_div(uint8_t v, int stereo);   /* the division the knob plays (an index into the fixed list), the same at any tempo */
uint8_t fx_sync_count(int stereo);            /* how many divisions the mode offers */
const char *fx_sync_name(uint8_t div);        /* its name: "1/16T" .. "1/4" */
int fx_sync_capped(uint8_t v, int stereo);    /* 1 when the division is longer than the line at this tempo (below SYNC_FLOOR_BPM) */
uint32_t fx_delay_free_len(uint8_t v);
uint32_t fx_delay_free_len_m(uint8_t v, int stereo);
/* DELAY SYNC's tempo: frames per 24 PPQN pulse, Q8 (midi_clock_period_q8; the runtime sets it each pass). 120 BPM
 * until then (and in the host tests that run the effects alone) */
extern uint32_t fx_pulse_q8;

/* One sample. delay_in_q15 is the summed delay sends, reverb_in_q15 the summed reverb sends.
 * Returns the delay and reverb wet outputs summed, Q15 mono. p is the live performance (EFFECTS page). */
int32_t fx_process_sample(const MasterPerformancePreset *p, int32_t delay_in_q15, int32_t reverb_in_q15);

/* The same over a block of n <= FX_BLOCK_MAX samples, stereo: delay_in the delay sends, reverb_io the reverb sends,
 * overwritten by the left wet output; wet_r the right. The sends may pass full scale (each effect limits its own,
 * 2026-10-16), and the wet outputs are not clipped to Q15 either: the caller's master limiter takes them (the delay is stereo with PING PONG, the ROOM network mono, the
 * MOD / DOLMEN / TUMULUS tank stereo). The FM1 runs its two parts on the two cores: fx_delay_block, fx_reverb_block (independent state), then
 * fx_mix_block. */
#define FX_BLOCK_MAX 128u
void fx_process_block(const MasterPerformancePreset *p, const int32_t *delay_in, int32_t *reverb_io, int32_t *wet_r,
                      uint32_t n);
void fx_delay_block(const MasterPerformancePreset *p, const int32_t *in_l, const int32_t *in_r, int32_t *out_l, int32_t *out_r,
                    uint32_t n);   /* in_r NULL: in_l is the mono send */
void fx_reverb_block(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n);
/* fx_reverb_block in its two halves, which share no state but a ring: the front takes the send (the tank's
 * input diffusion), the back gives the wet left (x) and right (r) of the send the front took FX_BLOCK_MAX samples
 * earlier. The FM1 runs the front on core 0 beside the delay, the back on core 1, at the same time. */
void fx_reverb_front(const MasterPerformancePreset *p, const int32_t *send, uint32_t n);
void fx_reverb_back(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n);
void fx_mix_block(const int32_t *delay_l, const int32_t *delay_r, int32_t *rev_l, int32_t *rev_r, uint32_t n);

/* The MIX page (2026-11-02): the performance's mix, 0..127 each, saved with it since v11 (perf_state.mix; LOOP
 * TONE's thinner half is DELAY HP too, kept in its stored bits by fx_tone_sync).
 *  - BALANCE (DUAL / SPLIT): the two layers, a fader each after its MFX and before its sends (synth_runtime.c); 64
 *    both at full, towards 0 layer 2 fades to silence, towards 127 layer 1. SINGL: DRY / WET, the same byte: towards
 *    0 the effects fade, towards 127 the dry (after the sends: the echoes go on, the dub drop-out).
 *  - LOOP TONE: the repeats' tone, 64 off; below, a low-pass in the delay's feedback (each repeat darker than the
 *    last); above, DELAY HP's high-pass there (each thinner). The cross-feeds' band-limits follow it.
 *  - DLY > REV, REV > DLY, the cross-feeds: the delay's output into the reverb's send, the reverb's into the delay's;
 *    both up, a loop that blooms (each way band-limited, so it darkens a pass, and soft-clipped on its own headroom,
 *    so it holds). A pass: fx_cross_begin on core 0 before core 1 starts (each way's settings for the pass), then
 *    on core 0 fx_cross_to_dly (REV > DLY's result from the reverb's last pass into the delay's send), the delay,
 *    fx_cross_to_rev (this pass's echoes into the reverb's send) before fx_reverb_front; on core 1, after the back,
 *    fx_cross_rev_job (REV > DLY's chain on the reverb's out, for the next pass; 2026-11-20, it was core 0's).
 *    fx_process_block does the same, on one core. */
enum { FX_L_BALANCE, FX_L_TONE, FX_L_DLY_REV, FX_L_REV_DLY, FX_L_COUNT };
#define fx_live (perf_state.mix)                  /* (preset.h) */
PS_STATIC_ASSERT(FX_L_COUNT == PERF_NMIX, "the MIX page's bytes in the performance");
enum { FX_X_DLY_REV, FX_X_REV_DLY };
#define fx_cross (&fx_live[FX_L_DLY_REV])          /* the two cross-feeds, [FX_X_*] */
/* LOOP TONE's thinner half is DELAY HP (its 7 bits in delay_pingpong, 0 off, 1..127): turned, the knob writes them;
 * a loaded performance (or the bits changed elsewhere) sets the knob from them. Before the effects, on core 0 */
void fx_tone_sync(MasterPerformancePreset *p);
/* the loop's return as a share of its headroom (0..1, the louder way): how near it is to holding itself up (the MIX
 * page's picture; written by the audio) */
float fx_cross_level(void);
void fx_cross_begin(uint32_t n);
void fx_cross_to_dly(int32_t *delay_in, int32_t *delay_in_r, uint32_t n);   /* delay_in_r NULL: mono */
/* 1 while DLY > REV feeds the reverb (on, or fading out): only then does the reverb's front need this pass's echoes */
int fx_cross_to_rev_on(void);
void fx_cross_to_rev(const int32_t *delay_l, const int32_t *delay_r, int32_t *reverb_in, uint32_t n);
void fx_cross_rev_job(const int32_t *rev_l, const int32_t *rev_r, uint32_t n);
