#ifndef MFX_H
#define MFX_H

#include <stdint.h>

/* MFX (2026-11-01): one modulation effect per layer, on the layer's dry sum before its delay and reverb sends
 * (synth_runtime.c render_pass). Each type is a model of one analog unit, its four knobs that unit's own controls
 * (and a switch, as a row beside TYPE).
 *
 * STONE PHASER: a model of a classic 1970s op-amp phaser (docs/effect schematics): five all-pass stages swept by a
 * CMOS buffer used as voltage-controlled resistors, a sixth fixed one in the output mixer, positive feedback through
 * a pot. Knobs (2026-11-01, the user's): MANUAL (the sweep's centre: the pedal's MANUAL SHIFT, which on the unit
 * replaces the LFO in its MANUAL mode, made an offset), RATE, FEEDBACK, DEPTH (how far the sweep travels about the
 * centre: not on the unit; MANUAL 64 DEPTH 127 is its own full sweep, DEPTH 0 holds it still). Row: L/R PHASE (the
 * right side's sweep behind the left's, 0 .. half a cycle). The unit's AUTO / MANUAL switch is dropped.
 *
 * BULLROARER (2026-11-01, an original, prototype): a worn bucket-brigade chorus (the 1970s BBD units' tone: dark,
 * band-limited, soft at the top of its headroom, its hiss breathing with the signal through the compander) whose
 * delay is swung by a whirl that surges, stalls and swings back, like the cord of a bullroarer winding up. Knobs:
 * WHIRL (the orbit's rate), REACH (how far the delay swings: the bend, and the far side darker and quieter), TWIST
 * (the surge: 0 a steady whirl, 64 it slows to a stop each cycle, 127 it swings back), DUST (the BBD's age: darker,
 * hotter, hiss, its clock wandering). Row: L/R PHASE (as STONE PHASER's).
 *
 * ENSEMBLE (2026-11-01): a model of the 1970s string ensemble's chorus (docs/effect schematics, the Solina service
 * sheets): a slow chorus oscillator and a fast tremolo oscillator, mixed and split three ways 120 degrees apart, each
 * phase swinging the clock of its own bucket brigade; the three lines summed, no dry. Knobs, in STONE PHASER's places:
 * AGE (the brigades' clock: low and the delay longer, the tone darker, hiss, the clock wandering; the unit's 39K /
 * 560p, its parts drifted), RATE (the CHORUS FREQ oscillator), SHIMMER (the TREMOLO FREQ oscillator's share of the
 * control: 0 the slow sweep alone, 127 the fast flutter as strong as it), DEPTH (how far the control swings the
 * clocks). Row: WIDTH (0 the unit's mono sum; 127 the first line left, the second centre, the third right).
 *
 * ENS+PHASER (2026-11-02): the string ensemble into a 4-stage phaser, the late-70s string chain (docs/effect
 * schematics, small_stone.png: four OTA all-pass stages swept by one LFO current, the dry and the chain mixed at the
 * output, COLOR's feedback from the chain's end to its start). The ensemble's controls folded into two (the user's):
 * ENS RATE (the chorus oscillator; the tremolo follows at the unit's 8:1, softly) and ENS DEPTH (the clocks' swing,
 * the shimmer at the unit's own share), then PHASE RATE and PHASE DEPTH (the sweep's width about its middle). Rows:
 * AGE (as ENSEMBLE's), COLOUR (the pedal's switch made a feedback amount), WIDTH (as ENSEMBLE's).
 *
 * CHORUS (2026-11-02): the classic two-channel synth chorus (docs/effect schematics, alpha juno chorus.png: a BBD a
 * side, the clocks swung against each other by one triangle, a compander round each), made variable (the user's):
 * RATE, DEPTH, DELAY (the sweep's centre), MIX. Rows: WIDTH (the second side's LFO behind the first's: 127 the
 * classic opposite swing), AGE (as ENSEMBLE's). */
enum { MFX_OFF, MFX_STONE_PHASER, MFX_ENSEMBLE, MFX_ENS_PHASER, MFX_CHORUS, MFX_BULLROARER, MFX_COUNT };
#define MFX_OFFERED (MFX_CHORUS + 1)      /* the MFX page's TYPE list: BULLROARER (the prototype) is not offered */
enum { MFX_K_MANUAL, MFX_K_RATE, MFX_K_FEEDBACK, MFX_K_DEPTH };    /* STONE PHASER's knobs */
enum { MFX_K_WHIRL, MFX_K_REACH, MFX_K_TWIST, MFX_K_DUST };         /* BULLROARER's */
enum { MFX_K_AGE = 0, MFX_K_SHIMMER = 2 };                          /* ENSEMBLE's (RATE, DEPTH as above) */
enum { MFX_K_ENS_RATE, MFX_K_ENS_DEPTH, MFX_K_PH_RATE, MFX_K_PH_DEPTH };   /* ENS+PHASER's */
enum { MFX_K_CH_RATE, MFX_K_CH_DEPTH, MFX_K_CH_DELAY, MFX_K_CH_MIX };     /* CHORUS's */
enum { MFX_R_AGE, MFX_R_COLOUR };                                   /* further rows (row[]): ENS+PHASER's AGE, COLOUR;
                                                                     * CHORUS's AGE */

typedef struct {
    uint8_t type;                /* MFX_* */
    uint8_t knob[4];             /* 0..127, the type's own controls */
    uint8_t lr;                  /* the type's menu row, 0..127: L/R PHASE (the right side's LFO behind the left's),
                                  * ENSEMBLE's and ENS+PHASER's WIDTH */
    uint8_t row[2];              /* further rows, 0..127: ENS+PHASER's AGE, COLOUR; CHORUS's AGE */
} MfxParams;

/* each layer's MFX (not stored yet: the patch format bump comes with the CC map's) */
extern MfxParams mfx_params[2];

/* clears every layer's effect state (synth_init) */
void mfx_init(void);

/* the MFX page's peek at the effect as it moves (read by the UI, written by the audio): STONE PHASER's sweep on side i
 * (0, 1), ENSEMBLE's line i delay (0..2) as a share of the brigade's reach, and (3, 4) the shortest and longest its
 * lines swing to at these settings (ENS+PHASER: the same, and 5 its phaser's sweep); 0..1, or -1 when there is none */
float mfx_peek(int layer, int i);

/* the layer's MFX over n frames of its dry sum, in place (Q15 units, the bus's floats); nothing at OFF */
void mfx_layer_block(int layer, float *l, float *r, uint32_t n);

#endif
