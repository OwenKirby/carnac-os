/* The factory bank "STONE & FIRE" (2026-10-16): 32 patches (slots 0..31) and 32 matching performances (slots 0..31),
 * a unity-build fragment like preset_store_fm1.c. synth_main_fm1.c includes it after syn_used / syn_perf_bank and calls
 * factory_install() BEFORE ps_load(): the factory slots are marked used, then anything the user saved over them in flash
 * replaces them. A performance holds the patch's effects (delay and reverb are shared, performance-level), so loading
 * a PERF brings the whole sound; loading only the PATCH keeps the current effects.
 *
 * Wavebanks: 0 Menhir 1 Barrow 2 Trilithon 3 Ley Line 4 Solstice 5 Moonstone 6 Star Chart 7 Druid Chant
 *            8 Overtone Rite 9 Votive Choir 10 Bone Flute 11 Erosion 12 Frost Shatter 13 Dust
 * Knob units: time 0..127 (32 = 34 ms, 64 = 0.37 s, 80 = 1.1 s, 96 = 3 s, 112 = 8 s), cutoff ~13.4 knob steps an octave
 * (85 = 1.6 kHz), LFO rate 0.5 Hz * 80^(k/127), LFO shape 0 sine 21 tri 43 saw 64 square 85 step 106 slew.
 * Built from the host-side reading of the engine: nothing here has been heard on the device yet. */
#define FP_OCT(k) ((uint8_t)((k) == 0 ? 64 : 18 * ((k) + 3) + 9))
#define FP_BIP(v) ((uint8_t)((v) + 64))
/* a fine tune in cents as the FINE TUNE byte nearest it (osc_fine_cents' square law, 2026-10-17) */
static uint8_t fp_fine(int8_t cents)
{
    uint8_t v, best = 64u;
    int32_t e, be = 1000;
    for (v = 0u; v < 128u; v++) {
        e = osc_fine_cents(v) - cents;
        e = e < 0 ? -e : e;
        if (e < be) { be = e; best = v; }
    }
    return best;
}

typedef struct {
    const char *name;
    uint8_t arch;
    uint8_t b1, s1; int8_t o1;           /* OSC1: wavebank, scan, octave */
    uint8_t b2, s2; int8_t o2, f2;       /* OSC2 (DUAL OSC / STACK): bank, scan, octave, fine cents */
    uint8_t mix, xm;                     /* DUAL: balance as bipolar offset / SPREAD: spread; xmod amount */
    uint8_t sub, noise;                  /* sub level, noise level */
    uint8_t flav, morph, cut, res;       /* FILTER1 */
    int8_t fenv;                         /* ENV2 -> cutoff, bipolar */
    uint8_t a, d, s, r;                  /* amp envelope (ENV1); ENV2 follows it, ENV3 scans */
    uint8_t shape, rate;                 /* LFO1 */
    uint8_t sc_src, sc_amt;              /* OSC1 scan modulation */
    uint8_t drv, ds, rsend;             /* AMP drive, delay send, reverb send */
    uint8_t vmode, rtype, rdec, rdmp, rpre;    /* performance: voices, reverb */
    uint8_t dtime, dfb, dtype, ping;     /* performance: delay */
} FactoryPatch;

