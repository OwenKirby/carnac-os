/* The factory bank "SAMHAIN" (2026-10-27): 32 patches (slots 0..31) showing off the Foundations bank (the SH-2's own
 * VCO shapes) through the SH-2 and MS-20 filters: dub basses, leads, mystical and pagan pads and bells, percussion;
 * with 8 whole performances, each with its sequence and arp pattern (slots 0..7 of those banks; the rest stay empty).
 * Performance 0, SAMHAIN, is the one the FM1 plays at power-on (syn_boot_perf). It replaced "RITES OF STONE"
 * (2026-10-21, in git history; NEWGRANGE DAWN kept as patch 16). A unity-build
 * fragment like preset_store_fm1.c. synth_main_fm1.c includes it after syn_used / syn_perf_name and the store, and calls
 * factory_install() BEFORE ps_load(): the factory slots are marked used, then anything the user saved over them in
 * flash replaces them. A performance (factory_perf, made when it loads) holds the patch, its effects (delay and reverb
 * are shared, performance-level), its tempo, its sequence and its arp pattern, so loading a PERF brings the whole
 * piece; loading only the PATCH keeps the current effects.
 *
 * Wavebanks: 0 Foundations 1 Hearth 2 Barrow 3 Trilithon 4 Ley Line 5 Solstice 6 Moonstone 7 Star Chart
 *            8 Druid Chant 9 Overtone Rite 10 Votive Choir 11 Bone Flute 12 Erosion 13 Frost Shatter 14 Dust;
 *            32.. the attacks as one-shot waves, 64.. the PCM waves (dsp/pcm_osc.h)
 * Knob units: time 0..127 (32 = 34 ms, 64 = 0.37 s, 80 = 1.1 s, 96 = 3 s, 112 = 8 s), cutoff ~13.4 knob steps an octave
 * (85 = 1.6 kHz), LFO rate 0.05 Hz * 800^(k/127), LFO shape 0 sine 21 tri 43 saw 64 square 85 step 106 slew.
 * Not yet heard on the device. */
#include <stddef.h>                 /* offsetof: the patches' settings are byte offsets into EngineState */
#define FP_OCT(k) ((uint8_t)((k) == 0 ? 64 : 18 * ((k) + 3) + 9))
#define FP_BIP(v) ((uint8_t)((v) + 64))
#define FP_N 32u                         /* SAMHAIN's patches, slots 0..31 */
#define FP_NPERF 8u                      /* its performances (and their sequences and arps), slots 0..7 */

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

/* ------------------------------------------------------------------------ SAMHAIN --- *
 * 2026-10-27: the factory bank, patch slots 0..31. Each patch is engine_defaults plus the list of (byte offset, value)
 * pairs below, so a patch states every setting it changes and nothing else: about 100 bytes of flash a patch, where
 * code setting the same fields would take several times that. The values are the stored bytes (bipolar ones 64 = 0:
 * FP_BIP; octaves FP_OCT; fine tune bytes by osc_fine_cents' square law, the cents in the comment).
 * Knob units: see the top of this file. A pitch mod amount a is a^2 * 4800 / 16129 cents (10 = 30 c, 13 = 50 c);
 * scan, mix, xmod, spread and pan amounts are knob units at a full source; a filter's MOD AMT a moves the cutoff a
 * knob steps at a full source (13.4 an octave); a mod slot's amount 63 is unity.
 * The FM1's own keys play at the HOME velocity knob and send no aftertouch, so the expressive control on the panel is
 * the MOD WHEEL (HOME knob 3): every patch gives it a job, named in its comment.
 * CPU: each architecture's voice cap holds; the SH-2 stays out of DUAL FILTER, STACK and BINAURAL, whose SH-2 rows run
 * over budget (docs/fm1-measure-checklist.md). */
#define V_(f, v) (uint8_t)offsetof(EngineState, f), (uint8_t)(v)
#define V_END 0xFFu
#define V_OSC(i, w, sc, oc) V_(osc[i].wavebank, w), V_(osc[i].scan, sc), V_(osc[i].octave, FP_OCT(oc))
#define V_FLT(i, fl, drv, mo, cut, res, amt) V_(filter[i].flavour, FLT_##fl), V_(filter[i].drive, drv), \
    V_(filter[i].morph, mo), V_(filter[i].cutoff, cut), V_(filter[i].resonance, res), V_(filter[i].mod_amt, 2 * (amt))
#define V_ENV(i, a, d, s, r) V_(env[i].attack, a), V_(env[i].decay, d), V_(env[i].sustain, s), V_(env[i].release, r)
#define V_LFO(i, sh, rt) V_(lfo[i].shape, sh), V_(lfo[i].rate, rt)
#define V_SLOT(i, a, fn, b, amt) V_(mod[i].src_a, MSRC_##a), V_(mod[i].func, MFN_##fn), V_(mod[i].src_b, MSRC_##b), \
    V_(mod[i].amount, FP_BIP(amt))
#define V_AMP(drv, model, ds, rs) V_(amp.drive, drv), V_(amp.drive_model, DRIVE_##model), V_(amp.delay_send, ds), \
    V_(amp.reverb_send, rs)
#define V_ATK(smp, lvl, flt, dec) V_(oscmix.atk_sample, smp), V_(oscmix.atk_level, lvl), V_(oscmix.atk_filter, flt), \
    V_(oscmix.atk_decay, dec)
#define V_KT(i, kt) V_(filter_key_track[i], kt)
/* a delayed vibrato on LFO2, blooming after each note starts */
#define V_VIB(rt, dl) V_LFO(1, L_SINE, rt), V_(lfo[1].delay, dl), V_(lfo[1].fade, FP_BIP(40)), V_(lfo[1].key_reset, 1)
/* the waves (dsp/pcm_osc.h): banks 0..14, the attacks as one-shots from 32, the PCM store's waves from 64
 * (gen_pcm_waves.py WAVES order) */
enum { W_FOUND, W_HEARTH, W_BARROW, W_TRILITHON, W_LEY, W_SOLSTICE, W_MOONSTONE, W_STARS, W_DRUID, W_OVERTONE,
       W_VOTIVE, W_BONEFLUTE, W_EROSION, W_FROST, W_DUST };
enum { W_WOOD = 64, W_RAIN, W_LORE, W_MOONSONG, W_CHOIR, W_HORNS, W_CHOIRWOOD, W_SINGSTONE, W_PANFLUTE, W_SPIRIT };
/* Foundations' frames by SCAN (gen_foundations.py): the SH-2's VCO shapes */
enum { F_TRI = 0, F_SAW = 18, F_SQUARE = 36, F_P35 = 54, F_P20 = 73, F_P10 = 91, F_PMIN = 109, F_SAWSQ = 127 };
/* attacks.h's sounds, by number (attack_names) */
enum { A_BONE_CHIFF, A_REED_CHIFF, A_HORN_BLAT, A_FLINT, A_STONE_KNOCK, A_MENHIR_THUD, A_LITHOPHONE, A_PEBBLES,
       A_WOOD_BLOCK, A_LOG_DRUM, A_BONE_CLACK, A_ANTLER_RASP, A_FRAME_DRUM, A_SEED_RATTLE, A_CRACKLE, A_CAVE_CLAP,
       A_BOW_PLUCK, A_MOUTH_BOW, A_POT_DRUM, A_CONCH };
