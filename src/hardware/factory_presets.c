/* The factory bank "RITES OF STONE" (2026-10-21): 32 patches (slots 0..31) built on everything the engine has now (PCM
 * waves, SH-2 / MS-20 / FORMANT filters with DRIVE and KEY TRACK, ANALOG, STRIKE attacks, the mod slots, envelope
 * retrigger, synced LFOs, DUAL and SPLIT performances), with 32 sequences, 32 arp patterns and 32 whole performances
 * (every slot of those banks). Performance 0, NEWGRANGE DAWN, is the one the FM1 plays at power-on (syn_boot_perf).
 * It replaced "STONE & FIRE" (2026-10-16, in git history: the firmware's flash had no room for both). A unity-build
 * fragment like preset_store_fm1.c. synth_main_fm1.c includes it after syn_used / syn_perf_name and the store, and calls
 * factory_install() BEFORE ps_load(): the factory slots are marked used, then anything the user saved over them in
 * flash replaces them. A performance (factory_perf, made when it loads) holds the patch, its effects (delay and reverb
 * are shared, performance-level), its tempo, its sequence and its arp pattern, so loading a PERF brings the whole
 * piece; loading only the PATCH keeps the current effects.
 *
 * Wavebanks: 0 Menhir 1 Barrow 2 Trilithon 3 Ley Line 4 Solstice 5 Moonstone 6 Star Chart 7 Druid Chant
 *            8 Overtone Rite 9 Votive Choir 10 Bone Flute 11 Erosion 12 Frost Shatter 13 Dust 14 Hearth
 *            15 Hollow Reeds; 32.. the attacks as one-shot waves, 64.. the PCM waves (dsp/pcm_osc.h)
 * Knob units: time 0..127 (32 = 34 ms, 64 = 0.37 s, 80 = 1.1 s, 96 = 3 s, 112 = 8 s), cutoff ~13.4 knob steps an octave
 * (85 = 1.6 kHz), LFO rate 0.05 Hz * 800^(k/127), LFO shape 0 sine 21 tri 43 saw 64 square 85 step 106 slew.
 * Levels balanced on the host (a held chord of each patch within a few dB); not yet heard on the device. */
#include <stddef.h>                 /* offsetof: the patches' settings are byte offsets into EngineState */
#define FP_OCT(k) ((uint8_t)((k) == 0 ? 64 : 18 * ((k) + 3) + 9))
#define FP_BIP(v) ((uint8_t)((v) + 64))
#define FP_N 32u                         /* RITES OF STONE, slots 0..31 */

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

/* ------------------------------------------------------------------------ RITES OF STONE --- *
 * 2026-10-21: the factory bank, slots 0..31. Each patch is engine_defaults plus the list of (byte offset, value)
 * pairs below, so a patch states every setting it changes and nothing else: about 100 bytes of flash a patch, where
 * code setting the same fields would take several times that. The values are the stored bytes (bipolar ones 64 = 0:
 * FP_BIP; octaves FP_OCT; fine tune bytes by osc_fine_cents' square law, the cents in the comment).
 * Knob units: see the top of this file. A pitch mod amount a is a^2 * 4800 / 16129 cents (10 = 30 c, 13 = 50 c);
 * scan, mix, xmod, spread and pan amounts are knob units at a full source; a filter's MOD AMT a moves the cutoff a
 * knob steps at a full source (13.4 an octave); a mod slot's amount 63 is unity.
 * The FM1's own keys play at the HOME velocity knob and send no aftertouch, so the expressive control on the panel is
 * the MOD WHEEL (HOME knob 3): every patch gives it a job, named in its comment.
 * CPU: each architecture's voice cap holds (PERF TEST rows exist for every arch with PCM, SH-2 and the reverbs); the
 * SH-2 stays out of DUAL FILTER, STACK and BINAURAL, whose SH-2 rows run over budget (docs/fm1-measure-checklist.md). */
#define V_(f, v) (uint8_t)offsetof(EngineState, f), (uint8_t)(v)
#define V_END 0xFFu
#define V_OSC(i, w, sc, oc) V_(osc[i].wavebank, w), V_(osc[i].scan, sc), V_(osc[i].octave, FP_OCT(oc))
#define V_FLT(i, fl, drv, mo, cut, res, amt) V_(filter[i].flavour, FLT_##fl), V_(filter[i].drive, drv), \
    V_(filter[i].morph, mo), V_(filter[i].cutoff, cut), V_(filter[i].resonance, res), V_(filter[i].mod_amt, FP_BIP(amt))
#define V_ENV(i, a, d, s, r) V_(env[i].attack, a), V_(env[i].decay, d), V_(env[i].sustain, s), V_(env[i].release, r)
#define V_LFO(i, sh, rt) V_(lfo[i].shape, sh), V_(lfo[i].rate, rt)
#define V_SLOT(i, a, fn, b, amt) V_(mod[i].src_a, MSRC_##a), V_(mod[i].func, MFN_##fn), V_(mod[i].src_b, MSRC_##b), \
    V_(mod[i].amount, FP_BIP(amt))
#define V_AMP(drv, model, ds, rs) V_(amp.drive, drv), V_(amp.drive_model, DRIVE_##model), V_(amp.delay_send, ds), \
    V_(amp.reverb_send, rs)
#define V_ATK(smp, lvl, flt, dec) V_(oscmix.atk_sample, smp), V_(oscmix.atk_level, lvl), V_(oscmix.atk_filter, flt), \
    V_(oscmix.atk_decay, dec)
#define V_KT(i, kt) V_(filter_key_track[i], kt)
/* the waves (dsp/pcm_osc.h): banks 0..15, the attacks as one-shots from 32, the PCM store's waves from 64
 * (gen_pcm_waves.py WAVES order) */
enum { W_MENHIR, W_BARROW, W_TRILITHON, W_LEY, W_SOLSTICE, W_MOONSTONE, W_STARS, W_DRUID, W_OVERTONE, W_VOTIVE,
       W_BONEFLUTE, W_EROSION, W_FROST, W_DUST, W_HEARTH, W_REEDS };
enum { W_WOOD = 64, W_CLAY, W_SHELL, W_STRINGS, W_CHOIR, W_HORNS, W_CHOIRWOOD };
/* attacks.h's sounds, by number (attack_names) */
enum { A_BONE_CHIFF, A_REED_CHIFF, A_HORN_BLAT, A_FLINT, A_STONE_KNOCK, A_MENHIR_THUD, A_LITHOPHONE, A_PEBBLES,
       A_WOOD_BLOCK, A_LOG_DRUM, A_BONE_CLACK, A_ANTLER_RASP, A_FRAME_DRUM, A_SEED_RATTLE, A_CRACKLE, A_CAVE_CLAP,
       A_BOW_PLUCK, A_MOUTH_BOW, A_POT_DRUM, A_CONCH };
#define W_ATK(a) (32 + (a))
enum { L_SINE = 0, L_TRI = 21, L_SAW = 43, L_SQUARE = 64, L_STEP = 85, L_SLEW = 106 };

/* ---- 0 NEWGRANGE DAWN: the winter-solstice sunrise creeping down the passage of Newgrange. STACK: A, a Moonstone
 * FM bell whose strike is bright and darkens as it rings (scan follows ENV1); B, the PCM CHOIR swelling in behind it,
 * its vowel drifting ooh..aah on a slow LFO3 of its own in every voice; ENV3 opens both filters over three seconds;
 * LFO3 walks each voice through the stereo field, LFO1 breathes the bell against the choir. Into CELESTIAL and a
 * ping-pong tape echo. WHEEL: vibrato, and the choir's filter opens. The power-on sound. */
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

/* ---- 1 CALANAIS MOON: the lunar standstill, when the moon skims the hills behind the stones of Calanais every 18.6
 * years. SPREAD over the PCM STRINGS ensemble, the four playheads breathing apart on a 10-second LFO1, through a warm
 * SH-2 with a little drive; the bass of the orchestra to the left, the top to the right (KEYBOARD pan). WHEEL: the
 * strings bow harder (SCAN soft -> bright), with vibrato. */
static const uint8_t FV_CALANAIS[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_STRINGS, 52, 0), V_(osc[0].scan_mod_src, MSRC_MODSLOT2), V_(osc[0].scan_mod_amt, 96),
    V_(osc[0].pitch_mod_src, MSRC_MODSLOT1), V_(osc[0].pitch_mod_amt, 7),
    V_(oscmix.spread, 66), V_(oscmix.spread_mod_src, MSRC_LFO1), V_(oscmix.spread_mod_amt, 12),
    V_(oscmix.sub_level, 56),
    V_FLT(0, SH2, 70, 0, 92, 24, 16), V_KT(0, 50),
    V_ENV(0, 80, 96, 116, 98), V_ENV(1, 88, 100, 64, 98), V_ENV(2, 0, 32, 64, 32),
    V_LFO(0, L_SINE, 12), V_LFO(1, L_TRI, 84), V_LFO(2, L_SINE, 30),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63), V_SLOT(1, MODWHEEL, ADD, NONE, 63),
    V_(amp.pan_mod_src, MSRC_KEYBOARD), V_(amp.pan_mod_depth, 26),
    V_AMP(0, SOFT, 18, 80), V_(analog, 44),
    V_END};

