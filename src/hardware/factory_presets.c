/* The factory bank "THE OLD KNOWLEDGE" (2026-11-19): 32 patches (slots 0..31) and 18 whole performances (slots 0..17,
 * each with its sequence and arp pattern in the same slots of those banks; the rest stay empty), built on everything
 * the instrument has learned since SAMHAIN: the VCO, the mod matrix (up to 16 routes a patch, VIA and VIA DEPTH), the
 * MFX units, STACK's CROSS-FEED, BINAURAL and DUAL FILTER, the noise resonance, the TUNINGs, the MIX page's cross-feeds
 * and LOOP TONE, per-lane loops and modes on the SEQ (polymeter), the J-6 chord sets on the ARP.
 * The theme: the stones remember what their builders knew. The lunar standstill at Calanais, the sky disc of Nebra,
 * the flint mines of Grimes Graves, the orrery the circles may have been.
 * 2026-11-19 later: three PCM patches (RAINMAKER, STONE KIT, FLINT KIT: the user asked for LORE and RAINSTICK and for
 * the techno to bang on rocks and wood), 35 patches and 20 performances.
 * Performance 0, LUNAR STANDSTILL, is the one the FM1 plays at power-on (syn_boot_perf): its keys play a polyphonic pad
 * (layer 1), PLAY adds the moon's bells arpeggiating over it (layer 2) and the pad's own slow drone.
 * The bank replaced SAMHAIN (2026-10-27, in git history; DUB BARROW, SPIRIT SIREN, SPIRIT ZAP and a few others carried
 * over, reworked).
 *
 * A unity-build fragment like preset_store_fm1.c. synth_main_fm1.c includes it after syn_used / syn_perf_name and the
 * store, and calls factory_install() BEFORE ps_load(): the factory slots are marked used, then anything the user saved
 * over them in flash replaces them. A performance (factory_perf, made when it loads) holds both patches, its effects
 * (delay, reverb, MIX: performance-level, so a patch loaded alone keeps the current ones), its tempo, its tuning, its
 * sequence and its arp pattern, so loading a PERF brings the whole piece.
 *
 * THE MOD WHEEL (user rule 2026-11-19): every patch gives it a job, through the mod matrix (test_preset_store checks
 * each patch has a route from or VIA the MOD WHEEL). The FM1's wheel is HOME knob 3; the comment of each patch says
 * what it does.
 *
 * Wavebanks: 0 the VCO (shown as Foundations) 1 Ley Line 2 Moonstone 3 Star Chart 4 Overtone Halo 5 Passage Tomb
 *            6 Aurora 7 Frost Shatter 8 Dust; 64.. the PCM waves (dsp/pcm_osc.h)
 * Knob units: time 0..127 (32 = 34 ms, 64 = 0.37 s, 80 = 1.1 s, 96 = 3 s, 112 = 8 s), cutoff ~13.4 knob steps an octave
 * (85 = 1.6 kHz), LFO rate 0.05 Hz * 800^(k/127) (14 = 0.1 Hz, 30 = 0.24, 50 = 0.7, 64 = 1.4, 82 = 3.7, 88 = 5 Hz),
 * LFO shape 0 sine 21 tri 43 saw 64 square 85 step 106 slew.
 * Route amounts (-64..+63, synth_voice.c): a cutoff route is square-tapered (20 ~ an octave, 32 ~ 2.4, 45 ~ 4.7 octaves
 * at a full source); a pitch route a gives ~1.2 a^2 cents (3 = 11 c, 4 = 19 c, 10 = 119 c); SCAN, MIX, the MFX knobs
 * twice the amount in knob units; PAN the amount in pan units; AMP LEVEL and the LFO DEPTHs a gain (0 none, 63 the
 * source alone sets it). A route's VIA DEPTH is 127 (the VIA alone sets the depth) unless V_RTV says otherwise. */
#include <stddef.h>                 /* offsetof: the patches' settings are byte offsets into EngineState */
#define FP_OCT(k) ((uint8_t)(OSC_COARSE_ZERO + 12 * (k)))   /* TUNE k octaves (2026-11-11: semitones) */
#define FP_BIP(v) ((uint8_t)((v) + 64))
#define FP_N 35u                         /* THE OLD KNOWLEDGE's patches, slots 0..34 */
#define FP_NPERF 20u                     /* its performances (and their sequences and arps), slots 0..19 */

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

/* ------------------------------------------------------------------------ THE PATCHES --- *
 * Each patch is engine_defaults plus the list of (byte offset, value) pairs below, so a patch states every setting it
 * changes and nothing else: about 100 bytes of flash a patch. The values are the stored bytes (bipolar ones 64 = 0:
 * FP_BIP; octaves FP_OCT; OSC2's fine tune 64 + cents, the cents in the comment).
 * CPU: every patch keeps 6 voices alone or more (varch_voice_cap, its routes priced since 2026-11-20; test_preset_store
 * checks it), most of them 8: an MFX only on a patch whose
 * architecture leaves the room for it, the SH-2 out of STACK and BINAURAL. */
#define V_(f, v) (uint8_t)offsetof(EngineState, f), (uint8_t)(v)
#define V_END 0xFFu
#define V_OSC(i, w, sc, oc) V_(osc[i].wavebank, w), V_(osc[i].scan, sc), V_(osc[i].coarse, FP_OCT(oc))
#define V_FLT(i, fl, drv, mo, cut, res, amt) V_(filter[i].flavour, FLT_##fl), V_(filter[i].drive, drv), \
    V_(filter[i].morph, mo), V_(filter[i].cutoff, cut), V_(filter[i].resonance, res), V_(filter[i].mod_amt, 2 * (amt))
#define V_ENV(i, a, d, s, r) V_(env[i].attack, a), V_(env[i].decay, d), V_(env[i].sustain, s), V_(env[i].release, r)
#define V_LFO(i, sh, rt) V_(lfo[i].shape, sh), V_(lfo[i].rate, rt)
/* a mod matrix route: SOURCE, VIA (NONE: none), DEST, AMOUNT -64..+63; V_RTV with a VIA DEPTH (127: the VIA alone) */
#define V_RT(i, s, vi, d, amt) V_(route[i].src, MSRC_##s), V_(route[i].via, MSRC_##vi), V_(route[i].dest, MDST_##d), \
    V_(route[i].amount, FP_BIP(amt))
#define V_RTV(i, s, vi, d, amt, vd) V_RT(i, s, vi, d, amt), V_(route[i].via_depth, vd)
#define V_AMP(drv, model, ds, rs) V_(amp.drive, drv), V_(amp.drive_model, DRIVE_##model), V_(amp.delay_send, ds), \
    V_(amp.reverb_send, rs)
#define V_KT(i, kt) V_(filter_key_track[i], kt)
/* the MFX: its type, its four knobs, its lr row (WIDTH / L/R PHASE / SPREAD) and its two further rows (dsp/mfx.h) */
#define V_MFX(t, k1, k2, k3, k4, wd, r0, r1) V_(mfx.type, MFX_##t), V_(mfx.knob[0], k1), V_(mfx.knob[1], k2), \
    V_(mfx.knob[2], k3), V_(mfx.knob[3], k4), V_(mfx.lr, wd), V_(mfx.row[0], r0), V_(mfx.row[1], r1)
/* a vibrato on LFO2 that waits (dl) and then fades in, restarting at each note */
#define V_VIB(rt, dl) V_LFO(1, L_SINE, rt), V_(lfo[1].delay, dl), V_(lfo[1].fade, FP_BIP(40)), V_(lfo[1].key_reset, 1)
/* the waves (dsp/pcm_osc.h): banks 0..8 (bank 0 the VCO), the PCM store's waves from 64 */
enum { W_VCO, W_LEY, W_MOONSTONE, W_STARS, W_HALO, W_TOMB, W_AURORA, W_FROST, W_DUST };
enum { W_CHOIR = 64, W_SPIRITVOX, W_PANFLUTE, W_HORNS, W_STRENS, W_HOARFROST, W_SINGSTONE, W_LORE, W_BELL,
       W_ROCKGONG, W_RAINSTICK, W_MENHIR, W_LOGDRUM, W_WOODBLOCK,   /* the rebuilt PCM bank (2026-11-06) */
       W_HARP, W_BEAKER, W_BOWED };                                /* the LA bank (2026-11-21) */
/* the VCO's shapes by SCAN (as Foundations' frames): the SH-2's VCO-1 */
enum { F_TRI = 0, F_SAW = 18, F_SQUARE = 36, F_P35 = 54, F_P20 = 73, F_P10 = 91, F_PMIN = 109, F_SAWSQ = 127 };
enum { L_SINE = 0, L_TRI = 21, L_SAW = 43, L_SQUARE = 64, L_STEP = 85, L_SLEW = 106 };
/* MULTI's morph, its modes */
enum { M_LP24 = 0, M_LP18 = 18, M_LP12 = 36, M_LP6 = 54, M_BP = 73, M_HP12 = 91, M_HP24 = 109 };

/* ======================================= the hero: LUNAR STANDSTILL ======================================= */

/* ---- 0 LUNAR STANDSTILL: every 18.6 years the moon skims the southern hills at Calanais, and the stones were set to
 * catch it. DUAL OSC: OSC1 the Overtone Halo (a soft drone and one singing overtone climbing the harmonic series), its
 * SCAN walked up and down by LFO1 over ten seconds; OSC2 the PCM STR ENS (the warm SOLSTICE strings), 3 cents flat.
 * Each note starts as the singing halo and the strings swell in under it (ENV2 > MIX); a slow ENV3 opens the filter
 * over three seconds (seven routes: trimmed from 14 after the PERF TESTs, 2026-11-19 / 20, each costs every voice
 * its tick, and the voice price counts them: the LFO3 drifts went last). The ENS+PHASER (the late-70s string chain) turns it all into the Jarre-era wash.
 * WHEEL: the overtones climb higher, the phaser speeds up, the filter opens and a vibrato comes in. */
static const uint8_t FV_LUNAR[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_HALO, 24, 0), V_OSC(1, W_STRENS, 40, 0), V_(osc[1].fine, 61 /* -3 c */),
    V_(oscmix.balance, 46), V_(oscmix.sub_level, 26),
    V_FLT(0, MULTI, 10, M_LP24, 70, 30, 0), V_KT(0, 50),
    V_ENV(0, 60, 100, 120, 84), V_ENV(1, 90, 100, 110, 84), V_ENV(2, 96, 110, 80, 84),
    V_LFO(0, L_TRI, 14), V_LFO(1, L_SINE, 82), V_(lfo[1].delay, 50),
    V_RT(0, LFO1, NONE, OSC1_SCAN, 24), V_RT(1, MODWHEEL, NONE, OSC1_SCAN, 30), V_RT(2, MODWHEEL, NONE, MFX_K3, 24),
    V_RT(3, MODWHEEL, NONE, F1_CUTOFF, 30), V_RT(4, ENV3, NONE, F1_CUTOFF, 30), V_RT(5, ENV2, NONE, OSC_MIX, 22),
    V_RT(6, LFO2, MODWHEEL, PITCH, 3),
    V_MFX(ENS_PHASER, 74, 64, 15, 100, 110, 30, 50),
    V_AMP(0, SOFT, 28, 80), V_(analog, 26),
    V_END};

/* ---- 1 MOON GLASS: moonlight on wet stone. A Moonstone FM bell an octave up, its strike clanging (ENV3 > SCAN) and
 * settling to the hum; higher notes further right. The bells LUNAR STANDSTILL arpeggiates. WHEEL: the bell turns
 * clangorous (SCAN toward the high FM index). */
