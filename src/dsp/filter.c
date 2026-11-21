#include "filter.h"
#include "dsp_clamp.h"
#include "PresetStructures.h"   /* FLT_* */
/* The TPT prewarped cutoff g = tan(pi fc / 44100) for knob 0..127: fc runs exponentially from 20 Hz (0) to 14 kHz
 * (127), fc = 20 * 700^(k / 127), 89.3 cents a step (13.4 steps an octave); 110 is about 5.8 kHz. SH-2, FORMANT
 * and MULTI run one zero-delay-feedback pass a sample on it. Generated: tan(pi * 20 * 700 ** (k / 127) / 44100), k = 0..127. */
static const float tpt_g_lut[128] = {
0.00142476f,0.001500182f,0.001579597f,0.001663216f,0.001751262f,0.001843969f,0.001941583f,0.002044364f,
0.002152587f,0.002266539f,0.002386523f,0.002512858f,0.002645882f,0.002785947f,0.002933428f,0.003088715f,
0.003252223f,0.003424387f,0.003605665f,0.00379654f,0.003997519f,0.004209138f,0.004431959f,0.004666577f,
0.004913615f,0.005173731f,0.005447617f,0.005736003f,0.006039657f,0.006359386f,0.006696042f,0.007050521f,
0.007423767f,0.007816774f,0.008230588f,0.008666311f,0.009125104f,0.009608188f,0.01011685f,0.01065244f,
0.0112164f,0.01181021f,0.01243547f,0.01309384f,0.01378707f,0.01451702f,0.01528562f,0.01609493f,
0.0169471f,0.01784442f,0.01878926f,0.01978416f,0.02083177f,0.02193488f,0.02309645f,0.02431957f,
0.02560752f,0.02696375f,0.02839187f,0.02989572f,0.03147933f,0.03314694f,0.03490302f,0.0367523f,
0.03869975f,0.0407506f,0.04291039f,0.04518495f,0.04758042f,0.05010329f,0.0527604f,0.05555897f,
0.05850663f,0.06161142f,0.06488185f,0.06832691f,0.07195608f,0.07577941f,0.07980752f,0.08405166f,
0.08852371f,0.09323631f,0.09820283f,0.1034375f,0.1089553f,0.1147723f,0.1209055f,0.1273731f,
0.1341945f,0.1413902f,0.1489825f,0.1569948f,0.1654527f,0.1743833f,0.1838159f,0.1937822f,
0.2043164f,0.2154555f,0.2272398f,0.2397132f,0.2529237f,0.2669238f,0.2817715f,0.2975307f,
0.3142723f,0.3320752f,0.351028f,0.37123f,0.3927934f,0.4158458f,0.4405332f,0.4670231f,
0.4955098f,0.5262196f,0.5594183f,0.5954207f,0.6346031f,0.677419f,0.7244213f,0.7762915f,
0.8338802f,0.8982643f,0.9708283f,1.053384f,1.148353f,1.259044f,1.390109f,1.548303f
};
/* The MS-20's one-pole coefficient g = 1 - exp(-2 pi fc / 44100), Q15, on the same cutoff scale. The MS-20 runs one
 * explicit pass a sample to cost about what the SVF does (its 2-pole loop stays stable at g up to 0.86; the price is
 * above). */
static const int16_t onepole_g44_lut[128] = {
94,98,104,110,114,120,128,134,142,148,156,164,174,181,191,201,
213,223,235,247,261,275,289,305,320,338,356,374,394,413,435,459,
482,508,536,563,593,624,655,691,728,765,804,847,892,938,987,1037,
1091,1149,1209,1271,1338,1407,1480,1556,1636,1719,1808,1901,1999,2102,2208,2322,
2439,2562,2693,2829,2973,3121,3279,3442,3615,3794,3983,4179,4385,4600,4825,5059,
5304,5559,5826,6104,6393,6694,7008,7335,7673,8026,8391,8770,9162,9567,9989,10423,
10871,11333,11810,12300,12803,13319,13849,14391,14945,15511,16087,16673,17267,17869,18478,19092,
19710,20331,20953,21574,22193,22807,23416,24016,24607,25186,25752,26301,26833,27347,27839,28310
};
void dual_filter_init(DualFilterMatrix *dfm){
for(int c=0;c<2;c++){
SvfCore *k=&dfm->core[c];
k->s1=k->s2=k->t1=k->t2=0.f;k->s[0]=k->s[1]=k->s[2]=k->s[3]=0.f;
for(int i=0;i<3;i++){k->c[i]=k->dc[i]=k->e[i]=k->de[i]=0.f;}
k->c[1]=1.f;k->e[0]=1.f;k->e[1]=1.f;       // MULTI: G = 0 (closed), its solve 1; FORMANT's a1 = 1, a2 = a3 = 0: closed
k->k=0.f;k->drive=1.f;k->makeup=1.f;
k->tap[0]=k->tap[1]=k->tap[2]=k->tap[3]=0.f;k->tap[4]=1.f;
k->flavour=FLT_MULTI;k->driven=0; // a closed lowpass until the voice's first control tick
k->ck_ok=0;k->lp=0;               // (lp: set with the taps, at the first prepare)
}
dfm->parallel=0.f;dfm->bal2=0.5f;
}
/* Q15-unit float -> the int sample, held to Q15 */
static inline int32_t f_to_q15(float y){
if(y>32767.f)return 32767;
if(y<-32768.f)return -32768;
return (int32_t)y;
}
static inline float fclampf(float x,float lo,float hi){return x>hi?hi:(x<lo?lo:x);}
/* A core's output, Q15 units: linear to 3/4 of full scale, then a soft knee y = T + d / (1 + d / R) (d the excess over
 * T, R the room left) that leaves the knee at slope 1 and only approaches full scale, so a loud resonant peak rounds
 * off like an analogue stage instead of clipping flat. One divide, only above the knee. */
