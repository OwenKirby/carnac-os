/* The oscillators' PCM waves (2026-10-21): an oscillator plays either a wavetable bank or a PCM wave, chosen by the
 * one OscParams.wavebank byte, so a patch needs no new bytes and every architecture plays PCM:
 *
 *   wave code  0 .. 31   a wavetable bank (NUM_BANKS of them; the rest wrap, as before)
 *              32 .. 63  a firmware attack (attacks.h) as a one-shot wave: plays once at each note, then silence
 *              64 ..     a PCM wave from the store (pcm_store.h): an optional attack on top of loop A, and an optional
 *                        loop B that SCAN crossfades to
 * Each range has room to grow without moving a stored number. The panel lists them banks, PCM waves, attacks
 * (wave_list_*). A PCM wave that is not there (the store not uploaded) plays bank 0.
 *
 * Cost: at most two samples are read at once, as a wavetable reads two frames: loop A and loop B (one of them at
 * SCAN 0 or 1), or, while the attack plays, the attack and the loop nearer SCAN. So a PCM wave costs no voices.
 * SPREAD reads each loop at four detuned playheads (pcm_spread), as SPREAD reads a bank's two frames at four.
 * The loops run free (their positions keep turning between notes, as the wavetables' phase does); the attack starts
 * at each note (not a legato one). The weights ramp over a control tick, so SCAN and the attack's end never click.
 * Pitch: each sample (at most 65535 long) plays at its rate at its root note; the step is held to 4 samples per
 * engine sample. Sync and phase modulation do nothing on a PCM wave; FM and the other cross-mods work. */
#ifndef PCM_OSC_H
#define PCM_OSC_H
#include <stdint.h>

#define WAVE_ATK_FIRST 32u
#define WAVE_PCM_FIRST 64u

typedef struct {
    const uint8_t *pa, *pb, *pk;    /* loop A, loop B (NULL: none), the attack while it plays (NULL: none) */
    uint32_t len_a, len_b;          /* the loops' lengths, Q16 samples */
    uint32_t pos_a, pos_b, pos_k;   /* read positions, Q16 samples */
    uint32_t step_a, step_b, step_k;
    uint32_t end_k;                 /* where the attack's reads stop: (length - 1) << 16 */
    int32_t wa, wb, wk;             /* the weights now, Q15 (32768 = 1) */
    int32_t ta, tb, tk;             /* this tick's targets */
    int32_t da, db, dk;             /* each sample's step toward them, for `ramp` more samples (then exactly there) */
    int32_t xf;                     /* SCAN as a crossfade, Q15: 0 = A, 32768 = B */
    float inv_freq;                 /* 1 / the tick's frequency (Q16 Hz): FM scales the steps by freq * this */
    uint8_t code;                   /* the wave playing (0: none) */
    uint8_t on;                     /* 1: this oscillator plays PCM (osc_select_wave) */
    uint8_t trig;                   /* 1: a note started: the attack starts at the next tick */
    uint8_t ramp;
    uint8_t slive;                  /* SPREAD: bit i, playhead i sounds (its steps within 4 samples) */
    int32_t sxf;                    /* SPREAD's crossfade from the undetuned reads, Q15: 0 = no spread */
    uint32_t spa[4], spb[4];        /* SPREAD: each loop's four detuned read positions, Q16 samples */
    uint32_t ssa[4], ssb[4];        /* and their steps */
} PcmOsc;

void pcm_osc_init(void);            /* the mu-law decode table, in RAM */
/* control rate: the wave's samples, steps and weights for this pitch (Q16 Hz) and SCAN (morph_q12, 0 .. 7 << 12) */
void pcm_prepare(PcmOsc *q, uint32_t freq_q16, int morph_q12);
/* control rate, after pcm_prepare: SPREAD's four detuned playheads in each loop at these pitch ratios (Q28, the
 * wavetables' spread_ratio_q28 row), crossfaded in from the undetuned reads by xfade_q15 (0: no spread). They start
 * from the undetuned positions when a spread starts, as the wavetables' do. The attack plays undetuned on top. */
void pcm_spread(PcmOsc *q, const uint32_t *ratio_q28, int32_t xfade_q15);
void pcm_render_block(PcmOsc *q, int32_t *out, int n);
int32_t pcm_render_one(PcmOsc *q);
int32_t pcm_render_one_fm(PcmOsc *q, uint32_t freq_q16);   /* audio-rate FM: this sample's frequency */
void pcm_idle(PcmOsc *q, int n);    /* a silent voice: the loops keep turning, n samples */

/* the panel's wave list: banks, then the store's PCM waves, then the attacks; position <-> wave code */
uint32_t wave_list_count(void);
uint8_t wave_list_code(uint32_t pos);
uint32_t wave_list_pos(uint8_t code);
const char *wave_name(uint8_t code, char *buf13);   /* the name to show (a store name copied into buf13) */
/* what a code plays now: 0 a bank, 1 an attack, 2 a store wave; -1 nothing there (it plays bank 0) */
int wave_kind(uint8_t code);
/* the panel's picture of a wave at SCAN (morph_q12) and a 32-bit phase across the plot: a bank's cycle (level 1), a
 * PCM wave's first 512 samples of loop A crossfaded to B, an attack's first 1024; Q15 */
int32_t wave_preview(uint8_t code, int morph_q12, uint32_t phase);
/* the stones under the OSC page's plot: a bank's frames (8), a PCM pair's loops (2), one loop or an attack (1) */
int wave_stones(uint8_t code);

#endif