#define W_ATK(a) (32 + (a))
enum { L_SINE = 0, L_TRI = 21, L_SAW = 43, L_SQUARE = 64, L_STEP = 85, L_SLEW = 106 };

/* ================================ basses: Foundations through the SH-2 and the MS-20 ================================ */

/* ---- 0 DUB BARROW: a round dub bass. Foundations SAW an octave down over a sine sub, a warm SH-2 low-pass with a
 * short ENV2 bump; tied steps slide. WHEEL: a clock-synced eighth-note wobble on the cutoff (MODSLOT1 = LFO2 x wheel,
 * added to ENV2 in MODSLOT2). */
static const uint8_t FV_DUBBARROW[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_SAW, -1),
    V_(oscmix.sub_level, 70), V_(oscmix.sub_waveform, SUBWAVE_SINE),
    V_FLT(0, SH2, 30, 0, 46, 40, 24), V_(filter[0].mod_src, MSRC_MODSLOT2), V_KT(0, 40),
    V_ENV(0, 0, 76, 100, 40), V_ENV(1, 0, 62, 20, 50),
    V_LFO(1, L_SINE, 56), V_(lfo[1].clock_sync, 1), V_(lfo[1].key_reset, 1),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63), V_SLOT(1, ENV2, ADD, MODSLOT1, 40),
    V_AMP(26, TUBE, 20, 10), V_(analog, 30),
    V_END};

/* ---- 1 SIDHE SUB: the hum under the fairy mound. Foundations TRIANGLE an octave down, a sine sub, an SH-2 driven warm
 * and nearly closed; nothing but weight. WHEEL: the cutoff opens. */
static const uint8_t FV_SIDHESUB[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_TRI, -1),
    V_(oscmix.sub_level, 60), V_(oscmix.sub_waveform, SUBWAVE_SINE),
    V_FLT(0, SH2, 50, 0, 52, 16, 30), V_(filter[0].mod_src, MSRC_MODWHEEL), V_KT(0, 40),
    V_ENV(0, 0, 90, 110, 44),
    V_AMP(0, SOFT, 8, 6), V_(analog, 24),
    V_END};

/* ---- 2 BOG SQUELCH: what the peat keeps. Foundations SAW an octave down into an SH-2 driven hard, resonance near the
 * whistle, a snapping ENV2 (the acid line); OVERDRIVE on the amp. WHEEL: the envelope digs deeper. */
static const uint8_t FV_BOG[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_SAW, -1),
    V_(oscmix.sub_level, 30), V_(oscmix.sub_waveform, SUBWAVE_SQUARE),
    V_FLT(0, SH2, 70, 0, 38, 100, 40), V_(filter[0].mod_src, MSRC_MODSLOT1), V_KT(0, 50),
    V_ENV(0, 0, 70, 90, 30), V_ENV(1, 0, 58, 0, 40),
    V_SLOT(0, ENV2, ADD, MODWHEEL, 50),
    V_AMP(30, OVERDRIVE, 20, 6), V_(analog, 20),
    V_END};

/* ---- 3 WIGHT GROWL: the barrow-wight. DUAL OSC: Foundations SAW+SQUARE against a SAW 6 cents flat, an octave down,
 * into an MS-20 low-pass at its growl, then TUBE. WHEEL: a clock-synced eighth-note wobble (as DUB BARROW). */
static const uint8_t FV_WIGHT[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_FOUND, F_SAWSQ, -1), V_OSC(1, W_FOUND, F_SAW, -1), V_(osc[1].fine, 58 /* -6 c */),
    V_(oscmix.balance, 64),
    V_FLT(0, MS20, 70, 0, 50, 90, 26), V_(filter[0].mod_src, MSRC_MODSLOT2), V_KT(0, 50),
    V_ENV(0, 0, 80, 100, 36), V_ENV(1, 0, 70, 40, 40),
    V_LFO(1, L_SINE, 56), V_(lfo[1].clock_sync, 1), V_(lfo[1].key_reset, 1),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63), V_SLOT(1, ENV2, ADD, MODSLOT1, 40),
    V_AMP(20, TUBE, 10, 8), V_(analog, 30),
    V_END};

/* ---- 4 CROM PULSE: the stooped one of the harvest. Foundations PULSE 20 % an octave down, LFO1 working the width
 * (SCAN across the pulse frames), an SH-2 with a short ENV2. WHEEL: the pulse narrows to a reedy nasal. */
static const uint8_t FV_CROM[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_P20, -1), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 30),
    V_(oscmix.sub_level, 40), V_(oscmix.sub_waveform, SUBWAVE_SINE),
    V_FLT(0, SH2, 40, 0, 54, 50, 28), V_KT(0, 50),
    V_ENV(0, 0, 70, 90, 36), V_ENV(1, 0, 60, 30, 40),
    V_LFO(0, L_TRI, 50),
    V_SLOT(0, LFO1, ADD, MODWHEEL, 40),
    V_AMP(10, TUBE, 14, 10), V_(analog, 30),
    V_END};

/* ---- 5 UNDERHILL: deep in the hollow hill. DUAL OSC: Foundations TRIANGLE an octave down and SQUARE two octaves
 * down, mostly the triangle; a soft MS-20 low-pass. The reggae bass. WHEEL: the square comes up (more growl). */
static const uint8_t FV_UNDERHILL[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_FOUND, F_TRI, -1), V_OSC(1, W_FOUND, F_SQUARE, -2),
    V_(oscmix.balance, 40), V_(oscmix.mix_mod_src, MSRC_MODWHEEL), V_(oscmix.mix_mod_amt, 40),
    V_FLT(0, MS20, 30, 0, 44, 30, 14), V_KT(0, 40),
    V_ENV(0, 0, 84, 100, 40), V_ENV(1, 0, 70, 50, 40),
    V_AMP(10, SOFT, 12, 6), V_(analog, 30),
    V_END};

/* ---- 6 PEAT THUMP: a stake driven into the bog. Foundations TRIANGLE an octave down, its pitch falling five semitones
 * into the note (ENV3), a driven SH-2; short, a bass that is half a drum. WHEEL: the filter opens. */
static const uint8_t FV_PEAT[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_TRI, -1), V_(osc[0].pitch_mod_src, MSRC_ENV3), V_(osc[0].pitch_mod_amt, 40),
    V_(oscmix.sub_level, 40), V_(oscmix.sub_waveform, SUBWAVE_SINE),
    V_FLT(0, SH2, 60, 0, 50, 30, 30), V_(filter[0].mod_src, MSRC_MODSLOT1), V_KT(0, 50),
    V_ENV(0, 0, 66, 0, 56), V_ENV(1, 0, 50, 0, 40), V_ENV(2, 0, 40, 0, 40),
    V_SLOT(0, ENV2, ADD, MODWHEEL, 40),
    V_AMP(20, TUBE, 16, 8), V_(analog, 20),
    V_END};

/* ---- 7 TUMULUS DRONE: the mound breathing. SPREAD Foundations SAW+SQUARE an octave down, the four playheads drifting
 * apart on a slow LFO1, an MS-20 low-pass wandering on a slewed random LFO3, into TAPE. WHEEL: the MS-20 morphs
 * low-pass -> band-pass (a hollow, vocal drone). */
