#ifndef FILTER_H
#define FILTER_H
#include <stdint.h>
/* One filter core: its state and the settings of the current control tick. Each flavour is its own circuit:
 *   SH-2     the Roland OTA cascade (SH-2 VCF, 2026-10-21): MULTI's four-stage loop, each stage an OTA that slews on its
 *            own input - output, the input mixer clipping, the resonance bounded by a tanh (it rings up softly, no flat
 *            top) and kept in tune when driven; the morph LP24 -> BP -> HP24. It replaced the SVF.
 *   MULTI    the transistor ladder: four one-pole stages, the last fed back (inverted) to the input through a
 *            saturating input stage, its output mixed from the loop's input and the four stages (the Oberheim Xpander's
 *            trick), so one resonant circuit gives LP24, LP18, LP12, LP6, BP, HP12, HP24 and NOTCH; the morph walks
 *            through them, crossfading neighbours (it replaced the LADDER, whose slopes are its first four modes)
 *   MS-20    the Korg 35 / Sallen-Key 2-pole: two one-poles, the band between them fed back (positive) through a
 *            diode clipper, so the resonance screams but stays bounded; the morph LP -> BP -> HP (its outputs,
 *            crossfaded; LOW and HIGH were two flavours until 2026-10-21)
 *   FORMANT  two resonant SVF bandpasses in parallel, the second above the cutoff; the morph spreads them apart
 * DRIVE (every flavour, 0..127): the level into the circuit. SH-2: into the input mixer's clip and the OTAs, its makeup
 * holding the level; MULTI: into the ladder's input pair; MS-20: 1x .. 24x into the diode clipper (what its morph did
 * before); FORMANT: an input clip like the SH-2's, off at 0.
 * 2026-10-15: single-precision float (the FM1's FPU: a multiply 7 cycles against 10 for int32, 15 for the int64 MACs
 * the fixed-point cores needed; docs/fm1-measure-checklist.md). Signals stay in Q15 units (full scale 32768.0f), so
 * the int samples convert in and out without a scale. SH-2, FORMANT and MULTI are zero-delay-feedback (TPT)
 * circuits: one pass a sample at 44.1 kHz, stable and in tune up to 14 kHz, where the fixed-point Chamberlin SVF
 * and ladder needed two passes (2x oversampling). The MS-20 keeps its one-pass explicit loop (its tuned growl). */
typedef struct {
float s1,s2;            // FORMANT: the first band's states (TPT ic1eq, ic2eq)
float t1,t2;            // FORMANT: the second band's states
float s[4];             // MULTI, SH-2: the four stages' TPT states; MS-20: s[0], s[1], plain one-poles
float c[3],dc[3];       // FORMANT band 1: a1, a2, a3 (TPT); ramped by dc a sample toward the tick's values;
                        // MULTI, SH-2: c[0] the stage gain G = g / (1 + g), c[1] the loop's solve 1 / (1 + k G^4), SH-2 c[2] g; MS-20: c[0] g
float e[3],de[3];       // FORMANT: the second band's a1, a2, a3, ramped the same way; SH-2: e[0] the feedback's ceiling, e[1] 1 / it
float k;                // FORMANT: damping (1 / Q); MULTI, SH-2, MS-20: the feedback (MULTI, SH-2 self-oscillate near 4, MS-20 near 2)
float drive;            // the input gain: MS-20 into the clipper, MULTI into the input pair (with the resonance's makeup), SH-2 and FORMANT into the input clip
float makeup;           // MS-20, FORMANT: the output gain that undoes the drive (SH-2, MULTI: in the taps)
float tap[5];           // MULTI, SH-2: the output's weights on the loop's input u and the four stages; MS-20: on u, s[0], s[1]
uint8_t flavour;        // FLT_*, the circuit
uint8_t driven;         // FORMANT: DRIVE above 0, the input clipped
} SvfCore;
typedef struct {
SvfCore core[2];
float parallel;         // routing: 0 = serial (1 into 2), ~1 = parallel
float bal2;             // balance: FILTER2's share of the output
} DualFilterMatrix;
void dual_filter_init(DualFilterMatrix *dfm);
/* Control rate: cutoffs, res and morph in Q8 knob units (0..127 << 8, res to 115 << 8), drive / routing / balance 0..127, flavour FLT_* per core. The
 * coefficients ramp to the new values over the 2^shift samples of the tick; snap = 1 jumps at once (a new note). */