static const uint8_t FV_MOONGLASS[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_MOONSTONE, 30, 1),
    V_FLT(0, MULTI, 0, M_LP12, 96, 20, 20), V_KT(0, 64),
    V_ENV(0, 0, 78, 0, 76), V_ENV(1, 0, 62, 0, 60), V_ENV(2, 0, 66, 0, 60),
    V_RT(0, ENV3, NONE, OSC1_SCAN, 20), V_RT(1, MODWHEEL, NONE, OSC1_SCAN, 40), V_RT(2, KEYBOARD, NONE, PAN, 20),
    V_RT(3, VELOCITY, NONE, F1_CUTOFF, 16),
    V_AMP(0, SOFT, 60, 70), V_(analog, 16),
    V_END};

/* ===================================== warm analog, vintage ===================================== */

/* ---- 2 HEARTH CIRCLE: the fire in the middle of the round house. DUAL OSC: VCO SAW and a VCO PULSE 35 % 5 cents flat,
 * LFO1 slowly working the pulse's width, a sine sub, a warm, driven ladder low-pass (MULTI LP24: the SH-2 was too dear
 * under its chords, PERF TEST 2026-11-19) with a soft ENV2; the 80s polysynth's CHORUS.
 * WHEEL: the filter opens and a vibrato comes in. */
static const uint8_t FV_HEARTH[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_VCO, F_SAW, 0), V_OSC(1, W_VCO, F_P35, 0), V_(osc[1].fine, 59 /* -5 c */),
    V_(oscmix.sub_level, 30),
    V_FLT(0, MULTI, 30, M_LP24, 66, 30, 14), V_KT(0, 50),
    V_ENV(0, 50, 90, 110, 66), V_ENV(1, 60, 90, 70, 66),
    V_LFO(0, L_TRI, 30), V_LFO(1, L_SINE, 80),
    V_RT(0, MODWHEEL, NONE, F1_CUTOFF, 34), V_RT(1, LFO2, MODWHEEL, PITCH, 3), V_RT(2, VELOCITY, NONE, F1_CUTOFF, 10),
    V_RT(3, LFO1, NONE, OSC2_SCAN, 10),
    V_MFX(CHORUS, 45, 94, 46, 64, 127, 20, 0),
    V_AMP(6, TUBE, 20, 50), V_(analog, 40),
    V_END};

/* ---- 3 BELTANE STRINGS: the May fire's string machine. SPREAD: a VCO SAW as four drifting playheads, a mellow
 * low-pass, the string ensemble's three swinging brigades (ENSEMBLE). WHEEL: brighter, and the ensemble swings
 * deeper. */
static const uint8_t FV_BELTANE[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_SAW, 0), V_(oscmix.spread, 50),
    V_FLT(0, MULTI, 0, M_LP12, 84, 10, 0), V_KT(0, 50),
    V_ENV(0, 66, 90, 120, 80),
    V_LFO(0, L_SINE, 20),
    V_RT(0, MODWHEEL, NONE, F1_CUTOFF, 30), V_RT(1, MODWHEEL, NONE, MFX_K4, 30), V_RT(2, LFO1, NONE, SPREAD, 10),
    V_RT(3, KEYBOARD, NONE, PAN, 12),
    V_MFX(ENSEMBLE, 40, 74, 45, 64, 110, 0, 0),
    V_AMP(0, SOFT, 20, 70), V_(analog, 40),
    V_END};

/* ---- 4 AUROCHS HORN: the great horn of the wild ox. DUAL OSC: two VCO SAWs 7 cents apart into an MS-20 low-pass
 * that ENV2 swells open like breath into brass, TUBE, a vibrato that waits. WHEEL: the horn blares (ENV2 VIA WHEEL
 * on the cutoff, on top of its own). */
static const uint8_t FV_AUROCHS[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_VCO, F_SAW, 0), V_OSC(1, W_VCO, F_SAW, 0), V_(osc[1].fine, 71 /* +7 c */),
    V_FLT(0, MS10, 10, 0, 54, 30, 28), V_KT(0, 64),
    V_ENV(0, 36, 80, 70, 70), V_ENV(1, 44, 72, 70, 70),
    V_VIB(84, 70),
    V_RT(0, ENV2, MODWHEEL, F1_CUTOFF, 30), V_RT(1, VELOCITY, NONE, F1_CUTOFF, 20), V_RT(2, LFO2, NONE, PITCH, 3),
    V_AMP(4, TUBE, 20, 50), V_(analog, 36),
    V_END};

/* ---- 5 AMBER KEYS: resin warmed in the hand. DUAL OSC: a VCO TRIANGLE and a VCO SQUARE an octave up, mostly the
 * triangle, through a soft low-pass that the key strike opens; a STONE PHASER barely moving. WHEEL: the tremolo
 * (LFO3 VIA WHEEL on the level) and the phaser deepen together, the 70s electric piano. */
static const uint8_t FV_AMBER[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_VCO, F_TRI, 0), V_OSC(1, W_VCO, F_SQUARE, 1), V_(oscmix.balance, 40),
    V_FLT(0, MULTI, 0, M_LP24, 80, 10, 30), V_KT(0, 64),
    V_ENV(0, 0, 90, 70, 74), V_(env[0].key_to_time, FP_BIP(-12)), V_ENV(1, 0, 74, 30, 70),
    V_LFO(2, L_SINE, 60),
    V_RT(0, LFO3, MODWHEEL, AMP_LEVEL, 40), V_RT(1, MODWHEEL, NONE, MFX_K4, 40), V_RT(2, VELOCITY, NONE, F1_CUTOFF, 20),
    V_RT(3, KEYBOARD, NONE, PAN, 12),
    V_MFX(STONE_PHASER, 64, 41, 50, 40, 30, 0, 0),
    V_AMP(26, TUBE, 20, 40), V_(analog, 30),
    V_END};

/* ---- 6 OGHAM LEAD: the notched alphabet cut on a standing stone's edge. A VCO SAW + SQUARE with a square sub, a
 * driven, resonant MS-20, a slow glide, a vibrato that waits (half its depth without the wheel). WHEEL: the cutoff
 * opens and the vibrato reaches its full depth. */
static const uint8_t FV_OGHAM[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_SAWSQ, 0), V_(oscmix.sub_level, 40), V_(oscmix.sub_waveform, SUBWAVE_SQUARE),
    V_FLT(0, MS10, 20, 0, 70, 70, 20), V_KT(0, 64),
    V_ENV(0, 10, 80, 90, 60), V_ENV(1, 0, 70, 60, 60),
    V_VIB(84, 60),
    V_RTV(0, LFO2, MODWHEEL, PITCH, 4, 64), V_RT(1, MODWHEEL, NONE, F1_CUTOFF, 26),
    V_AMP(4, TUBE, 40, 50), V_(analog, 30),
    V_END};

/* ---- 7 DOLMEN BASS: the capstone's weight. A VCO SAW an octave down over a sine sub through a driven SH-2 with a
 * bite of ENV2; tied notes slide. WHEEL: the filter opens. */
static const uint8_t FV_DOLMEN[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_SAW, -1), V_(oscmix.sub_level, 70),
    V_FLT(0, SH2, 40, 0, 44, 50, 20), V_KT(0, 40),
    V_ENV(0, 0, 76, 100, 40), V_ENV(1, 0, 60, 10, 40),
    V_RT(0, MODWHEEL, NONE, F1_CUTOFF, 30), V_RT(1, VELOCITY, NONE, F1_CUTOFF, 14),
    V_AMP(36, TUBE, 10, 10), V_(analog, 30),
    V_END};

/* ---- 8 SKYFIRE SYNC: lightning striking the cairn. FULL: OSC2 hard-synced to OSC1 (both VCO SAW), ENV3 throwing
 * OSC2 up an octave and a half and letting it fall (the sync tear), into an MS-20 low-pass and, in parallel, a
 * resonant SH-2 band-pass; OVERDRIVE. WHEEL: OSC2 climbs further (the tear stays open). */
static const uint8_t FV_SKYFIRE[] = {
    V_(arch, VARCH_FULL),
    V_OSC(0, W_VCO, F_SAW, 0), V_OSC(1, W_VCO, F_SAW, 0), V_(osc[1].sync, 1), V_(oscmix.balance, 100),
    V_FLT(0, MS10, 40, 0, 88, 40, 16), V_KT(0, 64), V_FLT(1, MULTI, 20, M_BP, 90, 60, 10), V_KT(1, 64),
    V_(fltmix.routing, 127), V_(fltmix.balance, 50),
    V_ENV(0, 0, 90, 110, 50), V_ENV(1, 0, 76, 60, 50), V_ENV(2, 0, 76, 30, 60),
    V_VIB(86, 70),
    V_RT(0, ENV3, NONE, OSC2_PITCH, 28), V_RT(1, MODWHEEL, NONE, OSC2_PITCH, 28), V_RT(2, LFO2, NONE, PITCH, 3),
    V_AMP(16, OVERDRIVE, 34, 40), V_(analog, 20),
    V_END};

/* ---- 9 SKARA BRAE ORGAN: the stone dressers of the village under the dune. DUAL OSC: VCO SQUAREs at 8' and 4', an
 * MS-20, TUBE driven a little, a scanner-vibrato CHORUS, the pan turning on LFO3 like a rotor. WHEEL: the rotor
 * spins up (LFO3 SPEED), slow to fast. */
static const uint8_t FV_SKARA[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_VCO, F_SQUARE, 0), V_OSC(1, W_VCO, F_SQUARE, 1), V_(osc[1].fine, 66 /* +2 c */),
    V_FLT(0, MS10, 0, 0, 76, 20, 0), V_KT(0, 64),
    V_ENV(0, 6, 80, 76, 40),
    V_LFO(2, L_SINE, 50),
    V_RT(0, LFO3, NONE, PAN, 20), V_RT(1, MODWHEEL, NONE, LFO3_SPEED, 40), V_RT(2, LFO3, NONE, F1_CUTOFF, 8),
    V_MFX(CHORUS, 60, 70, 40, 50, 127, 40, 0),
    V_AMP(8, TUBE, 10, 50), V_(analog, 30),
    V_END};

/* ===================================== Berlin school ===================================== */

/* ---- 10 ORRERY PULSE: the circles as a clockwork of the sky. The sequencer voice: a VCO PULSE 20 % whose width LFO1
 * works, a sine sub, a resonant SH-2 plucked by a short ENV2. WHEEL: the cutoff opens (the sequences ride it with a
 * lane). */
static const uint8_t FV_ORRERY[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_P20, 0), V_(oscmix.sub_level, 40),
    V_FLT(0, SH2, 50, 0, 50, 80, 22), V_KT(0, 50),
    V_ENV(0, 0, 64, 30, 44), V_ENV(1, 0, 54, 0, 40),
    V_LFO(0, L_TRI, 30),
    V_RT(0, MODWHEEL, NONE, F1_CUTOFF, 34), V_RT(1, LFO1, NONE, OSC1_SCAN, 12), V_RT(2, VELOCITY, NONE, F1_CUTOFF, 12),
    V_AMP(24, TUBE, 44, 34), V_(analog, 30),
    V_END};

/* ---- 11 CALANAIS SEQ: the counter-sequence, glassy. Star Chart (prime harmonics: the stars come out as SCAN rises),
 * each pluck lighting more of them (ENV3 > SCAN), a resonant MULTI low-pass, higher notes further right. WHEEL: the
 * whole sky comes out (SCAN) and the filter opens. */
