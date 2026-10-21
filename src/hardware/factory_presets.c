/* The factory bank "STONE & FIRE" (2026-10-16): 32 patches (slots 0..31), and since 2026-10-17 32 sequences, 32 arp
 * patterns and 32 whole performances (every slot of those banks), a unity-build fragment like preset_store_fm1.c.
 * synth_main_fm1.c includes it after syn_used / syn_perf_name and the store, and calls factory_install() BEFORE
 * ps_load(): the factory slots are marked used, then anything the user saved over them in flash replaces them. A
 * performance (factory_perf, made when it loads) holds the patch, its effects (delay and reverb are shared,
 * performance-level), its tempo, its sequence and its arp pattern, so loading a PERF brings the whole piece; loading
 * only the PATCH keeps the current effects.
 *
 * Wavebanks: 0 Menhir 1 Barrow 2 Trilithon 3 Ley Line 4 Solstice 5 Moonstone 6 Star Chart 7 Druid Chant
 *            8 Overtone Rite 9 Votive Choir 10 Bone Flute 11 Erosion 12 Frost Shatter 13 Dust
 * Knob units: time 0..127 (32 = 34 ms, 64 = 0.37 s, 80 = 1.1 s, 96 = 3 s, 112 = 8 s), cutoff ~13.4 knob steps an octave
 * (85 = 1.6 kHz), LFO rate 0.05 Hz * 800^(k/127), LFO shape 0 sine 21 tri 43 saw 64 square 85 step 106 slew.
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

/* ------------------------------------------------------------------------ SEQUENCES AND ARPS --- *
 * 2026-10-17: the 32 sequences and 32 arp patterns, one of each for each patch, and what each performance plays. A
 * performance is led by one of them, so PLAY never runs two tunes over each other:
 *  - an ARP-led one (pads, keys: FA_TAB's maq 1) plays its chord progression, and its sequence is a bass under it, root
 *    and fifth (FS_BASS) or root alone (FS_ROOT), one note a chord: TRANSPOSE MAQ moves it to each chord's root;
 *  - a SEQ-led one (drones, bells, plucks, bass, leads) plays its sequence; its arp pattern stays empty (arp_link 127),
 *    so its arp slot is only in the ARP bank, for the ARP page.
 * Notes: 16 steps, '0'..'9' then 'a'..'z' = 0..35 semitones over the root, '-' a TIE, '.' a rest (the root, muted).
 * Every note sits in the lane's SCALE (test_preset_store checks it), so the knob and the keyboard stay in key. Lanes 2..4
 * aim at lane 1 (a MOD WHEEL lane at layer 1): 16 values, '0'..'9' = 0..127. */
#define FS_BASS "0--70--70--70--7"
#define FS_ROOT "0---0---0---0---"
typedef struct {
    uint8_t root;                        /* the notes' MIDI base; an ARP-led one's: the first chord's root, an octave down */
    const char *notes;                   /* NULL: an ARP-led one's bass, FS_BASS */
    uint8_t prescale, mode, scale, sroot;
    uint8_t ev[SEQ_NLANE - 1u];          /* lanes 2..4 */
    const char *val[SEQ_NLANE - 1u];
} FactorySeq;
#define FS_SEQ(root, notes, p, mode, sc, sr, e2, v2, e3, v3, e4, v4) \
    {root, notes, p, SEQ_MODE_##mode, SEQSCALE_##sc, sr, {SEQEV_##e2, SEQEV_##e3, SEQEV_##e4}, {v2, v3, v4}}
#define FS_UNDER(notes) {0u, notes, 0u, SEQ_MODE_FORWARD, SEQSCALE_OFF, 0u, {SEQEV_GATE, SEQEV_DYNAMICS, SEQEV_RATCHET}, \
    {"9999999999999999", "6666666666666666", "0000000000000000"}}