#define SOFT_T 24576.f
#define SOFT_R 8192.f
static inline float soft_limit(float y){
float a=y<0.f?-y:y;
if(a<=SOFT_T)return y;
float d=a-SOFT_T;
a=SOFT_T+d/(1.f+d*(1.f/SOFT_R));
return y<0.f?-a:a;
}
/* The saturating stages (the ladder's input pair, the MS-20's diodes, the SH-2's input mixer and OTAs): a cubic soft
 * clip, unity slope at zero, flat at full scale (output 2/3 of 32768). Q15 units: x - x^3 / (3 * 32768^2). */
static inline float sat_f(float x){
x=fclampf(x,-32767.f,32767.f);
return x-x*x*x*(1.f/(3.f*32768.f*32768.f));
}
/* sat_f in the SH-2's own units, w = SH2_Q x: there the cubic is w - w^3, a multiply less (SH-2 runs six a sample).
 * SH2_Q = 1 / (sqrt 3 * 32768); the SH-2's input, ceiling and taps carry it (filter_core_prepare), so its output is
 * sat_f's to float rounding. */
#define SH2_Q (1.f/(1.7320508f*32768.f))
static inline float sat_w(float w){
w=fclampf(w,-32767.f*SH2_Q,32767.f*SH2_Q);
return w-w*w*w;
}
/* tanh, Pade (3,2), exact to 0.2 % below |x| 3 and meeting 1 there with zero slope: smooth, no corner (the SH-2's
 * resonance bound). 3 multiplies and a divide. */
static inline float tanh_f(float x){
x=fclampf(x,-3.f,3.f);
float x2=x*x;
return x*(27.f+x2)/(27.f+9.f*x2);
}
/* A table value for a cutoff in Q8 knob units (0..127 << 8), interpolated between the steps so a modulated cutoff
 * sweeps smoothly instead of in 13-cent steps. */
static float lut_g(const float *lut,int32_t cutoff_q8){
if(cutoff_q8>(127<<8))cutoff_q8=127<<8;
if(cutoff_q8<0)cutoff_q8=0;
int32_t i=cutoff_q8>>8;
if(i>=127)return lut[127];
return lut[i]+(lut[i+1]-lut[i])*(float)(cutoff_q8&255)*(1.f/256.f);
}
static int32_t lut_coeff(const int16_t *lut,int32_t cutoff_q8){
if(cutoff_q8>(127<<8))cutoff_q8=127<<8;
if(cutoff_q8<0)cutoff_q8=0;
int32_t i=cutoff_q8>>8,frac=cutoff_q8&255;
if(i>=127)return lut[127];
return lut[i]+(((lut[i+1]-lut[i])*frac)>>8);
}
/* The TPT SVF's three coefficients for g and damping k: a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2. Ramped
 * linearly across a tick they stay stable for every pair of cutoffs (checked: spectral radius < 1 throughout). */
static void svf_coeffs(float *a,float g,float k){
a[0]=1.f/(1.f+g*(g+k));a[1]=g*a[0];a[2]=g*a[1];
}
/* Set c toward n: at once (snap), or ramped by d over the 2^shift samples of the tick */
static void ramp_to(float *c,float *d,const float *n,int count,uint8_t shift,int snap){
float r=1.f/(float)(1u<<shift);
for(int i=0;i<count;i++){
if(snap){c[i]=n[i];d[i]=0.f;}
else d[i]=(n[i]-c[i])*r;
}
}
/* MS-10 (the Korg35 Sallen-Key, after T. Stinchcombe's study, docs/Korg Filter): the diode pair sits in the FORWARD
 * path, across the 58x amplifier the output is taken from, so a loud signal drops the loop's gain itself: the
 * resonance gives way, the cutoff sags and every frequency distorts, and a quiet one (a ramp's zero crossing) rings.
 * The diode amp: y = K (tanh(v) + v / 58), v = V3 / K, K the knee (Q15 units): gain 1 small, then the 1/58 slope.
 * The ringing is lopsided, as the chip's (the signal pulls on its transistors): cutoff and feedback lean with the
 * output's sign, higher on the negative half (simulated 1.7 vs 2 kHz). Resonance: its own law, the pot's linear
 * k from 0 to KMAX, just past the loop's edge (2 1/3 with the second section loading the first; the real one's
 * 58 / 26.5 x a JFET's ~1.1 = 2.4), so it rings more and more and only sings near the top. KMAX is that top as a
 * share of the edge; ms10_edge gives the edge where this sampled loop really has it (measured 2026-11-21 by
 * bisecting self-oscillation at each 8th cutoff step: 2.296 low down, the tanh's 1/58 leak taking the rest of 2 1/3;
 * it climbs as the one-poles lose loop gain at high cutoffs), so the knob means the same at every cutoff. It is held
 * from cutoff 120 (6.2) up: the top 7 steps (above ~9.8 kHz) fade instead of needing a feedback of 10. */