static const uint8_t FV_CALSEQ[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_STARS, 40, 0),
    V_FLT(0, MULTI, 0, M_LP24, 76, 50, 26), V_KT(0, 64),
    V_ENV(0, 0, 72, 0, 60), V_ENV(1, 0, 56, 0, 50), V_ENV(2, 0, 56, 0, 50),
    V_RT(0, ENV3, NONE, OSC1_SCAN, 24), V_RT(1, MODWHEEL, NONE, OSC1_SCAN, 30), V_RT(2, MODWHEEL, NONE, F1_CUTOFF, 20),
    V_RT(3, KEYBOARD, NONE, PAN, 18),
    V_AMP(0, SOFT, 50, 50), V_(analog, 20),
    V_END};

/* ---- 12 SKY DISC CHOIR: the tape-replay choir of the 70s, for the sky disc found at Nebra. The PCM CHOIR, one
 * playhead as the tapes had (SPREAD 0: cheaper under chords, PERF TEST 2026-11-19), LFO1 drifting the vowel, a slow wow on the pitch (LFO2) as the tapes stretch. WHEEL: ooh opens to aah. */
static const uint8_t FV_SKYDISC[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_CHOIR, 20, 0),
    V_FLT(0, MULTI, 0, M_LP12, 88, 10, 0), V_KT(0, 50),
    V_ENV(0, 70, 100, 116, 92),
    V_LFO(0, L_SINE, 12), V_LFO(1, L_SINE, 30),
    V_RT(0, LFO1, NONE, OSC1_SCAN, 16), V_RT(1, MODWHEEL, NONE, OSC1_SCAN, 40), V_RT(2, LFO2, NONE, PITCH, 2),
    V_RT(3, KEYBOARD, NONE, PAN, 10),
    V_AMP(0, SOFT, 24, 80), V_(analog, 40),
    V_END};

/* ---- 13 STONE FLUTE: a bone flute in a stone chamber. The PCM PAN FLUTE (hollow, toward 127 airy), its breath the
 * noise ringing in the voice's own band (NOISE BAND), a vibrato that waits. WHEEL: the flute turns airy and the
 * vibrato deepens. */
static const uint8_t FV_FLUTE[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_PANFLUTE, 30, 0),
    V_(oscmix.balance, 10), V_(oscmix.noise_colour, WHISTLE_BAND), V_(noise_tint, 127),   /* the band on brown, as before */
    V_FLT(0, MULTI, 0, M_LP12, 100, 10, 0), V_KT(0, 64),
    V_ENV(0, 40, 90, 110, 70),
    V_VIB(84, 70),
    V_RTV(0, LFO2, MODWHEEL, PITCH, 4, 64), V_RT(1, MODWHEEL, NONE, OSC1_SCAN, 40), V_RT(2, KEYBOARD, NONE, PAN, 10),
    V_AMP(0, SOFT, 34, 70), V_(analog, 30),
    V_END};

/* ---- 14 TUMULUS DRONE: the mound breathing. BINAURAL: a VCO SAW + SQUARE an octave down once per ear, the right ear
 * 4 Hz above the left, each ear's MS-20 cutoff apart, a slewed random LFO3 wandering the cutoff; TAPE. WHEEL: the
 * MS-20 morphs low-pass -> band-pass (a hollow, vocal drone). */
static const uint8_t FV_TUMULUS[] = {
    V_(arch, VARCH_BINAURAL),
    V_OSC(0, W_VCO, F_SAWSQ, -1),
    V_FLT(0, MS10, 10, 0, 54, 44, 0), V_KT(0, 40),
    V_(fltmix.bin_cutoff_offset, FP_BIP(10)),
    V_ENV(0, 90, 100, 80, 100),
    V_LFO(2, L_SLEW, 30),
    V_RT(0, LFO3, NONE, F1_CUTOFF, 16), V_RT(1, MODWHEEL, NONE, F1_MORPH, 50),
    V_AMP(0, TAPE, 30, 70), V_(analog, 50),
    V_END};

/* ---- 15 LONG MEG BASS: Long Meg and her daughters, turned to stone for dancing. The Berlin sequencer bass. DUAL
 * FILTER: a VCO SAW an octave down into a resonant SH-2 low-pass plucked by ENV2, then a MULTI high-pass keeping the
 * bottom tight. WHEEL: the SH-2 opens. */
static const uint8_t FV_LONGMEG[] = {
    V_(arch, VARCH_DUAL_FILTER),
    V_OSC(0, W_VCO, F_SAW, -1), V_(oscmix.sub_level, 50),
    V_FLT(0, SH2, 40, 0, 60, 60, 24), V_KT(0, 40), V_FLT(1, MULTI, 0, M_HP12, 10, 10, 0),
    V_(fltmix.routing, 0), V_(fltmix.balance, 64),
    V_ENV(0, 0, 66, 30, 44), V_ENV(1, 0, 50, 0, 40),
    V_RT(0, MODWHEEL, NONE, F1_CUTOFF, 34), V_RT(1, VELOCITY, NONE, F1_CUTOFF, 12),
    V_AMP(56, TUBE, 30, 20), V_(analog, 30),
    V_END};

/* ===================================== hypnotic techno ===================================== */

/* ---- 16 FLINT ACID: knapped flint, razor-edged. A VCO SAW into an SH-2 driven hard, resonance near the whistle, a
 * snapping ENV2; the velocity (the sequence's accents) digs deeper; OVERDRIVE; tied notes slide. WHEEL: the cutoff
 * opens and the SH-2 is driven harder. */
static const uint8_t FV_ACID[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_SAW, -1), V_(oscmix.sub_level, 20), V_(oscmix.sub_waveform, SUBWAVE_SQUARE),
    V_FLT(0, SH2, 70, 0, 36, 104, 32), V_KT(0, 50),
    V_ENV(0, 0, 70, 90, 30), V_ENV(1, 0, 56, 0, 40),
    V_RT(0, VELOCITY, NONE, F1_CUTOFF, 20), V_RT(1, MODWHEEL, NONE, F1_CUTOFF, 32), V_RT(2, MODWHEEL, NONE, F1_DRIVE, 30),
    V_AMP(30, OVERDRIVE, 24, 10), V_(analog, 20),
    V_END};

/* ---- 17 RITUAL STAB: the chord struck in the chamber and left to echo: the dub-techno stab. DUAL OSC: a VCO SAW and a
 * PULSE 35 % 6 cents sharp, a short MULTI low-pass envelope, most of it sent to the delay. WHEEL: the filter opens
 * (the knob every dub-techno record rides). */
static const uint8_t FV_STAB[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_VCO, F_SAW, 0), V_OSC(1, W_VCO, F_P35, 0), V_(osc[1].fine, 70 /* +6 c */),
    V_FLT(0, MULTI, 10, M_LP24, 60, 40, 22), V_KT(0, 50),
    V_ENV(0, 0, 60, 0, 60), V_ENV(1, 0, 54, 0, 50),
    V_RT(0, MODWHEEL, NONE, F1_CUTOFF, 34), V_RT(1, VELOCITY, NONE, F1_CUTOFF, 10),
    V_AMP(10, SOFT, 80, 50), V_(analog, 30),
    V_END};

/* ---- 18 MONOLITH BASS: the techno bass, deep as the stone goes into the ground. A VCO TRIANGLE an octave down over a
 * sine sub, a driven MS-20, a short pluck. WHEEL: the filter opens and the triangle leans to a saw (growl). */
static const uint8_t FV_MONOLITH[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_TRI, -1), V_(oscmix.sub_level, 60),
    V_FLT(0, MS10, 50, 0, 50, 20, 16), V_KT(0, 40),
    V_ENV(0, 0, 64, 40, 36), V_ENV(1, 0, 50, 0, 30),
    V_RT(0, MODWHEEL, NONE, F1_CUTOFF, 30), V_RT(1, MODWHEEL, NONE, OSC1_SCAN, 9),
    V_AMP(20, TUBE, 10, 6), V_(analog, 20),
    V_END};

/* ---- 19 CAIRN KICK: a stone dropped into the chamber. A VCO TRIANGLE two octaves down, its pitch falling two octaves
 * in a blink (ENV3), a driven SH-2, TUBE. WHEEL: a harder beater (the filter opens). */
static const uint8_t FV_KICK[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_TRI, -2), V_FLT(0, SH2, 80, 0, 60, 10, 0), V_ENV(0, 0, 60, 0, 50), V_ENV(2, 0, 36, 0, 36),
    V_AMP(42, TUBE, 6, 6),
    V_RT(0, ENV3, NONE, OSC1_PITCH, 45), V_RT(1, MODWHEEL, NONE, F1_CUTOFF, 30),
    V_END};

/* ---- 20 FLINT HAT: flint struck on flint. White noise and a VCO SQUARE three octaves up through a MULTI high-pass,
 * a tick of a decay. WHEEL: the noise darkens white -> pink -> brown (the hat turns to a shaker). */
static const uint8_t FV_HAT[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_VCO, F_SQUARE, 3),
    V_(oscmix.balance, 110), V_(oscmix.noise_colour, NOISE_WHITE),
    V_FLT(0, MULTI, 0, M_HP24, 112, 30, 0),
    V_ENV(0, 0, 42, 0, 38),
    V_RT(0, MODWHEEL, NONE, NOISE_COLOUR, 16), V_RT(1, VELOCITY, NONE, F1_CUTOFF, 10),
    V_AMP(0, SOFT, 30, 20),
    V_END};

/* ---- 21 TRISKELE: the triple spiral at Newgrange's entrance stone, turning without end. Frost Shatter (a sine
 * folded until it cracks) plucked through a resonant MULTI, LFO1 slowly working the cutoff. WHEEL: the ice cracks
 * (SCAN) and the filter opens a little (the TRISKELE sequence rides it with a lane looping against the notes). */
static const uint8_t FV_TRISKELE[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_FROST, 20, 0),
    V_FLT(0, MULTI, 0, M_LP24, 70, 60, 24), V_KT(0, 64),
    V_ENV(0, 0, 58, 0, 50), V_ENV(1, 0, 50, 0, 40),
    V_LFO(0, L_SINE, 18),
    V_RT(0, MODWHEEL, NONE, OSC1_SCAN, 25), V_RT(1, MODWHEEL, NONE, F1_CUTOFF, 16), V_RT(2, LFO1, NONE, F1_CUTOFF, 14),
    V_RT(3, KEYBOARD, NONE, PAN, 16),
    V_AMP(0, SOFT, 50, 40), V_(analog, 20),
    V_END};

/* ===================================== dub ===================================== */

/* ---- 22 DUB BARROW: a round dub bass. A VCO SAW an octave down over a sine sub, a warm SH-2 with a short ENV2 bump;
 * tied steps slide. WHEEL: a clock-synced eighth-note wobble on the cutoff (LFO2 VIA WHEEL), and a little more
 * cutoff under it. */
static const uint8_t FV_DUBBARROW[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_SAW, -1),
    V_(oscmix.sub_level, 70),
    V_FLT(0, SH2, 30, 0, 46, 40, 15), V_KT(0, 40),
    V_ENV(0, 0, 76, 100, 40), V_ENV(1, 0, 62, 20, 50),
    V_LFO(1, L_SINE, 56), V_(lfo[1].clock_sync, 1), V_(lfo[1].key_reset, 1),
    V_RT(0, LFO2, MODWHEEL, F1_CUTOFF, 22), V_RT(1, MODWHEEL, NONE, F1_CUTOFF, 14),
    V_AMP(36, TUBE, 20, 10), V_(analog, 30),
    V_END};

/* ---- 23 HENGE SKANK: the off-beat guitar chop of the sound system at the henge. DUAL OSC: a VCO SAW and a SQUARE an
 * octave up through a short, driven MS-20 band-pass, TUBE, a BBD ECHO slap ducking under the playing. WHEEL:
 * band-pass -> high-pass (the chop thins for the echoes). */