static const uint8_t FV_TUMULUS[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_FOUND, F_SAWSQ, -1),
    V_(oscmix.spread, 60), V_(oscmix.spread_mod_src, MSRC_LFO1), V_(oscmix.spread_mod_amt, 20),
    V_(oscmix.sub_level, 40),
    V_FLT(0, MS20, 40, 0, 60, 60, 10), V_(filter[0].mod_src, MSRC_LFO3), V_KT(0, 40),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(50)),
    V_ENV(0, 90, 100, 120, 100),
    V_LFO(0, L_SINE, 14), V_LFO(2, L_SLEW, 30),
    V_AMP(10, TAPE, 30, 70), V_(analog, 50),
    V_END};

/* ============================================ leads ============================================ */

/* ---- 8 WILL O WISP: the marsh light that leads walkers astray. Foundations PULSE 20 % through an SH-2 on the edge of
 * whistling, a vibrato that waits, then blooms; a slow glide. WHEEL: the pulse thins (SCAN toward PULSE MIN). */
static const uint8_t FV_WISP[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_P20, 0), V_(osc[0].scan_mod_src, MSRC_MODWHEEL), V_(osc[0].scan_mod_amt, 36),
    V_(osc[0].pitch_mod_src, MSRC_LFO2), V_(osc[0].pitch_mod_amt, 9),
    V_FLT(0, SH2, 20, 0, 80, 104, 20), V_KT(0, 80),
    V_ENV(0, 20, 80, 110, 70), V_ENV(1, 30, 80, 60, 70),
    V_VIB(88, 60),
    V_AMP(0, SOFT, 40, 60), V_(analog, 40),
    V_END};

/* ---- 9 BANSHEE: the keening at the door. Foundations SAW+SQUARE into a driven MS-20 BAND-PASS, resonance high, a
 * slow ENV2 swell, a delayed vibrato; TUBE. WHEEL: band-pass -> high-pass (a thinner shriek). */
static const uint8_t FV_BANSHEE[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_SAWSQ, 0), V_(osc[0].pitch_mod_src, MSRC_LFO2), V_(osc[0].pitch_mod_amt, 10),
    V_FLT(0, MS20, 70, 64, 82, 96, 24), V_KT(0, 80),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(40)),
    V_ENV(0, 30, 90, 110, 70), V_ENV(1, 40, 90, 80, 70),
    V_VIB(90, 50),
    V_AMP(10, TUBE, 34, 50), V_(analog, 30),
    V_END};

/* ---- 10 HAG SYNC: the Cailleach's cry. DUAL OSC, OSC2 hard-synced to OSC1 (both Foundations SAW), OSC2's pitch
 * thrown up an octave and a half by ENV3 and falling back: the sync tear. MS-20 low-pass, OVERDRIVE. WHEEL: OSC2 rises
 * further (the tear stays). */
static const uint8_t FV_HAGSYNC[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_FOUND, F_SAW, 0), V_OSC(1, W_FOUND, F_SAW, 0), V_(osc[1].sync, 1),
    V_(osc[1].pitch_mod_src, MSRC_MODSLOT1), V_(osc[1].pitch_mod_amt, 70),
    V_(oscmix.balance, 100),
    V_FLT(0, MS20, 40, 0, 88, 40, 16), V_KT(0, 64),
    V_ENV(0, 0, 90, 110, 50), V_ENV(1, 0, 76, 60, 50), V_ENV(2, 0, 76, 30, 60),
    V_SLOT(0, ENV3, ADD, MODWHEEL, 40),
    V_AMP(10, OVERDRIVE, 30, 40), V_(analog, 20),
    V_END};

/* ---- 11 GHOST LIGHT: a theremin in the passage tomb. Foundations TRIANGLE an octave up, an open SH-2, a long glide,
 * a long tail. WHEEL: vibrato (LFO2 x wheel). */
static const uint8_t FV_GHOST[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_TRI, 1), V_(osc[0].pitch_mod_src, MSRC_MODSLOT1), V_(osc[0].pitch_mod_amt, 13),
    V_FLT(0, SH2, 0, 0, 96, 30, 0), V_KT(0, 64),
    V_ENV(0, 60, 90, 120, 90),
    V_LFO(1, L_SINE, 82),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63),
    V_AMP(0, SOFT, 40, 90), V_(analog, 50),
    V_END};

/* ---- 12 PUCA PIPE: the shape-shifter's pipe. STRIKE: the REED CHIFF over a Foundations SQUARE an octave up (a hollow
 * reed), a breath of noise, an MS-20 low-pass, a delayed vibrato. WHEEL: more breath. */
static const uint8_t FV_PUCA[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_FOUND, F_SQUARE, 1), V_(osc[0].pitch_mod_src, MSRC_LFO2), V_(osc[0].pitch_mod_amt, 9),
    V_ATK(A_REED_CHIFF, 70, 40, 100),
    V_(oscmix.noise_level, 10), V_(oscmix.noise_colour, 40), V_(oscmix.noise_mod_src, MSRC_MODWHEEL),
    V_(oscmix.noise_mod_amt, FP_BIP(24)),
    V_FLT(0, MS20, 20, 0, 86, 30, 14), V_KT(0, 64),
    V_ENV(0, 24, 80, 110, 50), V_ENV(1, 20, 60, 70, 50),
    V_VIB(88, 60),
    V_AMP(0, SOFT, 30, 54), V_(analog, 24),
    V_END};

/* ---- 13 WILD HUNT: the host riding the storm. UNISON STACK of Foundations SAWs through a driven SH-2 with a bite of
 * ENV2, TUBE. WHEEL: the filter opens (ENV2 + wheel). */
static const uint8_t FV_HUNT[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_SAW, 0),
    V_FLT(0, SH2, 50, 0, 70, 60, 30), V_(filter[0].mod_src, MSRC_MODSLOT1), V_KT(0, 64),
    V_ENV(0, 0, 90, 110, 60), V_ENV(1, 0, 76, 50, 60),
    V_SLOT(0, ENV2, ADD, MODWHEEL, 40),
    V_AMP(22, TUBE, 30, 40), V_(analog, 30),
    V_END};

/* ---- 14 BONE WHISTLE: a whistle cut from a swan's wing bone. STRIKE: the BONE CHIFF over Foundations PULSE MIN an
 * octave up through a resonant SH-2 BAND-PASS that follows the keys fully; a delayed vibrato. WHEEL: band-pass ->
 * high-pass (airier). */
static const uint8_t FV_WHISTLE[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_FOUND, F_PMIN, 1), V_(osc[0].pitch_mod_src, MSRC_LFO2), V_(osc[0].pitch_mod_amt, 9),
    V_ATK(A_BONE_CHIFF, 90, 40, 100),
    V_FLT(0, SH2, 0, 64, 92, 70, 10), V_KT(0, 100),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(40)),
    V_ENV(0, 20, 80, 110, 60), V_ENV(1, 20, 70, 80, 60),
    V_VIB(90, 56),
    V_AMP(0, SOFT, 34, 60), V_(analog, 24),
    V_END};

/* ---- 15 SPIRIT SIREN: the dub siren. Foundations SQUARE through an SH-2; hold a note and push the WHEEL: LFO2 sweeps
 * the pitch up and down (MODSLOT1 = LFO2 x wheel), wider and wider. Made for the delay. */
static const uint8_t FV_SIREN[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_SQUARE, 0), V_(osc[0].pitch_mod_src, MSRC_MODSLOT1), V_(osc[0].pitch_mod_amt, 60),
    V_FLT(0, SH2, 30, 0, 90, 50, 0), V_KT(0, 64),
    V_ENV(0, 0, 90, 127, 70),
    V_LFO(1, L_TRI, 60),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63),
    V_AMP(10, SOFT, 60, 40), V_(analog, 20),
    V_END};

