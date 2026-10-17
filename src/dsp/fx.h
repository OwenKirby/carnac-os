#pragma once
#include <stdint.h>
#include "PresetStructures.h"

/* The shared effects block (spec 4.6 EFFECTS): a delay and a reverb in parallel, mono, Q15. Each layer reaches
 * them through its AMP sends: the delay send feeds the delay, the reverb send feeds the reverb. Neither feeds the
 * other; the sum of both is the wet signal, added to the dry mix. */

void fx_init(void);

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
void fx_delay_block(const MasterPerformancePreset *p, const int32_t *in, int32_t *out_l, int32_t *out_r, uint32_t n);
void fx_reverb_block(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n);
/* fx_reverb_block in its two halves, which share no state but a ring: the front takes the send (predelay, the tank's
 * input diffusion), the back gives the wet left (x) and right (r) of the send the front took FX_BLOCK_MAX samples
 * earlier. The FM1 runs the front on core 0 beside the delay, the back on core 1, at the same time. */
void fx_reverb_front(const MasterPerformancePreset *p, const int32_t *send, uint32_t n);
void fx_reverb_back(const MasterPerformancePreset *p, int32_t *x, int32_t *r, uint32_t n);
void fx_mix_block(const int32_t *delay_l, const int32_t *delay_r, int32_t *rev_l, int32_t *rev_r, uint32_t n);