static const uint8_t FV_SKANK[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_VCO, F_SAW, 0), V_OSC(1, W_VCO, F_SQUARE, 1),
    V_FLT(0, MS10, 40, 50, 84, 50, 24), V_KT(0, 64),
    V_ENV(0, 0, 58, 0, 50), V_ENV(1, 0, 50, 0, 40),
    V_RT(0, MODWHEEL, NONE, F1_MORPH, 40),
    V_MFX(BBD_ECHO, 99, 35, 40, 45, 40, MFX_QE_DUCK, 40),
    V_AMP(24, TUBE, 70, 40), V_(analog, 20),
    V_END};

/* ---- 24 SPIRIT SIREN: the dub siren. A VCO SQUARE through an SH-2; hold a note and push the WHEEL: LFO2 sweeps the
 * pitch up and down (LFO2 VIA WHEEL), wider and wider. Made for the delay. */
static const uint8_t FV_SIREN[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_SQUARE, 0), V_FLT(0, SH2, 30, 0, 90, 50, 0), V_KT(0, 64),
    V_ENV(0, 0, 90, 127, 70),
    V_LFO(1, L_TRI, 60),
    V_RT(0, LFO2, MODWHEEL, OSC1_PITCH, 30),
    V_AMP(24, SOFT, 60, 40), V_(analog, 20),
    V_END};

/* ---- 25 BOG REED: a reed cut from the bog, blown like the melodica in a dub session. DUAL OSC: a VCO SQUARE and a
 * PULSE 35 % 3 cents sharp, through an MS-20 between low-pass and band-pass (reedy), a vibrato that waits. WHEEL: the
 * reed brightens and the vibrato deepens. */
static const uint8_t FV_REED[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_VCO, F_SQUARE, 0), V_OSC(1, W_VCO, F_P35, 0), V_(osc[1].fine, 67 /* +3 c */),
    V_FLT(0, MS10, 10, 30, 86, 20, 6), V_KT(0, 64),
    V_ENV(0, 20, 80, 76, 50), V_ENV(1, 10, 70, 80, 50),
    V_VIB(86, 60),
    V_RTV(0, LFO2, MODWHEEL, PITCH, 4, 64), V_RT(1, MODWHEEL, NONE, F1_CUTOFF, 24),
    V_AMP(0, TUBE, 50, 40), V_(analog, 30),
    V_END};

/* ---- 26 SPIRIT ZAP: the dub laser, a spirit passing. A VCO SQUARE an octave up, its pitch falling two and a half
 * octaves (ENV3), through an SH-2 ringing on its resonance; mostly delay. WHEEL: low-pass -> band-pass. */
static const uint8_t FV_ZAP[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_VCO, F_SQUARE, 1), V_FLT(0, SH2, 40, 0, 96, 110, 50), V_KT(0, 64),
    V_ENV(0, 0, 60, 0, 56), V_ENV(1, 0, 50, 0, 50), V_ENV(2, 0, 50, 0, 50),
    V_AMP(20, SOFT, 80, 50),
    V_RT(0, ENV3, NONE, OSC1_PITCH, 50), V_RT(1, MODWHEEL, NONE, F1_MORPH, 50),
    V_END};

/* ===================================== 90s digital pads ===================================== */

/* ---- 27 AURORA ALTAR: northern lights over the ring of stones. SPREAD Aurora (a bright spectrum rippled like a frozen
 * phaser of many notches), the ripples tightening over the note (ENV3 > SCAN) and drifting on LFO1, each voice
 * wandering in the stereo field (LFO3), the 90s CHORUS. WHEEL: the curtains ripple faster (SCAN) and spread wider. */
static const uint8_t FV_AURORA[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_AURORA, 30, 0), V_(oscmix.spread, 50),
    V_FLT(0, MULTI, 0, M_LP12, 92, 20, 0), V_KT(0, 50),
    V_ENV(0, 76, 100, 116, 96), V_ENV(2, 96, 110, 60, 96),
    V_LFO(0, L_SINE, 16), V_LFO(2, L_SLEW, 36),
    V_RT(0, ENV3, NONE, OSC1_SCAN, 30), V_RT(1, LFO1, NONE, OSC1_SCAN, 16), V_RT(2, LFO3, NONE, PAN, 20),
    V_RT(3, MODWHEEL, NONE, OSC1_SCAN, 40), V_RT(4, MODWHEEL, NONE, SPREAD, 20),
    V_MFX(CHORUS, 40, 90, 50, 64, 127, 10, 0),
    V_AMP(0, SOFT, 24, 84), V_(analog, 16),
    V_END};

/* ---- 28 FROST RUNES: runes cut in rime. STACK, the early-90s way (a sampled attack over a synth pad): A, the PCM
 * BELL struck and dying away; B, the Overtone Halo swelling in behind it on its own envelope; A's filter fed into
 * B's (CROSS-FEED), so the strike rings into the pad; LFO1 walks the halo. WHEEL: the blend moves toward the pad
 * and the halo climbs. */
static const uint8_t FV_FROST[] = {
    V_(arch, VARCH_STACK),
    V_OSC(0, W_BELL, 40, 0), V_OSC(1, W_HALO, 30, 0), V_(osc[1].fine, 67 /* +3 c */),
    V_FLT(0, MULTI, 0, M_LP12, 100, 10, 0), V_KT(0, 64), V_FLT(1, MULTI, 0, M_LP24, 84, 20, 0), V_KT(1, 50),
    V_(fltmix.balance, 60), V_(fltmix.stack_xfeed, FP_BIP(16)),
    V_ENV(0, 0, 90, 0, 80), V_ENV(1, 80, 100, 110, 84),
    V_LFO(0, L_SINE, 16),
    V_RT(0, LFO1, NONE, OSC2_SCAN, 14), V_RT(1, MODWHEEL, NONE, BALANCE, 24), V_RT(2, MODWHEEL, NONE, OSC2_SCAN, 30),
    V_RT(3, KEYBOARD, NONE, PAN, 14),
    V_AMP(0, SOFT, 30, 84), V_(analog, 16),
    V_END};

/* ---- 29 HALO OF BRODGAR: the Ring of Brodgar at midsummer. DUAL OSC: Ley Line (the earth current: octaves and
 * fifths, each frame waking more of the stack) swelling up its harmonics over the note (ENV3 > SCAN), and the PCM
 * SPIRIT VOX; a PITCH SHIFT an octave up with feedback, the shifted sound going round again (the 90s shimmer).
 * WHEEL: the shimmer comes forward (MIX) and the Ley Line wakes higher. */
static const uint8_t FV_BRODGAR[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_LEY, 10, 0), V_OSC(1, W_SPIRITVOX, 30, 0), V_(osc[1].fine, 61 /* -3 c */),
    V_(oscmix.balance, 70),
    V_FLT(0, MULTI, 0, M_LP12, 90, 10, 0), V_KT(0, 50),
    V_ENV(0, 72, 100, 116, 100), V_ENV(2, 100, 110, 80, 100),
    V_LFO(0, L_SINE, 10), V_LFO(2, L_SLEW, 30),
    V_RT(0, ENV3, NONE, OSC1_SCAN, 30), V_RT(1, LFO1, NONE, OSC2_SCAN, 14), V_RT(2, MODWHEEL, NONE, MFX_K4, 30),
    V_RT(3, MODWHEEL, NONE, OSC1_SCAN, 30), V_RT(4, LFO3, NONE, PAN, 18),
    V_MFX(PITCH_SHIFT, 24, 70, 50, 30, 127, 20, 0),   /* +12, 6 cents spread either way, the spiral's feedback */
    V_AMP(0, SOFT, 20, 84), V_(analog, 20),
    V_END};

/* ---- 30 PASSAGE TOMB: a tone sounded in the passage of a tomb. SPREAD Passage Tomb (three resonances of the stone
 * passage sliding down and spreading as the passage opens into the chamber, ENV3 > SCAN over four seconds), and a
 * WIND HARP of six strings tuned to D minor ringing in sympathy. WHEEL: the harp blooms (MIX) and the chamber opens
 * further. */
static const uint8_t FV_PASSAGE[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_TOMB, 10, 0), V_(oscmix.spread, 30),
    V_FLT(0, MULTI, 0, M_LP12, 100, 10, 0), V_KT(0, 50),
    V_ENV(0, 70, 100, 116, 96), V_ENV(2, 100, 110, 90, 96),
    V_LFO(2, L_SLEW, 30),
    V_RT(0, ENV3, NONE, OSC1_SCAN, 40), V_RT(1, MODWHEEL, NONE, MFX_K4, 22), V_RT(2, MODWHEEL, NONE, OSC1_SCAN, 16),
    V_RT(3, LFO3, NONE, PAN, 18),
    V_MFX(WIND_HARP, 100, 70, 40, 40, 100, 14 /* D3 */, 1 /* MINOR */),
    V_AMP(0, SOFT, 24, 80), V_(analog, 20),
    V_END};

/* ---- 31 SINGING STONES: the stones that ring when struck (the rock gongs of the old monuments). The PCM SINGING STN,
 * and RINGING STONES: six struck stones tuned in fifths on D answering it. WHEEL: the scrape turns to the singing
 * tone (SCAN) and the stones ring louder (MIX). */
static const uint8_t FV_SINGING[] = {
    V_(arch, VARCH_SPREAD), V_(oscmix.balance, 0),
    V_OSC(0, W_SINGSTONE, 30, 0),
    V_FLT(0, MULTI, 0, M_LP12, 100, 10, 0), V_KT(0, 50),
    V_ENV(0, 10, 96, 60, 96),
    V_LFO(2, L_SLEW, 30),
    V_RT(0, MODWHEEL, NONE, OSC1_SCAN, 40), V_RT(1, MODWHEEL, NONE, MFX_K4, 30), V_RT(2, LFO3, NONE, PAN, 18),
    V_MFX(RINGING_STONES, 100, 60, 30, 44, 100, 14 /* D3 */, 0 /* FIFTHS */),
    V_AMP(0, SOFT, 24, 80), V_(analog, 16),
    V_END};

/* ===================================== stone, wood and rain (2026-11-19 later) ===================================== */

/* ---- 32 RAINMAKER: the rain called down on the stones. STACK: A, the PCM LORE (wooden knocks; toward 127 the wind
 * chimes) struck and ringing; B, the PCM RAINSTICK swelling in behind it on its own slow envelope and lingering, its
 * shake drifting toward the pour on LFO1; each voice placed by its key and wandering on LFO3. WHEEL: the knocks turn
 * to chimes and the rain comes forward. */
static const uint8_t FV_RAIN[] = {
    V_(arch, VARCH_STACK),
    V_OSC(0, W_LORE, 20, 0), V_OSC(1, W_RAINSTICK, 30, 0),
    V_FLT(0, MULTI, 0, M_LP12, 104, 10, 0), V_KT(0, 50), V_FLT(1, MULTI, 0, M_HP12, 60, 10, 0),
    V_(fltmix.balance, 54),
    V_ENV(0, 0, 96, 50, 84), V_ENV(1, 80, 104, 90, 90),
    V_LFO(0, L_SINE, 12), V_LFO(2, L_SLEW, 30),
    V_RT(0, MODWHEEL, NONE, OSC1_SCAN, 40), V_RT(1, MODWHEEL, NONE, BALANCE, 20), V_RT(2, LFO1, NONE, OSC2_SCAN, 20),
    V_RT(3, KEYBOARD, NONE, PAN, 16), V_RT(4, LFO3, NONE, PAN, 14), V_RT(5, VELOCITY, NONE, F1_CUTOFF, 12),
    V_AMP(36, SOFT, 30, 80), V_(analog, 16),
    V_END};