/* ---- 2 AVEBURY CIRCLE: the great henge with a village inside it. A warm, drifting polysynth pad: DUAL OSC, the
 * Hearth bank against a fresh Erosion saw tuned 7 cents up, the SUB playing OSC1's own wave an octave down (CURRENT),
 * a MULTI 24 dB low-pass, LFO1 rocking the balance; ANALOG high, so every voice drifts on its own, into the
 * chorused MOD reverb and a worn tape. WHEEL: FM between the oscillators, a glassy edge. */
static const uint8_t FV_AVEBURY[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_HEARTH, 40, 0), V_OSC(1, W_EROSION, 10, 0), V_(osc[1].fine, 70 /* +6 c */),
    V_(oscmix.xmod_type, XMOD_FM), V_(oscmix.xmod_mod_src, MSRC_MODWHEEL), V_(oscmix.xmod_mod_amt, 34),
    V_(oscmix.mix_mod_src, MSRC_LFO1), V_(oscmix.mix_mod_amt, 14),
    V_(oscmix.sub_level, 24), V_(oscmix.sub_waveform, SUBWAVE_CURRENT),
    V_FLT(0, MULTI, 10, 0, 70, 24, 22), V_KT(0, 64),
    V_ENV(0, 60, 90, 108, 90), V_ENV(1, 70, 92, 40, 90),
    V_LFO(0, L_TRI, 34),
    V_AMP(0, SOFT, 22, 58), V_(analog, 72),
    V_END};

/* ---- 3 HYPOGEUM ORACLE: the Oracle Chamber of Hal Saflieni, Malta, carved so a man's low voice booms through the
 * whole underground temple. STRIKE: a MENHIR THUD at the start of each note, then the Druid Chant bank an octave down
 * through a FORMANT pair parked low (no key track: the chamber's own resonances), the vowel wandering on a slewed
 * random LFO3, a brown breath under it; mono and legato. WHEEL: the vowel opens, with vibrato. */
static const uint8_t FV_HYPOGEUM[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_DRUID, 40, -1), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 60),
    V_(osc[0].pitch_mod_src, MSRC_MODSLOT2), V_(osc[0].pitch_mod_amt, 9),
    V_ATK(A_MENHIR_THUD, 110, 0, 127), V_(oscmix.atk_track, ATKTRK_FIXED),
    V_(oscmix.sub_level, 60), V_(oscmix.noise_level, 12), V_(oscmix.noise_colour, NOISE_BROWN),
    V_FLT(0, FORMANT, 50, 50, 52, 70, 0), V_(filter[0].morph_mod_src, MSRC_LFO1), V_(filter[0].morph_mod_amt, FP_BIP(20)),
    V_ENV(0, 70, 96, 120, 96),
    V_LFO(0, L_SINE, 18), V_LFO(1, L_TRI, 86), V_LFO(2, L_SLEW, 36),
    V_SLOT(0, LFO3, ADD, MODWHEEL, 40), V_SLOT(1, LFO2, MUL, MODWHEEL, 63),
    V_AMP(10, SOFT, 10, 66), V_(analog, 50),
    V_END};

/* ---- 4 SKARA BRAE FIRE: the hearth at the centre of a stone house on Orkney. BINAURAL Hearth, the right ear 5 Hz
 * above the left (the alpha-theta border), its LFO phase and cutoff offset drifting on LFO1; the fire's crackle is
 * the noise level jumping to a new random value at every LFO2 crossing: MODSLOT1 = LFO3 sampled on LFO2. WHEEL: the
 * ears' phase offsets swing (a slow swirl). */
static const uint8_t FV_SKARA[] = {
    V_(arch, VARCH_BINAURAL),
    V_OSC(0, W_HEARTH, 60, 0), V_(osc[0].scan_mod_src, MSRC_LFO1), V_(osc[0].scan_mod_amt, 16),
    V_(oscmix.bin_beat, 40), V_(oscmix.bin_scan_offset, FP_BIP(10)),
    V_(oscmix.noise_level, 14), V_(oscmix.noise_colour, 110), V_(oscmix.noise_mod_src, MSRC_MODSLOT1),
    V_(oscmix.noise_mod_amt, FP_BIP(30)),
    V_FLT(0, MULTI, 10, 36, 76, 20, 14), V_KT(0, 40),
    V_(fltmix.bin_width, 104), V_(fltmix.bin_cutoff_offset, FP_BIP(8)),
    V_(fltmix.bin_phase_mod_src, MSRC_MODWHEEL), V_(fltmix.bin_phase_mod_amt, FP_BIP(40)),
    V_(fltmix.bin_cutoff_mod_src, MSRC_LFO1), V_(fltmix.bin_cutoff_mod_amt, FP_BIP(10)),
    V_ENV(0, 72, 96, 110, 96), V_ENV(1, 80, 96, 60, 90),
    V_LFO(0, L_SINE, 16), V_LFO(1, L_SQUARE, 104), V_LFO(2, L_STEP, 112),
    V_SLOT(0, LFO2, SAMPLE_B_ON_A, LFO3, 63),
    V_AMP(0, SOFT, 12, 60), V_(analog, 36),
    V_END};

/* ---- 5 GOBEKLI PILLARS: the carved T-pillars of Gobekli Tepe, foxes and vultures in the stone. DUAL FILTER over
 * the Erosion bank: a driven MS-20 low-pass near its growl into a MULTI NOTCH that LFO2 sweeps in each voice (a
 * phaser), LFO1 easing the pair from serial toward parallel. WHEEL: the MS-20 morphs low-pass -> band-pass. */
static const uint8_t FV_GOBEKLI[] = {
    V_(arch, VARCH_DUAL_FILTER),
    V_OSC(0, W_EROSION, 30, 0), V_(osc[0].scan_mod_src, MSRC_LFO1), V_(osc[0].scan_mod_amt, 30),
    V_(oscmix.sub_level, 16), V_(oscmix.sub_waveform, SUBWAVE_SQUARE),
    V_FLT(0, MS20, 0, 0, 54, 56, 20), V_KT(0, 50),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(50)),
    V_FLT(1, MULTI, 0, 127, 80, 50, 16), V_(filter[1].mod_src, MSRC_LFO2),
    V_(fltmix.routing, 12), V_(fltmix.balance, 84), V_(fltmix.routing_mod_src, MSRC_LFO1),
    V_(fltmix.routing_mod_amt, FP_BIP(30)), V_(fltmix.balance_mod_amt, FP_BIP(-16)),
    V_ENV(0, 50, 90, 100, 88), V_ENV(1, 64, 88, 50, 88), V_ENV(2, 90, 96, 64, 90),
    V_LFO(0, L_TRI, 12), V_LFO(1, L_SINE, 34),
    V_AMP(0, SOFT, 18, 50), V_(analog, 40),
    V_END};

/* ---- 6 LUNAR STANDSTILL: the moon at its far turning point. BINAURAL Star Chart, the prime harmonics surfacing one
 * by one as ENV3 scans up over eight seconds, the ears a slow 1 Hz apart, open and glassy (MULTI 6 dB). WHEEL: the
 * ears' phase offsets swing. */
static const uint8_t FV_LUNAR[] = {
    V_(arch, VARCH_BINAURAL),
    V_OSC(0, W_STARS, 8, 0), V_(osc[0].scan_mod_amt, 92),
    V_(oscmix.bin_beat, 8), V_(oscmix.bin_scan_offset, FP_BIP(12)),
    V_FLT(0, MULTI, 0, 54, 84, 12, 0),
    V_(fltmix.bin_width, 127), V_(fltmix.bin_phase_mod_src, MSRC_MODWHEEL), V_(fltmix.bin_phase_mod_amt, FP_BIP(40)),
    V_ENV(0, 84, 100, 120, 104), V_ENV(2, 112, 112, 110, 104),
    V_LFO(0, L_SINE, 20),
    V_AMP(0, SOFT, 22, 56), V_(analog, 20),
    V_END};

/* ---- 7 MAESHOWE BEAM: the setting midwinter sun shining down the passage of Maeshowe onto the back wall. SPREAD
 * Solstice (a CZ resonance narrowing to a ringing beam as it scans), ENV3 drawing the beam in over three seconds and
 * letting it settle, through an MS-20 band-pass that LFO1 moves very slowly. WHEEL: the playheads spread wider. */
static const uint8_t FV_MAESHOWE[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_SOLSTICE, 20, 0), V_(osc[0].scan_mod_amt, 80),
    V_(oscmix.spread, 40), V_(oscmix.spread_mod_src, MSRC_MODWHEEL), V_(oscmix.spread_mod_amt, 40),
    V_(oscmix.sub_level, 10),
    V_FLT(0, MS20, 0, 64, 80, 48, 14), V_(filter[0].mod_src, MSRC_LFO1), V_KT(0, 64),
    V_ENV(0, 88, 100, 110, 100), V_ENV(2, 96, 104, 40, 96),
    V_LFO(0, L_SINE, 10),
    V_AMP(0, SOFT, 24, 72), V_(analog, 24),
    V_END};