#define FP_N 32u
static const FactoryPatch FP_TAB[FP_N] = {
/* ---- DRONES AND PADS: the standing stones ---- */
 /*name              arch  b1 s1 o1  b2 s2 o2 f2   mix xm sub nz  fl mo cut res fenv  a   d   s   r   sh rt  scsrc    sc drv ds rs  vmode              rev                 dec dmp pre  dt  fb  dty ping*/
 {"MENHIR DRONE",    0,    0, 30,-2,  0, 0, 0, 0,   0, 0, 70, 10, 0, 0, 52, 30,  12,  96, 80, 110, 100, 0, 18, MSRC_LFO1,  20, 20, 30, 70, VMODE_MONO_LEGATO, REVTYPE_DOLMEN,    96, 70, 20,  50, 40, 0, 0},
 {"CARNAC DAWN",     2,    5, 20, 0,  0, 0, 0, 0,   60, 0, 30, 0,  1, 0, 70, 20,  24, 100, 70, 100, 104, 0, 22, MSRC_ENV3,  50, 0,  30, 80, VMODE_POLY_REUSE,  REVTYPE_TUMULUS,  104, 60, 30,  60, 50, 0, 1},
 {"FOG BANK",        2,   13, 40, 0,  0, 0, 0, 0,   90, 0, 20, 60, 0, 0, 58, 18,  20, 104, 90, 100, 108, 0, 10, MSRC_LFO1,  40, 0,  20, 90, VMODE_POLY_REUSE,  REVTYPE_CELESTIAL,112, 90, 40,  70, 60, 1, 0},
 {"TRILITHON",       1,    2, 50, 0,  2, 60,-1, 6,  64, 30, 50, 0,  1, 0, 80, 25,  18,  90, 80, 100,  96, 0, 26, MSRC_LFO1,  35, 25, 20, 60, VMODE_POLY_REUSE,  REVTYPE_TOMB,      92, 60, 10,  54, 40, 0, 0},
 {"SOLSTICE PAD",    1,    4, 64, 0,  7, 40, 0,-5,  64, 20, 30, 0,  0, 0, 76, 22,  14,  98, 84, 100, 100, 0, 20, MSRC_LFO1,  45, 10, 30, 80, VMODE_POLY_REUSE,  REVTYPE_MOD,      100, 55, 20,  60, 45, 0, 1},
 {"STONE CIRCLE",    4,    0, 20, 0,  1, 50, 0, 8,  64, 40, 40, 0,  1, 0, 74, 28,  16,  70, 80, 100,  88, 0, 24, MSRC_LFO1,  30, 15, 25, 60, VMODE_POLY_REUSE,  REVTYPE_DOLMEN,    90, 65, 12,  50, 35, 0, 0},
 {"LEY LINE",        5,    3, 40, 0,  0, 0, 0, 0,   0, 0, 40, 0,  0, 0, 84, 20,  10,  90, 80, 110, 100, 0, 16, MSRC_LFO1,  40, 0,  20, 70, VMODE_POLY_REUSE,  REVTYPE_TOMB,     88, 50, 10,  58, 40, 0, 1},
 {"MIDWINTER SUN",   2,    4, 90, 0,  0, 0, 0, 0,   70, 0, 20, 0,  1, 8, 94, 15,  20,  84, 80, 100, 100, 0, 30, MSRC_ENV3,  60, 10, 25, 60, VMODE_POLY_REUSE,  REVTYPE_CELESTIAL,100, 40, 20,  56, 50, 0, 0},
 {"VOTIVE CHOIR",    1,    9, 30, 0,  8, 50, 0, 7,  64, 20, 20, 8,  4, 40,80, 30,  10,  80, 80, 100,  96, 0, 24, MSRC_LFO1,  30, 0,  15, 70, VMODE_POLY_REUSE,  REVTYPE_TOMB,     100, 70, 25,  60, 40, 0, 0},
 {"DRUID CHANT",     0,    7, 55, 0,  0, 0, 0, 0,   0, 0, 30, 14, 4, 70,72, 45,  16,  60, 70, 100,  80, 0, 28, MSRC_LFO1,  30, 15, 10, 50, VMODE_MONO_LEGATO, REVTYPE_DOLMEN,    84, 70, 10,  64, 40, 1, 0},
 {"BONE FLUTE",      0,   10, 40, 1,  0, 0, 0, 0,   0, 0, 0,  30, 0, 0, 90, 12,  16,  54, 60, 100,  76, 0, 46, MSRC_ENV3,  50, 0,  10, 50, VMODE_MONO_LEGATO, REVTYPE_TOMB,      70, 60,  5,  56, 35, 0, 0},
 {"FORGOTTEN RITE",  3,    8, 60, 0,  0, 0, 0, 0,   0, 0, 50, 0,  1, 0, 60, 55,  30, 100, 80, 100, 104, 0, 14, MSRC_LFO1,  60, 25, 40, 90, VMODE_POLY_REUSE,  REVTYPE_TUMULUS,  110, 75, 40,  66, 55, 1, 1},
/* ---- STRUCK AND PLUCKED: bells, chimes, cairn stones ---- */
 {"BARROW BELLS",    0,    1, 70, 1,  0, 0, 0, 0,   0, 0, 20, 0,  0, 0, 100,10,  10,  12, 70, 0,   84, 0, 40, MSRC_ENV3,  60, 0,  30, 60, VMODE_POLY_RESET, REVTYPE_TOMB,     100, 50, 15,  52, 45, 0, 1},
 {"STAR CHART",      1,    6, 90, 1,  6, 60, 2, 5,  64, 60, 0,  0,  0, 0, 96, 12,  20,   8, 56, 0,   76, 0, 40, MSRC_ENV3,  50, 0,  35, 55, VMODE_POLY_RESET, REVTYPE_CELESTIAL,104, 40, 15,  60, 50, 0, 1},
 {"FROST SHATTER",   0,   12, 80, 1,  0, 0, 0, 0,   0, 0, 0, 20,  3, 0, 70, 20,  40,   6, 44, 0,   64, 0, 40, MSRC_ENV3,  70, 0,  25, 55, VMODE_POLY_RESET, REVTYPE_TOMB,      90, 30,  8,  50, 45, 0, 1},
 {"DUST OF AGES",    0,   13, 30, 0,  0, 0, 0, 0,   0, 0, 0,  60, 4, 30,78, 30,  10,  30, 70, 0,   90, 0, 12, MSRC_LFO1,  60, 0,  20, 70, VMODE_POLY_REUSE,  REVTYPE_SPRING,    80, 70, 10,  52, 55, 0, 0},
 {"EROSION",         1,   11, 20, 0,  5, 60, 1, 4,  64, 50, 20, 20,  1, 0, 66, 40,  30,  90, 60, 100, 104, 0, 10, MSRC_LFO1,  70, 30, 25, 60, VMODE_POLY_REUSE,  REVTYPE_TOMB,     100, 80, 20,  56, 50, 1, 0},
 {"OGHAM",           0,    6, 60, 1,  0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 92, 18,  24,   4, 40, 0,   60, 0, 40, MSRC_ENV3,  40, 0,  30, 50, VMODE_POLY_RESET, REVTYPE_TOMB,      72, 50,  5,  60, 55, 0, 1},
 {"LUNAR GATE",      0,    5, 40, 0,  0, 0, 0, 0,   0, 0, 30, 0,  1, 0, 70, 30,  34,   4, 48, 0,   56, 0, 40, MSRC_ENV3,  50, 20, 40, 50, VMODE_POLY_RESET, REVTYPE_MOD,       90, 50, 10,  48, 60, 0, 1},
 {"LAST STONE",      0,    1, 30, 0,  0, 0, 0, 0,   0, 0, 40, 0,  0, 0, 80, 10,  14,   6, 80, 20,  120, 0, 14, MSRC_ENV3,  30, 0,  35, 70, VMODE_POLY_RESET, REVTYPE_TUMULUS,  120, 60, 30,  72, 50, 0, 1},
/* ---- LOW AND LEAD: earth and fire ---- */
 {"CAIRN BASS",      0,    0, 20,-2,  0, 0, 0, 0,   0, 0, 100,0,  2, 40, 44, 50,  30,   2, 50, 100,  30, 0, 10, MSRC_ENV3,  30, 40, 0,  10, VMODE_MONO_RETRIG,REVTYPE_TOMB,      40, 70,  0,  40, 20, 0, 0},
 {"DOLMEN SUB",      0,    0, 10,-2,  0, 0, 0, 0,   0, 0, 127,0,  0, 0, 36, 10,  12,   4, 60, 110,  40, 0, 10, MSRC_ENV3,  10, 10, 0,  15, VMODE_MONO_LEGATO,REVTYPE_DOLMEN,    50, 80,  0,  40, 20, 0, 0},
 {"MONOLITH LEAD",   1,    2, 70, 0,  0, 60, 0, 7,  64, 40, 30, 0,  2, 60, 70, 40,  26,   8, 70, 100,  48, 0, 34, MSRC_LFO1,  25, 50, 25, 35, VMODE_MONO_LEGATO,REVTYPE_TOMB,     64, 50,  6,  58, 40, 0, 1},
 {"NEOLITH KEYS",    1,    1, 50, 0,  4, 40, 1, 4,  64, 20, 10, 0,  0, 0, 90, 14,  22,   6, 60, 40,   68, 0, 24, MSRC_ENV3,  40, 10, 20, 40, VMODE_POLY_REUSE,  REVTYPE_TOMB,      60, 50,  5,  50, 30, 0, 0},
 {"HEARTH EMBER",    0,    5, 25, 0,  0, 0, 0, 0,   0, 0, 40, 6,  0, 0, 56, 15,  14,  50, 80, 100,  80, 0, 30, MSRC_ENV3,  20, 20, 15, 50, VMODE_POLY_REUSE,  REVTYPE_TOMB,      60, 85,  5,  50, 30, 0, 0},
 {"BONFIRE",         2,   11, 70, -1, 0, 0, 0, 0,   80, 0, 60, 20, 1, 20,60, 30,  40,  20, 80, 100,  80, 0, 100,MSRC_LFO1,  40, 90, 20, 40, VMODE_POLY_REUSE,  REVTYPE_TOMB,      70, 60, 10,  48, 35, 0, 0},
 {"STONE AND FIRE",  4,    0, 30, 0,  11,60, 0, 5,  64, 30, 60, 10, 2, 20, 56, 45,  30,  40, 80, 100,  88, 0, 20, MSRC_LFO1,  30, 60, 20, 50, VMODE_POLY_REUSE,  REVTYPE_DOLMEN,    90, 60, 10,  54, 40, 0, 0},
 {"ALIGNMENT",       3,    3, 50, 0,  0, 0, 0, 0,   0, 0, 40, 0,  1, 0, 56, 60,  36, 100, 80, 100, 104, 0, 18, MSRC_LFO1,  50, 20, 30, 80, VMODE_POLY_REUSE,  REVTYPE_CELESTIAL,110, 60, 30,  64, 55, 1, 1},
 {"ECLIPSE",         5,    4, 20, -1, 0, 0, 0, 0,   0, 0, 80, 0,  0, 0, 60, 30,  14, 104, 90, 100, 108, 0, 8,  MSRC_LFO1,  30, 15, 20, 80, VMODE_POLY_REUSE,  REVTYPE_TUMULUS,  118, 80, 40,  70, 50, 0, 0},
 {"TUMULUS HALL",    2,    9, 50, 0,  0, 0, 0, 0,   100,0, 30, 0,  1, 0, 70, 20,  16, 104, 90, 100, 112, 0, 12, MSRC_LFO1,  40, 0,  30, 100,VMODE_POLY_REUSE,  REVTYPE_TUMULUS,  127, 70, 50,  70, 55, 0, 1},
 {"WICKER MAN",      2,    0, 80, 0,  11,0, 0, 0,   60, 0, 50, 10, 2, 70, 60, 80,  34,   6, 70, 100,  44, 0, 70, MSRC_LFO1,  20, 100,15, 35, VMODE_UNISON_STACK,REVTYPE_TOMB,    50, 50,  4,  52, 35, 0, 0},
 {"SAMHAIN MIST",    2,   13, 70, 0,  0, 0, 0, 0,   110,0, 10, 80, 0, 0, 50, 10,  16, 112, 90, 100, 120, 0, 6,  MSRC_LFO1,  50, 0,  20, 100,VMODE_POLY_REUSE,  REVTYPE_CELESTIAL,127, 95, 60,  72, 65, 1, 1},
};