/* The KITS: DUAL OSC (was STACK until the PERF TEST, 2026-11-19: two filters and envelopes a hit were too dear) with
 * OSC1 one PCM hit and OSC2 another, both untracked (KEY TRACK 64: each at its own pitch on every key), the key
 * choosing between them: four KEYBOARD > MIX routes from a balance of 73 cross from OSC1 alone at E2 (40) to OSC2 alone
 * at D#5 (75), so a sequence lane rooted on E2 plays OSC1 on '0' and OSC2 on 'z', and the keys play OSC1 low, OSC2
 * high. */
#define V_KIT V_(osc[0].key_track, 64), V_(osc[1].key_track, 64), V_(oscmix.balance, 73), \
    V_RT(0, KEYBOARD, NONE, OSC_MIX, 58), V_RT(1, KEYBOARD, NONE, OSC_MIX, 58), V_RT(2, KEYBOARD, NONE, OSC_MIX, 58), \
    V_RT(3, KEYBOARD, NONE, OSC_MIX, 58)

/* ---- 33 STONE KIT: the barrow builders' drum. A, the PCM LOG DRUM two octaves down (~65 Hz: the kick, low keys); B,
 * the PCM WOOD BLOCK two octaves up (~1 kHz: the clicks, high keys; the hits are tuned waves, they ring at the pitch
 * they are played); TUBE. WHEEL: both swap to their other hits (SCAN). */
static const uint8_t FV_STONEKIT[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_LOGDRUM, 20, -2), V_OSC(1, W_WOODBLOCK, 20, 2), V_KIT,
    V_FLT(0, MULTI, 20, M_LP12, 108, 10, 0),
    V_ENV(0, 0, 68, 0, 56),
    V_RT(4, MODWHEEL, NONE, OSC1_SCAN, 50), V_RT(5, MODWHEEL, NONE, OSC2_SCAN, 50), V_RT(6, VELOCITY, NONE, F1_CUTOFF, 16),
    V_AMP(24, TUBE, 16, 24),
    V_END};

/* ---- 34 FLINT KIT: stone on stone, and the rain. A, the PCM MENHIR (a struck standing stone: the snare's place, low
 * keys); B, the PCM RAINSTICK cut short (the shaker, high keys). WHEEL: both swap to their other
 * hits (SCAN: the rainstick's pour). */
static const uint8_t FV_FLINTKIT[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_MENHIR, 20, 0), V_OSC(1, W_RAINSTICK, 10, 0), V_KIT,
    V_FLT(0, MULTI, 10, M_LP12, 108, 10, 0),
    V_ENV(0, 0, 60, 0, 50),
    V_RT(4, MODWHEEL, NONE, OSC1_SCAN, 50), V_RT(5, MODWHEEL, NONE, OSC2_SCAN, 50), V_RT(6, VELOCITY, NONE, F1_CUTOFF, 16),
    V_AMP(16, TUBE, 20, 30),
    V_END};
#undef V_KIT

/* each patch slot: its patch and its voice mode, glide and UNISON DETUNE (a performance's layer takes them) */
typedef struct {
    const char *name;
    const uint8_t *set;                  /* (offset, value) pairs over engine_defaults, V_END ends */
    uint8_t vmode, porta, unison;        /* voice mode, glide (PORTA_* << 7 | time), UNISON DETUNE */
} FactoryVoice;
#define FV_GLIDE(mode, t) ((uint8_t)(PORTA_##mode << 7 | (t)))
#define FV_ROW(name, set, vm, gl, un) {name, set, VMODE_##vm, gl, un}
static const FactoryVoice FV_TAB[FP_N] = {
 FV_ROW("LUNAR STANDSTILL", FV_LUNAR,     POLY_REUSE,  0,                         0),
 FV_ROW("MOON GLASS",       FV_MOONGLASS, POLY_REUSE,  0,                         0),
 FV_ROW("HEARTH CIRCLE",    FV_HEARTH,    POLY_REUSE,  0,                         0),
 FV_ROW("BELTANE STRINGS",  FV_BELTANE,   POLY_REUSE,  0,                         0),
 FV_ROW("AUROCHS HORN",     FV_AUROCHS,   POLY_REUSE,  0,                         0),
 FV_ROW("AMBER KEYS",       FV_AMBER,     POLY_RESET,  0,                         0),
 FV_ROW("OGHAM LEAD",       FV_OGHAM,     MONO_LEGATO, FV_GLIDE(CONST_TIME, 30),  0),
 FV_ROW("DOLMEN BASS",      FV_DOLMEN,    MONO_LEGATO, FV_GLIDE(LEGATO_TIED, 16), 0),
 FV_ROW("SKYFIRE SYNC",     FV_SKYFIRE,   MONO_LEGATO, FV_GLIDE(CONST_TIME, 20),  0),
 FV_ROW("SKARA BRAE ORGAN", FV_SKARA,     POLY_REUSE,  0,                         0),
 FV_ROW("ORRERY PULSE",     FV_ORRERY,    MONO_RETRIG, 0,                         0),
 FV_ROW("CALANAIS SEQ",     FV_CALSEQ,    POLY_REUSE,  0,                         0),
 FV_ROW("SKY DISC CHOIR",   FV_SKYDISC,   POLY_REUSE,  0,                         0),
 FV_ROW("STONE FLUTE",      FV_FLUTE,     POLY_REUSE,  0,                         0),
 FV_ROW("TUMULUS DRONE",    FV_TUMULUS,   POLY_REUSE,  0,                         0),
 FV_ROW("LONG MEG BASS",    FV_LONGMEG,   MONO_RETRIG, 0,                         0),
 FV_ROW("FLINT ACID",       FV_ACID,      MONO_LEGATO, FV_GLIDE(LEGATO_TIED, 14), 0),
 FV_ROW("RITUAL STAB",      FV_STAB,      POLY_RESET,  0,                         0),
 FV_ROW("MONOLITH BASS",    FV_MONOLITH,  MONO_RETRIG, 0,                         0),
 FV_ROW("CAIRN KICK",       FV_KICK,      MONO_RETRIG, 0,                         0),
 FV_ROW("FLINT HAT",        FV_HAT,       POLY_RESET,  0,                         0),
 FV_ROW("TRISKELE",         FV_TRISKELE,  POLY_REUSE,  0,                         0),
 FV_ROW("DUB BARROW",       FV_DUBBARROW, MONO_LEGATO, FV_GLIDE(LEGATO_TIED, 20), 0),
 FV_ROW("HENGE SKANK",      FV_SKANK,     POLY_RESET,  0,                         0),
 FV_ROW("SPIRIT SIREN",     FV_SIREN,     MONO_RETRIG, 0,                         0),
 FV_ROW("BOG REED",         FV_REED,      POLY_REUSE,  0,                         0),
 FV_ROW("SPIRIT ZAP",       FV_ZAP,       POLY_RESET,  0,                         0),
 FV_ROW("AURORA ALTAR",     FV_AURORA,    POLY_REUSE,  0,                         0),
 FV_ROW("FROST RUNES",      FV_FROST,     POLY_REUSE,  0,                         0),
 FV_ROW("HALO OF BRODGAR",  FV_BRODGAR,   POLY_REUSE,  0,                         0),
 FV_ROW("PASSAGE TOMB",     FV_PASSAGE,   POLY_REUSE,  0,                         0),
 FV_ROW("SINGING STONES",   FV_SINGING,   POLY_REUSE,  0,                         0),
 FV_ROW("RAINMAKER",        FV_RAIN,      POLY_REUSE,  0,                         0),
 FV_ROW("STONE KIT",        FV_STONEKIT,  POLY_RESET,  0,                         0),
 FV_ROW("FLINT KIT",        FV_FLINTKIT,  POLY_RESET,  0,                         0),
};
#undef FV_ROW
enum {   /* the patch slots, by name (the performances name them) */
    FPS_LUNAR, FPS_MOONGLASS, FPS_HEARTH, FPS_BELTANE, FPS_AUROCHS, FPS_AMBER, FPS_OGHAM, FPS_DOLMEN, FPS_SKYFIRE, FPS_SKARA, FPS_ORRERY,
    FPS_CALSEQ, FPS_SKYDISC, FPS_FLUTE, FPS_TUMULUS, FPS_LONGMEG, FPS_ACID, FPS_STAB, FPS_MONOLITH, FPS_KICK, FPS_HAT, FPS_TRISKELE,
    FPS_DUBBARROW, FPS_SKANK, FPS_SIREN, FPS_REED, FPS_ZAP, FPS_AURORA, FPS_FROST, FPS_BRODGAR, FPS_PASSAGE, FPS_SINGING, FPS_RAIN, FPS_STONEKIT,
    FPS_FLINTKIT, FPS_COUNT
};
PS_STATIC_ASSERT(FPS_COUNT == FP_N, "a name for every factory patch");

/* The performances: layer 1's patch, the mode, layer 2's patch (DUAL / SPLIT; SINGL: unheard) and its voice mode
 * (FP_VM_PATCH: the patch's own), the tempo, the effects (performance-level: reverb, the delay in sixteenths at the
 * tempo, its feedback, PING PONG + DELAY HP, TAPE DEGRADE), the MIX page (LOOP TONE 64 none, DLY > REV, REV > DLY) and
 * the TUNING and its root (dsp/tuning.h). DUAL: `apart` keeps the layers' MIDI channels apart, so the keys play the
 * selected layer (layer 1 at power-on) and the arp and each SEQ lane play the layer they name; otherwise the keys play
 * both. An ARP-led DUAL performance is always apart (test_preset_store checks it). */