#ifndef MS10_KMAX
#define MS10_KMAX 1.067f
#endif
static const float ms10_edge_lut[16]={
2.2962f,2.2962f,2.2985f,2.3031f,2.3103f,2.3211f,2.3370f,2.3613f,2.3982f,2.4557f,2.5465f,2.6942f,2.9445f,3.3927f,4.2720f,6.2456f};
/* The sampled loop rings flat of the knob, more so as the cutoff rises (2.1 kHz at knob 95's 2.7). Its cutoff is
 * moved up by this many knob steps (Q8), so self-oscillation lands on the knob's pitch (measured 2026-11-21, every
 * 8th step); the two one-poles cannot ring above ~4.7 kHz, so from 112 the map just runs on to the top. */
static const int16_t ms10_warp_lut[17]={0,0,26,44,59,77,120,179,269,407,632,996,1626,2895,1920,1792,0};
static int32_t ms10_warp(int32_t cutoff_q8){
if(cutoff_q8<0)cutoff_q8=0;
if(cutoff_q8>(127<<8))cutoff_q8=127<<8;
int32_t p=cutoff_q8>>3,i=p>>8,w;               /* 8 knob steps a table step */
w=i>=16?0:ms10_warp_lut[i]+(((ms10_warp_lut[i+1]-ms10_warp_lut[i])*(p&255))>>8);
cutoff_q8+=w;
return cutoff_q8>(127<<8)?127<<8:cutoff_q8;
}
static float ms10_edge(int32_t cutoff_q8){
int32_t p=cutoff_q8>>3,i=p>>8;                /* 8 knob steps a table step */
if(i>=15)return ms10_edge_lut[15];
if(p<0)return ms10_edge_lut[0];
return ms10_edge_lut[i]+(ms10_edge_lut[i+1]-ms10_edge_lut[i])*(float)(p&255)*(1.f/256.f);
}
#ifndef MS10_KNEE
#define MS10_KNEE 32768.f
#endif
#ifndef MS10_LEAK
#define MS10_LEAK (1.f/58.f)
#endif
#ifndef MS10_GASYM
#define MS10_GASYM 0.08f
#endif
#ifndef MS10_KASYM
#define MS10_KASYM 0.05f
#endif

/* SH-2: the level the input mixer and the OTAs see at DRIVE 0 (clean), DRIVE's span above it (7 octaves: the top
 * drives as hard as MULTI's), the share of the feedback that boosts the input (before the mixer's clip), the
 * resonance's ceiling (the ringing peaks just under the output knee at the loop's edge, k 4) and the driven ringing's
 * flattening made up in the cutoff (cents, measured 2026-10-21) */
#define SH2_BASE 0.125f
#define SH2_DRIVE_Q8 1792
#define SH2_BASS 0.1f
#define SH2_RES_CEIL (0.92f*SOFT_T*4.f)
#define SH2_PITCH 96.f
/* MULTI: DRIVE's span (4 octaves into the input pair), the output eased 2^-0.05 an octave of it (the clipped ladder's
 * extra energy comes through, a little held back) */
#define MULTI_DRIVE_Q8 1024
#define MULTI_DRIVE_EASE 0.05f
/* The input clip's large-signal gain for an input gain L: a signal at the output knee comes out of sat_f at Le; a
 * makeup of 1 / Le gives small signals unity and a fully clipped one the knee (SH-2, FORMANT) */
static inline float clip_gain(float L){return sat_f(SOFT_T*L)*(1.f/SOFT_T);}
/* MS-20 / SH-2: the morph's weights on LP, BP, HP (0 LP .. 64 BP .. 127 HP, crossfaded), morph in Q8 knob units */
static void lbh_weights(int32_t morph_q8,float *wl,float *wb,float *wh){
float f=(float)morph_q8*(1.f/(63.5f*256.f));
if(f<=1.f){*wl=1.f-f;*wb=f;*wh=0.f;}
else{*wl=0.f;*wb=2.f-f;*wh=f-1.f;}
}
/* filter_feedback_q12 between the knob's steps, resonance in Q8 knob units (a smoothed knob turns through them):
 * the same value on each whole step */
static int32_t feedback_q8(int flavor,int32_t res_q8){
int32_t i=res_q8>>8,f=res_q8&255,a=filter_feedback_q12(flavor,i);
if(f==0)return a;
return a+(((filter_feedback_q12(flavor,i+1)-a)*f)>>8);
}
/* One core's settings for the tick. The coefficients ramp from where they are to the new ones over the tick (snap:
 * jump at once, on a new note). A change of flavour clears the state: one circuit's state is noise to another.
 * Resonance and morph come in Q8 knob units, as the cutoff does, so a smoothed knob moves them between its steps. */