/* ---- 8 DAWN PROCESSION: voices along the avenue to the henge. STACK: A, the PCM CHOIR on its open aah, drifting on
 * LFO3; B, the Votive Choir bank an octave up through a FORMANT that LFO2 moves, swelling in later than A, like a
 * second choir answering; spread wide (WIDTH) into the cavern. WHEEL: vibrato. */
static const uint8_t FV_PROCESSION[] = {
    V_(arch, VARCH_STACK),
    V_OSC(0, W_CHOIR, 100, 0), V_(osc[0].scan_mod_src, MSRC_LFO3), V_(osc[0].scan_mod_amt, 30),
    V_(osc[0].pitch_mod_src, MSRC_MODSLOT1), V_(osc[0].pitch_mod_amt, 9),
    V_OSC(1, W_VOTIVE, 70, 1), V_(osc[1].fine, 59 /* -4 c */), V_(osc[1].scan_mod_src, MSRC_LFO3),
    V_(osc[1].scan_mod_amt, 30), V_(osc[1].pitch_mod_src, MSRC_MODSLOT1), V_(osc[1].pitch_mod_amt, 9),
    V_FLT(0, MULTI, 0, 36, 92, 10, 0),
    V_FLT(1, FORMANT, 0, 40, 76, 40, 0), V_(filter[1].morph_mod_src, MSRC_LFO2), V_(filter[1].morph_mod_amt, FP_BIP(24)),
    V_(fltmix.balance, 56), V_(fltmix.stack_width, 100),
    V_ENV(0, 70, 90, 110, 92), V_ENV(1, 88, 96, 116, 96),
    V_LFO(1, L_TRI, 30), V_LFO(2, L_SINE, 22),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63),
    V_AMP(0, SOFT, 14, 80), V_(analog, 20),
    V_END};

/* ---- 9 CURSUS WIND: wind down the two-mile ditch of the Stonehenge Cursus. The oscillator plays CRACKLE once (an
 * attack as a one-shot wave) and falls silent; the rest is noise through a resonant SH-2 band-pass that whistles.
 * The gusts are MODSLOT1: a random LFO3 smoothed by LAG (its pace set by ENV3, held at full), moving the cutoff, the
 * noise level and the pan; LFO2 shifts the noise colour. WHEEL: the band-pass morphs toward high-pass (a thinner,
 * colder wind). */
static const uint8_t FV_CURSUS[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_ATK(A_CRACKLE), 0, 0),
    V_(oscmix.noise_level, 90), V_(oscmix.noise_colour, 40), V_(oscmix.noise_mod_src, MSRC_MODSLOT1),
    V_(oscmix.noise_mod_amt, FP_BIP(28)), V_(oscmix.noise_col_mod_src, MSRC_LFO2),
    V_(oscmix.noise_col_mod_amt, FP_BIP(30)),
    V_FLT(0, SH2, 20, 64, 74, 78, 22), V_(filter[0].mod_src, MSRC_MODSLOT1), V_KT(0, 90),
    V_(filter[0].morph_mod_src, MSRC_MODWHEEL), V_(filter[0].morph_mod_amt, FP_BIP(40)),
    V_ENV(0, 90, 100, 120, 100), V_ENV(2, 0, 32, 127, 100),
    V_LFO(1, L_SINE, 18), V_LFO(2, L_STEP, 40),
    V_SLOT(0, LFO3, LAG, ENV3, 63),
    V_(amp.pan_mod_src, MSRC_MODSLOT1), V_(amp.pan_mod_depth, 50),
    V_AMP(0, SOFT, 30, 70), V_(analog, 16),
    V_END};

/* ---- 10 LITHOPHONE: ringing stones, struck. STRIKE: the LITHOPHONE attack over a Moonstone bell an octave up, the
 * attack partly through the filter, the bell's index falling with ENV3; higher notes ring shorter (KEY > TIME), the
 * filter follows the keys. WHEEL: the strike brightens (ENV3 + wheel into the scan). */
static const uint8_t FV_LITHOPHONE[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_MOONSTONE, 10, 1), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 60),
    V_ATK(A_LITHOPHONE, 90, 30, 104),
    V_FLT(0, MULTI, 0, 18, 84, 18, 30), V_KT(0, 100),
    V_ENV(0, 0, 76, 0, 76), V_(env[0].key_to_time, FP_BIP(-20)), V_ENV(1, 0, 56, 0, 60), V_ENV(2, 0, 60, 0, 60),
    V_SLOT(0, ENV3, ADD, MODWHEEL, 40),
    V_(amp.pan_mod_depth, 30),
    V_AMP(0, SOFT, 26, 54),
    V_END};

/* ---- 11 RING OF BRODGAR: sixty stones in a ring on Orkney, tuned. The PCM CLAY CHIMES cloud through two MULTI
 * band-passes in parallel (DUAL FILTER), both tracking the keys fully, the second an octave and a fifth above, so
 * the rattling clay sings each note. WHEEL: the cloud thickens (SCAN A -> B). */
static const uint8_t FV_BRODGAR[] = {
    V_(arch, VARCH_DUAL_FILTER),
    V_OSC(0, W_CLAY, 20, 0), V_(osc[0].scan_mod_src, MSRC_MODWHEEL), V_(osc[0].scan_mod_amt, 100),
    V_FLT(0, MULTI, 10, 73, 50, 86, 0), V_KT(0, 127),
    V_FLT(1, MULTI, 10, 73, 71, 82, 0), V_KT(1, 127),
    V_(fltmix.routing, 127), V_(fltmix.balance, 50), V_(fltmix.balance_mod_src, MSRC_LFO1),
    V_(fltmix.balance_mod_amt, FP_BIP(14)),
    V_ENV(0, 0, 90, 60, 96),
    V_LFO(0, L_SINE, 24), V_LFO(1, L_SINE, 30),
    V_(amp.pan_mod_src, MSRC_LFO2), V_(amp.pan_mod_depth, 40),
    V_AMP(0, SOFT, 40, 70),
    V_END};

/* ---- 12 BEAKER BELLS: bronze was coming. DUAL OSC: a sine (Menhir) RING-modulated by a Moonstone bell a fifth and
 * an octave up, the ring fading as ENV3 falls, so a clangorous strike settles to a purer tone. WHEEL: more ring. */
static const uint8_t FV_BEAKER[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_MENHIR, 0, 0), V_OSC(1, W_MOONSTONE, 30, 1), V_(osc[1].fine, 127 /* +700 c */),
    V_(oscmix.xmod_type, XMOD_RING), V_(oscmix.xmod_amt, 70), V_(oscmix.xmod_mod_src, MSRC_MODSLOT1),
    V_(oscmix.xmod_mod_amt, 40),
    V_FLT(0, MULTI, 0, 36, 100, 10, 10), V_KT(0, 80),
    V_ENV(0, 0, 84, 0, 84), V_(env[0].key_to_time, FP_BIP(-16)), V_ENV(1, 0, 70, 0, 70), V_ENV(2, 0, 64, 0, 64),
    V_SLOT(0, ENV3, ADD, MODWHEEL, 40),
    V_(amp.pan_mod_depth, 30),
    V_AMP(0, SOFT, 30, 56), V_(analog, 12),
    V_END};

/* ---- 13 FLINT KNAPPING: striking blades from a flint core. STRIKE: the FLINT attack a fifth up, bent in from above,
 * over Frost Shatter whose fold flares with ENV3, through an MS-20 high-pass with drive and the amp's HARD clip.
 * WHEEL: the flake bends further (BEND MOD). */
static const uint8_t FV_FLINT[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_FROST, 30, 0), V_(osc[0].scan_mod_amt, 70),
    V_ATK(A_FLINT, 80, 0, 92), V_(oscmix.atk_tune, FP_BIP(7)), V_(oscmix.atk_bend, FP_BIP(8)),
    V_(oscmix.atk_bend_mod_src, MSRC_MODWHEEL), V_(oscmix.atk_bend_mod_amt, FP_BIP(24)),
    V_FLT(0, MS20, 40, 127, 50, 50, 30), V_KT(0, 64),
    V_ENV(0, 0, 60, 0, 50), V_ENV(1, 0, 50, 0, 50), V_ENV(2, 0, 50, 0, 50),
    V_(amp.pan_mod_depth, 24),
    V_AMP(24, HARD, 34, 40),
    V_END};

/* ---- 14 MUSICAL BOW: the hunting bow plucked against the mouth, the oldest string. STRIKE: BOW PLUCK into the
 * filter with the Barrow bank, bent up from below, a resonant MULTI 24 dB snapping open with ENV2; higher notes
 * shorter, a quick repeat shortens the release (AUTO DECAY). WHEEL: the pluck bends further. */
static const uint8_t FV_BOW[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_BARROW, 50, 0),
    V_ATK(A_BOW_PLUCK, 96, 100, 110), V_(oscmix.atk_bend, FP_BIP(-6)),
    V_(oscmix.atk_bend_mod_src, MSRC_MODWHEEL), V_(oscmix.atk_bend_mod_amt, FP_BIP(-20)),
    V_FLT(0, MULTI, 30, 0, 64, 42, 40), V_KT(0, 64),
    V_ENV(0, 0, 80, 0, 70), V_(env[0].key_to_time, FP_BIP(-24)), V_(env[0].auto_decay, 1),
    V_ENV(1, 0, 58, 0, 50),
    V_(amp.pan_mod_depth, 20),
    V_AMP(0, SOFT, 20, 46), V_(analog, 30),
    V_END};

