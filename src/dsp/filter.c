#include "filter.h"
#include "dsp_clamp.h"
#include "PresetStructures.h"   /* FLT_* */
/* The TPT prewarped cutoff g = tan(pi fc / 44100) for knob 0..127: fc runs exponentially from 20 Hz (0) to 14 kHz
 * (127), fc = 20 * 700^(k / 127), about 13.4 cents per step; 110 is about 5.8 kHz. The SVF, FORMANT and MULTI run
 * one zero-delay-feedback pass a sample on it. Generated: tan(pi * 20 * 700 ** (k / 127) / 44100), k = 0..127. */
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
k->c[1]=1.4142f;k->e[0]=1.f;                // SVF: g = 0 (closed), flat damping; FORMANT's a1 = 1, a2 = a3 = 0: closed
k->k=1.4f;k->bp=0.f;k->drive=1.f;k->makeup=1.f;
k->m0=0.f;k->m1=0.f;k->m2=1.f;k->tap[0]=k->tap[1]=k->tap[2]=k->tap[3]=0.f;k->tap[4]=1.f;
k->flavour=FLT_SVF;k->hp=0; // SVF lowpass until the voice's first control tick
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
/* The saturating stages (the ladder's input pair, the MS-20's diodes): a cubic soft clip, unity slope at zero, flat
 * at full scale (output 2/3 of 32768). Q15 units: x - x^3 / (3 * 32768^2). */