void filter_core_prepare(SvfCore *k,int cutoff_q8,int res_q8,int morph_q8,int drive,int flavor,uint8_t shift,int snap){
if(res_q8>(115<<8))res_q8=115<<8;
if(res_q8<0)res_q8=0;
if(morph_q8<0)morph_q8=0;
if(morph_q8>(127<<8))morph_q8=127<<8;
if(drive<0)drive=0;
if(drive>127)drive=127;
if(flavor<0||flavor>=FLT_COUNT)flavor=FLT_MULTI;
if(k->flavour!=(uint8_t)flavor){
k->s1=k->s2=k->t1=k->t2=0.f;k->s[0]=k->s[1]=k->s[2]=k->s[3]=0.f;
snap=1;
k->ck_ok=0;
}
k->flavour=(uint8_t)flavor;
/* same: the resonance, morph and drive the last prepare had (SvfCore): what only they set is kept. The cutoff too:
 * the coefficients are the kept ones, only the ramp toward them is set (FILTER_NO_KEEP: every tick in full, the
 * reference the kept path is checked against) */
#if defined(FILTER_NO_KEEP)
#define FILTER_LP(k) 0
const int same=0;
#else
#define FILTER_LP(k) ((k)->lp)
const int same=k->ck_ok&&k->ck_res==res_q8&&k->ck_morph==morph_q8&&k->ck_drive==drive;
if(same&&k->ck_cut==cutoff_q8){
ramp_to(k->c,k->dc,k->cn,flavor==FLT_MULTI||flavor==FLT_MS10?2:3,shift,snap);
if(flavor==FLT_FORMANT)ramp_to(k->e,k->de,k->cn+3,3,shift,snap);
return;
}
#endif
k->ck_ok=1;k->ck_res=(int16_t)res_q8;k->ck_morph=(int16_t)morph_q8;k->ck_drive=(uint8_t)drive;k->ck_cut=cutoff_q8;
float *n=k->cn;
switch(flavor){
case FLT_MULTI:{
/* the feedback, and the mode's taps. The passband sinks by 1 / (1 + fb) as the resonance rises (the ladder's
 * thinning bass); a quarter of the feedback is made up at the input, with DRIVE's gain. The loop is solved for this
 * sample's output (zero delay): u = (x - k S) / (1 + k G^4), S the stages' stored part of the 4th's output. */
if(!same){
int32_t fb=feedback_q8(FLT_MULTI,res_q8),dq=(drive*MULTI_DRIVE_Q8)/127;
int16_t tap[5]={0,0,0,0,0};
k->ck_fb=fb;
k->drive=(1.f+(float)fb*(1.f/16384.f))*(float)filter_exp2_q8(dq)*(1.f/256.f);
filter_multi_taps_q8(morph_q8,tap);
float ease=(float)filter_exp2_q8(-(int32_t)(MULTI_DRIVE_EASE*(float)dq))*(1.f/256.f);
for(int i=0;i<5;i++)k->tap[i]=(float)tap[i]*(1.f/128.f)*ease;
k->lp=(uint8_t)(k->tap[0]==0.f&&k->tap[1]==0.f&&k->tap[2]==0.f&&k->tap[3]==0.f);
}
float g=lut_g(tpt_g_lut,cutoff_q8),G=g/(1.f+g),G2=G*G;
k->k=(float)k->ck_fb*(1.f/4096.f)*(1.f-0.3f*filter_res_taper(cutoff_q8));
n[0]=G;n[1]=1.f/(1.f+k->k*G2*G2);
ramp_to(k->c,k->dc,n,2,shift,snap);
return;
}
case FLT_MS10:{
/* MS-10: DRIVE drives the input 1x .. 24x into the circuit (the diode amp distorts it), made up by drive^-1/16
 * (filter.h). The morph moves where the input goes in, as the Korg35's modes do: into R1 (LP), into C1's foot
 * beside the feedback (BP) or into C2's foot (HP, the real one's 6 dB/oct with its resonant bump): taps 0..2, the
 * drive folded in. The BP is made up: unity at its peak at resonance 0 (1 / 2 1/3 bare), 1.7x toward the edge
 * (level with the old MS-20's BP there). */
if(!same){
float wl,wb,wh,r;
r=(float)res_q8*(1.f/(115.f*256.f));
k->ck_a=MS10_KMAX*r;                         /* the resonance pot: linear, as VR13 sets k2 (x the edge, below) */
k->drive=(float)filter_ms20_drive_q8(drive)*(1.f/256.f);
k->makeup=(float)filter_ms20_makeup_q8(drive)*(1.f/256.f);
lbh_weights(morph_q8,&wl,&wb,&wh);
k->tap[0]=wl*k->drive;k->tap[1]=wb*(1.f+(4.f/3.f)*(1.f-r))*k->drive;k->tap[2]=wh*k->drive;
}
{
int32_t cw=ms10_warp(cutoff_q8);
float g=(float)lut_coeff(onepole_g44_lut,cw)*(1.f/32768.f);
k->k=k->ck_a*ms10_edge(cw);
n[0]=g;n[1]=g*(4.f/3.f)/(1.f+g*(1.f/3.f));   /* C1's node runs at 4/3 the rate (R1 || R2): 1 - (1 - g)^(4/3), near */
}
ramp_to(k->c,k->dc,n,2,shift,snap);
return;
}
case FLT_FORMANT:{
/* FORMANT: damping 1 / Q from 0.73 (resonance 0) down to 0.10 (115), eased back toward 0.73 at high cutoffs
 * (filter_res_taper). The level gives way as the resonance rises, as an analogue filter's passband does
 * (1 - 0.39 r^2: -0.7 dB at resonance 50, -3 dB at 100, -4.3 dB at 115). DRIVE above 0 clips the input as the
 * SH-2's mixer does (the same level and span), its makeup holding the level; at 0 the input goes straight in. */
if(!same){
float r=(float)res_q8*(1.f/(115.f*256.f)),comp=1.f-0.39f*r*r;
k->ck_kb=((float)((127<<8)-res_q8)*(180.f/256.f)+1200.f)*(1.f/32768.f);
k->makeup=0.5f*comp;                         /* the two bands averaged, and the resonance's give */
k->driven=(uint8_t)(drive>0);
if(drive>0){
k->drive=SH2_BASE*(float)filter_exp2_q8((drive*SH2_DRIVE_Q8)/127)*(1.f/256.f);
k->makeup*=1.f/clip_gain(k->drive);
}
k->ck_fb=filter_formant_gap_q8(morph_q8);
}
k->k=k->ck_kb;
k->k+=(0.734375f-k->k)*0.4f*filter_res_taper(cutoff_q8);
svf_coeffs(n,lut_g(tpt_g_lut,cutoff_q8),k->k);
ramp_to(k->c,k->dc,n,3,shift,snap);
svf_coeffs(n+3,lut_g(tpt_g_lut,cutoff_q8+k->ck_fb),k->k);
ramp_to(k->e,k->de,n+3,3,shift,snap);
return;
}
default:{
/* SH-2: MULTI's loop, each stage an OTA (sat on its own input - output). DRIVE sets the level the input mixer and
 * the OTAs see, L = SH2_BASE (clean) .. 128x it. The input mixer clips (run_ota), so the made-up output is 1 / Le
 * (clip_gain): small signals come out at unity, a fully clipped one peaks at the knee, so drive thickens the sound
 * without running it into the output limiter. The resonance's ceiling follows Le too. The passband thins with the
 * resonance (1 / (1 + k)); SH2_BASS of the feedback boosts the input, before the mixer's clip, and is not made up
 * after it (a made-up boost ran a clipped input past the knee). No high-cutoff taper: it self-oscillates to the top,
 * as the circuit does. LP24 -> BP -> HP24. */
if(!same){
int32_t fb=feedback_q8(FLT_MULTI,res_q8);
float kk=(float)fb*(1.f/4096.f);
float bass=1.f+SH2_BASS*kk;
float dr=SH2_BASE*(float)filter_exp2_q8((drive*SH2_DRIVE_Q8)/127)*(1.f/256.f)*bass;
float Le=clip_gain(dr),wl,wb,wh;
k->ck_kb=kk;
/* the pitch: past the edge (k 4) the ringing's level in the stages, Le (1 - 4 / k) squared in effect, slows the OTAs
 * and pulls it flat, by SH2_PITCH (Le / the clean level)^2 (1 - 4 / k) cents (-8 c at drive 0, -165 c driven),
 * less toward the top (the cutoff's taper, below). The cutoff is raised by as much: within 4 cents at drive 0 and
 * 127, 19 at worst between. */
k->ck_a=0.f;
if(kk>4.f){
float lr=Le/(SH2_BASE*bass);
k->ck_a=SH2_PITCH*lr*lr*(1.f-4.f/kk);
}
k->e[0]=SH2_RES_CEIL*Le*SH2_Q;k->e[1]=1.f/k->e[0];  /* the feedback's ceiling, with the level the OTAs see (run_ota),
                                                     in the SH-2's units (sat_w) */
k->makeup=1.f/Le;
k->drive=dr*SH2_Q;                           /* the input into the SH-2's units: Le above was taken unscaled */
/* LP24 y4, BP 4 (y2 - 2 y3 + y4) = 4 H^2 (1 - H)^2 (unity at the cutoff), HP24 (1 - H)^4 */
static const float LP[5]={0,0,0,0,1},BP[5]={0,0,4,-8,4},HP[5]={1,-4,6,-4,1};
lbh_weights(morph_q8,&wl,&wb,&wh);
for(int i=0;i<5;i++)k->tap[i]=(wl*LP[i]+wb*BP[i]+wh*HP[i])*(k->makeup*(1.f/SH2_Q));   /* back to Q15 units */
k->lp=(uint8_t)(k->tap[0]==0.f&&k->tap[1]==0.f&&k->tap[2]==0.f&&k->tap[3]==0.f);
}
k->k=k->ck_kb;
if(k->k>4.f){
float tp=filter_res_taper(cutoff_q8),cents=k->ck_a*(1.f-0.65f*tp*tp);
cutoff_q8+=(int)(cents*(256.f/89.3f));
}
float g=lut_g(tpt_g_lut,cutoff_q8),G=g/(1.f+g),G2=G*G;
n[0]=G;n[1]=1.f/(1.f+k->k*G2*G2);n[2]=g;
ramp_to(k->c,k->dc,n,3,shift,snap);
return;
}
}
}
void dual_filter_prepare(DualFilterMatrix *dfm,int cutoff1_q8,int res1_q8,int morph1_q8,int drive1,int flavor1,int cutoff2_q8,int res2_q8,int morph2_q8,int drive2,int flavor2,int routing_morph,int balance_morph,uint8_t shift,int snap){
filter_core_prepare(&dfm->core[0],cutoff1_q8,res1_q8,morph1_q8,drive1,flavor1,shift,snap);
filter_core_prepare(&dfm->core[1],cutoff2_q8,res2_q8,morph2_q8,drive2,flavor2,shift,snap);
dfm->parallel=(float)(routing_morph*258)*(1.f/32768.f);
dfm->bal2=(float)(balance_morph*258)*(1.f/32768.f);
}
/* One TPT SVF step (Zavalishin / Simper): returns v1, the band; *lo gets v2, the low. Linear, its states unclamped:
 * shrinking a TPT state each sample is heavy damping, not saturation (-4 dB at no resonance); a loud resonant peak is
 * rounded off by the core's output knee (soft_limit) instead. */
