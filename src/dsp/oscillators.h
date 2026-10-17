
#ifndef OSCILLATORS_H
#define OSCILLATORS_H

#include <stdint.h>

#define NUM_TABLES  32
#define NUM_FRAMES  8
#define LUT_SAMPLES 512                /* = WAVE_SAMPLES in wavetables.h */

typedef struct {
    uint32_t phases_q16[4];      // 16.16 Fixed point playback playheads
    uint32_t step_increments[4]; // FIXED: Now explicitly allocated inside the struct boundary!
    uint32_t center_phase;       // undetuned playhead: the whole sound at spread 0, faded out over SPREAD_XFADE
    int bank;                    // 0 to NUM_BANKS - 1 (14): the wavetable bank
    int morph_q12;               // scan position across the 8 frames, Q12: 0 = frame 0, 7 << 12 = frame 7
    /* set once per control tick by osc_prepare, read every sample by osc_render */
    uint32_t center_step;        // the undetuned playhead's phase step
    int32_t xfade_q15;           // 0 = the undetuned playhead alone, 32768 = the four detuned ones alone
    uint8_t center_level;        // mip level of the undetuned playhead
    uint8_t levels[4];           // mip level of each detuned playhead
    uint8_t live;                // bit i: playhead i is below Nyquist and sounds
    /* the table reads, worked out once per tick by osc_prepare (the bank, scan and mip levels hold still within a
     * tick): the two frames either side of the scan and the fraction between them, and each playhead's level as an
     * offset into the frame, the shift from phase to sample index and the index wrap mask. [4] is the undetuned one. */
    const int16_t *frame_a, *frame_b;
    int32_t morph_frac_q15;      // 0: frame_a alone
    uint16_t rd_off[5];
    uint16_t rd_mask[5];
    uint8_t rd_shift[5];
} WavetableOsc;

/* One sample of two frames at a cycle phase: linear within each frame (the level at off, its index shift and mask),
 * then across them by morph_frac_q15. Integer on purpose: int16 tables read with a 1-cycle add and a 10-cycle
 * multiply a lerp, where float would pay a 6-cycle add twice for each (fm1-measure-checklist.md op costs). */
static inline int32_t wt_read_frames(const int16_t *fa, const int16_t *fb, int32_t morph_frac_q15, uint32_t off,
                                     uint32_t shift, uint32_t mask, uint32_t phase)
{
    uint32_t i0 = phase >> shift, i1 = (i0 + 1u) & mask;
    int32_t fr_q15 = (int32_t)((phase << (32u - shift)) >> 17);
    const int16_t *a = fa + off;
    int32_t s = a[i0];
    int32_t va = s + (((a[i1] - s) * fr_q15) >> 15);
    if (morph_frac_q15 == 0) return va;
    const int16_t *b = fb + off;
    s = b[i0];
    int32_t vb = s + (((b[i1] - s) * fr_q15) >> 15);
    return va + (((vb - va) * morph_frac_q15) >> 15);
}

/* Scan knob (0..127) in Q8 -> morph position in Q12 frames: scan * 7 / 127, by multiply (57795 = 7 * 16 / 127
 * in Q16). Full scale lands one step short of frame 7, so frame 6 + its fraction always has a next frame. */
static inline int scan_q8_to_morph_q12(int32_t scan_q8) { return (int)(((uint32_t)scan_q8 * 57795u) >> 16); }

/* Spread values over which the undetuned playhead crossfades into the four detuned ones. The four start in
 * phase and drift apart, so switching to them at once would make the level swell and dip at low spread. */
#define SPREAD_XFADE 24

void osc_init(WavetableOsc *osc);
/* One sample at a frequency given per sample (audio-rate FM): works out the steps and mip levels itself. */
int16_t osc_process_skulpt_spread_fixed(WavetableOsc *osc, uint32_t frequency_q16, int raw_spread);

/* Control rate: the steps, mip levels and spread crossfade for this pitch and spread, held until the next tick. */
void osc_prepare(WavetableOsc *osc, uint32_t frequency_q16, int raw_spread);
/* Audio rate: one sample from what osc_prepare set. The same sound as osc_process_skulpt_spread_fixed. */
int16_t osc_render(WavetableOsc *osc);
/* n samples of osc_render into out (int16 values), the same ones in the same order: the undetuned playhead with its
 * reads held in registers across the block; SPREAD's detuned playheads sample by sample. */
void osc_render_block(WavetableOsc *osc, int32_t *out, int n);
uint32_t mtof_q16(int note_number);

/* Starts every playhead (the undetuned one and SPREAD's four) at one 32-bit cycle phase. */
void osc_set_phase(WavetableOsc *osc, uint32_t phase);

/* One sample of the bank at a 32-bit cycle phase and mip level, interpolated linearly within each frame and
 * across the two frames either side of morph_q12, so a scan sweep crossfades smoothly through all 8 frames. */
int32_t wt_read_morph(int bank, int morph_q12, uint8_t level, uint32_t phase);

/* The mip level to read for a playback frequency (Hz in Q16): the richest level whose top harmonic stays below
 * Nyquist. Level L is alias-free up to 44100 / (512 >> L) Hz: 86 Hz at level 0, 2.7 kHz at level 5. Level 6
 * (harmonics to 4) is the last; above about 5.5 kHz its harmonics start to fold back, which is the top octave's limit. */
uint8_t wt_level_for_freq(uint32_t freq_q16);

#endif