/* arp_chords.c's genres used here (test_preset_store checks the names) */
enum { FG_TRAD_MAJ = 6, FG_TRAD_MIN_1 = 7, FG_TRAD_MIN_2 = 8, FG_OCT = 13, FG_4TH = 14, FG_5TH = 15, FG_MIN = 17,
       FG_M7 = 18, FG_MIN7 = 19, FG_MIN9 = 21, FG_POP_INV = 24, FG_CINE_1 = 32, FG_CINE_2 = 33, FG_CINE_3 = 34,
       FG_CINE_5 = 36, FG_CINE_6 = 37, FG_CLASSICAL_1 = 77, FG_MODERN_1 = 81 };
#define FA_T ARP_CHORD_TIE
/* chord keys */
enum { FA_F2 = 41, FA_G2 = 43, FA_AB2 = 44, FA_A2 = 45, FA_BB2 = 46, FA_B2 = 47, FA_C3 = 48, FA_D3 = 50, FA_EB3 = 51,
       FA_E3 = 52 };
typedef struct {
    uint8_t genre, div, style, oct, gate;
    uint8_t maq;                         /* 1: TRANSPOSE MAQ, and the performance is ARP-led */
    uint8_t root[ARP_NSTEP];             /* chord keys, FA_T a TIE */
    const char *var;                     /* 8 steps: 'a' + the RHYTHM variation (8.3: a = 01A, q = 09A 1 1 1 _, s = 10A
                                          * 1 1 _ 1, u = 11A 1 _ 1 1) */
    const char *acc;                     /* 8 steps: '1' accented */
    uint8_t bpm;                         /* the performance's INT TEMPO */
} FactoryArp;
/* a pad's progression: four chords, each tied over the next step */
#define FA_P(a, b, c, d) {a, FA_T, b, FA_T, c, FA_T, d, FA_T}
#define FA_ARP(g, div, st, oct, gate, maq, roots, var, acc, bpm) \
    {FG_##g, ARPDIV_##div, ARPSTYLE_##st, oct, gate, maq, roots, var, acc, bpm}
#define FA_8(a, b, c, d) {a, a, b, b, c, c, d, d}
#define FA_R(a, b, c, d, e, f, g, h) {a, b, c, d, e, f, g, h}
#define FA_NOACC "00000000"

static const FactoryArp FA_TAB[FP_N] = {
 /* ---- drones and pads ---- */
 FA_ARP(5TH,         1_2, BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 70),   /* MENHIR DRONE */
 FA_ARP(TRAD_MIN_1,  1_2, STRUM_UP, 0, 127, 1, FA_P(FA_C3, FA_AB2, FA_EB3, FA_BB2), "aaaaaaaa", FA_NOACC, 72),   /* CARNAC DAWN */
 FA_ARP(MIN9,        1_2, BLOCK,    0, 127, 1, FA_P(FA_C3, FA_F2, FA_C3, FA_G2),    "aaaaaaaa", FA_NOACC, 66),   /* FOG BANK */
 FA_ARP(5TH,         1_4, UPDN,     1,  96, 1, FA_P(FA_D3, FA_C3, FA_BB2, FA_C3),   "qqqqqqqq", FA_NOACC, 84),   /* TRILITHON */
 FA_ARP(TRAD_MAJ,    1_2, STRUM_UP, 0, 127, 1, FA_P(FA_C3, FA_A2, FA_F2, FA_G2),    "aaaaaaaa", FA_NOACC, 76),   /* SOLSTICE PAD */
 FA_ARP(4TH,         1_4, UP,       1,  96, 1, FA_P(FA_E3, FA_D3, FA_A2, FA_G2),    "qqqqqqqq", FA_NOACC, 90),   /* STONE CIRCLE */
 FA_ARP(CINE_6,      1_2, BLOCK,    0, 127, 1, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 70),   /* LEY LINE */
 FA_ARP(CINE_5,      1_2, STRUM_UP, 0, 127, 1, FA_P(FA_C3, FA_D3, FA_C3, FA_G2),    "aaaaaaaa", FA_NOACC, 74),   /* MIDWINTER SUN */
 FA_ARP(CLASSICAL_1, 1_2, BLOCK,    0, 127, 1, FA_P(FA_A2, FA_F2, FA_C3, FA_G2),    "aaaaaaaa", FA_NOACC, 72),   /* VOTIVE CHOIR */
 FA_ARP(4TH,         1,   BLOCK,    0, 127, 0, FA_P(FA_D3, FA_C3, FA_D3, FA_A2),    "aaaaaaaa", FA_NOACC, 80),   /* DRUID CHANT */
 FA_ARP(MIN,         1_8, UP,       1,  96, 0, FA_8(FA_A2, FA_D3, FA_E3, FA_A2),    "qqqqqqqq", FA_NOACC, 88),   /* BONE FLUTE */
 FA_ARP(TRAD_MIN_2,  1_2, BLOCK,    0, 127, 1, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 68),   /* FORGOTTEN RITE */
 /* ---- struck and plucked ---- */
 FA_ARP(TRAD_MAJ,    1_8, UP,       1,  80, 0, FA_8(FA_C3, FA_A2, FA_F2, FA_G2),    "qqqqqqqq", FA_NOACC, 100),  /* BARROW BELLS */
 FA_ARP(CINE_5,      1_4, UPDN,     2,  80, 0, FA_8(FA_C3, FA_D3, FA_C3, FA_G2),    "qqqqqqqq", FA_NOACC, 108),  /* STAR CHART */
 FA_ARP(MIN,         1_4, RANDOM,   2,  64, 0, FA_8(FA_C3, FA_F2, FA_G2, FA_C3),    "qqqqqqqq", FA_NOACC, 116),  /* FROST SHATTER */
 FA_ARP(POP_INV,     1_2, STRUM_DN, 0, 127, 0, FA_P(FA_A2, FA_F2, FA_C3, FA_G2),    "aaaaaaaa", FA_NOACC, 84),   /* DUST OF AGES */
 FA_ARP(MIN7,        1_4, UP,       1,  96, 1, FA_P(FA_A2, FA_D3, FA_E3, FA_A2),    "uuuuuuuu", FA_NOACC, 92),   /* EROSION */
 FA_ARP(MIN,         1_8, UP,       1,  80, 0, FA_8(FA_D3, FA_G2, FA_A2, FA_D3),    "qqqqqqqq", FA_NOACC, 112),  /* OGHAM */
 FA_ARP(MIN7,        1_4, UPDN,     1,  80, 0, FA_8(FA_A2, FA_B2, FA_E3, FA_A2),    "qqqqqqqq", FA_NOACC, 96),   /* LUNAR GATE */
 FA_ARP(OCT,         1,   BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_EB3, FA_G2),  "aaaaaaaa", FA_NOACC, 64),   /* LAST STONE */
 /* ---- low and lead ---- */
 FA_ARP(OCT,         1_4, UP,       0,  64, 0, FA_R(FA_C3, FA_C3, FA_C3, FA_C3, FA_AB2, FA_AB2, FA_BB2, FA_BB2),
        "qqqqqqqq", "10001000", 118),                                                                            /* CAIRN BASS */
 FA_ARP(OCT,         1_2, BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 92),   /* DOLMEN SUB */
 FA_ARP(MIN,         1_4, DNUP,     2,  96, 0, FA_8(FA_C3, FA_F2, FA_G2, FA_C3),    "qqqqqqqq", FA_NOACC, 100),  /* MONOLITH LEAD */
 FA_ARP(TRAD_MAJ,    1_4, BLOCK,    0,  80, 1, FA_8(FA_C3, FA_A2, FA_F2, FA_G2),    "ssssssss", "10101010", 104),/* NEOLITH KEYS */
 FA_ARP(M7,          1_2, STRUM_UP, 0, 127, 1, FA_P(FA_F2, FA_C3, FA_BB2, FA_C3),   "aaaaaaaa", FA_NOACC, 80),   /* HEARTH EMBER */
 FA_ARP(CINE_1,      1_4, STRUM_UP, 0,  96, 1, FA_8(FA_C3, FA_AB2, FA_EB3, FA_BB2), "ssssssss", "10001000", 110),/* BONFIRE */
 FA_ARP(TRAD_MIN_2,  1_4, UPDN,     1,  80, 0, FA_8(FA_C3, FA_F2, FA_G2, FA_C3),    "qqqqqqqq", FA_NOACC, 120),  /* STONE AND FIRE */
 FA_ARP(5TH,         1_8, UP,       2,  96, 1, FA_P(FA_C3, FA_AB2, FA_F2, FA_BB2),  "qqqqqqqq", FA_NOACC, 96),   /* ALIGNMENT */
 FA_ARP(CINE_3,      1_2, BLOCK,    0, 127, 1, FA_P(FA_C3, FA_AB2, FA_D3, FA_G2),   "aaaaaaaa", FA_NOACC, 66),   /* ECLIPSE */
 FA_ARP(MODERN_1,    1_2, STRUM_UP, 0, 127, 1, FA_P(FA_C3, FA_A2, FA_F2, FA_G2),    "aaaaaaaa", FA_NOACC, 70),   /* TUMULUS HALL */
 FA_ARP(MIN,         1_4, DNUP,     2,  80, 0, FA_8(FA_A2, FA_D3, FA_E3, FA_A2),    "qqqqqqqq", FA_NOACC, 124),  /* WICKER MAN */
 FA_ARP(CINE_2,      1_2, BLOCK,    0, 127, 1, FA_P(FA_C3, FA_AB2, FA_EB3, FA_G2),  "aaaaaaaa", FA_NOACC, 64),   /* SAMHAIN MIST */
};

