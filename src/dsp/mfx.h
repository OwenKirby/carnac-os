#ifndef MFX_H
#define MFX_H

#include <stdint.h>
#include "preset.h"                     /* MfxParams (PresetStructures.h), engine_state */

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
 * classic opposite swing), AGE (as ENSEMBLE's).
 *
 * TAPE FLANGER (2026-11-02): through-zero flanging as the studios did it before any box could, with two tape machines
 * playing the same tape: a reference deck, and a second slowed by a thumb on its reel so it falls behind, catches up
 * and runs past. Both decks' heads and wobbles (a deck's own flutter and wander, the reference's too: the nulls are
 * never quite perfect), the tape's saturation, its hiss riding the level. Knobs: MANUAL (where the second deck sits
 * against the reference: ~32 in step with it, below that ahead, above behind), RATE (the thumb's sweep), DEPTH (how
 * far it pushes), TAPE (the machines' wear: hotter, darker heads, more flutter and hiss). Rows: POLARITY (POSITIVE
 * the decks summed; NEGATIVE one wired out of phase, the sound vanishing where they meet), L/R PHASE (as STONE
 * PHASER's; 0 is one tape for both sides).
 *
 * BBD FLANGER (2026-11-02): the late-70s rack stereo flanger (docs/effect schematics, roland sbf325.png, its block
 * diagram): a bucket brigade a channel, each with its FEEDBACK, the direct and the delay summed at the output, the
 * delay's phase switchable, one LFO (CENTER FREQ, RATE, DEPTH) whose sweep the second channel can take inverted.
 * Knobs: CENTER (the delay the sweep centres on), RATE, DEPTH, FEEDBACK. Rows: MODE (FLANGER I: the channels swept
 * alike; II: B's sweep inverted; III: as II with the delays crossed, A's out of B and B's out of A; CHORUS: no
 * feedback, the sweep inverted), POLARITY (TAPE FLANGER's row: POSITIVE, NEGATIVE the delay inverted).
 *
 * PITCH SHIFT (2026-11-02): the mid-70s digital pitch changer: two read heads circling a short memory (~16 ms), each
 * faded out as it wraps, the other faded in, the converters' grit; with feedback the shifted sound goes round again,
 * a step further each time (the famous spirals). Knobs: PITCH (-12 .. +12 semitones, gliding between), FINE (+-50
 * cents), FEEDBACK, MIX. Rows: SPREAD (the right side's FINE mirrored: 127 the classic stereo micro-pitch doubling,
 * one side up, the other down), AGE (the converters: fewer bits, a darker band, hiss).
 *
 * BBD ECHO (2026-11-02): a short bucket-brigade echo (4 .. 130 ms, slapback and doubling) whose clock or level
 * follows how hard it is played. One brigade a layer, mono as the pedals were, its clock about a quarter of the
 * rate (the long BBD's dark band); the right side reads further along it (WIDTH). Knobs: TIME, FEEDBACK (to the
 * edge of running away; the brigade's headroom holds it), ENV (the follower's amount, both ways from the middle),
 * MIX. Rows: ENV MODE (SWOOP: the playing speeds the clock up, ENV past the middle, or slows it, below: what is in
 * the brigade bends in pitch and settles back as the note fades; DUCK: the echo held down while playing and blooming
 * after, below the middle the other way, the echo only on what is played hard), AGE (a darker band, hiss, the clock
 * wandering), WIDTH. */
enum { MFX_OFF, MFX_STONE_PHASER, MFX_ENSEMBLE, MFX_ENS_PHASER, MFX_CHORUS, MFX_TAPE_FLANGER, MFX_BBD_FLANGER,
       MFX_PITCH_SHIFT, MFX_BBD_ECHO, MFX_BULLROARER, MFX_COUNT };
#define MFX_OFFERED (MFX_BBD_ECHO + 1)   /* the MFX page's TYPE list: BULLROARER (the prototype) is not offered */
enum { MFX_K_MANUAL, MFX_K_RATE, MFX_K_FEEDBACK, MFX_K_DEPTH };    /* STONE PHASER's knobs */
enum { MFX_K_WHIRL, MFX_K_REACH, MFX_K_TWIST, MFX_K_DUST };         /* BULLROARER's */
enum { MFX_K_AGE = 0, MFX_K_SHIMMER = 2 };                          /* ENSEMBLE's (RATE, DEPTH as above) */
enum { MFX_K_ENS_RATE, MFX_K_ENS_DEPTH, MFX_K_PH_RATE, MFX_K_PH_DEPTH };   /* ENS+PHASER's */
enum { MFX_K_CH_RATE, MFX_K_CH_DEPTH, MFX_K_CH_DELAY, MFX_K_CH_MIX };     /* CHORUS's */
enum { MFX_K_TF_MANUAL, MFX_K_TF_RATE, MFX_K_TF_DEPTH, MFX_K_TF_TAPE };   /* TAPE FLANGER's */
enum { MFX_K_BF_CENTER, MFX_K_BF_RATE, MFX_K_BF_DEPTH, MFX_K_BF_FEEDBACK };   /* BBD FLANGER's */
enum { MFX_K_PS_PITCH, MFX_K_PS_FINE, MFX_K_PS_FEEDBACK, MFX_K_PS_MIX };      /* PITCH SHIFT's (PITCH: 0..24, 12 none) */
enum { MFX_K_QE_TIME, MFX_K_QE_FEEDBACK, MFX_K_QE_ENV, MFX_K_QE_MIX };    /* BBD ECHO's (ENV: 64 none) */
enum { MFX_R_AGE, MFX_R_COLOUR, MFX_R_POLARITY = 1, MFX_R_MODE = 0, MFX_R_AGE2 = 1 };   /* further rows (row[]): ENS+PHASER's AGE,
                                                                     * COLOUR; CHORUS's and PITCH SHIFT's AGE; TAPE and
                                                                     * BBD FLANGER's POLARITY (0, 1); BBD FLANGER's MODE;
                                                                     * BBD ECHO's ENV MODE (MODE) and AGE (AGE2) */
enum { MFX_BF_FLANGER1, MFX_BF_FLANGER2, MFX_BF_FLANGER3, MFX_BF_CHORUS, MFX_BF_MODES };
enum { MFX_QE_SWOOP, MFX_QE_DUCK, MFX_QE_MODES };


/* each layer's MFX: its patch's (saved with it since the patch format's v11, 2026-11-03) */
#define MFX_PARAMS(layer) (engine_state[(layer) & 1].mfx)

/* clears every layer's effect state (synth_init) */
void mfx_init(void);

/* the MFX page's peek at the effect as it moves (read by the UI, written by the audio): STONE PHASER's sweep on side i
 * (0, 1), ENSEMBLE's line i delay (0..2) as a share of the brigade's reach, and (3, 4) the shortest and longest its
 * lines swing to at these settings (ENS+PHASER: the same, and 5 its phaser's sweep); TAPE FLANGER: the second deck
 * on side i (0, 1), 2 the reference (where they meet), 3 / 4 the thumb's sweep's ends; 0..1, or -1 when there is
 * none */
float mfx_peek(int layer, int i);

/* the layer's MFX over n frames of its dry sum, in place (Q15 units, the bus's floats); nothing at OFF */
void mfx_layer_block(int layer, float *l, float *r, uint32_t n);

/* The same in two parts (2026-11-02), so the two sides can run on the two cores: mfx_layer_plan works out the pass
 * (n <= MFX_PASS_MAX frames: the knobs, the LFOs, what both sides share; the type's change) and returns 1 when the
 * sides are left to run, 0 when nothing is (OFF; BULLROARER, the prototype, ran whole there); then
 * mfx_layer_side(layer, 0, l) and (layer, 1, r), in either order or at once: each writes only its own side's state
 * and samples. */
#define MFX_PASS_MAX 128u
int mfx_layer_plan(int layer, float *l, float *r, uint32_t n);
void mfx_layer_side(int layer, int side, float *y);

#endif