static void factory_name(uint8_t *dst, const char *s)
{
    static const char map[] = UI_CHAR_MAP;
    uint8_t i = 0u, k;
    for (; i < NAME_LEN && s[i]; i++) {
        dst[i] = 0u;                                     /* (the firmware libc has no strchr) */
        for (k = 1u; map[k]; k++)
            if (map[k] == s[i])
                dst[i] = k;
    }
    for (; i < NAME_LEN; i++)
        dst[i] = 0u;
}

/* patch slot s <- row f, over the defaults in base */
static void factory_patch(EngineState *e, const EngineState *base, const FactoryPatch *f)
{
    uint8_t i;
    *e = *base;
    e->arch = f->arch;
    e->osc[0].wavebank = f->b1; e->osc[0].scan = f->s1; e->osc[0].octave = FP_OCT(f->o1);
    e->osc[0].scan_mod_src = f->sc_src; e->osc[0].scan_mod_amt = f->sc_amt;
    e->osc[1].wavebank = f->b2; e->osc[1].scan = f->s2; e->osc[1].octave = FP_OCT(f->o2);
    e->osc[1].fine = fp_fine(f->f2);
    e->oscmix.sub_level = f->sub;
    e->oscmix.noise_level = f->noise;
    e->oscmix.noise_colour = NOISE_PINK;
    e->oscmix.xmod_amt = f->xm;
    if (f->arch == VARCH_SPREAD)
        e->oscmix.spread = f->mix;
    else
        e->oscmix.balance = f->mix == 0u ? (uint8_t)BIPOLAR_CENTER : f->mix;   /* stored 64 = the middle */
    if (f->arch == VARCH_BINAURAL)
        e->oscmix.bin_beat = 20;                          /* 5 Hz: the alpha-theta border */
    e->filter[0].flavour = f->flav; e->filter[0].morph = f->morph;
    e->filter[0].cutoff = f->cut; e->filter[0].resonance = f->res;
    e->filter[0].mod_amt = FP_BIP(f->fenv);
    e->filter[1] = e->filter[0];                         /* a second filter, where the architecture has one */
    if (f->arch == VARCH_DUAL_FILTER) {
        e->filter[1].flavour = FLT_SVF; e->filter[1].morph = 64; e->filter[1].cutoff = 76;
    }
    e->env[0].attack = f->a; e->env[0].decay = f->d; e->env[0].sustain = f->s; e->env[0].release = f->r;
    e->env[1] = e->env[0];
    e->env[1].decay = (uint8_t)(f->d * 3u / 4u); e->env[1].sustain = (uint8_t)(f->s / 2u);
    e->env[2] = e->env[0];
    e->env[2].attack = (uint8_t)(f->a + 12u > 127u ? 127u : f->a + 12u);
    for (i = 0; i < 2; i++)                              /* an all-zero rate would freeze the second and third LFO */
        e->lfo[i + 1].rate = (uint8_t)(f->rate + 8 * i);
    e->lfo[0].shape = f->shape; e->lfo[0].rate = f->rate;
    e->amp.drive = f->drv; e->amp.delay_send = f->ds; e->amp.reverb_send = f->rsend;
    e->amp.drive_model = f->drv > 60u ? DRIVE_FOLD : DRIVE_SOFT;
    factory_name(e->name, f->name);
}