void dual_filter_prepare(DualFilterMatrix *dfm,int cutoff1_q8,int res1_q8,int morph1_q8,int drive1,int flavor1,int cutoff2_q8,int res2_q8,int morph2_q8,int drive2,int flavor2,int routing_morph,int balance_morph,uint8_t shift,int snap);
/* Audio rate: one sample through both cores, routed and balanced (DUAL FILTER). */
int32_t dual_filter_render(DualFilterMatrix *dfm,int32_t input_sample_q15);
/* One core on its own, for the architectures that run each filter as a separate path (SINGLE, STACK, BINAURAL...).
 * res_q8 and morph_q8 are the knobs' (with their mod) in Q8 knob units, as cutoff_q8. morph: SH-2 and MS-20 0 = LP .. 64 = BP .. 127 = HP, MULTI 0 = LP24 .. 127 = NOTCH,
 * FORMANT 0 = the bands close (about 2/3 octave) .. 127 = far apart (about 3 octaves). drive 0 = clean .. 127. */
void filter_core_prepare(SvfCore *k,int cutoff_q8,int res_q8,int morph_q8,int drive,int flavor,uint8_t shift,int snap);
int32_t filter_core_render(SvfCore *k,int32_t input_q15);
/* The same two renders with the output left in float, Q15 units inside +-32768 (the knee holds it): the voice's
 * float amp takes it as it is, without the round trip through int (~11 cycles a sample on the FM1). */
float filter_core_render_f(SvfCore *k,int32_t input_q15);
float dual_filter_render_f(DualFilterMatrix *dfm,int32_t input_sample_q15);
/* Blocks of n <= FILTER_BLOCK_MAX samples, float in (the int samples as float) and out: the same arithmetic as the
 * per-sample renders, in the same order, with the circuit's state and coefficients held in registers over the block
 * and the flavour picked once (the FM1 pays ~8 cycles a load: the per-sample calls reloaded everything). */
#define FILTER_BLOCK_MAX 32
void filter_core_render_block(SvfCore *k,const float *x,float *y,int n);
void dual_filter_render_block(DualFilterMatrix *dfm,const float *x,float *y,int n);
/* The circuits' settings from the knobs, shared with the FILTER pages' curves (ui_page_draw.c). knob_morph is the
 * knob's own 0..127. */
/* FORMANT: the second band's distance above the cutoff in knob steps (13.4 an octave): 2/3 octave .. 3 octaves */
static inline int32_t filter_formant_gap(int knob_morph){return 9+(knob_morph*31)/127;}
/* the same in Q8 knob steps for a morph in Q8 knob units: filter_formant_gap's steps, interpolated between the knob's */
static inline int32_t filter_formant_gap_q8(int32_t morph_q8){
int32_t i=morph_q8>>8,f=morph_q8&255,a=filter_formant_gap(i)<<8;
return f==0?a:a+(((filter_formant_gap(i+1)<<8)-a)*f>>8);
}
/* MULTI / MS-20: the feedback (Q12) for resonance 0..115. Up to 105 it climbs on 1 - (1 - r)^2 to the loop's edge
 * (4, 2) on 1 - (x + x^2) / 2 (x the distance to 105): half straight, so the Q climbs through the knob's upper half
 * instead of piling up just below the edge (squared alone put 90 .. 105 within 1 % of it, every input clipped to one
 * level); below the edge the resonance rings in proportion to what goes in, as an analogue filter's does. Past it
 * the loop sings on its own at its clipper's level (MULTI: the last 10 steps; MS-20: the last 35, see below). */