/* ===================================== mystical, arcane, pagan: pads, keys, bells ===================================== */

/* ---- 16 NEWGRANGE DAWN: the winter-solstice sunrise creeping down the passage of Newgrange (kept from RITES OF
 * STONE). STACK: A, a Moonstone FM bell whose strike darkens as it rings; B, the PCM CHOIR swelling in behind it, its
 * vowel drifting on LFO3; ENV3 opens both filters over three seconds; LFO3 walks each voice through the stereo field.
 * WHEEL: vibrato, and the choir's filter opens. */
static const uint8_t FV_NEWGRANGE[] = {
    V_(arch, VARCH_STACK),
    V_OSC(0, W_MOONSTONE, 24, 0), V_(osc[0].scan_mod_src, MSRC_ENV1), V_(osc[0].scan_mod_amt, 56),
    V_(osc[0].pitch_mod_src, MSRC_MODSLOT1), V_(osc[0].pitch_mod_amt, 9),
    V_OSC(1, W_CHOIR, 46, 0), V_(osc[1].fine, 69 /* +4 c */), V_(osc[1].scan_mod_src, MSRC_LFO3),
    V_(osc[1].scan_mod_amt, 44), V_(osc[1].pitch_mod_src, MSRC_MODSLOT1), V_(osc[1].pitch_mod_amt, 9),
    V_FLT(0, MULTI, 0, 36, 94, 18, 0), V_KT(0, 64),
    V_FLT(1, MULTI, 10, 0, 78, 26, 26), V_(filter[1].mod_src, MSRC_MODWHEEL), V_KT(1, 40),
    V_(fltmix.balance, 68), V_(fltmix.stack_width, 76), V_(fltmix.env3_cutoff, FP_BIP(22)),
    V_(fltmix.balance_mod_src, MSRC_LFO1), V_(fltmix.balance_mod_amt, FP_BIP(12)),
    V_ENV(0, 0, 84, 22, 88), V_ENV(1, 72, 92, 112, 96), V_ENV(2, 96, 100, 104, 96),
    V_LFO(0, L_SINE, 22), V_LFO(1, L_TRI, 86), V_LFO(2, L_SINE, 28),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63),
    V_(amp.pan_mod_src, MSRC_LFO3), V_(amp.pan_mod_depth, 30),
    V_AMP(0, SOFT, 34, 80), V_(analog, 28),
    V_END};

/* ---- 17 THIN VEIL: the night the veil is thinnest. DUAL SPREAD: Foundations SAW and PULSE 35 % 4 cents apart, each
 * as four drifting playheads, LFO1 moving the saw's shape; a soft MS-20 low-pass swelling with ENV2. WHEEL: the
 * MS-20 morphs toward band-pass (the pad turns to voices). */
static const uint8_t FV_VEIL[] = {
    V_(arch, VARCH_DUAL_SPREAD),
    V_OSC(0, W_FOUND, F_SAW, 0), V_(osc[0].scan_mod_src, MSRC_LFO1), V_(osc[0].scan_mod_amt, 20),
    V_OSC(1, W_FOUND, F_P35, 0), V_(osc[1].fine, 69 /* +4 c */),
    V_(oscmix.balance, 64), V_(oscmix.spread, 50),
    V_FLT(0, MS20, 20, 0, 70, 40, 16), V_KT(0, 50),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(44)),
    V_ENV(0, 80, 100, 116, 98), V_ENV(1, 90, 100, 70, 98),
    V_LFO(0, L_SINE, 14),
    V_AMP(0, SOFT, 30, 80), V_(analog, 40),
    V_END};

/* ---- 18 CRYPT ORGAN: an organ nobody built, under the hill. DUAL OSC: Foundations SQUAREs at 8' and 4', an MS-20
 * low-pass, TUBE, the pan turning on LFO3 like a slow rotor. WHEEL: the 4' comes forward. */
static const uint8_t FV_CRYPT[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_FOUND, F_SQUARE, 0), V_OSC(1, W_FOUND, F_SQUARE, 1), V_(osc[1].fine, 66 /* +2 c */),
    V_(oscmix.balance, 50), V_(oscmix.mix_mod_src, MSRC_MODWHEEL), V_(oscmix.mix_mod_amt, 40),
    V_FLT(0, MS20, 30, 0, 92, 20, 0), V_KT(0, 64),
    V_ENV(0, 10, 80, 120, 50),
    V_LFO(2, L_SINE, 70),
    V_(amp.pan_mod_src, MSRC_LFO3), V_(amp.pan_mod_depth, 30),
    V_AMP(10, TUBE, 20, 70), V_(analog, 30),
    V_END};

/* ---- 19 CORPSE CANDLE: the light that walks to the house of the dying. DUAL OSC: a Foundations TRIANGLE RING-
 * modulated by a Moonstone bell a tritone and an octave up, the ring fading as ENV3 falls: an iron toll settling to a
 * pale tone. WHEEL: more ring. */
static const uint8_t FV_CANDLE[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_FOUND, F_TRI, 0), V_OSC(1, W_MOONSTONE, 40, 1), V_(osc[1].fine, 122 /* +600 c */),
    V_(oscmix.xmod_type, XMOD_RING), V_(oscmix.xmod_amt, 80), V_(oscmix.xmod_mod_src, MSRC_MODSLOT1),
    V_(oscmix.xmod_mod_amt, 40),
    V_FLT(0, MS20, 0, 0, 100, 10, 10), V_KT(0, 80),
    V_ENV(0, 0, 90, 0, 90), V_(env[0].key_to_time, FP_BIP(-16)), V_ENV(1, 0, 70, 0, 70), V_ENV(2, 0, 70, 0, 70),
    V_SLOT(0, ENV3, ADD, MODWHEEL, 40),
    V_(amp.pan_mod_depth, 30),
    V_AMP(0, SOFT, 40, 80), V_(analog, 16),
    V_END};

/* ---- 20 ALL SOULS: the choir of the dead on the eve of November. SPREAD over the PCM CHOIR, the playheads wide, LFO1
 * drifting the vowel, a warm SH-2 with a little drive. WHEEL: the choir opens (SCAN ooh -> aah). */
static const uint8_t FV_SOULS[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_CHOIR, 20, 0), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 80),
    V_(oscmix.spread, 90),
    V_FLT(0, SH2, 30, 0, 84, 24, 10), V_KT(0, 50),
    V_ENV(0, 80, 100, 116, 100), V_ENV(1, 80, 100, 90, 100),
    V_LFO(0, L_SINE, 12),
    V_SLOT(0, LFO1, ADD, MODWHEEL, 40),
    V_(amp.pan_mod_depth, 20),
    V_AMP(0, SOFT, 24, 90), V_(analog, 30),
    V_END};

/* ---- 21 OTHERWORLD: Tir na nOg glimpsed. STACK: A, the PCM MOONSONG hum; B, Foundations PULSE 10 % an octave up and
 * 4 cents flat through an MS-20 band-pass that LFO2 moves, swelling in later; spread wide. WHEEL: the overtone halo
 * rises over the hum (A's SCAN). */