/* ---------------------------------------------------------------------------------- MOD SECTION --- *
 * Per patch: the three MODSLOTs (source A, function, source B, amount) and up to six routes from a source to a
 * destination. Routes only name sources; where a destination already had an envelope on it (filter cutoff = ENV2) the
 * route's source is a slot that still contains it (ENV2 + something, or ENV2 x VELOCITY), so the sweep survives.
 * The recipes: mod wheel x LFO2 = vibrato that the player brings in; ENV2 x VELOCITY = hit harder, brighter;
 * ENV2 + AFTERTOUCH = pressure opens the filter; two slow LFOs summed = a pad that never repeats; a square LFO through
 * MAX 0 = a gate on the amp. LFO2 / LFO3 rates and shapes are set per row (rate 0 = leave the default).
 * Not heard on the device yet; every route is a handful of mixes per voice tick (measure with PERF TEST). */
enum { DM_NONE, DM_CUT, DM_MORPH, DM_PITCH, DM_SCAN, DM_MIX, DM_XMOD, DM_SPREAD, DM_NOISE, DM_NCOL, DM_AMP, DM_PAN,
       DM_BAL, DM_ROUT, DM_BINCUT, DM_BINPH };
#define MSRC_S1 MSRC_MODSLOT1
#define MSRC_S2 MSRC_MODSLOT2
#define MSRC_S3 MSRC_MODSLOT3
#define MSRC_MW MSRC_MODWHEEL
#define MSRC_AT MSRC_AFTERTOUCH
#define MSRC_VEL MSRC_VELOCITY
#define MSRC_KEY MSRC_KEYBOARD
#define M_(a, f, b, amt) {MSRC_##a, MFN_##f, MSRC_##b, (amt)}
#define R_(d, s, amt) {DM_##d, MSRC_##s, (amt)}
#define FM_SLOTS 3u
#define FM_ROUTES 6u
typedef struct { uint8_t a, fn, b; int8_t amt; } FMSlot;      /* amt -64..63, 63 = unity */
typedef struct { uint8_t dst, src; int8_t amt; } FMRoute;     /* bipolar destinations: -64..63; the rest 0..127 */
typedef struct {
    const char *name;
    uint8_t l2shape, l2rate, l3shape, l3rate;
    FMSlot sl[FM_SLOTS];
    FMRoute rt[FM_ROUTES];
} FactoryMod;