static inline float svf_step(float *ic1,float *ic2,const float *a,float x,float *lo){
float i1=*ic1,i2=*ic2;
float v3=x-i2;
float v1=a[0]*i1+a[1]*v3;
float v2=i2+a[1]*i1+a[2]*v3;
*ic1=2.f*v1-i1;
*ic2=2.f*v2-i2;
*lo=v2;
return v1;
}
static inline void ramp3(float *c,const float *d){c[0]+=d[0];c[1]+=d[1];c[2]+=d[2];}
/* FORMANT: two resonant bandpasses side by side, each its own SVF on the same input, averaged */
static float run_formant(SvfCore *k,float x){
if(k->driven)x=sat_f(x*k->drive);
float lo,b1=svf_step(&k->s1,&k->s2,k->c,x,&lo),b2=svf_step(&k->t1,&k->t2,k->e,x,&lo);
ramp3(k->c,k->dc);ramp3(k->e,k->de);
return (b1+b2)*k->makeup;
}
/* MULTI: u = sat(x - k y4) into four TPT one-poles (y = G in + (1 - G) s), y4 solved without delay (the
 * saturator taken after the linear solve: one nonlinearity in the loop, the ladder's input pair); the output mixed
 * from u and the stages by the slope or the mode */
static float run_ladder(SvfCore *k,float x){
float G=k->c[0],inv=k->c[1];
k->c[0]+=k->dc[0];k->c[1]+=k->dc[1];
float s0=k->s[0],s1=k->s[1],s2=k->s[2],s3=k->s[3];
float S=(((s0*G+s1)*G+s2)*G+s3)*(1.f-G);     /* y4 = G^4 u + S */
float u=sat_f((x*k->drive-k->k*S)*inv);
float v,y1,y2,y3,y4;
v=(u-s0)*G;y1=v+s0;k->s[0]=y1+v;
v=(y1-s1)*G;y2=v+s1;k->s[1]=y2+v;
v=(y2-s2)*G;y3=v+s2;k->s[2]=y3+v;
v=(y3-s3)*G;y4=v+s3;k->s[3]=y4+v;
return k->tap[0]*u+k->tap[1]*y1+k->tap[2]*y2+k->tap[3]*y3+k->tap[4]*y4;
}
/* MS-10, one sample. State a = C1's voltage (V2 less what drives its foot), b = C2's (V3 less the HP input).
 * V3 = b + hp x; the diode amp's output y (the filter's output) feeds C1's foot through the resonance, k y, beside the
 * BP input; V2 = a + k y + bp x. C1's node relaxes toward (3 lp x + V3) / 4 (R1 = R2 / 3) at 4/3 the rate, C2's
 * toward V2. Linear: 1 / (s^2 + (2 1/3 - k) s + 1). The rates and the feedback lean with t, the output's sign. */