static const uint8_t FV_OTHERWORLD[] = {
    V_(arch, VARCH_STACK),
    V_OSC(0, W_MOONSONG, 30, 0), V_(osc[0].scan_mod_src, MSRC_MODWHEEL), V_(osc[0].scan_mod_amt, 90),
    V_OSC(1, W_FOUND, F_P10, 1), V_(osc[1].fine, 59 /* -4 c */),
    V_FLT(0, MULTI, 0, 36, 90, 10, 0),
    V_FLT(1, MS20, 10, 64, 80, 50, 0), V_(filter[1].morph_mod_src, MSRC_LFO2), V_(filter[1].morph_mod_amt, FP_BIP(20)),
    V_KT(1, 64),
    V_(fltmix.balance, 60), V_(fltmix.stack_width, 96),
    V_ENV(0, 80, 96, 116, 100), V_ENV(1, 96, 100, 110, 100),
    V_LFO(1, L_TRI, 24),
    V_AMP(0, SOFT, 30, 90), V_(analog, 30),
    V_END};

/* ---- 22 CAIRN SPIRIT: the spirits in the cairn on Samhain night. SPREAD over the PCM CAIRN SPIRIT (dark "oh" choir
 * and breathy flutes over a stone drone; toward 127 the bright "aah" with its halo and bell shimmer), LFO1 drifting it
 * between the two over ten seconds, the playheads wide, a warm SH-2. WHEEL: the choir opens to the shimmer. */
static const uint8_t FV_SPIRIT[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_SPIRIT, 24, 0), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 90),
    V_(oscmix.spread, 70),
    V_FLT(0, SH2, 20, 0, 92, 20, 0), V_KT(0, 50),
    V_ENV(0, 72, 96, 116, 98),
    V_LFO(0, L_SINE, 12),
    V_SLOT(0, LFO1, ADD, MODWHEEL, 30),
    V_(amp.pan_mod_depth, 20),
    V_AMP(0, SOFT, 30, 84), V_(analog, 24),
    V_END};

/* ---- 23 RUNE PLUCK: a stave cut into yew. Foundations PULSE 10 % plucked through a resonant SH-2 that snaps open on
 * ENV2; higher notes shorter. WHEEL: the pluck opens (ENV2 + wheel). */
static const uint8_t FV_RUNE[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_P10, 0),
    V_FLT(0, SH2, 30, 0, 60, 60, 34), V_(filter[0].mod_src, MSRC_MODSLOT1), V_KT(0, 64),
    V_ENV(0, 0, 74, 0, 70), V_(env[0].key_to_time, FP_BIP(-16)), V_ENV(1, 0, 56, 0, 50),
    V_SLOT(0, ENV2, ADD, MODWHEEL, 40),
    V_(amp.pan_mod_depth, 30),
    V_AMP(0, SOFT, 40, 50), V_(analog, 30),
    V_END};

/* ============================================ percussion ============================================ */

/* ---- 24 CAIRN KICK: a stone dropped into the chamber. Foundations TRIANGLE two octaves down, its pitch falling two
 * octaves in a blink (ENV3), a driven SH-2, TUBE. WHEEL: the filter opens (a harder beater). */
static const uint8_t FV_KICK[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_TRI, -2), V_(osc[0].pitch_mod_src, MSRC_ENV3), V_(osc[0].pitch_mod_amt, 90),
    V_FLT(0, SH2, 80, 0, 60, 10, 30), V_(filter[0].mod_src, MSRC_MODWHEEL),
    V_ENV(0, 0, 60, 0, 50), V_ENV(2, 0, 36, 0, 36),
    V_AMP(30, TUBE, 6, 6),
    V_END};

/* ---- 25 BONE SNARE: a shoulder blade struck. STRIKE: the BONE CLACK over a Foundations TRIANGLE dropping into the
 * note and white noise, an MS-20 band-pass, HARD clip. WHEEL: the noise darkens (white -> brown). */
static const uint8_t FV_SNARE[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_FOUND, F_TRI, 0), V_(osc[0].pitch_mod_src, MSRC_ENV3), V_(osc[0].pitch_mod_amt, 40),
    V_ATK(A_BONE_CLACK, 90, 0, 90),
    V_(oscmix.noise_level, 90), V_(oscmix.noise_colour, 10), V_(oscmix.noise_col_mod_src, MSRC_MODWHEEL),
    V_(oscmix.noise_col_mod_amt, FP_BIP(40)),
    V_FLT(0, MS20, 40, 64, 90, 40, 0),
    V_ENV(0, 0, 56, 0, 52), V_ENV(2, 0, 30, 0, 30),
    V_AMP(20, HARD, 30, 40),
    V_END};

/* ---- 26 BODHRAN: the frame drum at the bonfire. STRIKE: the FRAME DRUM, tracked to the keys, over a Foundations
 * TRIANGLE body that drops into the note, an MS-20 low-pass following the keys; higher notes shorter. WHEEL: the
 * skin is bent (the attack slides in from below). */
static const uint8_t FV_BODHRAN[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_FOUND, F_TRI, -1), V_(osc[0].pitch_mod_src, MSRC_ENV3), V_(osc[0].pitch_mod_amt, 30),
    V_ATK(A_FRAME_DRUM, 76, 60, 110), V_(oscmix.atk_track, ATKTRK_TRACK),
    V_(oscmix.atk_bend_mod_src, MSRC_MODWHEEL), V_(oscmix.atk_bend_mod_amt, FP_BIP(-16)),
    V_FLT(0, MS20, 40, 0, 66, 40, 20), V_KT(0, 80),
    V_ENV(0, 0, 66, 0, 60), V_(env[0].key_to_time, FP_BIP(-20)), V_ENV(1, 0, 46, 0, 40), V_ENV(2, 0, 36, 0, 36),
    V_(amp.pan_mod_depth, 30),
    V_AMP(0, TAPE, 20, 30),
    V_END};

/* ---- 27 SEED RATTLE: the gourd shaken over the fire. STRIKE: the SEED RATTLE over the PEBBLES attack as a one-shot
 * wave, with white noise, through an MS-20 high-pass; short. WHEEL: the high-pass lowers (a fuller shake). */
static const uint8_t FV_RATTLE[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_ATK(A_PEBBLES), 0, 0),
    V_ATK(A_SEED_RATTLE, 90, 0, 100),
    V_(oscmix.noise_level, 70), V_(oscmix.noise_colour, 0),
    V_FLT(0, MS20, 20, 127, 96, 30, 20), V_(filter[0].mod_src, MSRC_MODWHEEL), V_KT(0, 60),
    V_ENV(0, 0, 50, 0, 46),
    V_(amp.pan_mod_depth, 40),
    V_AMP(0, SOFT, 30, 30),
    V_END};

/* ---- 28 HENGE STAB: the skank. DUAL OSC: Foundations SAW and SQUARE a fifth apart (one key, a power chord), a
 * short, driven MS-20 band-pass, TUBE, with most of it sent to the delay. WHEEL: band-pass -> high-pass (the stab
 * thins for the echoes). */
static const uint8_t FV_STAB[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_FOUND, F_SAW, 0), V_OSC(1, W_FOUND, F_SQUARE, 0), V_(osc[1].fine, 127 /* +700 c */),
    V_(oscmix.balance, 64),
    V_FLT(0, MS20, 40, 50, 84, 50, 24), V_KT(0, 64),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(40)),
    V_ENV(0, 0, 58, 0, 50), V_ENV(1, 0, 50, 0, 40),
    V_AMP(24, TUBE, 70, 40), V_(analog, 20),
    V_END};