static const FactoryMod FM_TAB[FP_N] = {
/* ---- DRONES AND PADS ---- */
{"MENHIR DRONE", 0,5, 21,11, {M_(LFO2,ADD,LFO3,40), M_(ENV2,ADD,S1,63), M_(NONE,ADD,NONE,0)},
   {R_(CUT,S2,0), R_(SCAN,S1,30), R_(PITCH,S1,3), R_(PAN,LFO3,30)}},
{"CARNAC DAWN", 0,66, 0,0, {M_(LFO2,MUL,MW,63), M_(ENV2,ADD,AT,63), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,S1,11), R_(CUT,S2,0), R_(SPREAD,ENV3,25), R_(PAN,LFO1,30)}},
{"FOG BANK", 0,5, 0,9, {M_(LFO2,MUL,LFO3,30), M_(ENV2,ADD,S1,63), M_(NONE,ADD,NONE,0)},
   {R_(CUT,S2,0), R_(NOISE,LFO3,18), R_(SPREAD,LFO2,22), R_(MORPH,LFO1,22), R_(PAN,S1,40)}},
{"TRILITHON", 0,70, 0,0, {M_(LFO2,MUL,MW,63), M_(ENV2,MUL,VEL,63), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,S1,12), R_(CUT,S2,0), R_(MIX,LFO1,18), R_(XMOD,ENV3,20)}},
{"SOLSTICE PAD", 0,68, 21,9, {M_(LFO2,MUL,MW,63), M_(ENV2,ADD,AT,63), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,S1,10), R_(CUT,S2,0), R_(MIX,LFO3,25), R_(XMOD,LFO1,15), R_(PAN,LFO3,28)}},
{"STONE CIRCLE", 0,70, 0,0, {M_(LFO2,MUL,MW,63), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,S1,10), R_(BAL,LFO1,20), R_(XMOD,AT,30)}},
{"LEY LINE", 0,11, 0,4, {M_(LFO2,ADD,NONE,25), M_(ENV2,ADD,S1,63), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,LFO1,6), R_(MORPH,LFO2,15), R_(CUT,S2,0), R_(BINCUT,LFO1,10), R_(BINPH,MW,30)}},
{"MIDWINTER SUN", 0,70, 0,0, {M_(LFO2,MUL,MW,63), M_(ENV2,MUL,VEL,63), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,S1,10), R_(CUT,S2,0), R_(SPREAD,AT,30), R_(MORPH,LFO1,12)}},
{"VOTIVE CHOIR", 0,68, 21,9, {M_(LFO2,MUL,MW,63), M_(LFO3,ADD,AT,45), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,S1,11), R_(MORPH,S2,25), R_(MIX,LFO3,20), R_(PAN,LFO3,25)}},
{"DRUID CHANT", 0,70, 21,12, {M_(LFO3,ADD,AT,40), M_(LFO2,MUL,MW,63), M_(ENV2,MUL,VEL,63)},
   {R_(MORPH,S1,28), R_(PITCH,S2,12), R_(NOISE,VEL,12), R_(CUT,S3,0)}},
{"BONE FLUTE", 0,72, 0,0, {M_(LFO2,MUL,MW,63), M_(ENV2,MUL,VEL,63), M_(ENV3,ADD,AT,63)},
   {R_(PITCH,S1,13), R_(NOISE,VEL,18), R_(CUT,S2,0), R_(SCAN,S3,50)}},
{"FORGOTTEN RITE", 0,7, 21,10, {M_(LFO3,ADD,NONE,30), M_(ENV2,ADD,S1,63), M_(NONE,ADD,NONE,0)},
   {R_(BAL,LFO1,22), R_(ROUT,LFO2,18), R_(CUT,S2,0), R_(MORPH,AT,20)}},