#define MS10_STEP(xin,a,b,g,ga,kk,pl,pb,ph,yo) do{\
float v3_=(b)+(ph)*(xin),v_=v3_*(1.f/MS10_KNEE),t_=tanh_f(v_);\
yo=MS10_KNEE*(t_+v_*MS10_LEAK);\
float l_=1.f-MS10_GASYM*t_,f_=(kk)*(1.f-MS10_KASYM*t_)*(yo)+(pb)*(xin);\
(a)+=(ga)*l_*(((pl)*(xin)*3.f+v3_)*0.25f-(a)-f_);\
(b)+=(g)*l_*((a)+f_-v3_);\
}while(0)
static float run_ms10(SvfCore *k,float x){
float g=k->c[0],ga=k->c[1],y;
k->c[0]+=k->dc[0];k->c[1]+=k->dc[1];
float a=k->s[0],b=k->s[1];
MS10_STEP(x,a,b,g,ga,k->k,k->tap[0],k->tap[1],k->tap[2],y);
k->s[0]=a;k->s[1]=b;
return y*k->makeup;
}
/* SH-2: an OTA stage, y = s + g ota(in - y). The OTA's input is taken from the linear solve (in - y =
 * (in - s)(1 - G)), so small signals run exactly as MULTI's linear stages; big ones slew (the OTA's current limit) */