/* ---- 29 LITHIC CHIME: a stone that sings when struck. STRIKE: the LITHOPHONE over the PCM SINGING STONE, a soft
 * SH-2; higher notes ring shorter. WHEEL: the stone is struck harder (SCAN soft -> hard). */
static const uint8_t FV_CHIME[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_SINGSTONE, 30, 0), V_(osc[0].scan_mod_src, MSRC_MODWHEEL), V_(osc[0].scan_mod_amt, 80),
    V_ATK(A_LITHOPHONE, 80, 30, 100),
    V_FLT(0, SH2, 0, 0, 92, 20, 0), V_KT(0, 80),
    V_ENV(0, 0, 78, 0, 78), V_(env[0].key_to_time, FP_BIP(-20)),
    V_(amp.pan_mod_depth, 30),
    V_AMP(0, SOFT, 40, 60),
    V_END};

/* ---- 30 ANTLER CLAV: an antler tine struck on a sinew string. Foundations PULSE MIN through a driven MS-20
 * band-pass snapping with ENV2, HARD clip: a funky, woody clav. WHEEL: band-pass -> low-pass (rounder). */
static const uint8_t FV_CLAV[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_PMIN, 0),
    V_FLT(0, MS20, 50, 64, 80, 50, 30), V_KT(0, 100),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(-40)),
    V_ENV(0, 0, 60, 40, 30), V_ENV(1, 0, 48, 0, 30),
    V_AMP(10, HARD, 40, 40), V_(analog, 20),
    V_END};

/* ---- 31 SPIRIT ZAP: the dub laser, a spirit passing. Foundations SQUARE an octave up, its pitch falling two and a
 * half octaves (ENV3), through an SH-2 ringing on its resonance; mostly delay. WHEEL: low-pass -> band-pass. */
static const uint8_t FV_ZAP[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_FOUND, F_SQUARE, 1), V_(osc[0].pitch_mod_src, MSRC_ENV3), V_(osc[0].pitch_mod_amt, 100),
    V_FLT(0, SH2, 40, 0, 96, 110, 50), V_KT(0, 64),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(50)),
    V_ENV(0, 0, 60, 0, 56), V_ENV(1, 0, 50, 0, 50), V_ENV(2, 0, 50, 0, 50),
    V_AMP(0, SOFT, 80, 50),
    V_END};