/* ---- 15 BONE FLUTE RITE: a flute of swan bone. STRIKE: the tongued BONE CHIFF over the Hollow Reeds bank an octave
 * up, a breath of noise beside it; mono and legato with a short glide, and a vibrato that waits half a second, then
 * blooms (LFO2 DELAY and FADE, restarted at each note). WHEEL: more breath, the reed brighter. */
static const uint8_t FV_BONEFLUTE[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_REEDS, 40, 1), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 40),
    V_(osc[0].pitch_mod_src, MSRC_LFO2), V_(osc[0].pitch_mod_amt, 9),
    V_ATK(A_BONE_CHIFF, 90, 40, 100),
    V_(oscmix.noise_level, 14), V_(oscmix.noise_colour, 30), V_(oscmix.noise_mod_src, MSRC_MODWHEEL),
    V_(oscmix.noise_mod_amt, FP_BIP(20)),
    V_FLT(0, MULTI, 0, 36, 90, 10, 12), V_KT(0, 64),
    V_ENV(0, 24, 80, 110, 50), V_ENV(1, 20, 60, 70, 50),
    V_LFO(1, L_SINE, 88), V_(lfo[1].delay, 60), V_(lfo[1].fade, FP_BIP(40)), V_(lfo[1].key_reset, 1),
    V_SLOT(0, ENV2, ADD, MODWHEEL, 40),
    V_AMP(0, SOFT, 26, 54), V_(analog, 20),
    V_END};

/* ---- 16 CONCH CALL: the shell trumpet summoning the clans. STRIKE: the CONCH blast over the PCM HORNS (with their
 * own HORN BLAT), ENV2 pushing them soft -> brassy, an SH-2 low-pass with drive opening with it; the pitch falls
 * into the note (ENV3 inverted). WHEEL: brassier still. */
static const uint8_t FV_CONCH[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_HORNS, 20, 0), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 90),
    V_(osc[0].pitch_mod_src, MSRC_ENV3), V_(osc[0].pitch_mod_amt, 10),
    V_ATK(A_CONCH, 70, 0, 90),
    V_FLT(0, SH2, 40, 0, 76, 20, 26), V_KT(0, 64),
    V_ENV(0, 30, 80, 100, 60), V_ENV(1, 40, 76, 50, 60), V_ENV(2, 36, 127, 127, 40), V_(env[2].invert, 1),
    V_SLOT(0, ENV2, ADD, MODWHEEL, 40),
    V_AMP(0, SOFT, 16, 56), V_(analog, 24),
    V_END};

/* ---- 17 LOG DRUM CIRCLE: hollowed trunks around the fire. STRIKE: LOG DRUM, tracked to the keys, over a Menhir
 * sine-to-body; a marimba's short decay, shorter up the keys, the filter following the keys, a little TAPE drive.
 * WHEEL: the body brightens (SCAN). */
static const uint8_t FV_LOGDRUM[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_MENHIR, 20, 0), V_(osc[0].scan_mod_src, MSRC_MODWHEEL), V_(osc[0].scan_mod_amt, 80),
    V_ATK(A_LOG_DRUM, 96, 0, 110), V_(oscmix.atk_track, ATKTRK_TRACK),
    V_FLT(0, MULTI, 0, 0, 70, 30, 36), V_KT(0, 100),
    V_ENV(0, 0, 64, 0, 60), V_(env[0].key_to_time, FP_BIP(-20)), V_ENV(1, 0, 44, 0, 44),
    V_(amp.pan_mod_depth, 34),
    V_AMP(10, TAPE, 18, 40),
    V_END};

/* ---- 18 CAIRN DRUMS: a frame drum and a clay pot drum. STRIKE: the FRAME DRUM attack, and for the oscillator the
 * POT DRUM attack as a one-shot wave, tuned by the keys (low keys deep pots, high keys small ones); a short noise
 * slap rides on ENV3; a driven MS-20 and the amp's drive for punch. WHEEL: the slap gets louder. */
static const uint8_t FV_CAIRN[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_ATK(A_POT_DRUM), 0, 0),
    V_ATK(A_FRAME_DRUM, 80, 0, 110), V_(oscmix.atk_track, ATKTRK_FIXED),
    V_(oscmix.noise_colour, 50), V_(oscmix.noise_mod_src, MSRC_MODSLOT1), V_(oscmix.noise_mod_amt, FP_BIP(50)),
    V_FLT(0, MS20, 40, 0, 96, 30, 0),
    V_ENV(0, 0, 76, 0, 70), V_ENV(2, 0, 40, 0, 40),
    V_SLOT(0, ENV3, ADD, MODWHEEL, 30),
    V_(amp.pan_mod_depth, 40),
    V_AMP(30, SOFT, 10, 34),
    V_END};

/* ---- 19 ANCESTOR PULSE: the beat under the dancing. STACK: A, the Trilithon pulse, its ENV1 retriggered on the
 * clock's sixteenths (RETRIG SYNC), through a MULTI low-pass that a clock-synced LFO1 sweeps each half note; B, the
 * PCM CHOIR+WOOD sustaining under it. Hold a chord: it pulses. WHEEL: the pulse's filter opens. */
static const uint8_t FV_PULSE[] = {
    V_(arch, VARCH_STACK),
    V_OSC(0, W_TRILITHON, 60, 0), V_OSC(1, W_CHOIRWOOD, 40, 0),
    V_(oscmix.sub_level, 20),
    V_FLT(0, MULTI, 20, 0, 78, 44, 20), V_(filter[0].mod_src, MSRC_MODSLOT1), V_KT(0, 64),
    V_FLT(1, MULTI, 0, 36, 84, 10, 0),
    V_(fltmix.balance, 60), V_(fltmix.stack_width, 90),
    V_ENV(0, 0, 50, 0, 50), V_(env[0].retrig_rate, 88), V_(env[0].retrig_sync, 1),
    V_ENV(1, 80, 90, 100, 90),
    V_LFO(0, L_SINE, 16), V_(lfo[0].clock_sync, 1),
    V_SLOT(0, LFO1, ADD, MODWHEEL, 40),
    V_AMP(20, SOFT, 34, 56), V_(analog, 20),
    V_END};

/* ---- 20 SILBURY BASS: the great mound, built in a summer. A squelching mono bass: the Barrow bank an octave down
 * through a driven, very resonant SH-2 with a snappy ENV2; tied steps slide (LEGATO TIED glide); the sub is a square.
 * WHEEL: the cutoff wobbles on LFO2. */
static const uint8_t FV_SILBURY[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_BARROW, 70, -1),
    V_(oscmix.sub_level, 60), V_(oscmix.sub_waveform, SUBWAVE_SQUARE),
    V_FLT(0, SH2, 60, 0, 40, 92, 0), V_(filter[0].mod_src, MSRC_MODSLOT1), V_KT(0, 64),
    V_ENV(0, 0, 70, 90, 30), V_ENV(1, 0, 54, 0, 30),
    V_LFO(1, L_SINE, 76),
    V_SLOT(0, ENV2, ADD, MODSLOT2, 44), V_SLOT(1, LFO2, MUL, MODWHEEL, 40),
    V_AMP(30, SOFT, 12, 8), V_(analog, 30),
    V_END};

/* ---- 21 QUOIT GROWL: a capstone grinding on its uprights. DUAL OSC an octave down, Menhir phase-modulating a
 * Trilithon pulse (PM, deepest at the note's start), into an MS-20 low-pass driven hard at its growl, then the amp's
 * TAPE saturation. WHEEL: a clock-synced eighth-note wobble on the cutoff (MODSLOT1 = LFO2 x wheel, added to ENV2). */
static const uint8_t FV_QUOIT[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_MENHIR, 60, -1), V_OSC(1, W_TRILITHON, 40, -1), V_(osc[1].fine, 58 /* -6 c */),
    V_(oscmix.balance, 50), V_(oscmix.xmod_type, XMOD_PM), V_(oscmix.xmod_amt, 40),
    V_(oscmix.xmod_mod_src, MSRC_ENV3), V_(oscmix.xmod_mod_amt, 40),
    V_(oscmix.sub_level, 10), V_(oscmix.sub_octave, SUBOCT_DOWN2),
    V_FLT(0, MS20, 50, 0, 50, 84, 28), V_(filter[0].mod_src, MSRC_MODSLOT2), V_KT(0, 64),
    V_ENV(0, 0, 80, 100, 30), V_ENV(1, 0, 64, 30, 40), V_ENV(2, 0, 60, 20, 40),
    V_LFO(1, L_SINE, 56), V_(lfo[1].clock_sync, 1), V_(lfo[1].key_reset, 1),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63), V_SLOT(1, ENV2, ADD, MODSLOT1, 40),
    V_AMP(6, TAPE, 4, 6), V_(analog, 20),
    V_END};