#define FS_V9 "9999999999999999"
static const FactorySeq FS_TAB[FP_N] = {
 FS_SEQ(60, "0-------3---7---", 24, FORWARD, MINOR, 0,
        MODWHEEL, "0234567898765432", GATE, FS_V9, DYNAMICS, "7777777777777777"),                       /* MENHIR DRONE */
 FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS),
 FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS),                                                 /* 1..8 */
 FS_SEQ(50, "0-2-3-5-3-2-0---", 12, FORWARD, DORIAN, 2,
        GATE, FS_V9, DYNAMICS, "8686868686868686", MODWHEEL, "0123456789876543"),                        /* DRUID CHANT */
 FS_SEQ(57, "0-3-5-7-5---3-0-", 12, FORWARD, MIN_PENTA, 9,
        GATE, FS_V9, DYNAMICS, "8575857585758575", MODWHEEL, "0000001234567777"),                        /* BONE FLUTE */
 FS_UNDER(FS_BASS),                                                                                       /* FORGOTTEN RITE */
 FS_SEQ(60, "0479c974e9c7420.", 12, FORWARD, MAJ_PENTA, 0,
        GATE, "4444444444444444", DYNAMICS, "9575957595759575", PROB_SKIP, "0000100000001000"),          /* BARROW BELLS */
 FS_SEQ(60, "024679bcegb97420", 6, RANDOM, LYDIAN, 0,
        GATE, "3333333333333333", DYNAMICS, "9596959695969596", PROB_SKIP, "2222222222222222"),          /* STAR CHART */
 FS_SEQ(60, "0.3.7a.c.a7.53..", 6, FORWARD, MIN_PENTA, 0,
        GATE, "3333333333333333", DYNAMICS, "9696969696969696", RATCHET, "0000300000060000"),            /* FROST SHATTER */
 FS_SEQ(48, "0..7..3...a..5..", 12, FORWARD, MINOR, 0,
        GATE, "6666666666666666", DYNAMICS, "7777777777777777", PROB_SKIP, "3333333333333333"),          /* DUST OF AGES */
 FS_UNDER(FS_BASS),                                                                                       /* EROSION */
 FS_SEQ(62, "07c70a7c0753a753", 6, FORWARD, MINOR, 2,
        GATE, "3333333333333333", DYNAMICS, "9575957595759575", RATCHET, "0000000000000000"),            /* OGHAM */
 FS_SEQ(57, "037acefjjfeca730", 12, FORWARD, DORIAN, 9,
        GATE, "5555555555555555", DYNAMICS, "9686968696869686", PROB_SKIP, "0001000100010001"),          /* LUNAR GATE */
 FS_SEQ(48, "0...7...3...5...", 24, FORWARD, MINOR, 0,
        GATE, FS_V9, DYNAMICS, "9777977797779777", RATCHET, "0000000000000000"),                         /* LAST STONE */
 FS_SEQ(60, "0.0c0.0a0.07.3.5", 6, FORWARD, MINOR, 0,
        GATE, "3353335333533353", DYNAMICS, "9575957595759575", RATCHET, "0000000000000300"),            /* CAIRN BASS */
 FS_SEQ(60, "0-----3-5---7-a-", 12, FORWARD, MINOR, 0,
        GATE, FS_V9, DYNAMICS, "8888888888888888", RATCHET, "0000000000000000"),                         /* DOLMEN SUB */
 FS_SEQ(60, "c-a-7-8-7-5-3-5-", 12, FORWARD, MINOR, 0,
        GATE, FS_V9, DYNAMICS, "8787878787878787", MODWHEEL, "0000123456789999"),                        /* MONOLITH LEAD */
 FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS),                                                 /* 23..25 */
 FS_SEQ(48, "0.3.7.b.c.b.8.7.", 6, FORWARD, HARM_MINOR, 0,
        GATE, "4444444444444444", DYNAMICS, "9575957595759575", RATCHET, "0000000000003000"),            /* STONE AND FIRE */
 FS_UNDER(FS_BASS),                                                                                       /* ALIGNMENT */
 FS_UNDER(FS_ROOT),                                                                                       /* ECLIPSE: G7b9, Ddim */
 FS_UNDER(FS_BASS),                                                                                       /* TUMULUS HALL */
 FS_SEQ(57, "00c0a0c00073c0a7", 6, FORWARD, MINOR, 9,
        GATE, "3333333333333333", DYNAMICS, "9555955595559555", RATCHET, "0000000300000006"),            /* WICKER MAN */
 FS_UNDER(FS_BASS),                                                                                       /* SAMHAIN MIST */
};
#undef FS_SEQ
#undef FS_UNDER
#undef FA_ARP
#undef FA_P
#undef FA_8
#undef FA_R