static inline int32_t filter_feedback_q12(int flavor,int raw_res){
/* 1 = FLT_MULTI (asserted in PresetStructures.h): the edge 4 at 105, 4.3 at the top. MS-20: the edge 2 at 80, then on to 3x the edge (6) at the top:
 * clean below 80, its own tone from about 80, and from about 95 the growl, the tone breaking up against the input */
int32_t edge=flavor==1?16384:8192,top=flavor==1?17613:24576,at=flavor==1?105:80;
if(raw_res>115)raw_res=115;
if(raw_res<0)raw_res=0;
if(raw_res>at)return edge+((raw_res-at)*(top-edge))/(115-at);
int32_t x=((at-raw_res)*16384)/at,c=16384-((x+((x*x)>>14))>>1);   /* 1 - (x + x^2) / 2, x the distance to the edge */
return (c*edge)>>14;
}
/* Resonance tapers off above about 1.6 kHz (cutoff knob 85), as an analogue filter's does, so a high resonant peak
 * rings without piercing: 0 below, 1 at the top of the range (cutoff Q8 knob units) */
static inline float filter_res_taper(int32_t cutoff_q8){
int32_t t=cutoff_q8-85*256;
return t<=0?0.f:t>=42*256?1.f:(float)t*(1.f/(42.f*256.f));
}
/* MULTI: the mode's weights on (u, stage 1 .. 4), Q7. With H = 1 / (1 + s) a stage, (1 - H) is a highpass pole and
 * H (1 - H) a bandpass, so LP24 .. LP6 H^4 .. H, BP 2 H (1 - H) (x2: unity peak), HP12 (1 - H)^2, HP24 (1 - H)^4 and
 * the notch 1 - 2H + 2H^2 (zero at the cutoff). The knob (0..127) walks LP24 -> .. -> NOTCH, crossfading neighbours. */
#define FILTER_MULTI_MODES 8
static inline void filter_multi_taps_q8(int32_t morph_q8,int16_t *tap){   /* morph in Q8 knob units */
static const int16_t M[FILTER_MULTI_MODES][5]={
{0,0,0,0,128},{0,0,0,128,0},{0,0,128,0,0},{0,128,0,0,0},
{0,256,-256,0,0},{128,-256,128,0,0},{128,-512,768,-512,128},{128,-256,256,0,0}};
int32_t pos=morph_q8*(FILTER_MULTI_MODES-1),i=pos/(127*256),fr=((pos-i*(127*256))*128)/(127*256);
if(i>=FILTER_MULTI_MODES-1){i=FILTER_MULTI_MODES-2;fr=128;}
for(int j=0;j<5;j++)tap[j]=(int16_t)((M[i][j]*(128-fr)+M[i+1][j]*fr)/128);
}
static inline void filter_multi_taps(int knob_morph,int16_t *tap){filter_multi_taps_q8((int32_t)knob_morph<<8,tap);}
static inline const char *filter_multi_mode_name(int knob_morph){
static const char *const N[FILTER_MULTI_MODES]={"LP24","LP18","LP12","LP6","BP","HP12","HP24","NOTCH"};
return N[(knob_morph*(FILTER_MULTI_MODES-1)+63)/127];
}
/* 2^(e / 256) in Q8, e Q8 (-2048 .. 1279): the fraction on 1 + f (2/3 + f / 3), within 0.4 % */
static inline int32_t filter_exp2_q8(int32_t e_q8){
int32_t i=e_q8>>8,f=e_q8&255,m=256+((f*(171+((85*f)>>8)))>>8);
return i>=0?m<<i:m>>(-i);
}
/* MS-20: DRIVE's input gain into the clipper it shares with the resonance, Q8, exponential 1x .. 24x (log2 24 = 1174 Q8), and the output's makeup,
 * drive^-1/16. A voice reaches the filter near full scale, already at the clipper's ceiling, so a makeup that undid
 * the small-signal gain (1 / sqrt(drive)) only turned it down: more drive sounded weaker. drive^-1/16 lets the clipped
 * wave's extra energy through: a full-scale saw (LP, cutoff 70) comes out about 2 dB louder by a quarter of the knob,
 * then holds within 0.5 dB to the top; a quarter-scale one about 12 dB louder. */
static inline int32_t filter_ms20_drive_q8(int drive){return filter_exp2_q8((drive*1174)/127);}
static inline int32_t filter_ms20_makeup_q8(int drive){return filter_exp2_q8(-(drive*1174)/(127*16));}
#endif