#define OTA_STAGE(in,s,yo) do{float d_=((in)-(s))*om;float v_=g*sat_w(d_);yo=v_+(s);s=yo+v_;}while(0)
static float run_ota(SvfCore *k,float x){
float G=k->c[0],inv=k->c[1],g=k->c[2],om=1.f-G;
k->c[0]+=k->dc[0];k->c[1]+=k->dc[1];k->c[2]+=k->dc[2];
float s0=k->s[0],s1=k->s[1],s2=k->s[2],s3=k->s[3];
float S=(((s0*G+s1)*G+s2)*G+s3)*(1.f-G);
/* the feedback bounded by a tanh at e[0] (1 / it in e[1]): the ringing compresses gradually as it grows, as an
 * analogue loop's does, with no flat top (the cubic's), so the stages stay clean at drive 0 and a zap is not squashed.
 * The loop is solved linearly (u), its feedback part f = drive x - u then bounded. tanh_f: 3 mul, 1 div a sample. */
float xd=sat_w(x*k->drive),u=(xd-k->k*S)*inv,f=xd-u;   /* the input mixer clips (a 2nd cubic a sample) */
u=xd-k->e[0]*tanh_f(f*k->e[1]);
float y1,y2,y3,y4;
OTA_STAGE(u,s0,y1);OTA_STAGE(y1,s1,y2);OTA_STAGE(y2,s2,y3);OTA_STAGE(y3,s3,y4);
k->s[0]=s0;k->s[1]=s1;k->s[2]=s2;k->s[3]=s3;
return k->tap[0]*u+k->tap[1]*y1+k->tap[2]*y2+k->tap[3]*y3+k->tap[4]*y4;
}
/* One sample of one core, Q15 units in and out, the output soft-limited inside Q15 */
static float core_run(SvfCore *k,float x){
float y;
switch(k->flavour){
case FLT_MULTI:y=run_ladder(k,x);break;
case FLT_MS10:y=run_ms10(k,x);break;
case FLT_FORMANT:y=run_formant(k,x);break;
default:y=run_ota(k,x);break;   /* FLT_SH2 */
}
return soft_limit(y);
}
float filter_core_render_f(SvfCore *k,int32_t input_q15){
return core_run(k,(float)input_q15);
}
/* ---- blocks: each flavour's loop is its run_* above, line for line, on locals ---- */
static void block_formant(SvfCore *k,const float *x,float *y,int n){
float xc[FILTER_BLOCK_MAX];
if(k->driven){                               /* DRIVE: the input clipped first (a cubic a sample, only then) */
float dr=k->drive;
for(int i=0;i<n;i++)xc[i]=sat_f(x[i]*dr);
x=xc;
}
float s1=k->s1,s2=k->s2,t1=k->t1,t2=k->t2,mk=k->makeup;
float c0=k->c[0],c1=k->c[1],c2=k->c[2],d0=k->dc[0],d1=k->dc[1],d2=k->dc[2];
float e0=k->e[0],e1=k->e[1],e2=k->e[2],f0=k->de[0],f1=k->de[1],f2=k->de[2];
for(int i=0;i<n;i++){
float xi=x[i];
float v3=xi-s2;
float b1=c0*s1+c1*v3;
float v2=s2+c1*s1+c2*v3;
s1=2.f*b1-s1;
s2=2.f*v2-s2;
float w3=xi-t2;
float b2=e0*t1+e1*w3;
float w2=t2+e1*t1+e2*w3;
t1=2.f*b2-t1;
t2=2.f*w2-t2;
c0+=d0;c1+=d1;c2+=d2;
e0+=f0;e1+=f1;e2+=f2;
y[i]=soft_limit((b1+b2)*mk);
}
k->s1=s1;k->s2=s2;k->t1=t1;k->t2=t2;
k->c[0]=c0;k->c[1]=c1;k->c[2]=c2;k->e[0]=e0;k->e[1]=e1;k->e[2]=e2;
}
static void block_ladder(SvfCore *k,const float *x,float *y,int n){
float G=k->c[0],inv=k->c[1],dG=k->dc[0],dinv=k->dc[1];
float s0=k->s[0],s1=k->s[1],s2=k->s[2],s3=k->s[3],dr=k->drive,kk=k->k;
float p0=k->tap[0],p1=k->tap[1],p2=k->tap[2],p3=k->tap[3],p4=k->tap[4];
for(int i=0;i<n;i++){
float g=G,iv=inv;
G+=dG;inv+=dinv;
float S=(((s0*g+s1)*g+s2)*g+s3)*(1.f-g);
float u=sat_f((x[i]*dr-kk*S)*iv);
float v,y1,y2,y3,y4;
v=(u-s0)*g;y1=v+s0;s0=y1+v;
v=(y1-s1)*g;y2=v+s1;s1=y2+v;
v=(y2-s2)*g;y3=v+s2;s2=y3+v;
v=(y3-s3)*g;y4=v+s3;s3=y4+v;
y[i]=soft_limit(p0*u+p1*y1+p2*y2+p3*y3+p4*y4);
}
k->c[0]=G;k->c[1]=inv;k->s[0]=s0;k->s[1]=s1;k->s[2]=s2;k->s[3]=s3;
}
/* The plain lowpass (SvfCore.lp: the output is the last stage alone, the other taps exactly 0): block_ladder with the
 * four products and sums that add nothing left out (2026-11-12: 8 of the loop's ~48 float operations). The same
 * samples: a tap of 0 times a finite stage is a zero, and a zero added to the last stage's product leaves it as it
 * is (only a zero's sign can differ, which no later stage can turn into a value). */