/* each patch slot: its patch, its voice mode and glide, and the effects a performance led by it uses */
typedef struct {
    const char *name;
    const uint8_t *set;                  /* (offset, value) pairs over engine_defaults, V_END ends */
    uint8_t vmode, porta, unison;        /* voice mode, glide (PORTA_* << 7 | time), UNISON DETUNE */
    uint8_t rtype, rdec, rdmp, rpre;     /* reverb */
    uint8_t dtime, dfb, dtype, ping, degrade, dsync;   /* delay: dtime set to the tempo (dotted eighths mostly) */
} FactoryVoice;
#define FV_GLIDE(mode, t) ((uint8_t)(PORTA_##mode << 7 | (t)))
#define FV_PP(ping, hp) ((uint8_t)((hp) << 1 | (ping)))   /* delay_pingpong: PING PONG and DELAY HP */
#define FV_ROW(name, set, vm, gl, un, rt, rd, rm, rp, dt, df, dy, pp, dg, ds) \
    {name, set, VMODE_##vm, gl, un, REVTYPE_##rt, rd, rm, rp, dt, df, DELAYTYPE_##dy, pp, dg, ds}
static const FactoryVoice FV_TAB[FP_N] = {
 /*      name             set            voice mode   glide                      uni reverb    dec dmp pre  dt  fb  type pp           deg sync */
 FV_ROW("DUB BARROW",     FV_DUBBARROW,  MONO_LEGATO, FV_GLIDE(LEGATO_TIED, 20), 0, TUMULUS,  110, 70, 20, 75, 62, TAPE, FV_PP(1, 24), 50, 0),
 FV_ROW("SIDHE SUB",      FV_SIDHESUB,   MONO_LEGATO, FV_GLIDE(CONST_TIME, 16), 0,  TOMB,      50, 70,  0, 75, 30, TAPE, FV_PP(0, 20), 36, 0),
 FV_ROW("BOG SQUELCH",    FV_BOG,        MONO_LEGATO, FV_GLIDE(LEGATO_TIED, 14), 0, SPRING,    60, 60,  4, 46, 50, TAPE, FV_PP(1, 30), 40, 0),
 FV_ROW("WIGHT GROWL",    FV_WIGHT,      MONO_RETRIG, 0,                        0,  DOLMEN,    60, 80,  0, 62, 30, BBD,  FV_PP(0, 20), 36, 0),
 FV_ROW("CROM PULSE",     FV_CROM,       MONO_LEGATO, FV_GLIDE(CONST_TIME, 12), 0,  TOMB,      50, 70,  0, 62, 30, TAPE, FV_PP(0, 20), 36, 0),
 FV_ROW("UNDERHILL",      FV_UNDERHILL,  MONO_LEGATO, FV_GLIDE(LEGATO_TIED, 16), 0, TOMB,      50, 70,  0, 79, 40, TAPE, FV_PP(0, 24), 50, 0),
 FV_ROW("PEAT THUMP",     FV_PEAT,       POLY_RESET,  0,                        0,  TOMB,      50, 70,  0, 62, 30, TAPE, FV_PP(0, 20), 36, 0),
 FV_ROW("TUMULUS DRONE",  FV_TUMULUS,    POLY_REUSE,  0,                        0,  TUMULUS,  120, 70, 30, 87, 50, TAPE, FV_PP(1, 16), 60, 0),
 FV_ROW("WILL O WISP",    FV_WISP,       MONO_LEGATO, FV_GLIDE(CONST_TIME, 30), 0,  CELESTIAL,112, 50, 30, 71, 50, TAPE, FV_PP(1, 20), 40, 0),
 FV_ROW("BANSHEE",        FV_BANSHEE,    MONO_LEGATO, FV_GLIDE(CONST_TIME, 40), 0,  TUMULUS,  110, 60, 30, 71, 46, BBD,  FV_PP(1, 20), 36, 0),
 FV_ROW("HAG SYNC",       FV_HAGSYNC,    MONO_LEGATO, FV_GLIDE(CONST_TIME, 20), 0,  DOLMEN,    80, 60, 10, 62, 40, TAPE, FV_PP(1, 20), 40, 0),
 FV_ROW("GHOST LIGHT",    FV_GHOST,      MONO_LEGATO, FV_GLIDE(CONST_TIME, 60), 0,  CELESTIAL,120, 50, 40, 79, 50, TAPE, FV_PP(1, 16), 50, 0),
 FV_ROW("PUCA PIPE",      FV_PUCA,       MONO_LEGATO, FV_GLIDE(CONST_TIME, 20), 0,  DOLMEN,    90, 60, 10, 71, 40, TAPE, FV_PP(0, 16), 40, 0),
 FV_ROW("WILD HUNT",      FV_HUNT,       UNISON_STACK,FV_GLIDE(CONST_TIME, 16), 44, TUMULUS,  100, 60, 20, 46, 40, TAPE, FV_PP(1, 24), 36, 0),
 FV_ROW("BONE WHISTLE",   FV_WHISTLE,    MONO_LEGATO, FV_GLIDE(CONST_TIME, 24), 0,  TOMB,      90, 60, 10, 71, 40, TAPE, FV_PP(0, 16), 40, 0),
 FV_ROW("SPIRIT SIREN",   FV_SIREN,      MONO_RETRIG, 0,                        0,  SPRING,    80, 50,  4, 75, 76, TAPE, FV_PP(1, 30), 60, 0),
 FV_ROW("NEWGRANGE DAWN", FV_NEWGRANGE,  POLY_REUSE,  0,                        0,  CELESTIAL,112, 60, 24, 80, 48, TAPE, FV_PP(1, 0),  30, 0),
 FV_ROW("THIN VEIL",      FV_VEIL,       POLY_REUSE,  0,                        0,  TUMULUS,  118, 70, 30, 87, 40, TAPE, FV_PP(1, 16), 50, 0),
 FV_ROW("CRYPT ORGAN",    FV_CRYPT,      POLY_REUSE,  0,                        0,  DOLMEN,   100, 70, 12, 71, 30, TAPE, FV_PP(0, 16), 50, 0),
 FV_ROW("CORPSE CANDLE",  FV_CANDLE,     POLY_RESET,  0,                        0,  CELESTIAL,116, 40, 30, 79, 50, BBD,  FV_PP(1, 16), 36, 0),
 FV_ROW("ALL SOULS",      FV_SOULS,      POLY_REUSE,  0,                        0,  TUMULUS,  120, 70, 40, 87, 40, TAPE, FV_PP(1, 16), 40, 0),
 FV_ROW("OTHERWORLD",     FV_OTHERWORLD, POLY_REUSE,  0,                        0,  CELESTIAL,124, 50, 40, 87, 50, TAPE, FV_PP(1, 16), 40, 0),
 FV_ROW("CAIRN SPIRIT",   FV_SPIRIT,     POLY_REUSE,  0,                        0,  TUMULUS,  120, 70, 30, 79, 60, TAPE, FV_PP(1, 24), 60, 0),
 FV_ROW("RUNE PLUCK",     FV_RUNE,       POLY_REUSE,  0,                        0,  MOD,      100, 50, 15, 68, 56, TAPE, FV_PP(1, 24), 50, 0),
 FV_ROW("CAIRN KICK",     FV_KICK,       POLY_RESET,  0,                        0,  TOMB,      40, 70,  0, 55, 30, TAPE, FV_PP(0, 30), 36, 0),
 FV_ROW("BONE SNARE",     FV_SNARE,      POLY_RESET,  0,                        0,  SPRING,    60, 50,  4, 55, 50, TAPE, FV_PP(1, 30), 50, 0),
 FV_ROW("BODHRAN",        FV_BODHRAN,    POLY_RESET,  0,                        0,  DOLMEN,    70, 60,  4, 55, 40, TAPE, FV_PP(1, 24), 50, 0),
 FV_ROW("SEED RATTLE",    FV_RATTLE,     POLY_RESET,  0,                        0,  TOMB,      50, 50,  4, 55, 40, BBD,  FV_PP(1, 24), 36, 0),
 FV_ROW("HENGE STAB",     FV_STAB,       POLY_RESET,  0,                        0,  SPRING,    80, 50,  4, 79, 70, TAPE, FV_PP(1, 30), 60, 0),
 FV_ROW("LITHIC CHIME",   FV_CHIME,      POLY_RESET,  0,                        0,  CELESTIAL,108, 40, 20, 71, 50, TAPE, FV_PP(1, 16), 36, 0),
 FV_ROW("ANTLER CLAV",    FV_CLAV,       POLY_REUSE,  0,                        0,  SPRING,    60, 50,  4, 55, 40, TAPE, FV_PP(1, 24), 40, 0),
 FV_ROW("SPIRIT ZAP",     FV_ZAP,        POLY_RESET,  0,                        0,  SPRING,    70, 50,  4, 71, 80, TAPE, FV_PP(1, 30), 60, 0),
};
#undef FV_ROW

/* the performances: the patch on layer 1 (its effects with it), the mode, and layer 2's patch (DUAL / SPLIT; SINGL:
 * the next slot, unheard) */
typedef struct {
    const char *name;
    uint8_t patch, pmode, l2;
} FactoryPerf;
#define FP_ROW(name, patch, pm, l2) {name, patch, PERF_##pm, l2}
static const FactoryPerf FP_TAB[FP_NPERF] = {
 FP_ROW("SAMHAIN",       0, SPLIT_SYNTH, 22),   /* DUB BARROW under C4, CAIRN SPIRIT from C4; its dub bass line plays */
 FP_ROW("SIDHE DOOR",   23, SINGL_SYNTH, 24),   /* RUNE PLUCK arpeggiating a minor-ninth progression into the echoes */
 FP_ROW("WILD HUNT",    13, SINGL_SYNTH, 14),   /* the galloping Phrygian line */
 FP_ROW("BOG SQUELCH",   2, SINGL_SYNTH,  3),   /* the acid line, Phrygian, sliding */
 FP_ROW("CAIRN RITE",   26, SINGL_SYNTH, 27),   /* the BODHRAN pattern, pitch as the drum */
 FP_ROW("DUB OFFERING", 28, SINGL_SYNTH, 29),   /* HENGE STAB on the off-beats, the echoes dubbed out */
 FP_ROW("ALL SOULS",    20, SINGL_SYNTH, 21),   /* the choir in slow block chords */
 FP_ROW("WILL O WISP",   8, SINGL_SYNTH,  9),   /* a harmonic-minor melody that leads you off the path */
};
#undef FP_ROW

/* patch slot s of the bank, over the defaults in base */
static void factory_voice(EngineState *e, const EngineState *base, uint32_t s)
{
    const uint8_t *p = FV_TAB[s].set;
    *e = *base;
    for (; p[0] != V_END; p += 2)
        ((uint8_t *)e)[p[0]] = p[1];
    factory_name(e->name, FV_TAB[s].name);
    varch_single_to_spread(e);
}
#undef V_

/* ------------------------------------------------------------------------ SEQUENCES AND ARPS --- *
 * The 8 sequences and 8 arp patterns, one of each for each performance. A performance is led by one of them, so PLAY
 * never runs two tunes over each other:
 *  - an ARP-led one (FA_TAB's maq 1) plays its chord progression, and its sequence is a bass under it, root and fifth
 *    (FS_BASS) or root alone (FS_ROOT), one note a chord: TRANSPOSE MAQ moves it to each chord's root;
 *  - a SEQ-led one plays its sequence; its arp pattern stays empty (arp_link 127), so its arp slot is only in the ARP
 *    bank, for the ARP page.
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

static const FactoryArp FA_TAB[FP_NPERF] = {
 FA_ARP(MIN,    1_8, UP,     1,  80, 0, FA_8(FA_D3, FA_BB2, FA_F2, FA_A2), "qqqqqqqq", FA_NOACC, 76),     /* SAMHAIN */
 FA_ARP(MIN9,   1_8, UPDN,   1,  80, 1, FA_8(FA_D3, FA_BB2, FA_F2, FA_A2), "qqqqqqqq", "10001000", 84),   /* SIDHE DOOR */
 FA_ARP(MIN,    1_4, DNUP,   2,  96, 0, FA_8(FA_E3, FA_F2, FA_D3, FA_E3),  "qqqqqqqq", FA_NOACC, 128),    /* WILD HUNT */
 FA_ARP(OCT,    1_4, UP,     0,  64, 0, FA_R(FA_A2, FA_A2, FA_A2, FA_A2, FA_BB2, FA_BB2, FA_G2, FA_G2),
        "qqqqqqqq", "10001000", 124),                                                                    /* BOG SQUELCH */
 FA_ARP(5TH,    1_8, RANDOM, 1,  64, 0, FA_8(FA_C3, FA_F2, FA_G2, FA_C3),  "qqqqqqqq", FA_NOACC, 104),    /* CAIRN RITE */
 FA_ARP(MIN,    1_4, BLOCK,  0,  64, 0, FA_8(FA_D3, FA_D3, FA_G2, FA_A2),  "ssssssss", "10101010", 72),  /* DUB OFFERING */
 FA_ARP(CINE_6, 1_2, BLOCK,  0, 127, 1, FA_P(FA_D3, FA_BB2, FA_F2, FA_A2), "aaaaaaaa", FA_NOACC, 66),     /* ALL SOULS */
 FA_ARP(MIN,    1_8, UP,     1,  80, 0, FA_8(FA_D3, FA_G2, FA_A2, FA_D3),  "qqqqqqqq", FA_NOACC, 80),     /* WILL O WISP */
};