/* ---- 22 ANTLER PICK: red-deer antler, the tool that dug the ditches. STRIKE: the ANTLER RASP into the filter over a
 * fresh Erosion saw an octave down and a square sub, a plucked MULTI 24 dB. WHEEL: the rasp bends in from above. */
static const uint8_t FV_ANTLER[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_EROSION, 0, -1),
    V_ATK(A_ANTLER_RASP, 80, 80, 70), V_(oscmix.atk_bend_mod_src, MSRC_MODWHEEL),
    V_(oscmix.atk_bend_mod_amt, FP_BIP(24)),
    V_(oscmix.sub_level, 70), V_(oscmix.sub_waveform, SUBWAVE_SQUARE),
    V_FLT(0, MULTI, 40, 0, 44, 50, 48), V_KT(0, 64),
    V_ENV(0, 0, 70, 60, 30), V_ENV(1, 0, 48, 0, 30),
    V_AMP(20, SOFT, 10, 8), V_(analog, 40),
    V_END};

/* ---- 23 KNOWTH SPIRAL: the carved spirals of Knowth's kerbstones. A sync lead: DUAL OSC, the Barrow bank an octave
 * up hard-synced to an Erosion saw, its pitch swept by ENV3 at each note (the sync scream), the saw's vibrato waiting
 * and fading in; mono, legato, a constant-rate glide. WHEEL: a longer sweep (ENV3 + wheel). */
static const uint8_t FV_KNOWTH[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_EROSION, 10, 0), V_(osc[0].pitch_mod_src, MSRC_LFO2), V_(osc[0].pitch_mod_amt, 9),
    V_OSC(1, W_BARROW, 90, 1), V_(osc[1].sync, 1), V_(osc[1].pitch_mod_src, MSRC_MODSLOT1),
    V_(osc[1].pitch_mod_amt, 44),
    V_(oscmix.balance, 90),
    V_FLT(0, MULTI, 24, 36, 96, 30, 20), V_KT(0, 64),
    V_ENV(0, 0, 80, 110, 50), V_ENV(1, 0, 70, 60, 50), V_ENV(2, 0, 70, 30, 50),
    V_LFO(1, L_TRI, 86), V_(lfo[1].delay, 50), V_(lfo[1].fade, FP_BIP(30)), V_(lfo[1].key_reset, 1),
    V_SLOT(0, ENV3, ADD, MODWHEEL, 40),
    V_AMP(0, SOFT, 30, 30), V_(analog, 24),
    V_END};

/* ---- 24 SOLSTICE FIRE: the midsummer bonfire. SPREAD Erosion saws (a supersaw, four playheads a voice) stacked six
 * voices deep in UNISON, through a driven SH-2: twenty-four saws on one key. WHEEL: wider spread and vibrato. */
static const uint8_t FV_SOLFIRE[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_EROSION, 0, 0), V_(osc[0].pitch_mod_src, MSRC_MODSLOT1), V_(osc[0].pitch_mod_amt, 8),
    V_(oscmix.spread, 78), V_(oscmix.spread_mod_src, MSRC_MODWHEEL), V_(oscmix.spread_mod_amt, 20),
    V_FLT(0, SH2, 70, 0, 92, 30, 26), V_KT(0, 64),
    V_ENV(0, 2, 80, 110, 60), V_ENV(1, 0, 70, 60, 50),
    V_LFO(1, L_TRI, 84),
    V_SLOT(0, LFO2, MUL, MODWHEEL, 63),
    V_AMP(40, SOFT, 30, 34), V_(analog, 16),
    V_END};

/* ---- 25 OVERTONE CHANT: a throat singer at the tomb mouth. STRIKE: MOUTH BOW at the start of each note, then the
 * Overtone Rite bank an octave down, its whistling harmonic wandering on a slewed random LFO3; mono, legato, a slow
 * glide. WHEEL: sweeps the whistle up through the overtones. */
static const uint8_t FV_OVERTONE[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_OVERTONE, 30, -1), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 80),
    V_ATK(A_MOUTH_BOW, 100, 60, 110),
    V_FLT(0, MULTI, 0, 36, 100, 0, 0),
    V_ENV(0, 40, 96, 120, 80),
    V_LFO(2, L_SLEW, 40),
    V_SLOT(0, LFO3, ADD, MODWHEEL, 40),
    V_AMP(0, SOFT, 16, 64), V_(analog, 30),
    V_END};

/* ---- 26 GGANTIJA ORGAN: the giants' temple on Gozo. DUAL OSC: Ley Line (octaves and fifths, like drawbars) with
 * the Barrow bank an octave up, a key click (a noise burst on ENV3), a rotary speaker made from LFO1: tremolo
 * (AMP MOD) and pan together, into the TAPE drive. WHEEL: the rotor speeds up (LFO1 SPEED MOD). */
static const uint8_t FV_GGANTIJA[] = {
    V_(arch, VARCH_DUAL_OSC),
    V_OSC(0, W_LEY, 80, 0), V_OSC(1, W_BARROW, 0, 1),
    V_(oscmix.balance, 50),
    V_(oscmix.noise_colour, 30), V_(oscmix.noise_mod_src, MSRC_ENV3), V_(oscmix.noise_mod_amt, FP_BIP(40)),
    V_FLT(0, MULTI, 0, 36, 100, 0, 0), V_KT(0, 64),
    V_ENV(0, 4, 60, 127, 30), V_ENV(2, 0, 24, 0, 24),
    V_LFO(0, L_SINE, 58), V_(lfo[0].speed_mod_src, MSRC_MODWHEEL), V_(lfo[0].speed_mod_amt, FP_BIP(24)),
    V_(amp.amp_mod_src, MSRC_LFO1), V_(amp.amp_mod_depth, 20),
    V_(amp.pan_mod_src, MSRC_LFO1), V_(amp.pan_mod_depth, 40),
    V_AMP(10, TAPE, 0, 24), V_(analog, 16),
    V_END};

/* ---- 27 KERMARIO KEYS: the Kermario alignments, Carnac, at walking pace. An electric piano: STRIKE, a STONE KNOCK
 * an octave up for the tine over Moonstone at its lowest FM index (a Rhodes' bell), the index barking at each strike
 * (ENV3), higher notes shorter, an auto-pan on LFO1. WHEEL: the bark grows. */
static const uint8_t FV_KERMARIO[] = {
    V_(arch, VARCH_STRIKE),
    V_OSC(0, W_MOONSTONE, 6, 0), V_(osc[0].scan_mod_src, MSRC_MODSLOT1), V_(osc[0].scan_mod_amt, 40),
    V_ATK(A_STONE_KNOCK, 60, 0, 40), V_(oscmix.atk_tune, FP_BIP(12)),
    V_FLT(0, MULTI, 0, 36, 100, 0, 10), V_KT(0, 64),
    V_ENV(0, 0, 90, 20, 60), V_(env[0].key_to_time, FP_BIP(-16)), V_ENV(1, 0, 70, 30, 60), V_ENV(2, 0, 56, 0, 56),
    V_LFO(0, L_SINE, 70),
    V_SLOT(0, ENV3, ADD, MODWHEEL, 40),
    V_(amp.pan_mod_src, MSRC_LFO1), V_(amp.pan_mod_depth, 50),
    V_AMP(16, SOFT, 14, 40), V_(analog, 26),
    V_END};

/* ---- 28 WOODHENGE: the rings of great posts at Durrington Walls. The PCM WOOD CHIMES, the clatter of their
 * attack sweep, then the knocking loop through a MULTI 12 dB that LFO1 slowly opens and closes, long release. WHEEL:
 * the loop thickens (SCAN A -> B). */
static const uint8_t FV_WOODHENGE[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_WOOD, 10, 0), V_(osc[0].scan_mod_src, MSRC_MODWHEEL), V_(osc[0].scan_mod_amt, 110),
    V_FLT(0, MULTI, 0, 36, 104, 10, 14), V_(filter[0].mod_src, MSRC_LFO1),
    V_ENV(0, 0, 96, 70, 100),
    V_LFO(0, L_TRI, 26), V_LFO(1, L_SINE, 32),
    V_(amp.pan_mod_src, MSRC_LFO2), V_(amp.pan_mod_depth, 36),
    V_AMP(20, SOFT, 30, 64),
    V_END};

/* ---- 29 MENEC ALIGNMENT: the eleven rows of the Menec alignments. A SPLIT performance: below C4 this round bass (a
 * Trilithon pulse, a sine sub, a MULTI 24 dB that bumps open with ENV2, ANALOG), above it KERMARIO KEYS. WHEEL: the
 * bass's filter opens. */
static const uint8_t FV_MENEC[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_TRILITHON, 30, -1),
    V_(oscmix.sub_level, 30),
    V_FLT(0, MULTI, 10, 0, 44, 20, 0), V_(filter[0].mod_src, MSRC_MODSLOT1), V_KT(0, 64),
    V_ENV(0, 0, 80, 100, 30), V_ENV(1, 0, 60, 20, 30),
    V_SLOT(0, ENV2, ADD, MODWHEEL, 30),
    V_AMP(0, SOFT, 6, 14), V_(analog, 50),
    V_END};