static void block_ladder_lp(SvfCore *k,const float *x,float *y,int n){
float G=k->c[0],inv=k->c[1],dG=k->dc[0],dinv=k->dc[1];
float s0=k->s[0],s1=k->s[1],s2=k->s[2],s3=k->s[3],dr=k->drive,kk=k->k;
float p4=k->tap[4];
for(int i=0;i<n;i++){
float g=G,iv=inv;
G+=dG;inv+=dinv;
float S=(((s0*g+s1)*g+s2)*g+s3)*(1.f-g);
float u=sat_f((x[i]*dr-kk*S)*iv);
float v,y1,y2,y3,y4;
v=(u-s0)*g;y1=v+s0;s0=y1+v;
v=(y1-s1)*g;y2=v+s1;s1=y2+v;
v=(y2-s2)*g;y3=v+s2;s2=y3+v;
v=(y3-s3)*g;y4=v+s3;s3=y4+v;
y[i]=soft_limit(p4*y4);
}
k->c[0]=G;k->c[1]=inv;k->s[0]=s0;k->s[1]=s1;k->s[2]=s2;k->s[3]=s3;
}
static void block_ms10(SvfCore *k,const float *x,float *y,int n){
float G=k->c[0],dG=k->dc[0],GA=k->c[1],dGA=k->dc[1],a=k->s[0],b=k->s[1],kk=k->k,mk=k->makeup;
float p0=k->tap[0],p1=k->tap[1],p2=k->tap[2];
for(int i=0;i<n;i++){
float g=G,ga=GA,yo;
G+=dG;GA+=dGA;
MS10_STEP(x[i],a,b,g,ga,kk,p0,p1,p2,yo);
y[i]=soft_limit(yo*mk);
}
k->c[0]=G;k->c[1]=GA;k->s[0]=a;k->s[1]=b;
}
/* SH-2: run_ota line for line, its state and coefficients in locals over the block */
static void block_ota(SvfCore *k,const float *x,float *y,int n){
float G=k->c[0],inv=k->c[1],g=k->c[2],dG=k->dc[0],dinv=k->dc[1],dg=k->dc[2];
float s0=k->s[0],s1=k->s[1],s2=k->s[2],s3=k->s[3],dr=k->drive,kk=k->k,ce=k->e[0],ci=k->e[1];
float p0=k->tap[0],p1=k->tap[1],p2=k->tap[2],p3=k->tap[3],p4=k->tap[4];
for(int i=0;i<n;i++){
float Gi=G,iv=inv,gi=g,om=1.f-Gi;
G+=dG;inv+=dinv;g+=dg;
float S=(((s0*Gi+s1)*Gi+s2)*Gi+s3)*(1.f-Gi);
float xd=sat_w(x[i]*dr),u=(xd-kk*S)*iv,f=xd-u;
u=xd-ce*tanh_f(f*ci);
float y1,y2,y3,y4;
{float d_=(u-s0)*om,v_=gi*sat_w(d_);y1=v_+s0;s0=y1+v_;}
{float d_=(y1-s1)*om,v_=gi*sat_w(d_);y2=v_+s1;s1=y2+v_;}
{float d_=(y2-s2)*om,v_=gi*sat_w(d_);y3=v_+s2;s2=y3+v_;}
{float d_=(y3-s3)*om,v_=gi*sat_w(d_);y4=v_+s3;s3=y4+v_;}
y[i]=soft_limit(p0*u+p1*y1+p2*y2+p3*y3+p4*y4);
}
k->c[0]=G;k->c[1]=inv;k->c[2]=g;k->s[0]=s0;k->s[1]=s1;k->s[2]=s2;k->s[3]=s3;
}
/* SH-2 as the plain lowpass (LP24, the morph at 0): block_ota, the last stage alone (as block_ladder_lp) */
static void block_ota_lp(SvfCore *k,const float *x,float *y,int n){
float G=k->c[0],inv=k->c[1],g=k->c[2],dG=k->dc[0],dinv=k->dc[1],dg=k->dc[2];
float s0=k->s[0],s1=k->s[1],s2=k->s[2],s3=k->s[3],dr=k->drive,kk=k->k,ce=k->e[0],ci=k->e[1];
float p4=k->tap[4];
for(int i=0;i<n;i++){
float Gi=G,iv=inv,gi=g,om=1.f-Gi;
G+=dG;inv+=dinv;g+=dg;
float S=(((s0*Gi+s1)*Gi+s2)*Gi+s3)*(1.f-Gi);
float xd=sat_w(x[i]*dr),u=(xd-kk*S)*iv,f=xd-u;
u=xd-ce*tanh_f(f*ci);
float y1,y2,y3,y4;
{float d_=(u-s0)*om,v_=gi*sat_w(d_);y1=v_+s0;s0=y1+v_;}
{float d_=(y1-s1)*om,v_=gi*sat_w(d_);y2=v_+s1;s1=y2+v_;}
{float d_=(y2-s2)*om,v_=gi*sat_w(d_);y3=v_+s2;s2=y3+v_;}
{float d_=(y3-s3)*om,v_=gi*sat_w(d_);y4=v_+s3;s3=y4+v_;}
y[i]=soft_limit(p4*y4);
}
k->c[0]=G;k->c[1]=inv;k->c[2]=g;k->s[0]=s0;k->s[1]=s1;k->s[2]=s2;k->s[3]=s3;
}
void filter_core_render_block(SvfCore *k,const float *x,float *y,int n){
switch(k->flavour){
case FLT_MULTI:if(FILTER_LP(k))block_ladder_lp(k,x,y,n);else block_ladder(k,x,y,n);break;
case FLT_MS10:block_ms10(k,x,y,n);break;
case FLT_FORMANT:block_formant(k,x,y,n);break;
default:if(FILTER_LP(k))block_ota_lp(k,x,y,n);else block_ota(k,x,y,n);break;   /* FLT_SH2 */
}
}
void dual_filter_render_block(DualFilterMatrix *dfm,const float *x,float *y,int n){
float y1[FILTER_BLOCK_MAX],in2[FILTER_BLOCK_MAX];
float par=dfm->parallel,bal=dfm->bal2;
if(n<=0)return;
filter_core_render_block(&dfm->core[0],x,y1,n);
for(int i=0;i<n;i++)in2[i]=y1[i]+(x[i]-y1[i])*par;
filter_core_render_block(&dfm->core[1],in2,y,n);
for(int i=0;i<n;i++)y[i]=y1[i]+(y[i]-y1[i])*bal;
}
int32_t filter_core_render(SvfCore *k,int32_t input_q15){
return f_to_q15(filter_core_render_f(k,input_q15));
}
float dual_filter_render_f(DualFilterMatrix *dfm,int32_t input_sample_q15){
float x=(float)input_sample_q15;
float out1=core_run(&dfm->core[0],x);
float in2=out1+(x-out1)*dfm->parallel;        /* a blend of two in-range signals: in range */
float out2=core_run(&dfm->core[1],in2);
return out1+(out2-out1)*dfm->bal2;
}
int32_t dual_filter_render(DualFilterMatrix *dfm,int32_t input_sample_q15){
return f_to_q15(dual_filter_render_f(dfm,input_sample_q15));
}