/* ---- BELLS, GLASS AND DUST ---- */
{"BARROW BELLS", 0,0, 0,0, {M_(ENV3,ADD,VEL,40), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(SCAN,S1,60), R_(PAN,KEY,40)}},
{"STAR CHART", 0,0, 0,0, {M_(ENV3,ADD,VEL,40), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(XMOD,S1,25), R_(MIX,KEY,30), R_(SCAN,S1,50), R_(PAN,KEY,45)}},
{"FROST SHATTER", 0,0, 0,0, {M_(ENV3,MUL,VEL,63), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(NOISE,ENV3,28), R_(MORPH,S1,25), R_(PAN,KEY,40)}},
{"DUST OF AGES", 106,9, 106,12, {M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(NOISE,LFO3,22), R_(NCOL,LFO2,30), R_(MORPH,LFO1,20), R_(PAN,LFO3,40)}},
{"EROSION", 106,8, 21,13, {M_(LFO2,ADD,LFO3,50), M_(ENV2,ADD,S1,63), M_(NONE,ADD,NONE,0)},
   {R_(MIX,S1,25), R_(XMOD,S1,22), R_(SCAN,S1,35), R_(CUT,S2,0), R_(PAN,LFO3,30)}},
{"OGHAM", 0,0, 0,0, {M_(ENV2,MUL,VEL,63), M_(ENV3,ADD,VEL,40), M_(NONE,ADD,NONE,0)},
   {R_(CUT,S1,0), R_(SCAN,S2,40), R_(PAN,KEY,35)}},
{"LUNAR GATE", 64,38, 21,20, {M_(LFO2,MAX,NONE,63), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(AMP,S1,70), R_(PAN,LFO2,40), R_(MORPH,LFO3,15)}},
{"LAST STONE", 0,0, 0,0, {M_(ENV2,ADD,KEY,25), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(CUT,S1,0), R_(SCAN,KEY,30), R_(PAN,KEY,30), R_(MORPH,AT,25)}},
/* ---- BASSES, LEADS AND KEYS ---- */
{"CAIRN BASS", 0,64, 0,0, {M_(LFO2,MUL,AT,63), M_(ENV2,MUL,VEL,63), M_(NONE,ADD,NONE,0)},
   {R_(MORPH,VEL,20), R_(CUT,S2,0), R_(PITCH,S1,9)}},
{"DOLMEN SUB", 0,60, 0,0, {M_(ENV2,ADD,AT,63), M_(LFO2,MUL,MW,63), M_(NONE,ADD,NONE,0)},
   {R_(CUT,S1,22), R_(PITCH,S2,8)}},
{"MONOLITH LEAD", 0,70, 0,0, {M_(LFO2,MUL,MW,63), M_(ENV2,ADD,AT,63), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,S1,13), R_(CUT,S2,22), R_(MORPH,AT,25), R_(XMOD,AT,30)}},
{"NEOLITH KEYS", 0,0, 0,0, {M_(ENV2,MUL,VEL,63), M_(ENV3,ADD,VEL,40), M_(NONE,ADD,NONE,0)},
   {R_(MIX,KEY,20), R_(XMOD,VEL,35), R_(CUT,S1,0), R_(SCAN,S2,40), R_(PAN,KEY,30)}},
{"HEARTH EMBER", 106,12, 106,20, {M_(LFO2,ADD,LFO3,40), M_(ENV2,ADD,S1,63), M_(NONE,ADD,NONE,0)},
   {R_(NOISE,LFO3,25), R_(CUT,S2,0), R_(SCAN,S1,30), R_(PAN,LFO3,35)}},
{"BONFIRE", 0,0, 106,80, {M_(LFO3,ADD,NONE,25), M_(ENV2,ADD,S1,63), M_(NONE,ADD,NONE,0)},
   {R_(SPREAD,LFO1,20), R_(NOISE,LFO3,20), R_(CUT,S2,0), R_(PITCH,LFO1,4), R_(PAN,LFO3,40)}},
{"STONE AND FIRE", 0,70, 106,60, {M_(LFO2,MUL,MW,63), M_(ENV2,MUL,VEL,63), M_(NONE,ADD,NONE,0)},
   {R_(PITCH,S1,10), R_(CUT,S2,0), R_(BAL,LFO1,25), R_(XMOD,LFO3,22)}},
{"ALIGNMENT", 21,8, 0,11, {M_(ENV2,ADD,LFO2,20), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(CUT,S1,0), R_(BAL,LFO1,25), R_(ROUT,AT,30), R_(MORPH,LFO3,15), R_(PAN,LFO2,30)}},
{"ECLIPSE", 0,5, 0,4, {M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(BINCUT,LFO1,12), R_(PITCH,LFO3,5), R_(SCAN,LFO2,30), R_(MORPH,LFO2,15), R_(BINPH,MW,30)}},
{"TUMULUS HALL", 0,68, 0,5, {M_(LFO2,MUL,MW,63), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(SPREAD,AT,30), R_(MORPH,LFO1,12), R_(PITCH,S1,9), R_(PAN,LFO3,25)}},
{"WICKER MAN", 0,0, 0,0, {M_(LFO1,ADD,NONE,63), M_(ENV2,MUL,VEL,63), M_(NONE,ADD,NONE,0)},
   {R_(AMP,S1,40), R_(PITCH,LFO1,6), R_(NOISE,VEL,20), R_(SPREAD,AT,35), R_(CUT,S2,0)}},
{"SAMHAIN MIST", 0,4, 21,7, {M_(LFO2,MUL,LFO3,63), M_(NONE,ADD,NONE,0), M_(NONE,ADD,NONE,0)},
   {R_(NOISE,S1,20), R_(NCOL,LFO3,40), R_(SPREAD,LFO2,25), R_(MORPH,LFO3,15), R_(PAN,LFO2,40)}},
};
#undef M_
#undef R_
#undef MSRC_S1
#undef MSRC_S2
#undef MSRC_S3
#undef MSRC_MW
#undef MSRC_AT
#undef MSRC_VEL
#undef MSRC_KEY

