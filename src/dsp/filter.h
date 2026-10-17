#ifndef FILTER_H
#define FILTER_H
#include <stdint.h>
/* One filter core: its state and the settings of the current control tick. Each flavour is its own circuit:
 *   SVF      state-variable, the morph HP -> BP -> LP
 *   MULTI    the transistor ladder: four one-pole stages, the last fed back (inverted) to the input through a
 *            saturating input stage, its output mixed from the loop's input and the four stages (the Oberheim Xpander's
 *            trick), so one resonant circuit gives LP24, LP18, LP12, LP6, BP, HP12, HP24 and NOTCH; the morph walks
 *            through them, crossfading neighbours (it replaced the LADDER, whose slopes are its first four modes)
 *   MS-20    LOW / HIGH: the Korg 35 / Sallen-Key 2-pole, its lowpass or its highpass: two one-poles, the band between them fed back (positive) through a
 *            diode clipper, so the resonance screams but stays bounded; the morph drives the input into it
 *   FORMANT  two resonant SVF bandpasses in parallel, the second above the cutoff; the morph spreads them apart
 * 2026-10-15: single-precision float (the FM1's FPU: a multiply 7 cycles against 10 for int32, 15 for the int64 MACs
 * the fixed-point cores needed; docs/fm1-measure-checklist.md). Signals stay in Q15 units (full scale 32768.0f), so
 * the int samples convert in and out without a scale. The SVF, FORMANT and MULTI are zero-delay-feedback (TPT)
 * circuits: one pass a sample at 44.1 kHz, stable and in tune up to 14 kHz, where the fixed-point Chamberlin SVF
 * and ladder needed two passes (2x oversampling). The MS-20 keeps its one-pass explicit loop (its tuned growl). */
typedef struct {
float s1,s2;            // SVF: the two integrators' states (TPT ic1eq, ic2eq); FORMANT: the first band
float t1,t2;            // FORMANT: the second band's states
float s[4];             // MULTI: the four stages' TPT states; MS-20: s[0], s[1], plain one-poles
float c[3],dc[3];       // SVF: c[0] g, c[1] the base damping; FORMANT band 1: a1, a2, a3 (TPT); ramped by dc a sample toward the tick's values;
                        // MULTI: c[0] the stage gain G = g / (1 + g), c[1] the loop's solve 1 / (1 + k G^4); MS-20: c[0] g
float e[3],de[3];       // FORMANT: the second band's a1, a2, a3, ramped the same way
float k;                // SVF / FORMANT: damping (1 / Q; SVF: the tick's base, ramped in c[1]); MULTI, MS-20: the feedback (MULTI self-oscillates near 4, MS-20 near 2)
float m0,m1,m2;         // SVF: the morph's weights on hp, bp, lp (lp's with hp's folded in: hp = x - k bp - lp)
float bp;               // SVF: the last sample's band, which sets this sample's extra damping
float drive;            // MS-20: the input gain into the clipper; MULTI: the resonance's makeup gain
float makeup;           // MS-20: the output gain that undoes the drive
float tap[5];           // MULTI: the output's weights on the loop's input u and the four stages
uint8_t flavour;        // FLT_*, the circuit (FLT_MS20_HP runs as FLT_MS20 with hp set)
uint8_t hp;             // MS-20: the highpass output (FLT_MS20_HP)
} SvfCore;
typedef struct {
SvfCore core[2];
float parallel;         // routing: 0 = serial (1 into 2), ~1 = parallel
float bal2;             // balance: FILTER2's share of the output
} DualFilterMatrix;
void dual_filter_init(DualFilterMatrix *dfm);
/* Control rate: cutoffs in Q8 knob units, res / morph / routing / balance 0..127, flavour FLT_* per core. The
 * coefficients ramp to the new values over the 2^shift samples of the tick; snap = 1 jumps at once (a new note). */
void dual_filter_prepare(DualFilterMatrix *dfm,int cutoff1_q8,int res1,int morph1,int flavor1,int cutoff2_q8,int res2,int morph2,int flavor2,int routing_morph,int balance_morph,uint8_t shift,int snap);
/* Audio rate: one sample through both cores, routed and balanced (DUAL FILTER). */
int32_t dual_filter_render(DualFilterMatrix *dfm,int32_t input_sample_q15);
/* One core on its own, for the architectures that run each filter as a separate path (SINGLE, STACK, BINAURAL...).
 * raw_morph is filter.c's: SVF 0 = HP .. 127 = LP (the knob reversed). The other flavours read the knob's own way
 * round, 127 - raw_morph: MULTI 0 = LP24 .. 127 = NOTCH, MS-20 0 = clean .. 127 = driven hard (1x .. 24x), FORMANT 0 = the bands
 * close (about 2/3 octave) .. 127 = far apart (about 3 octaves). */