static uint8_t fs_digit(char c) { return (uint8_t)((c - '0') * 127 / 9); }

/* sequence slot s: lane 1 its notes, lanes 2..4 its value lanes; an ARP-led one's bass sits an octave under the first
 * chord and steps two arp steps a note (8 arp steps = 16 of its own) */
static void factory_seq(uint32_t s, SequencerLane lanes[SEQ_NLANE])
{
    static const uint8_t DIV_TICKS[ARPDIV_COUNT] = {192, 96, 48, 24, 16, 12, 8, 6, 4, 3};
    const FactorySeq *f = &FS_TAB[s];
    const FactoryArp *a = &FA_TAB[s];
    uint8_t root = f->root, p = f->prescale, l, i;
    char c;
    if (a->maq) {
        root = (uint8_t)(a->root[0] - 12u);
        p = (uint8_t)(DIV_TICKS[a->div] / 2u);
    }
    seq_defaults(lanes);
    for (l = 0; l < SEQ_NLANE; l++) {
        SequencerLane *ln = &lanes[l];
        ln->playback_mode = f->mode;
        ln->prescale = p;
        ln->loop_start = 0u;
        ln->loop_end = SEQ_NSTEP - 1u;
        ln->scale = SEQSCALE_OFF;
        ln->scale_root = 0u;
        for (i = 0; i < SEQ_NSTEP; i++) {
            SequencerStep *st = &ln->steps[i];
            st->note_buffer[0] = root;
            st->note_buffer[1] = st->note_buffer[2] = st->note_buffer[3] = SEQ_NOTE_NULL;
            st->flags = 0u;
            if (l == 0u) {
                c = f->notes[i];
                if (c == '-')
                    st->note_buffer[0] = SEQ_NOTE_TIE;
                else if (c == '.')
                    st->flags = SEQ_FLAG_MUTE;
                else
                    st->note_buffer[0] = (uint8_t)(root + (c <= '9' ? c - '0' : c - 'a' + 10));
                st->value = 0u;
            } else {
                st->value = fs_digit(f->val[l - 1u][i]);
            }
        }
        if (l == 0u) {
            ln->event_type = SEQEV_NOTE;
            ln->target_mask = SEQ_TGT_L1;
            ln->scale = f->scale;
            ln->scale_root = f->sroot;
        } else {
            ln->event_type = SEQEV_NOTE;                 /* (so seq_lane_set_event sets the new type's first target) */
            seq_lane_set_event(ln, f->ev[l - 1u]);
        }
    }
}