/* ---- 30 SKY BURIAL: the dead laid out for the birds before the bones went into the tomb. A DUAL performance: this
 * patch, PCM SHELL CHIMES rattling through a resonant SH-2 that LFO1 sweeps from low-pass through band-pass, layered
 * with CURSUS WIND on every key. WHEEL: denser shells. */
static const uint8_t FV_SKYBURIAL[] = {
    V_(arch, VARCH_SINGLE),
    V_OSC(0, W_SHELL, 30, 0), V_(osc[0].scan_mod_src, MSRC_MODWHEEL), V_(osc[0].scan_mod_amt, 90),
    V_FLT(0, SH2, 10, 30, 90, 50, 0), V_(filter[0].morph_mod_src, MSRC_LFO1), V_(filter[0].morph_mod_amt, FP_BIP(30)),
    V_KT(0, 64),
    V_ENV(0, 30, 96, 90, 100),
    V_LFO(0, L_SINE, 20),
    V_(amp.pan_mod_src, MSRC_LFO2), V_(amp.pan_mod_depth, 40),
    V_AMP(0, SOFT, 30, 70),
    V_END};

/* ---- 31 EQUINOX: day and night in balance, the closing of the rite. SPREAD in its chord range: the four playheads
 * of the PCM CHOIR stacked in fifths (root, fifth, octave, twelfth), so one key sings a quintal chord; a warm MULTI,
 * a slow swell, the cavern. WHEEL: the choir opens (SCAN ooh -> aah) and the filter with it. */
static const uint8_t FV_EQUINOX[] = {
    V_(arch, VARCH_SPREAD),
    V_OSC(0, W_CHOIR, 30, 0), V_(osc[0].scan_mod_src, MSRC_MODWHEEL), V_(osc[0].scan_mod_amt, 80),
    V_(oscmix.spread, 113),
    V_FLT(0, MULTI, 0, 36, 84, 14, 20), V_(filter[0].mod_src, MSRC_MODWHEEL), V_KT(0, 50),
    V_ENV(0, 76, 96, 116, 100),
    V_LFO(1, L_TRI, 84),
    V_(amp.pan_mod_depth, 20),
    V_AMP(0, SOFT, 20, 80), V_(analog, 20),
    V_END};