/* the patch's mod section over a freshly built patch; a row whose name is not the patch's is skipped, not misapplied */
static void factory_mod(EngineState *e, const FactoryMod *m, const char *patch_name)
{
    uint8_t i, k, amt;
    for (k = 0; m->name[k] || patch_name[k]; k++)
        if (m->name[k] != patch_name[k])
            return;
    for (i = 0; i < FM_SLOTS; i++) {
        e->mod[i].src_a = m->sl[i].a; e->mod[i].func = m->sl[i].fn; e->mod[i].src_b = m->sl[i].b;
        e->mod[i].amount = FP_BIP(m->sl[i].amt);
    }
    if (m->l2rate) { e->lfo[1].shape = m->l2shape; e->lfo[1].rate = m->l2rate; }
    if (m->l3rate) { e->lfo[2].shape = m->l3shape; e->lfo[2].rate = m->l3rate; }
    for (i = 0; i < FM_ROUTES && m->rt[i].dst != DM_NONE; i++) {
        const uint8_t src = m->rt[i].src;
        const int8_t a = m->rt[i].amt;
        amt = (uint8_t)(a < 0 ? 0 : a);                    /* the unipolar destinations */
        switch (m->rt[i].dst) {
        case DM_CUT:                                       /* amount 0 keeps the patch's ENV2 amount */
            for (k = 0; k < NFILTER; k++) {
                e->filter[k].mod_src = src;
                if (a) e->filter[k].mod_amt = FP_BIP(a);
            }
            break;
        case DM_MORPH:
            for (k = 0; k < NFILTER; k++) { e->filter[k].morph_mod_src = src; e->filter[k].morph_mod_amt = FP_BIP(a); }
            break;
        case DM_PITCH:
            for (k = 0; k < NOSC; k++) { e->osc[k].pitch_mod_src = src; e->osc[k].pitch_mod_amt = amt; }
            break;
        case DM_SCAN:   e->osc[0].scan_mod_src = src; e->osc[0].scan_mod_amt = amt; break;
        case DM_MIX:    e->oscmix.mix_mod_src = src; e->oscmix.mix_mod_amt = amt; break;
        case DM_XMOD:   e->oscmix.xmod_mod_src = src; e->oscmix.xmod_mod_amt = amt; break;
        case DM_SPREAD: e->oscmix.spread_mod_src = src; e->oscmix.spread_mod_amt = amt; break;
        case DM_NOISE:  e->oscmix.noise_mod_src = src; e->oscmix.noise_mod_amt = FP_BIP(a); break;
        case DM_NCOL:   e->oscmix.noise_col_mod_src = src; e->oscmix.noise_col_mod_amt = FP_BIP(a); break;
        case DM_AMP:    e->amp.amp_mod_src = src; e->amp.amp_mod_depth = amt; break;
        case DM_PAN:    e->amp.pan_mod_src = src; e->amp.pan_mod_depth = amt; break;
        case DM_BAL:    e->fltmix.balance_mod_src = src; e->fltmix.balance_mod_amt = FP_BIP(a); break;
        case DM_ROUT:   e->fltmix.routing_mod_src = src; e->fltmix.routing_mod_amt = FP_BIP(a); break;
        case DM_BINCUT: e->fltmix.bin_cutoff_mod_src = src; e->fltmix.bin_cutoff_mod_amt = FP_BIP(a); break;
        case DM_BINPH:  e->fltmix.bin_phase_mod_src = src; e->fltmix.bin_phase_mod_amt = FP_BIP(a); break;
        default: break;
        }
    }
}