void filter_core_prepare(SvfCore *k,int cutoff_q8,int raw_res,int raw_morph,int flavor,uint8_t shift,int snap);
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
 * knob's own 0..127 (127 - raw_morph). */
/* FORMANT: the second band's distance above the cutoff in knob steps (13.4 an octave): 2/3 octave .. 3 octaves */
static inline int32_t filter_formant_gap(int knob_morph){return 9+(knob_morph*31)/127;}
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
/* SVF: the damping (1 / Q, Q12) for resonance 0..115: Q 0.707 at 0 (Butterworth, flat: no bump at the cutoff)
 * rising exponentially to about 12 at 115, so the peak grows by the same dB a step across the whole knob.
 * Generated: round(4096 * sqrt(2) * (0.085 / sqrt(2)) ** (r / 115)), r = 0..115. */
static const uint16_t filter_svf_damp_q12[116]={
5793,5653,5516,5383,5253,5126,5002,4881,4764,4648,4536,4427,4320,4215,4114,4014,
3917,3823,3730,3640,3552,3467,3383,3301,3221,3144,3068,2994,2921,2851,2782,2715,
2649,2585,2523,2462,2402,2344,2288,2232,2178,2126,2074,2024,1975,1928,1881,1836,
1791,1748,1706,1665,1625,1585,1547,1510,1473,1438,1403,1369,1336,1304,1272,1241,
1211,1182,1154,1126,1099,1072,1046,1021,996,972,949,926,903,882,860,840,
819,799,780,761,743,725,707,690,674,657,642,626,611,596,582,568,
554,541,528,515,502,490,478,467,456,445,434,423,413,403,393,384,
375,366,357,348};
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
static inline void filter_multi_taps(int knob_morph,int16_t *tap){
static const int16_t M[FILTER_MULTI_MODES][5]={
{0,0,0,0,128},{0,0,0,128,0},{0,0,128,0,0},{0,128,0,0,0},
{0,256,-256,0,0},{128,-256,128,0,0},{128,-512,768,-512,128},{128,-256,256,0,0}};
int32_t pos=knob_morph*(FILTER_MULTI_MODES-1),i=pos/127,fr=((pos-i*127)*128)/127;
if(i>=FILTER_MULTI_MODES-1){i=FILTER_MULTI_MODES-2;fr=128;}
for(int j=0;j<5;j++)tap[j]=(int16_t)((M[i][j]*(128-fr)+M[i+1][j]*fr)/128);
}
static inline const char *filter_multi_mode_name(int knob_morph){
static const char *const N[FILTER_MULTI_MODES]={"LP24","LP18","LP12","LP6","BP","HP12","HP24","NOTCH"};
return N[(knob_morph*(FILTER_MULTI_MODES-1)+63)/127];
}
/* 2^(e / 256) in Q8, e Q8 (-2048 .. 1279): the fraction on 1 + f (2/3 + f / 3), within 0.4 % */
static inline int32_t filter_exp2_q8(int32_t e_q8){
int32_t i=e_q8>>8,f=e_q8&255,m=256+((f*(171+((85*f)>>8)))>>8);
return i>=0?m<<i:m>>(-i);
}
/* MS-20: the input gain into the clipper it shares with the resonance, Q8, exponential 1x .. 24x (log2 24 = 1174 Q8), and the output's makeup,
 * drive^-1/16. A voice reaches the filter near full scale, already at the clipper's ceiling, so a makeup that undid
 * the small-signal gain (1 / sqrt(drive)) only turned it down: more drive sounded weaker. drive^-1/16 lets the clipped
 * wave's extra energy through: a full-scale saw (LP, cutoff 70) comes out about 2 dB louder by a quarter of the knob,
 * then holds within 0.5 dB to the top; a quarter-scale one about 12 dB louder. */
static inline int32_t filter_ms20_drive_q8(int knob_morph){return filter_exp2_q8((knob_morph*1174)/127);}
static inline int32_t filter_ms20_makeup_q8(int knob_morph){return filter_exp2_q8(-(knob_morph*1174)/(127*16));}
#endif