/* each slot's patch and its performance settings */
typedef struct {
    const char *name;
    const uint8_t *set;                  /* (offset, value) pairs over engine_defaults, V_END ends */
    uint8_t vmode, porta, unison;        /* layer 1 voice mode, its glide (PORTA_* << 7 | time), UNISON DETUNE */
    uint8_t pmode, l2;                   /* PERF_*; layer 2's patch slot (DUAL / SPLIT; SINGL: the next slot) */
    uint8_t rtype, rdec, rdmp, rpre;     /* reverb */
    uint8_t dtime, dfb, dtype, ping, degrade, dsync;   /* delay; dtime set to the tempo (dotted eighths mostly) */
} FactoryVoice;
#define FV_GLIDE(mode, t) ((uint8_t)(PORTA_##mode << 7 | (t)))
#define FV_ROW(name, set, vm, gl, un, pm, l2, rt, rd, rm, rp, dt, df, dy, pp, dg, ds) \
    {name, set, VMODE_##vm, gl, un, PERF_##pm, l2, REVTYPE_##rt, rd, rm, rp, dt, df, DELAYTYPE_##dy, pp, dg, ds}
static const FactoryVoice FV_TAB[FP_N] = {
 /*      name                set            voice mode   glide                    uni  perf        l2  reverb    dec dmp pre  dt  fb  type pp deg sync */
 FV_ROW("NEWGRANGE",        FV_NEWGRANGE,  POLY_REUSE,  0,                       0,   SINGL_SYNTH, 1, CELESTIAL,112, 60, 24, 54, 48, TAPE, 1, 30, 0),
 FV_ROW("CALANAIS MOON",    FV_CALANAIS,   POLY_REUSE,  0,                       0,   SINGL_SYNTH, 2, TUMULUS,  116, 72, 30, 58, 36, BBD,  0, 36, 0),
 FV_ROW("AVEBURY",         FV_AVEBURY,    POLY_REUSE,  0,                       0,   SINGL_SYNTH, 3, MOD,       92, 50, 10, 51, 30, TAPE, 0, 70, 0),
 FV_ROW("HYPOGEUM",        FV_HYPOGEUM,   MONO_LEGATO, FV_GLIDE(CONST_TIME, 30), 0,  SINGL_SYNTH, 4, DOLMEN,   100, 70, 12, 60, 24, TAPE, 0, 36, 0),
 FV_ROW("SKARA BRAE",      FV_SKARA,      POLY_REUSE,  0,                       0,   SINGL_SYNTH, 5, TOMB,      76, 80,  8, 48, 30, BBD,  0, 36, 0),
 FV_ROW("GOBEKLI",         FV_GOBEKLI,    POLY_REUSE,  0,                       0,   SINGL_SYNTH, 6, TUMULUS,  104, 64, 20, 57, 40, TAPE, 0, 50, 0),
 FV_ROW("LUNAR",           FV_LUNAR,      POLY_REUSE,  0,                       0,   SINGL_SYNTH, 7, CELESTIAL,120, 40, 40, 55, 52, BBD,  1, 36, 0),
 FV_ROW("MAESHOWE BEAM",    FV_MAESHOWE,   POLY_REUSE,  0,                       0,   SINGL_SYNTH, 8, CELESTIAL,108, 50, 30, 52, 44, TAPE, 1, 36, 0),
 FV_ROW("PROCESSION",      FV_PROCESSION, POLY_REUSE,  0,                       0,   SINGL_SYNTH, 9, TUMULUS,  118, 70, 40, 54, 36, TAPE, 0, 36, 0),
 FV_ROW("CURSUS WIND",      FV_CURSUS,     POLY_REUSE,  0,                       0,   SINGL_SYNTH,10, MOD,      100, 60, 20, 55, 50, BBD,  1, 36, 0),
 FV_ROW("LITHOPHONE",       FV_LITHOPHONE, POLY_RESET,  0,                       0,   SINGL_SYNTH,11, TOMB,      80, 50, 10, 39, 40, TAPE, 1, 36, 0),
 FV_ROW("BRODGAR",         FV_BRODGAR,    POLY_REUSE,  0,                       0,   SINGL_SYNTH,12, CELESTIAL,110, 30, 20, 36, 56, TAPE, 1, 30, 0),
 FV_ROW("BEAKER BELLS",     FV_BEAKER,     POLY_RESET,  0,                       0,   SINGL_SYNTH,13, MOD,      100, 40, 15, 35, 44, BBD,  1, 36, 0),
 FV_ROW("FLINT",           FV_FLINT,      POLY_RESET,  0,                       0,   SINGL_SYNTH,14, SPRING,    60, 40,  4, 33, 50, BBD,  1, 36, 0),
 FV_ROW("MUSICAL BOW",      FV_BOW,        POLY_REUSE,  0,                       0,   SINGL_SYNTH,15, DOLMEN,    84, 60,  6, 40, 35, TAPE, 0, 36, 0),
 FV_ROW("BONE FLUTE",      FV_BONEFLUTE,  MONO_LEGATO, FV_GLIDE(CONST_TIME, 24), 0,  SINGL_SYNTH,16, TOMB,      90, 60, 10, 44, 40, TAPE, 0, 40, 0),
 FV_ROW("CONCH CALL",       FV_CONCH,      POLY_REUSE,  0,                       0,   SINGL_SYNTH,17, TUMULUS,  100, 70, 30, 42, 30, TAPE, 0, 36, 0),
 FV_ROW("LOG DRUM",        FV_LOGDRUM,    POLY_RESET,  0,                       0,   SINGL_SYNTH,18, DOLMEN,    70, 60,  4, 36, 30, BBD,  1, 36, 0),
 FV_ROW("CAIRN DRUMS",      FV_CAIRN,      POLY_RESET,  0,                       0,   SINGL_SYNTH,19, DOLMEN,    70, 70,  2, 36, 25, TAPE, 0, 50, 0),
 FV_ROW("ANCESTOR",        FV_PULSE,      POLY_REUSE,  0,                       0,   SINGL_SYNTH,20, MOD,       90, 50, 10, 56, 50, TAPE, 1, 36, 1),
 FV_ROW("SILBURY BASS",     FV_SILBURY,    MONO_LEGATO, FV_GLIDE(LEGATO_TIED, 20), 0, SINGL_SYNTH,21, TOMB,      40, 70,  0, 33, 30, BBD,  0, 36, 0),
 FV_ROW("QUOIT GROWL",      FV_QUOIT,      MONO_RETRIG, 0,                       0,   SINGL_SYNTH,22, DOLMEN,    50, 80,  0, 42, 20, TAPE, 0, 36, 0),
 FV_ROW("ANTLER PICK",      FV_ANTLER,     MONO_RETRIG, 0,                       0,   SINGL_SYNTH,23, TOMB,      40, 70,  0, 35, 20, BBD,  0, 36, 0),
 FV_ROW("KNOWTH SPIRAL",    FV_KNOWTH,     MONO_LEGATO, FV_GLIDE(CONST_RATE, 30), 0,  SINGL_SYNTH,24, TOMB,      70, 50,  8, 39, 45, TAPE, 1, 36, 0),
 FV_ROW("SOLSTICE FIRE",    FV_SOLFIRE,    UNISON_STACK,FV_GLIDE(CONST_TIME, 18), 44, SINGL_SYNTH,25, MOD,       80, 50, 10, 31, 40, TAPE, 1, 36, 0),
 FV_ROW("OVERTONE",        FV_OVERTONE,   MONO_LEGATO, FV_GLIDE(CONST_TIME, 40), 0,  SINGL_SYNTH,26, DOLMEN,    96, 70, 16, 60, 30, TAPE, 0, 36, 0),
 FV_ROW("GGANTIJA",        FV_GGANTIJA,   POLY_REUSE,  0,                       0,   SINGL_SYNTH,27, TOMB,      70, 60,  6, 40, 20, TAPE, 0, 36, 0),
 FV_ROW("KERMARIO KEYS",    FV_KERMARIO,   POLY_REUSE,  0,                       0,   SINGL_SYNTH,28, MOD,       70, 50,  6, 37, 30, TAPE, 1, 50, 0),
 FV_ROW("WOODHENGE",        FV_WOODHENGE,  POLY_REUSE,  0,                       0,   SINGL_SYNTH,29, CELESTIAL,104, 40, 20, 55, 50, TAPE, 1, 30, 0),
 FV_ROW("MENEC",           FV_MENEC,      POLY_REUSE,  0,                       0,   SPLIT_SYNTH,27, MOD,       80, 50,  8, 40, 30, TAPE, 1, 36, 0),
 FV_ROW("SKY BURIAL",       FV_SKYBURIAL,  POLY_REUSE,  0,                       0,   DUAL_SYNTH,  9, TUMULUS,  120, 80, 40, 58, 50, BBD,  1, 36, 0),
 FV_ROW("EQUINOX",          FV_EQUINOX,    POLY_REUSE,  0,                       0,   SINGL_SYNTH, 0, TUMULUS,  124, 70, 40, 60, 44, TAPE, 1, 30, 0),
};
#undef FV_ROW

/* patch slot s of the bank, over the defaults in base */
static void factory_voice(EngineState *e, const EngineState *base, uint32_t s)
{
    const uint8_t *p = FV_TAB[s].set;
    *e = *base;
    for (; p[0] != V_END; p += 2)
        ((uint8_t *)e)[p[0]] = p[1];
    factory_name(e->name, FV_TAB[s].name);
}
#undef V_

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
 /* ---- pads and atmospheres ---- */
 FA_ARP(TRAD_MIN_1,  1_2, STRUM_UP, 0, 127, 1, FA_P(FA_C3, FA_AB2, FA_EB3, FA_BB2), "aaaaaaaa", FA_NOACC, 72),   /* NEWGRANGE DAWN */
 FA_ARP(MIN9,        1_2, BLOCK,    0, 127, 1, FA_P(FA_C3, FA_F2, FA_C3, FA_G2),    "aaaaaaaa", FA_NOACC, 66),   /* CALANAIS MOON */
 FA_ARP(TRAD_MAJ,    1_2, STRUM_UP, 0, 127, 1, FA_P(FA_C3, FA_A2, FA_F2, FA_G2),    "aaaaaaaa", FA_NOACC, 76),   /* AVEBURY CIRCLE */
 FA_ARP(4TH,         1,   BLOCK,    0, 127, 0, FA_P(FA_D3, FA_C3, FA_D3, FA_A2),    "aaaaaaaa", FA_NOACC, 64),   /* HYPOGEUM ORACLE */
 FA_ARP(M7,          1_2, STRUM_UP, 0, 127, 1, FA_P(FA_F2, FA_C3, FA_BB2, FA_C3),   "aaaaaaaa", FA_NOACC, 80),   /* SKARA BRAE FIRE */
 FA_ARP(TRAD_MIN_2,  1_2, BLOCK,    0, 127, 1, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 68),   /* GOBEKLI PILLARS */
 FA_ARP(CINE_6,      1_2, BLOCK,    0, 127, 1, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 70),   /* LUNAR STANDSTILL */
 FA_ARP(CINE_5,      1_2, STRUM_UP, 0, 127, 1, FA_P(FA_C3, FA_D3, FA_C3, FA_G2),    "aaaaaaaa", FA_NOACC, 74),   /* MAESHOWE BEAM */
 FA_ARP(CLASSICAL_1, 1_2, BLOCK,    0, 127, 1, FA_P(FA_A2, FA_F2, FA_C3, FA_G2),    "aaaaaaaa", FA_NOACC, 72),   /* DAWN PROCESSION */
 FA_ARP(5TH,         1_2, BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 70),   /* CURSUS WIND */
 /* ---- struck, plucked, blown ---- */
 FA_ARP(TRAD_MAJ,    1_8, UP,       1,  80, 0, FA_8(FA_C3, FA_A2, FA_F2, FA_G2),    "qqqqqqqq", FA_NOACC, 100),  /* LITHOPHONE */
 FA_ARP(CINE_5,      1_4, UPDN,     2,  80, 0, FA_8(FA_C3, FA_D3, FA_C3, FA_G2),    "qqqqqqqq", FA_NOACC, 108),  /* RING OF BRODGAR */
 FA_ARP(MIN,         1_8, UP,       1,  80, 0, FA_8(FA_D3, FA_G2, FA_A2, FA_D3),    "qqqqqqqq", FA_NOACC, 112),  /* BEAKER BELLS */
 FA_ARP(MIN,         1_4, RANDOM,   2,  64, 0, FA_8(FA_C3, FA_F2, FA_G2, FA_C3),    "qqqqqqqq", FA_NOACC, 116),  /* FLINT KNAPPING */
 FA_ARP(MIN7,        1_4, UPDN,     1,  80, 0, FA_8(FA_A2, FA_B2, FA_E3, FA_A2),    "qqqqqqqq", FA_NOACC, 96),   /* MUSICAL BOW */
 FA_ARP(MIN,         1_8, UP,       1,  96, 0, FA_8(FA_A2, FA_D3, FA_E3, FA_A2),    "qqqqqqqq", FA_NOACC, 88),   /* BONE FLUTE RITE */
 FA_ARP(5TH,         1_4, BLOCK,    0,  80, 1, FA_P(FA_D3, FA_C3, FA_BB2, FA_C3),   "ssssssss", "10101010", 92), /* CONCH CALL */
 FA_ARP(MIN,         1_8, UP,       1,  80, 0, FA_8(FA_A2, FA_D3, FA_E3, FA_A2),    "qqqqqqqq", FA_NOACC, 108),  /* LOG DRUM CIRCLE */
 FA_ARP(OCT,         1_4, UP,       0,  64, 0, FA_R(FA_C3, FA_C3, FA_C3, FA_C3, FA_AB2, FA_AB2, FA_BB2, FA_BB2),
        "qqqqqqqq", "10001000", 108),                                                                            /* CAIRN DRUMS */
 FA_ARP(POP_INV,     1_2, HOLD,     0, 127, 1, FA_P(FA_A2, FA_F2, FA_C3, FA_G2),    "aaaaaaaa", FA_NOACC, 120),  /* ANCESTOR PULSE */
 /* ---- bass and lead ---- */
 FA_ARP(OCT,         1_4, UP,       0,  64, 0, FA_R(FA_C3, FA_C3, FA_C3, FA_C3, FA_AB2, FA_AB2, FA_BB2, FA_BB2),
        "qqqqqqqq", "10001000", 118),                                                                            /* SILBURY BASS */
 FA_ARP(OCT,         1_2, BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 92),   /* QUOIT GROWL */
 FA_ARP(MIN,         1_4, DNUP,     2,  96, 0, FA_8(FA_A2, FA_D3, FA_E3, FA_A2),    "qqqqqqqq", FA_NOACC, 112),  /* ANTLER PICK */
 FA_ARP(MIN,         1_4, DNUP,     2,  96, 0, FA_8(FA_C3, FA_F2, FA_G2, FA_C3),    "qqqqqqqq", FA_NOACC, 100),  /* KNOWTH SPIRAL */
 FA_ARP(MIN,         1_4, DNUP,     2,  80, 0, FA_8(FA_A2, FA_D3, FA_E3, FA_A2),    "qqqqqqqq", FA_NOACC, 124),  /* SOLSTICE FIRE */
 FA_ARP(OCT,         1,   BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_EB3, FA_G2),  "aaaaaaaa", FA_NOACC, 64),   /* OVERTONE CHANT */
 /* ---- keys and the closing rites ---- */
 FA_ARP(5TH,         1_8, UP,       2,  96, 1, FA_P(FA_C3, FA_AB2, FA_F2, FA_BB2),  "qqqqqqqq", FA_NOACC, 96),   /* GGANTIJA ORGAN */
 FA_ARP(TRAD_MAJ,    1_4, BLOCK,    0,  80, 1, FA_8(FA_C3, FA_A2, FA_F2, FA_G2),    "ssssssss", "10101010", 104),/* KERMARIO KEYS */
 FA_ARP(MODERN_1,    1_2, STRUM_UP, 0, 127, 1, FA_P(FA_C3, FA_A2, FA_F2, FA_G2),    "aaaaaaaa", FA_NOACC, 70),   /* WOODHENGE */
 FA_ARP(OCT,         1_2, BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_F2, FA_G2),   "aaaaaaaa", FA_NOACC, 96),   /* MENEC ALIGNMENT */
 FA_ARP(CINE_2,      1_2, BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_EB3, FA_G2),  "aaaaaaaa", FA_NOACC, 66),   /* SKY BURIAL */
 FA_ARP(CINE_3,      1_2, BLOCK,    0, 127, 0, FA_P(FA_C3, FA_AB2, FA_D3, FA_G2),   "aaaaaaaa", FA_NOACC, 64),   /* EQUINOX */
};