/* boot, before ps_load: the bank into patch_bank and syn_perf_bank, marked used */
static void factory_install(void)
{
    EngineState base = patch_bank[PATCH_SLOTS - 1u];     /* untouched: the defaults */
    MasterPerformancePreset pb = perf_state;             /* the defaults (the live performance is not loaded yet) */
    uint32_t s;
    for (s = 0; s < FP_N; s++) {
        const FactoryPatch *f = &FP_TAB[s];
        MasterPerformancePreset *p = &syn_perf_bank[s];
        factory_patch(&patch_bank[s], &base, f);
        factory_mod(&patch_bank[s], &FM_TAB[s], f->name);
        *p = pb;
        factory_name(p->name, f->name);
        p->layer_patch[0] = (uint8_t)s;
        p->layer_patch[1] = (uint8_t)(s + 1u < FP_N ? s + 1u : 0u);
        p->seq_link = 127u; p->arp_link = 127u;         /* a link to a slot never saved leaves SEQ and ARP alone */
        p->perf_mode = PERF_SINGL_SYNTH;
        p->voice_mode[0] = p->voice_mode[1] = f->vmode;
        p->porta_mode[0] = PORTA_CONST_TIME;
        p->porta_time[0] = f->vmode == VMODE_MONO_LEGATO ? 20u : 0u;
        p->reverb_type = f->rtype; p->reverb_decay = f->rdec; p->reverb_damp = f->rdmp; p->reverb_predelay = f->rpre;
        p->delay_time = f->dtime; p->delay_feedback = f->dfb; p->delay_type = f->dtype; p->delay_pingpong = f->ping;
        p->delay_degrade = 36u;
        syn_used[UISAVE_PATCH_PRESET][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_WHOLE_PERF][s >> 3] |= (uint8_t)(1u << (s & 7u));
    }
}