#define FP_VM_PATCH 0xFFu
typedef struct {
    const char *name;
    uint8_t patch, pmode, l2, vm2, apart, bpm;
    uint8_t rtype, rdec, rdmp;
    uint8_t d16, dfb, ping, degrade;
    uint8_t tone, dly_rev, rev_dly;
    uint8_t tuning, troot;
} FactoryPerf;
#define FV_PP(ping, hp) ((uint8_t)((hp) << 1 | (ping)))   /* delay_pingpong: PING PONG and DELAY HP */
#define FP_ROW(name, p1, pm, p2, vm2, ap, bpm, rt, rd, rm, d16, df, pp, dg, tone, dr, rd2, tu, tr) \
    {name, FPS_##p1, PERF_##pm, FPS_##p2, vm2, ap, bpm, REVTYPE_##rt, rd, rm, d16, df, pp, dg, tone, dr, rd2, TUNING_##tu, tr}
#define FP_MONO VMODE_MONO_RETRIG
static const FactoryPerf FP_TAB[FP_NPERF] = {
 /*     name                L1         mode         L2         L2 mode      ap bpm  reverb    dec dmp  dly fb  pp            deg  tone d>r r>d tuning */
 FP_ROW("LUNAR STANDSTILL", LUNAR,     DUAL_SYNTH,  MOONGLASS, FP_MONO,     1,  92, MOD,      116, 50,  3, 50, FV_PP(1, 16),  30,   64,  0,  0, EQUAL, 0),
    /* the hero, at power-on. Keys: the pad (layer 1). PLAY: the arp runs the MOON GLASS bells (layer 2, one voice, so
     * the pad keeps seven) up and down two octaves of minor-ninth chords, Dm9 - Bbm9 - Gm9 - Am9, two bars each; the
     * sequence holds the pad's root under each chord and rides its MOD WHEEL lane (the overtones
     * climb, the phaser quickens) and the bells' (they clang) */
 FP_ROW("HEARTH CIRCLE",    HEARTH,    SINGL_SYNTH, BELTANE,   FP_VM_PATCH, 0,  84, MOD,      100, 60,  3, 34, FV_PP(1, 20),  30,   64,  0,  0, EQUAL, 0),
    /* warm vintage: PLAY strums open quartal chords a bar each, the root below them */
 FP_ROW("OGHAM",            DOLMEN,    SPLIT_SYNTH, OGHAM,     FP_VM_PATCH, 0, 108, DOLMEN,    80, 60,  3, 40, FV_PP(1, 20),  40,   64,  0,  0, EQUAL, 0),
    /* DOLMEN BASS under C4, OGHAM LEAD from C4: PLAY an A Dorian bassline (below the split), its cutoff swept by a lane */
 FP_ROW("CALANAIS",         ORRERY,    SPLIT_SYNTH, SKYDISC,   FP_VM_PATCH, 0, 116, TUMULUS,  116, 60,  3, 56, FV_PP(1, 20),  40,   64,  0,  0, EQUAL, 0),
    /* Berlin school: ORRERY PULSE under C4 (the sequence, octave-jumping sixteenths in E Phrygian, a four-bar cutoff
     * sweep), SKY DISC CHOIR from C4 for the hands */
 FP_ROW("NEBRA SKY DISC",   SKYDISC,   DUAL_SYNTH,  ORRERY,    FP_MONO,     1, 112, TUMULUS,  112, 60,  3, 50, FV_PP(1, 20),  40,   64,  0,  0, EQUAL, 0),
    /* Berlin school: the arp runs ORRERY PULSE (layer 2) up two octaves of minor triads, Em - Cm - Dm - Em, the choir
     * (layer 1, the keys) holds the root under them, a lane sweeps the pulse's cutoff */
 FP_ROW("ORRERY",           LONGMEG,   DUAL_SYNTH,  CALSEQ,    FP_VM_PATCH, 1, 120, TUMULUS,  100, 60,  3, 46, FV_PP(1, 20),  40,   64,  0,  0, EQUAL, 0),
    /* Berlin school in polymeter: LONG MEG BASS (layer 1) in eighths over 16 steps, CALANAIS SEQ (layer 2) swinging
     * back and forth over 12, the stars' wheel lane over 7: the wheels come round together every few bars */
 FP_ROW("GRIMES GRAVES",    ACID,      SINGL_SYNTH, MONOLITH,  FP_VM_PATCH, 0, 128, DOLMEN,    60, 70,  3, 40, FV_PP(1, 30),  40,   64,  0,  0, EQUAL, 0),
    /* hypnotic techno: FLINT ACID in A Phrygian, accents digging into the filter, slides, a wheel lane wandering at
     * random over 7 steps against the 16 */
 FP_ROW("ROLLRIGHT",        MONOLITH,  DUAL_SYNTH,  STONEKIT,  FP_VM_PATCH, 1, 128, TOMB,      40, 70,  3, 30, FV_PP(1, 30),  36,   64,  0,  0, EQUAL, 0),
    /* techno: STONE KIT (layer 2) beating the log drum four to the floor and knocking wood between, MONOLITH BASS
     * rolling on the off-beats (layer 1, the keys), its filter swelling over four bars */
 FP_ROW("TRISKELE",         TRISKELE,  DUAL_SYNTH,  FLINTKIT,  FP_VM_PATCH, 1, 124, MOD,       90, 50,  3, 60, FV_PP(1, 30),  40,   64,  0,  0, EQUAL, 0),
    /* hypnotic: TRISKELE's minor-pentatonic sixteenths over 16 steps, its ice cracking on a lane of 7, its dynamics on
     * a lane of 5; FLINT KIT (layer 2): the rainstick shaking sixteenths, the menhir struck on two and four */
 FP_ROW("DEEP CHAMBER",     MONOLITH,  DUAL_SYNTH,  STAB,      FP_VM_PATCH, 1, 118, TUMULUS,  110, 70,  3, 70, FV_PP(1, 20),  50,  44, 40,  0, EQUAL, 0),
    /* dub techno: RITUAL STAB minor sevenths on the off-beats (the arp, layer 2) into a dark, long echo that feeds
     * the cavern, a lane riding the stab's filter; MONOLITH BASS follows the roots */
 FP_ROW("DUB OFFERING",     DUBBARROW, DUAL_SYNTH,  SKANK,     FP_VM_PATCH, 1,  72, SPRING,    84, 50,  3, 76, FV_PP(1, 34),  60,  44, 30,  0, EQUAL, 0),
    /* dub: the arp skanks HENGE SKANK chords on the off-beats (layer 2), DUB BARROW follows the chords, the echoes
     * near feedback, each repeat darker, spilling into the spring */
 FP_ROW("MELODICA RITE",    REED,      DUAL_SYNTH,  DUBBARROW, FP_VM_PATCH, 1,  70, SPRING,    84, 50,  3, 70, FV_PP(1, 34),  60,  50, 30,  0, EQUAL, 0),
    /* dub: BOG REED (layer 1, the keys) plays a falling D minor melody, DUB BARROW (layer 2) the one-drop bassline */
 FP_ROW("SIGNAL FIRE",      SIREN,     DUAL_SYNTH,  ZAP,       FP_VM_PATCH, 1,  76, SPRING,    80, 50,  3, 80, FV_PP(1, 30),  60,  50, 30,  0, EQUAL, 0),
    /* dub: PLAY holds the SPIRIT SIREN (layer 1) while a wheel lane winds it up for two bars of every four; SPIRIT ZAP
     * (layer 2) fires into the echoes */
 FP_ROW("AURORA ALTAR",     AURORA,    SINGL_SYNTH, FROST,     FP_VM_PATCH, 0,  80, CELESTIAL,120, 50,  3, 50, FV_PP(1, 16),  30,   64,  0,  0, EQUAL, 0),
    /* 90s: major-ninth chords planing a bar each, Fmaj9 - Dbmaj9 - Ebmaj9 - Cmaj9, strummed */
 FP_ROW("FROST RUNES",      FROST,     SINGL_SYNTH, AURORA,    FP_VM_PATCH, 0,  96, MOD,      112, 50,  3, 46, FV_PP(1, 16),  30,   64,  0,  0, EQUAL, 0),
    /* 90s: the bell-pad arpeggiated in sixteenths over open sus chords */
 FP_ROW("SINGING STONES",   SINGING,   SINGL_SYNTH, PASSAGE,   FP_VM_PATCH, 0,  66, TUMULUS,  120, 60,  4, 40, FV_PP(1, 16),  30,   64,  0,  0, PYTHAGOREAN, 2),
    /* 90s / ritual: chords of open fifths strummed down, in pure Pythagorean fifths on D, the stones answering */
 FP_ROW("OVERTONE RITE",    PASSAGE,   SINGL_SYNTH, SINGING,   FP_VM_PATCH, 0,  60, TUMULUS,  124, 60,  4, 50, FV_PP(1, 16),  30,   64,  0,  0, OVERTONE, 2),
    /* ritual: the OVERTONE tuning on D (every key a harmonic of D: the throat singer's scale), a slow melody swinging
     * back and forth, the WIND HARP in D minor answering, a lane blooming it */
 FP_ROW("BELTANE",          BELTANE,   SINGL_SYNTH, HEARTH,    FP_VM_PATCH, 0,  72, TUMULUS,  118, 70,  3, 40, FV_PP(1, 16),  40,   64,  0,  0, EQUAL, 0),
    /* warm vintage: the string machine in slow block chords */
 FP_ROW("SARSEN BEAT",      STONEKIT,  DUAL_SYNTH,  FLINTKIT,  FP_VM_PATCH, 1, 124, DOLMEN,    70, 60,  3, 40, FV_PP(1, 30),  40,   64,  0,  0, EQUAL, 0),
    /* hypnotic techno of stone and wood alone: STONE KIT (layer 1, the keys) the log drum four to the floor and the
     * wood knocking between, FLINT KIT (layer 2) the rain shaking and the menhir struck, its hits swapped by a wheel
     * lane over 7, the accents over 5 */
 FP_ROW("RAINMAKER",        RAIN,      SINGL_SYNTH, PASSAGE,   FP_VM_PATCH, 0,  84, TUMULUS,  116, 50,  3, 44, FV_PP(1, 16),  30,   64,  0,  0, EQUAL, 0),
    /* ritual: RAINMAKER's knocks and chimes walking up and down open chords, the rain swelling under them */
};
#undef FP_ROW
#undef FP_MONO

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
 * One sequence and one arp pattern for each performance. A performance is led by one of them, so PLAY never runs two
 * tunes over each other:
 *  - an ARP-led one (FA_TAB's maq 1) plays its chord progression, and its sequence plays under it on TRANSPOSE MAQ,
 *    each note moved to each chord's root (written for the first chord, an octave under it unless its lane says);
 *  - a SEQ-led one plays its sequence; its arp pattern stays empty (arp_link 127), so its arp slot is only in the ARP
 *    bank, for the ARP page.
 * Each lane: its event, its target (a NOTE or a controller lane: the layer, SEQ_TGT_L1 / L2 / BOTH; GATE, DYNAMICS,
 * RATCHET, PROB_*: the lanes it shapes, SEQ_TGT_LANE*), its own step length, playback mode and loop (lanes of
 * different lengths turn against each other: polymeter), and its 16 values:
 *  NOTE: '0'..'9' then 'a'..'z' = 0..35 semitones over the lane's root, '-' a TIE, '.' a rest (the root, muted);
 *        every note in the lane's SCALE (test_preset_store checks it);
 *  others: '0'..'9' = 0..127. */
typedef struct {
    uint8_t ev, tgt;                     /* SEQEV_*, its target */
    uint8_t root;                        /* NOTE: the lane's MIDI base (0: the sequence's) */
    uint8_t pre;                         /* clock ticks a step (0: the sequence's) */
    uint8_t mode, len, scale;            /* SEQ_MODE_*, loop length 1..16 (0: 16), NOTE: SEQSCALE_* on the sequence's sroot */
    const char *v;
} FsLane;
typedef struct {
    uint8_t root;                        /* the NOTE lanes' MIDI base (0, ARP-led: the first chord's root, an octave down) */
    uint8_t prescale;                    /* clock ticks a step (0, ARP-led: two steps an arp step) */
    uint8_t sroot;                       /* the scales' root, 0..11 */
    uint8_t stack[2];                    /* lane 1: semitones added over each note it strikes (a chord; 0: none) */
    FsLane lane[SEQ_NLANE];
} FactorySeq;
#define FL_(ev, tgt, root, pre, mode, len, sc, v) {SEQEV_##ev, tgt, root, pre, SEQ_MODE_##mode, len, SEQSCALE_##sc, v}
#define FL_NOTE(tgt, root, pre, mode, len, sc, v) FL_(NOTE, SEQ_TGT_##tgt, root, pre, mode, len, sc, v)
#define FL_CC(ev, tgt, pre, mode, len, v) FL_(ev, SEQ_TGT_##tgt, 0, pre, mode, len, OFF, v)   /* MODWHEEL etc. on a layer */
#define FL_SHAPE(ev, mask, pre, mode, len, v) FL_(ev, mask, 0, pre, mode, len, OFF, v)        /* GATE etc. on lanes */
#define L1 SEQ_TGT_LANE1
#define L2 SEQ_TGT_LANE2
#define FS_V9 "9999999999999999"
#define FS_V6 "6666666666666666"

/* arp_chords.c's genres used here (test_preset_store checks the names) */
enum { FG_5TH = 15, FG_MIN = 17, FG_MIN7 = 19, FG_MAJ9 = 20, FG_MIN9 = 21, FG_CINE_2 = 33, FG_CINE_5 = 36,
       FG_CINE_6 = 37 };
#define FA_T ARP_CHORD_TIE
/* chord keys */
enum { FA_F2 = 41, FA_G2 = 43, FA_AB2 = 44, FA_A2 = 45, FA_BB2 = 46, FA_B2 = 47, FA_C3 = 48, FA_DB3 = 49, FA_D3 = 50,
       FA_EB3 = 51, FA_E3 = 52 };
#define FA_NSTEP 8u                      /* the factory patterns' steps (9..16 left empty) */
#define FA_RATE_DIV 0xFFu                /* a step's RATE: the one whose rhythm fills the step (arp_rate_of_div) */
typedef struct {
    uint8_t genre, div, rate, style, oct, gate;
    uint8_t maq;                         /* 1: TRANSPOSE MAQ, and the performance is ARP-led */
    uint8_t root[FA_NSTEP];              /* chord keys, FA_T a TIE */
    const char *var;                     /* 8 steps: 'a' + the RHYTHM variation (8.3: a = 01A, j = 05B _ o _ o the
                                          * off-beats, q = 09A 1 1 1 _, y = ALL every cell); HOLD: b strum up, c down */
    const char *acc;                     /* 8 steps: '1' accented */
} FactoryArp;
/* a progression of four chords, each tied over the next step */
#define FA_P(a, b, c, d) {a, FA_T, b, FA_T, c, FA_T, d, FA_T}
#define FA_ARP(g, div, rate, st, oct, gate, maq, roots, var, acc) \
    {FG_##g, ARPDIV_##div, rate, ARPSTYLE_##st, oct, gate, maq, roots, var, acc}
#define FA_8(a, b, c, d) {a, a, b, b, c, c, d, d}
#define FA_R(a, b, c, d, e, f, g, h) {a, b, c, d, e, f, g, h}
#define FA_NOACC "00000000"
#define FA_NONE FA_ARP(MIN, 1_4, FA_RATE_DIV, UP, 0, 96, 0, FA_R(FA_A2, FA_A2, FA_A2, FA_A2, FA_A2, FA_A2, FA_A2, FA_A2), \
                       "aaaaaaaa", FA_NOACC)   /* a SEQ-led performance's arp slot (for the ARP page alone) */

static const FactoryArp FA_TAB[FP_NPERF] = {
 /* LUNAR STANDSTILL: a bar a step, its sixteenths every cell, up and down two octaves */
 FA_ARP(MIN9,   1,   ARPRATE_1_16, UPDN, 1,  60, 1, FA_P(FA_D3, FA_BB2, FA_G2, FA_A2), "yyyyyyyy", "10101010"),
 /* HEARTH CIRCLE: held chords strummed up quickly, a bar each */
 FA_ARP(CINE_5, 1_2, ARPRATE_1_64, HOLD,  0, 127, 1, FA_P(FA_F2, FA_A2, FA_C3, FA_G2), "bbbbbbbb", FA_NOACC),
 FA_NONE,                                                                                                /* OGHAM */
 FA_NONE,                                                                                                /* CALANAIS */
 /* NEBRA SKY DISC: a bar a step, sixteenths up two octaves */
 FA_ARP(MIN,    1,   ARPRATE_1_16, UP,   1,  50, 1, FA_R(FA_E3, FA_T, FA_C3, FA_T, FA_D3, FA_T, FA_E3, FA_T), "yyyyyyyy",
        "10001000"),
 FA_NONE,                                                                                                /* ORRERY */
 FA_NONE,                                                                                                /* GRIMES GRAVES */
 FA_NONE,                                                                                                /* ROLLRIGHT */
 FA_NONE,                                                                                                /* TRISKELE */
 /* DEEP CHAMBER: minor sevenths on the off-beats, Am7 for six bars, Gm7 for two */
 FA_ARP(MIN7,   1_2, FA_RATE_DIV, BLOCK, 0,  30, 1, FA_R(FA_A2, FA_A2, FA_A2, FA_A2, FA_A2, FA_A2, FA_G2, FA_G2), "jjjjjjjj",
        FA_NOACC),
 /* DUB OFFERING: the off-beat skank, Dm - Gm - Dm - Am */
 FA_ARP(MIN,    1_2, FA_RATE_DIV, BLOCK, 0,  40, 1, FA_8(FA_D3, FA_G2, FA_D3, FA_A2), "jjjjjjjj", FA_NOACC),
 FA_NONE,                                                                                                /* MELODICA RITE */
 FA_NONE,                                                                                                /* SIGNAL FIRE */
 /* AURORA ALTAR: major ninths planing, strummed up, a bar each */
 FA_ARP(MAJ9,   1_2, ARPRATE_1_32, HOLD,  0, 127, 1, FA_P(FA_F2, FA_DB3, FA_EB3, FA_C3), "bbbbbbbb", FA_NOACC),
 /* FROST RUNES: sixteenths up two octaves of sus chords, a bar each */
 FA_ARP(CINE_2, 1_2, ARPRATE_1_16, UP,   1,  60, 1, FA_P(FA_A2, FA_F2, FA_D3, FA_E3), "yyyyyyyy", "10001000"),
 /* SINGING STONES: open fifths strummed down, a bar each */
 FA_ARP(5TH,    1_2, ARPRATE_1_32, HOLD,  1, 127, 1, FA_P(FA_D3, FA_BB2, FA_F2, FA_A2), "cccccccc", FA_NOACC),
 FA_NONE,                                                                                                /* OVERTONE RITE */
 /* BELTANE: block chords, a bar each */
 FA_ARP(CINE_6, 1_2, FA_RATE_DIV, BLOCK, 0, 127, 1, FA_P(FA_D3, FA_BB2, FA_F2, FA_C3), "aaaaaaaa", FA_NOACC),
 FA_NONE,                                                                                                /* SARSEN BEAT */
 /* RAINMAKER: eighths up and down two octaves of open chords, a bar each */
 FA_ARP(CINE_2, 1_2, ARPRATE_1_8, UPDN,  1,  90, 1, FA_P(FA_D3, FA_BB2, FA_G2, FA_A2), "yyyyyyyy", "10001000"),
};

#define FS_SEQ(root, pre, sroot, l1, l2, l3, l4) {root, pre, sroot, {0, 0}, {l1, l2, l3, l4}}
#define FS_ARPSEQ(...) {0, 0, 0, {0, 0}, {__VA_ARGS__}}   /* ARP-led: under the chords (four lanes) */
/* an ARP-led one's usual shaping lanes: GATE full, DYNAMICS a little under, no ratchet */
#define FS_UNDER_LANES FL_SHAPE(GATE, L1, 0, FORWARD, 0, FS_V9), FL_SHAPE(DYNAMICS, L1, 0, FORWARD, 0, FS_V6), \
    FL_SHAPE(RATCHET, L1, 0, FORWARD, 0, "0000000000000000")
static const FactorySeq FS_TAB[FP_NPERF] = {
 /* LUNAR STANDSTILL: the pad's drone, D3 on each chord (a strike every two bars, quarter steps), its wheel
  * lane over four bars, the bells' over four */
 {0, 24, 0, {0, 0}, {
   FL_NOTE(L1, 0, 0, FORWARD, 0, OFF, "c-------c-------"),
   FL_CC(MODWHEEL, L1, 24, FORWARD, 0, "0123456789876543"),
   FL_SHAPE(GATE, L1, 24, FORWARD, 0, FS_V9),
   FL_CC(MODWHEEL, L2, 24, FORWARD, 0, "0000224466442200")}},
 /* HEARTH CIRCLE: the root under each chord (F2, not an octave down: the pad plays it) */
 FS_ARPSEQ(FL_NOTE(L1, 41, 0, FORWARD, 0, OFF, "0---0---0---0---"), FS_UNDER_LANES),
 /* OGHAM: A Dorian, below C4 (the split), the wheel over four bars */
 FS_SEQ(45, 6, 9,
   FL_NOTE(L1, 0, 0, FORWARD, 0, DORIAN, "0.0c0.7.0.a.c.53"),
   FL_SHAPE(GATE, L1, 0, FORWARD, 0, "5353535353535353"),
   FL_SHAPE(DYNAMICS, L1, 0, FORWARD, 0, "9575957595759575"),
   FL_CC(MODWHEEL, L1, 24, FORWARD, 0, "0123456789876543")),
 /* CALANAIS: E Phrygian, octave jumps, below C4 */
 FS_SEQ(40, 6, 4,
   FL_NOTE(L1, 0, 0, FORWARD, 0, PHRYGIAN, "0c3c7c3c0c5c8c7c"),
   FL_CC(MODWHEEL, L1, 24, FORWARD, 0, "0123456789876543"),
   FL_SHAPE(DYNAMICS, L1, 0, FORWARD, 0, "9575957595759575"),
   FL_SHAPE(GATE, L1, 0, FORWARD, 0, "4444444444444444")),
 /* NEBRA SKY DISC: the choir's root on each chord (quarter steps, a strike every two bars), the pulse's
  * cutoff over four bars */
 {0, 24, 0, {0, 0}, {
   FL_NOTE(L1, 0, 0, FORWARD, 0, OFF, "c-------c-------"),
   FL_CC(MODWHEEL, L2, 24, FORWARD, 0, "0123456789987654"),
   FL_SHAPE(GATE, L1, 24, FORWARD, 0, FS_V9),
   FL_SHAPE(DYNAMICS, L1, 24, FORWARD, 0, FS_V6)}},
 /* ORRERY: A minor; the bass in eighths over 16, the stars over 12 swinging, their wheel over 7, the bass's over 16
  * quarters */
 FS_SEQ(45, 6, 9,
   FL_NOTE(L1, 0, 12, FORWARD, 0, MINOR, "0.0c07.30a0.7c.5"),
   FL_NOTE(L2, 57, 0, PENDULUM1, 12, MINOR, "037a0c7a3c70"),
   FL_CC(MODWHEEL, L2, 6, FORWARD, 7, "2468642"),
   FL_CC(MODWHEEL, L1, 24, FORWARD, 0, "0123456789876543")),
 /* GRIMES GRAVES: A Phrygian, slides on the ties, accents, the wheel wandering over 7 eighths */
 FS_SEQ(45, 6, 9,
   FL_NOTE(L1, 0, 0, FORWARD, 0, PHRYGIAN, "00c-0.310a0c-713"),
   FL_SHAPE(DYNAMICS, L1, 0, FORWARD, 0, "9595559595559559"),
   FL_CC(MODWHEEL, L1, 12, RANDOM, 7, "1357924"),
   FL_SHAPE(GATE, L1, 0, FORWARD, 0, "5595555555595555")),   /* (full before a tie: the slide) */
 /* ROLLRIGHT: the log drum four to the floor and the wood between (layer 2), the bass on the off-beats (layer 1), its
  * wheel over four bars, chance drop-outs on the bass */
 FS_SEQ(45, 6, 9,
   FL_NOTE(L1, 0, 0, FORWARD, 0, MINOR, "..0...0...0..c0."),
   FL_NOTE(L2, 40, 0, FORWARD, 0, OFF, "0..z0.z.0..z0.zz"),   /* (STONE KIT: '0' the log drum, 'z' the wood) */
   FL_CC(MODWHEEL, L1, 24, FORWARD, 0, "0123456776543210"),
   FL_SHAPE(PROB_MUTE, L1, 0, FORWARD, 0, "0000000000300300")),
 /* TRISKELE: A minor pentatonic over 16, the rain and the menhir (layer 2), the wheel over 7, the dynamics over 5 */
 FS_SEQ(57, 6, 9,
   FL_NOTE(L1, 0, 0, FORWARD, 0, MIN_PENTA, "0.7.3.a.0.c.7.5."),
   FL_NOTE(L2, 40, 0, FORWARD, 0, OFF, "zzz.0zzzzz.z0zzz"),   /* (FLINT KIT: '0' the menhir, 'z' the rain) */
   FL_CC(MODWHEEL, L1, 6, FORWARD, 7, "2468642"),
   FL_SHAPE(DYNAMICS, L1, 6, FORWARD, 5, "95759")),
 /* DEEP CHAMBER: the bass on the chords' roots, the stab's filter ridden by a lane */
 FS_ARPSEQ(FL_NOTE(L1, 45, 0, FORWARD, 0, OFF, "0..0..0.0..0..0."),
   FL_SHAPE(GATE, L1, 0, FORWARD, 0, "6666666666666666"),
   FL_SHAPE(DYNAMICS, L1, 0, FORWARD, 0, "9666966696669666"),
   FL_CC(MODWHEEL, L2, 12, FORWARD, 0, "2468864213579753")),
 /* DUB OFFERING: the dub bass under the skank */
 FS_ARPSEQ(FL_NOTE(L1, 50, 0, FORWARD, 0, OFF, "0-.70-.70-.70-a7"), FL_SHAPE(GATE, L1, 0, FORWARD, 0, FS_V9),
   FL_SHAPE(DYNAMICS, L1, 0, FORWARD, 0, FS_V6), FL_SHAPE(RATCHET, L1, 0, FORWARD, 0, "0000000000000000")),
 /* MELODICA RITE: D minor; the melody in eighths (layer 1), the one-drop bass (layer 2) */
 FS_SEQ(62, 12, 2,
   FL_NOTE(L1, 0, 0, FORWARD, 0, MINOR, "c-a-7---5-3-0---"),
   FL_NOTE(L2, 50, 0, FORWARD, 0, MINOR, "0-.0..7-0-.0a.7."),
   FL_SHAPE(GATE, L1 | L2, 0, FORWARD, 0, FS_V9),
   FL_SHAPE(DYNAMICS, L1 | L2, 0, FORWARD, 0, "8686868686868686")),
 /* SIGNAL FIRE: the siren held (layer 1, quarter steps), wound up by its wheel for two bars of four; the zaps (layer
  * 2) */
 FS_SEQ(69, 24, 0,
   FL_NOTE(L1, 0, 0, FORWARD, 0, OFF, "0---------------"),
   FL_CC(MODWHEEL, L1, 24, FORWARD, 0, "0000000035799753"),
   FL_NOTE(L2, 60, 12, FORWARD, 0, OFF, "....0.......7..."),
   FL_SHAPE(GATE, L1, 24, FORWARD, 0, FS_V9)),   /* (full: the tie holds it) */
 /* AURORA ALTAR, FROST RUNES, SINGING STONES: the root under each chord */
 FS_ARPSEQ(FL_NOTE(L1, 41, 0, FORWARD, 0, OFF, "0---0---0---0---"), FS_UNDER_LANES),
 FS_ARPSEQ(FL_NOTE(L1, 45, 0, FORWARD, 0, OFF, "0---0---0---0---"), FS_UNDER_LANES),
 FS_ARPSEQ(FL_NOTE(L1, 0, 0, FORWARD, 0, OFF, "0---0---0---0---"), FS_UNDER_LANES),
 /* OVERTONE RITE: D, quarter steps swinging back and forth, the harp blooming on the wheel over four bars */
 FS_SEQ(50, 24, 2,
   FL_NOTE(L1, 0, 0, PENDULUM1, 0, OFF, "0-7-c-a-5---3---"),
   FL_CC(MODWHEEL, L1, 24, FORWARD, 0, "0123456789987654"),
   FL_SHAPE(GATE, L1, 0, FORWARD, 0, FS_V9),
   FL_SHAPE(DYNAMICS, L1, 0, FORWARD, 0, "8686868686868686")),
 /* BELTANE: the root under each chord */
 FS_ARPSEQ(FL_NOTE(L1, 0, 0, FORWARD, 0, OFF, "0---0---0---0---"), FS_UNDER_LANES),
 /* SARSEN BEAT: the log drum on the beats, the wood between (layer 1); the rain and the menhir (layer 2), their hits
  * swapped by the wheel over 7, the accents over 5 */
 FS_SEQ(40, 6, 0,
   FL_NOTE(L1, 0, 0, FORWARD, 0, OFF, "0...0..z0...0.zz"),
   FL_NOTE(L2, 0, 0, FORWARD, 0, OFF, "zzzz0zzzzzz.0zzz"),
   FL_CC(MODWHEEL, L2, 6, FORWARD, 7, "0000590"),
   FL_SHAPE(DYNAMICS, L1 | L2, 6, FORWARD, 5, "95768")),
 /* RAINMAKER: the root under each chord */
 FS_ARPSEQ(FL_NOTE(L1, 0, 0, FORWARD, 0, OFF, "0---0---0---0---"), FS_UNDER_LANES),
};
#undef FS_SEQ
#undef FS_ARPSEQ
#undef FS_UNDER_LANES
#undef FL_
#undef FL_NOTE
#undef FL_CC
#undef FL_SHAPE
#undef L1
#undef L2
#undef FA_ARP
#undef FA_NONE
#undef FA_P
#undef FA_8
#undef FA_R

static uint8_t fs_digit(char c) { return (uint8_t)((c - '0') * 127 / 9); }
static uint8_t fs_semis(char c) { return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10); }

/* sequence slot s: each lane from FS_TAB; an ARP-led one's NOTE lanes sit an octave under the first chord (unless a
 * lane names its root) and step two steps an arp step (unless the sequence names its step) */
static void factory_seq(uint32_t s, SequencerLane lanes[SEQ_NLANE])
{
    static const uint8_t DIV_TICKS[ARPDIV_COUNT] = {192, 96, 48, 24, 16, 12, 8, 6, 4, 3, 2};
    const FactorySeq *f = &FS_TAB[s];
    const FactoryArp *a = &FA_TAB[s];
    uint8_t root = f->root, p = f->prescale, l, i, k;
    char c;
    if (a->maq && !root)
        root = (uint8_t)(a->root[0] - 12u);
    if (a->maq && !p) {
        uint8_t t = (uint8_t)(DIV_TICKS[a->div] / 2u);
        p = t > SEQ_PRESCALE_MAX ? (uint8_t)SEQ_PRESCALE_MAX : t;
    }
    seq_defaults(lanes);
    for (l = 0; l < SEQ_NLANE; l++) {
        const FsLane *fl = &f->lane[l];
        SequencerLane *ln = &lanes[l];
        uint8_t r = fl->root ? fl->root : root;
        ln->playback_mode = fl->mode;
        ln->prescale = fl->pre ? fl->pre : p;
        ln->loop_start = 0u;
        ln->loop_end = (uint8_t)((fl->len ? fl->len : SEQ_NSTEP) - 1u);
        ln->scale = SEQSCALE_OFF;
        ln->scale_root = 0u;
        ln->event_type = SEQEV_NOTE;                     /* (so seq_lane_set_event sets the new type's first target) */
        seq_lane_set_event(ln, fl->ev);
        ln->target_mask = fl->tgt;
        for (i = 0; i < SEQ_NSTEP; i++) {
            SequencerStep *st = &ln->steps[i];
            st->note_buffer[0] = r;
            st->note_buffer[1] = st->note_buffer[2] = st->note_buffer[3] = SEQ_NOTE_NULL;
            st->flags = 0u;
            st->value = 0u;
            c = i < (fl->len ? fl->len : SEQ_NSTEP) ? fl->v[i] : '.';
            if (fl->ev == SEQEV_NOTE) {
                if (c == '-')
                    st->note_buffer[0] = SEQ_NOTE_TIE;
                else if (c == '.')
                    st->flags = SEQ_FLAG_MUTE;
                else {
                    st->note_buffer[0] = (uint8_t)(r + fs_semis(c));
                    if (l == 0u)
                        for (k = 0; k < 2u; k++)
                            if (f->stack[k])
                                st->note_buffer[1u + k] = (uint8_t)(st->note_buffer[0] + f->stack[k]);
                }
            } else {
                st->value = c == '.' ? 0u : fs_digit(c);
            }
        }
        if (fl->ev == SEQEV_NOTE) {
            ln->scale = fl->scale;
            ln->scale_root = f->sroot;
        }
    }
}

/* arp slot s */
static void factory_arp(uint32_t s, ArpState *st)
{
    const FactoryArp *f = &FA_TAB[s];
    uint8_t i;
    arp_defaults(st);
    for (i = 0; i < FA_NSTEP; i++) {
        ArpChordStep *c = &st->steps[i];
        c->chord_root = f->root[i];
        c->rate = f->rate == FA_RATE_DIV ? arp_rate_of_div(f->div) : f->rate;   /* its rhythm across the step */
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

/* performance slot s, whole: its patches (layer 1, and layer 2 for DUAL / SPLIT), each layer's voice mode and glide,
 * its effects, MIX page, tempo and tuning, its sequence, and its arp pattern if it leads (factory_seq above): 0, or -1
 * for a slot past the factory bank */
static int factory_perf(uint32_t s, WholePerformance *w)
{
    const FactoryPerf *fp;
    const FactoryVoice *f, *f2;
    EngineState base;
    MasterPerformancePreset *p = &w->perf;
    uint32_t d;
    if (s >= FP_NPERF)
        return -1;
    fp = &FP_TAB[s];
    f = &FV_TAB[fp->patch];
    f2 = fp->pmode == PERF_SINGL_SYNTH ? f : &FV_TAB[fp->l2];
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
    p->voice_mode[1] = fp->pmode != PERF_SINGL_SYNTH && fp->vm2 != FP_VM_PATCH ? fp->vm2 : f2->vmode;
    p->porta_mode[0] = (uint8_t)(f->porta >> 7);
    p->porta_time[0] = (uint8_t)(f->porta & 127u);
    p->porta_mode[1] = (uint8_t)(f2->porta >> 7);
    p->porta_time[1] = (uint8_t)(f2->porta & 127u);
    p->unison_detune[0] = f->unison;
    p->unison_detune[1] = f2->unison;
    if (fp->pmode == PERF_DUAL_SYNTH && !fp->apart)
        p->midi_ch[1] = p->midi_ch[0];                    /* the keys play both layers at once, whichever is selected */
    p->split_note = 60u;                                  /* SPLIT: layer 1 below C4, layer 2 from C4 */
    p->reverb_type = fp->rtype; p->reverb_decay = fp->rdec; p->reverb_damp = fp->rdmp;
    d = (uint32_t)fp->d16 * 661500u / (256u * fp->bpm);   /* sixteenths at the tempo, in DELAY TIME's steps (fx.c: 256 samples) */
    p->delay_time = (uint8_t)(d > VAL_MAX ? VAL_MAX : d);
    p->delay_feedback = fp->dfb; p->delay_pingpong = fp->ping;
    p->delay_degrade = fp->degrade;
    p->delay_sync = (uint8_t)(fp->tuning << 1);           /* DELAY SYNC off; TUNING in the high bits */
    p->delay_type = (uint8_t)(fp->troot << 1);            /* TUNING's ROOT in the high bits */
    p->mix[1] = fp->tone;                                 /* the MIX page (dsp/fx.h FX_L_*): BALANCE, LOOP TONE, */
    p->mix[2] = fp->dly_rev;                              /* DLY > REV, */
    p->mix[3] = fp->rev_dly;                              /* REV > DLY */
    p->int_tempo = (uint8_t)(fp->bpm - MIDI_CLOCK_BPM_BASE);
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
PS_STATIC_ASSERT(FP_N <= PATCH_SLOTS && FP_NPERF <= BANK_SLOTS, "the factory patches and performances fit");