/* arp slot s */
static void factory_arp(uint32_t s, ArpState *st)
{
    const FactoryArp *f = &FA_TAB[s];
    uint8_t i;
    arp_defaults(st);
    for (i = 0; i < ARP_NSTEP; i++) {
        ArpChordStep *c = &st->steps[i];
        c->chord_root = f->root[i];
        c->rate = f->div;
        c->octave_range = f->oct;
        c->style = f->style;
        c->variation = (uint8_t)(f->var[i] - 'a');
        c->accent = (uint8_t)(f->acc[i] == '1');
    }
    st->gate_length = f->gate;
    st->transpose_maq_option = f->maq;
    st->current_genre_bank = f->genre;
}

/* performance slot s, whole: its patch on layer 1 (and the next one on layer 2, for DUAL / SPLIT), its effects and tempo,
 * its sequence, and its arp pattern if it leads (factory_seq above): 0, or -1 for a slot past the factory bank */
static int factory_perf(uint32_t s, WholePerformance *w)
{
    const FactoryPatch *f;
    EngineState base;
    MasterPerformancePreset *p = &w->perf;
    uint32_t l;
    if (s >= FP_N)
        return -1;
    f = &FP_TAB[s];
    engine_defaults(&base);
    for (l = 0; l < NLAYER; l++) {
        uint32_t ps = (s + l) % FP_N;
        factory_patch(&w->engine[l], &base, &FP_TAB[ps]);
        factory_mod(&w->engine[l], &FM_TAB[ps], FP_TAB[ps].name);
    }
    perf_defaults(p);
    factory_name(p->name, f->name);
    p->layer_patch[0] = (uint8_t)s;
    p->layer_patch[1] = (uint8_t)((s + 1u) % FP_N);
    p->seq_link = (uint8_t)s;
    p->arp_link = FA_TAB[s].maq ? (uint8_t)s : (uint8_t)127u;   /* SEQ-led: no arp pattern */
    p->perf_mode = PERF_SINGL_SYNTH;
    p->voice_mode[0] = p->voice_mode[1] = f->vmode;
    p->porta_mode[0] = PORTA_CONST_TIME;
    p->porta_time[0] = f->vmode == VMODE_MONO_LEGATO ? 20u : 0u;
    p->reverb_type = f->rtype; p->reverb_decay = f->rdec; p->reverb_damp = f->rdmp; p->reverb_predelay = f->rpre;
    p->delay_time = f->dtime; p->delay_feedback = f->dfb; p->delay_type = f->dtype; p->delay_pingpong = f->ping;
    p->delay_degrade = 36u;
    p->int_tempo = (uint8_t)(FA_TAB[s].bpm - MIDI_CLOCK_BPM_BASE);
    factory_seq(s, w->seq);
    if (FA_TAB[s].maq)
        factory_arp(s, &w->arp);
    else
        arp_defaults(&w->arp);
    return 0;
}

/* boot, before ps_load: the patches, sequences and arp patterns into their banks and the performances' names, all
 * marked used (a factory performance is made whole when it loads: factory_perf) */
static void factory_install(void)
{
    EngineState base;
    uint32_t s;
    engine_defaults(&base);
    for (s = 0; s < FP_N; s++) {
        factory_patch(&patch_bank[s], &base, &FP_TAB[s]);
        factory_mod(&patch_bank[s], &FM_TAB[s], FP_TAB[s].name);
        factory_seq(s, syn_seq_bank[s]);
        factory_arp(s, &syn_arp_bank[s]);
        factory_name(syn_perf_name[s], FP_TAB[s].name);
        syn_used[UISAVE_PATCH_PRESET][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_SEQ_SETTINGS][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_ARP_SEQ][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_WHOLE_PERF][s >> 3] |= (uint8_t)(1u << (s & 7u));
    }
}
PS_STATIC_ASSERT(FP_N == BANK_SLOTS, "a factory sequence, arp and performance for every slot");