#define FS_V9 "9999999999999999"
static const FactorySeq FS_TAB[FP_NPERF] = {
 FS_SEQ(38, "0-0.c-a.7--.5-3.", 12, FORWARD, MINOR, 2,
        GATE, "6666666666666666", DYNAMICS, "9686968696869686", MODWHEEL, "0000000000005555"),          /* SAMHAIN */
 FS_UNDER(FS_BASS),                                                                                       /* SIDHE DOOR */
 FS_SEQ(40, "00.00.07.00.01.3", 6, FORWARD, PHRYGIAN, 4,
        GATE, "5353535353535353", DYNAMICS, "9575957595759575", MODWHEEL, "0011223344556677"),          /* WILD HUNT */
 FS_SEQ(45, "0-c0.3-10a.0c7-5", 6, FORWARD, PHRYGIAN, 9,
        GATE, "3363336333633363", DYNAMICS, "9595559595559559", RATCHET, "0000000300000000"),            /* BOG SQUELCH */
 FS_SEQ(36, "0..70.c.0.7.0cc7", 6, FORWARD, OFF, 0,
        GATE, "2222222222222222", DYNAMICS, "9575957595759575", RATCHET, "0000000300000060"),            /* CAIRN RITE */
 FS_SEQ(50, ".0.0.3.0.0.0.5.3", 12, FORWARD, MINOR, 2,
        GATE, "4444444444444444", DYNAMICS, "8888888888888888", MODWHEEL, "0000000000009999"),          /* DUB OFFERING */
 FS_UNDER(FS_BASS),                                                                                       /* ALL SOULS */
 FS_SEQ(62, "0-3-7-8---7-b-c-", 12, FORWARD, HARM_MINOR, 2,
        GATE, FS_V9, DYNAMICS, "8686868686868686", MODWHEEL, "0000123456789999"),                        /* WILL O WISP */
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
    static const uint8_t DIV_TICKS[ARPDIV_COUNT] = {192, 96, 48, 24, 16, 12, 8, 6, 4, 3, 2};
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
        c->rate = arp_rate_of_div(f->div);          /* its rhythm across the step, as TIMING plays it */
        c->octave_range = f->oct;
        c->style = f->style;
        c->variation = (uint8_t)(f->var[i] - 'a');
        c->accent = (uint8_t)(f->acc[i] == '1');
    }
    arp_set_timing(st, f->div);
    st->gate_length = f->gate;
    st->transpose_maq_option = f->maq;
    st->current_genre_bank = f->genre;
}

/* performance slot s, whole: its patch on layer 1 (and its layer 2 patch, for DUAL / SPLIT), the effects of its layer
 * 1 patch and its tempo, its sequence, and its arp pattern if it leads (factory_seq above): 0, or -1 for a slot past the
 * factory bank */
static int factory_perf(uint32_t s, WholePerformance *w)
{
    const FactoryPerf *fp;
    const FactoryVoice *f;
    EngineState base;
    MasterPerformancePreset *p = &w->perf;
    if (s >= FP_NPERF)
        return -1;
    fp = &FP_TAB[s];
    f = &FV_TAB[fp->patch];
    engine_defaults(&base);
    factory_voice(&w->engine[0], &base, fp->patch);
    factory_voice(&w->engine[1], &base, fp->l2);
    perf_defaults(p);
    factory_name(p->name, fp->name);
    p->layer_patch[0] = fp->patch;
    p->layer_patch[1] = fp->l2;
    p->seq_link = (uint8_t)s;
    p->arp_link = FA_TAB[s].maq ? (uint8_t)s : (uint8_t)127u;   /* SEQ-led: no arp pattern */
    p->perf_mode = fp->pmode;
    p->voice_mode[0] = f->vmode;
    p->voice_mode[1] = fp->pmode == PERF_SINGL_SYNTH ? f->vmode : FV_TAB[fp->l2].vmode;
    p->porta_mode[0] = p->porta_mode[1] = (uint8_t)(f->porta >> 7);
    p->porta_time[0] = p->porta_time[1] = (uint8_t)(f->porta & 127u);
    if (f->vmode == VMODE_UNISON_STACK)
        p->unison_detune[0] = p->unison_detune[1] = f->unison;
    if (fp->pmode == PERF_DUAL_SYNTH)
        p->midi_ch[1] = p->midi_ch[0];                    /* the keys play both layers at once, whichever is selected */
    p->split_note = 60u;                                  /* SPLIT: layer 1 below C4, layer 2 from C4 */
    p->reverb_type = f->rtype; p->reverb_decay = f->rdec; p->reverb_damp = f->rdmp; p->reverb_predelay = f->rpre;
    p->delay_time = f->dtime; p->delay_feedback = f->dfb; p->delay_type = f->dtype; p->delay_pingpong = f->ping;
    p->delay_degrade = f->degrade;
    p->delay_sync = f->dsync;
    p->int_tempo = (uint8_t)(FA_TAB[s].bpm - MIDI_CLOCK_BPM_BASE);
    factory_seq(s, w->seq);
    if (FA_TAB[s].maq)
        factory_arp(s, &w->arp);
    else
        arp_defaults(&w->arp);
    return 0;
}

/* boot, before ps_load: the patches into their bank, the sequences and arp patterns into theirs, the performances',
 * sequences' and arps' names, all marked used (a factory performance is made whole when it loads: factory_perf) */
static void factory_install(void)
{
    EngineState base;
    uint32_t s;
    engine_defaults(&base);
    for (s = 0; s < FP_N; s++) {
        factory_voice(&patch_bank[s], &base, s);
        syn_used[UISAVE_PATCH_PRESET][s >> 3] |= (uint8_t)(1u << (s & 7u));
    }
    for (s = 0; s < FP_NPERF; s++) {
        factory_seq(s, syn_seq_bank[s]);
        factory_arp(s, &syn_arp_bank[s]);
        factory_name(syn_perf_name[s], FP_TAB[s].name);
        factory_name(syn_seq_name[s], FP_TAB[s].name);   /* (a performance's sequence and arp pattern: its name) */
        factory_name(syn_arp_name[s], FP_TAB[s].name);
        syn_used[UISAVE_SEQ_SETTINGS][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_ARP_SEQ][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_WHOLE_PERF][s >> 3] |= (uint8_t)(1u << (s & 7u));
    }
}
PS_STATIC_ASSERT(FP_N == BANK_SLOTS && FP_NPERF <= BANK_SLOTS, "a factory patch for every slot, performances fit");