static inline float sat_f(float x){
x=fclampf(x,-32767.f,32767.f);
return x-x*x*x*(1.f/(3.f*32768.f*32768.f));
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
/* The feedback's diode pair: clipping at 4/3 full scale, as loud as the driven input it shares the loop's clipper
 * with, so past the loop's edge the resonance is a tone of its own that fights the input: it pulls into step with the
 * note and breaks loose again, the MS-20's warbly growl (measured: 20 .. 70 % of the energy off the note's harmonics
 * at a hot input, the resonance's own tone at a soft one). Clipped lower (G = 3, tried) the input always won and the
 * resonance only followed the note. A small bias mismatches the pair (asymmetric clipping, the even harmonics of
 * real diodes), its offset taken back at rest. k holds fb * G / 2 (filter_core_prepare). */
#define MS20_DIODE_G 1.f
#define MS20_DIODE_B 1000.f
#define MS20_DIODE_SB (MS20_DIODE_B-MS20_DIODE_B*MS20_DIODE_B*MS20_DIODE_B*(1.f/(3.f*32768.f*32768.f)))
/* One core's settings for the tick. The coefficients ramp from where they are to the new ones over the tick (snap:
 * jump at once, on a new note). A change of flavour clears the state: one circuit's state is noise to another. */
void filter_core_prepare(SvfCore *k,int cutoff_q8,int raw_res,int raw_morph,int flavor,uint8_t shift,int snap){
if(raw_res>115)raw_res=115;
if(raw_res<0)raw_res=0;
if(raw_morph<0)raw_morph=0;
if(raw_morph>127)raw_morph=127;
if(flavor<0||flavor>=FLT_COUNT)flavor=FLT_SVF;
uint8_t hp=(uint8_t)(flavor==FLT_MS20_HP);
if(hp)flavor=FLT_MS20;                       /* the same circuit, the other output */
int km=127-raw_morph;                        /* the knob's own direction, for every flavour but the SVF */
if(k->flavour!=(uint8_t)flavor){
k->s1=k->s2=k->t1=k->t2=k->bp=0.f;k->s[0]=k->s[1]=k->s[2]=k->s[3]=0.f;
snap=1;
}
k->flavour=(uint8_t)flavor;
k->hp=hp;
float n[3];
switch(flavor){
case FLT_MULTI:{
/* the feedback, and the mode's taps. The passband sinks by 1 / (1 + fb) as the resonance rises (the ladder's
 * thinning bass); a quarter of the feedback is made up at the input. The loop is solved for this sample's output
 * (zero delay): u = (x - k S) / (1 + k G^4), S the stages' stored part of the 4th's output. */
int32_t fb=filter_feedback_q12(FLT_MULTI,raw_res);
int16_t tap[5]={0,0,0,0,0};
float g=lut_g(tpt_g_lut,cutoff_q8),G=g/(1.f+g),G2=G*G;
k->k=(float)fb*(1.f/4096.f)*(1.f-0.3f*filter_res_taper(cutoff_q8));
k->drive=1.f+(float)fb*(1.f/16384.f);
n[0]=G;n[1]=1.f/(1.f+k->k*G2*G2);
ramp_to(k->c,k->dc,n,2,shift,snap);
filter_multi_taps(km,tap);
for(int i=0;i<5;i++)k->tap[i]=(float)tap[i]*(1.f/128.f);
return;
}
case FLT_MS20:
/* the positive feedback; the morph drives the input 1x .. 24x into the clipper, made up by drive^-1/16: dirtier as
 * it turns, never quieter (filter.h) */
k->k=(float)filter_feedback_q12(FLT_MS20,raw_res)*(MS20_DIODE_G*0.5f/4096.f)*(1.f-0.25f*filter_res_taper(cutoff_q8));
k->drive=(float)filter_ms20_drive_q8(km)*(1.f/256.f);
k->makeup=(float)filter_ms20_makeup_q8(km)*(1.f/256.f);
n[0]=(float)lut_coeff(onepole_g44_lut,cutoff_q8)*(1.f/32768.f);
ramp_to(k->c,k->dc,n,1,shift,snap);
return;
case FLT_FORMANT:{
/* FORMANT: damping 1 / Q from 0.73 (resonance 0) down to 0.10 (115), eased back toward 0.73 at high cutoffs
 * (filter_res_taper). The level gives way as the resonance rises, as an analogue filter's passband does
 * (1 - 0.39 r^2: -0.7 dB at resonance 50, -3 dB at 100, -4.3 dB at 115). */
float r=(float)raw_res*(1.f/115.f),comp=1.f-0.39f*r*r;
k->k=(float)((127-raw_res)*180+1200)*(1.f/32768.f);
k->k+=(0.734375f-k->k)*0.4f*filter_res_taper(cutoff_q8);
svf_coeffs(n,lut_g(tpt_g_lut,cutoff_q8),k->k);
ramp_to(k->c,k->dc,n,3,shift,snap);
k->makeup=0.5f*comp;                         /* the two bands averaged, and the resonance's give */
svf_coeffs(n,lut_g(tpt_g_lut,cutoff_q8+(filter_formant_gap(km)<<8)),k->k);
ramp_to(k->e,k->de,n,3,shift,snap);
return;
}
default:{
/* SVF: the damping from filter_svf_damp_q12 (Q 0.707 .. 12, even in dB), eased a quarter of the way back toward
 * flat at high cutoffs (filter_res_taper). g and the damping ramp over the tick and the coefficients are solved each
 * sample (run_svf), as the damping also rises with the band's level. The passband gives way as the resonance
 * rises, as an analogue filter's does (1 - 0.39 r^2: -3 dB at resonance 100, -4.3 dB at 115). */
float r=(float)raw_res*(1.f/115.f),comp=1.f-0.39f*r*r;
k->k=(float)filter_svf_damp_q12[raw_res]*(1.f/4096.f);
k->k+=(1.4142f-k->k)*0.25f*filter_res_taper(cutoff_q8);
n[0]=lut_g(tpt_g_lut,cutoff_q8);n[1]=k->k;
ramp_to(k->c,k->dc,n,2,shift,snap);
/* the morph's weights (64 steps a half: HP..BP, BP..LP): w_hp hp + w_bp bp + w_lp lp with hp = x - k bp - lp is
 * w_hp (x - k bp) + w_bp bp + (w_lp - w_hp) lp, k this sample's damping */
float wh=0.f,wb,wl=0.f;
if(raw_morph<64){wh=(float)(64-raw_morph)*(1.f/64.f);wb=(float)raw_morph*(1.f/64.f);}
else{wb=(float)(128-raw_morph)*(1.f/64.f);wl=(float)(raw_morph-64)*(1.f/64.f);}
k->m0=wh*comp;k->m1=wb*comp;k->m2=(wl-wh)*comp;
return;
}
}
}
void dual_filter_prepare(DualFilterMatrix *dfm,int cutoff1_q8,int res1,int morph1,int flavor1,int cutoff2_q8,int res2,int morph2,int flavor2,int routing_morph,int balance_morph,uint8_t shift,int snap){
filter_core_prepare(&dfm->core[0],cutoff1_q8,res1,morph1,flavor1,shift,snap);
filter_core_prepare(&dfm->core[1],cutoff2_q8,res2,morph2,flavor2,shift,snap);
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
/* SVF: above SVF_NL_T the damping rises with the square of the band's level (the last sample's: the coefficients
 * follow it, the signal path stays zero delay), as an analogue filter's stages run out of headroom: a resonant peak
 * that would pierce rounds itself off and stays warm, while below the threshold the resonance rings exactly as
 * set, loud or soft. More damping is always stable. */
#define SVF_NL_T (0.6f*32768.f)
#define SVF_NL_T2 (SVF_NL_T*SVF_NL_T)
#define SVF_NL (1.0f/(32768.f*32768.f))
static inline float svf_nl_damp(float bp){float e=bp*bp-SVF_NL_T2;return e>0.f?SVF_NL*e:0.f;}
static float run_svf(SvfCore *k,float x){
float g=k->c[0],kk=k->c[1]+svf_nl_damp(k->bp),a[3];
svf_coeffs(a,g,kk);
k->c[0]+=k->dc[0];k->c[1]+=k->dc[1];
float lo,bp=svf_step(&k->s1,&k->s2,a,x,&lo);
k->bp=bp;
return k->m0*(x-kk*bp)+k->m1*bp+k->m2*lo;
}
/* FORMANT: two resonant bandpasses side by side, each its own SVF on the same input, averaged */
static float run_formant(SvfCore *k,float x){
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
/* MS-20: u = clip(drive x + diode(fb (s1 - s2))) into two one-poles: the driven input and the resonance clip
 * together, so drive chokes the scream into a fat, dirty tone (the MS-20's own trade) as well as distorting; the lowpass s2, the highpass u - 2 s1 + s2,
 * its level made up after the drive (the clipping stays: that is the growl). With G = 1 / (1 + s) a
 * stage, the loop gives 1 / (s^2 + (2 - fb) s + 1): the resonance peaks as fb nears 2, the diodes holding it. */
static float run_ms20(SvfCore *k,float x){
float g=k->c[0];
k->c[0]+=k->dc[0];
float s0=k->s[0],s1=k->s[1];
float fb=(sat_f(k->k*(s0-s1)+MS20_DIODE_B)-MS20_DIODE_SB)*(2.f/MS20_DIODE_G);   /* the diodes clip the feedback */
float u=2.f*sat_f(0.5f*(x*k->drive+fb));     /* the driven input and the resonance clip together: drive chokes it */
s0+=g*(u-s0);
s1+=g*(s0-s1);
k->s[0]=s0;k->s[1]=s1;
return (k->hp?u-2.f*s0+s1:s1)*k->makeup;
}
/* One sample of one core, Q15 units in and out, the output soft-limited inside Q15 */
static float core_run(SvfCore *k,float x){
float y;
switch(k->flavour){
case FLT_MULTI:y=run_ladder(k,x);break;
case FLT_MS20:y=run_ms20(k,x);break;
case FLT_FORMANT:y=run_formant(k,x);break;
default:y=run_svf(k,x);break;
}
return soft_limit(y);
}
float filter_core_render_f(SvfCore *k,int32_t input_q15){
return core_run(k,(float)input_q15);
}
/* ---- blocks: each flavour's loop is its run_* above, line for line, on locals ---- */
static void block_svf(SvfCore *k,const float *x,float *y,int n){
float s1=k->s1,s2=k->s2,g=k->c[0],kb=k->c[1],dg=k->dc[0],dk=k->dc[1],bp=k->bp;
float m0=k->m0,m1=k->m1,m2=k->m2;
for(int i=0;i<n;i++){
float xi=x[i];
float kk=kb+svf_nl_damp(bp);
float a1=1.f/(1.f+g*(g+kk)),a2=g*a1,a3=g*a2;
float v3=xi-s2;
float v1=a1*s1+a2*v3;
float v2=s2+a2*s1+a3*v3;
s1=2.f*v1-s1;
s2=2.f*v2-s2;
bp=v1;
g+=dg;kb+=dk;
y[i]=soft_limit(m0*(xi-kk*v1)+m1*v1+m2*v2);
}
k->s1=s1;k->s2=s2;k->c[0]=g;k->c[1]=kb;k->bp=bp;
}
static void block_formant(SvfCore *k,const float *x,float *y,int n){
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
static void block_ms20(SvfCore *k,const float *x,float *y,int n){
float G=k->c[0],dG=k->dc[0],s0=k->s[0],s1=k->s[1],kk=k->k,dr=k->drive,mk=k->makeup;
uint8_t hp=k->hp;
for(int i=0;i<n;i++){
float g=G;
G+=dG;
float fb=(sat_f(kk*(s0-s1)+MS20_DIODE_B)-MS20_DIODE_SB)*(2.f/MS20_DIODE_G);
float u=2.f*sat_f(0.5f*(x[i]*dr+fb));
s0+=g*(u-s0);
s1+=g*(s0-s1);
y[i]=soft_limit((hp?u-2.f*s0+s1:s1)*mk);
}
k->c[0]=G;k->s[0]=s0;k->s[1]=s1;
}
void filter_core_render_block(SvfCore *k,const float *x,float *y,int n){
switch(k->flavour){
case FLT_MULTI:block_ladder(k,x,y,n);break;
case FLT_MS20:block_ms20(k,x,y,n);break;
case FLT_FORMANT:block_formant(k,x,y,n);break;
default:block_svf(k,x,y,n);break;
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