#define FS_V9 "9999999999999999"
static const FactorySeq FS_TAB[FP_N] = {
 FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS),                                                 /* 0..2 */
 FS_SEQ(50, "0-2-3-5-3-2-0---", 12, FORWARD, DORIAN, 2,
        GATE, FS_V9, DYNAMICS, "8686868686868686", MODWHEEL, "0123456789876543"),                        /* HYPOGEUM ORACLE */
 FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS),           /* 4..8 */
 FS_SEQ(60, "0-------3---7---", 24, FORWARD, MINOR, 0,
        MODWHEEL, "0234567898765432", GATE, FS_V9, DYNAMICS, "7777777777777777"),                       /* CURSUS WIND */
 FS_SEQ(60, "0479c974e9c7420.", 12, FORWARD, MAJ_PENTA, 0,
        GATE, "4444444444444444", DYNAMICS, "9575957595759575", PROB_SKIP, "0000100000001000"),          /* LITHOPHONE */
 FS_SEQ(60, "024679bcegb97420", 6, RANDOM, LYDIAN, 0,
        GATE, "3333333333333333", DYNAMICS, "9596959695969596", PROB_SKIP, "2222222222222222"),          /* RING OF BRODGAR */
 FS_SEQ(62, "07c70a7c0753a753", 6, FORWARD, MINOR, 2,
        GATE, "3333333333333333", DYNAMICS, "9575957595759575", RATCHET, "0000000000000000"),            /* BEAKER BELLS */
 FS_SEQ(60, "0.3.7a.c.a7.53..", 6, FORWARD, MIN_PENTA, 0,
        GATE, "3333333333333333", DYNAMICS, "9696969696969696", RATCHET, "0000300000060000"),            /* FLINT KNAPPING */
 FS_SEQ(57, "037acefjjfeca730", 12, FORWARD, DORIAN, 9,
        GATE, "5555555555555555", DYNAMICS, "9686968696869686", PROB_SKIP, "0001000100010001"),          /* MUSICAL BOW */
 FS_SEQ(57, "0-3-5-7-5---3-0-", 12, FORWARD, MIN_PENTA, 9,
        GATE, FS_V9, DYNAMICS, "8575857585758575", MODWHEEL, "0000001234567777"),                        /* BONE FLUTE RITE */
 FS_UNDER(FS_BASS),                                                                                       /* CONCH CALL */
 FS_SEQ(57, "0.7a.5370.a7c.53", 6, FORWARD, MIN_PENTA, 9,
        GATE, "3333333333333333", DYNAMICS, "9575957595759575", RATCHET, "0000000300000000"),            /* LOG DRUM CIRCLE */
 FS_SEQ(48, "0.c70.c.0.c770c7", 6, FORWARD, OFF, 0,
        GATE, "2222222222222222", DYNAMICS, "9576957695769576", RATCHET, "0000000300000060"),            /* CAIRN DRUMS */
 FS_UNDER(FS_BASS),                                                                                       /* ANCESTOR PULSE */
 FS_SEQ(48, "0-0c0.0a0-07.3-5", 6, FORWARD, MINOR, 0,
        GATE, "3353335333533353", DYNAMICS, "9575957595759575", RATCHET, "0000000000000300"),            /* SILBURY BASS */
 FS_SEQ(48, "0-----3-5---7-a-", 12, FORWARD, MINOR, 0,
        GATE, FS_V9, DYNAMICS, "8888888888888888", RATCHET, "0000000000000000"),                         /* QUOIT GROWL */
 FS_SEQ(45, "0.0.7.0.a.0.5.3.", 6, FORWARD, MINOR, 9,
        GATE, "4444444444444444", DYNAMICS, "9575957595759575", RATCHET, "0000000000000030"),            /* ANTLER PICK */
 FS_SEQ(60, "c-a-7-8-7-5-3-5-", 12, FORWARD, MINOR, 0,
        GATE, FS_V9, DYNAMICS, "8787878787878787", MODWHEEL, "0000123456789999"),                        /* KNOWTH SPIRAL */
 FS_SEQ(57, "00c0a0c00073c0a7", 6, FORWARD, MINOR, 9,
        GATE, "3333333333333333", DYNAMICS, "9555955595559555", RATCHET, "0000000300000006"),            /* SOLSTICE FIRE */
 FS_SEQ(60, "0---7---3---5---", 24, FORWARD, MINOR, 0,
        GATE, FS_V9, DYNAMICS, "9777977797779777", MODWHEEL, "0123456789876543"),                        /* OVERTONE CHANT */
 FS_UNDER(FS_BASS), FS_UNDER(FS_BASS), FS_UNDER(FS_BASS),                                                 /* 26..28 */
 FS_SEQ(36, "0..0.7..3..5.a..", 12, FORWARD, MINOR, 0,
        GATE, "6666666666666666", DYNAMICS, "9777977797779777", PROB_SKIP, "0000000000000000"),          /* MENEC ALIGNMENT */
 FS_SEQ(60, "0-..7-..a-..c-..", 12, FORWARD, MIN_PENTA, 0,
        GATE, FS_V9, DYNAMICS, "7777777777777777", MODWHEEL, "0011223344556677"),                        /* SKY BURIAL */
 FS_SEQ(48, "0---5---3---a---", 24, FORWARD, MINOR, 0,
        MODWHEEL, "0123456789999876", GATE, FS_V9, DYNAMICS, "8888888888888888"),                       /* EQUINOX */
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

/* performance slot s, whole: its patch on layer 1 (and the next one on layer 2, for DUAL / SPLIT), its effects and tempo,
 * its sequence, and its arp pattern if it leads (factory_seq above): 0, or -1 for a slot past the factory bank */
static int factory_perf(uint32_t s, WholePerformance *w)
{
    const FactoryVoice *f;
    EngineState base;
    MasterPerformancePreset *p = &w->perf;
    uint8_t l2;
    if (s >= FP_N)
        return -1;
    f = &FV_TAB[s];
    l2 = f->l2 < FP_N ? f->l2 : (uint8_t)((s + 1u) % FP_N);
    engine_defaults(&base);
    factory_voice(&w->engine[0], &base, s);
    factory_voice(&w->engine[1], &base, l2);
    perf_defaults(p);
    factory_name(p->name, f->name);
    p->layer_patch[0] = (uint8_t)s;
    p->layer_patch[1] = l2;
    p->seq_link = (uint8_t)s;
    p->arp_link = FA_TAB[s].maq ? (uint8_t)s : (uint8_t)127u;   /* SEQ-led: no arp pattern */
    p->perf_mode = f->pmode;
    p->voice_mode[0] = f->vmode;
    p->voice_mode[1] = f->pmode == PERF_DUAL_SYNTH ? FV_TAB[l2].vmode : f->vmode;   /* (SPLIT: layer 2 as layer 1) */
    p->porta_mode[0] = p->porta_mode[1] = (uint8_t)(f->porta >> 7);
    p->porta_time[0] = p->porta_time[1] = (uint8_t)(f->porta & 127u);
    if (f->vmode == VMODE_UNISON_STACK)
        p->unison_detune[0] = p->unison_detune[1] = f->unison;
    if (f->pmode == PERF_DUAL_SYNTH)
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

/* boot, before ps_load: the patches, sequences and arp patterns into their banks, the performances', sequences' and
 * arps' names, all marked used (a factory performance is made whole when it loads: factory_perf) */
static void factory_install(void)
{
    EngineState base;
    uint32_t s;
    engine_defaults(&base);
    for (s = 0; s < FP_N; s++) {
        factory_voice(&patch_bank[s], &base, s);
        factory_seq(s, syn_seq_bank[s]);
        factory_arp(s, &syn_arp_bank[s]);
        factory_name(syn_perf_name[s], FV_TAB[s].name);
        factory_name(syn_seq_name[s], FV_TAB[s].name);   /* (a performance's sequence and arp pattern: its name) */
        factory_name(syn_arp_name[s], FV_TAB[s].name);
        syn_used[UISAVE_PATCH_PRESET][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_SEQ_SETTINGS][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_ARP_SEQ][s >> 3] |= (uint8_t)(1u << (s & 7u));
        syn_used[UISAVE_WHOLE_PERF][s >> 3] |= (uint8_t)(1u << (s & 7u));
    }
}
PS_STATIC_ASSERT(FP_N == BANK_SLOTS, "a factory sequence, arp and performance for every slot");
