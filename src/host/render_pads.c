/* Listening render of 90s-style pads on the whole-note PCM prototype waves (2026-11-05, proto/pcm-handoff): each
 * patch built as the factory bank builds one (engine_defaults plus offset/value pairs), played by the real engine
 * with its effects: a chord progression, then a short line on top. Prototype scaffolding, not a test.
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/wt_areas_host.c src/host/pcm_image_pads.c src/host/render_pads.c -lm -o render_pads
 *   render_pads OUTDIR */
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/pcm_store.h"
#include "dsp/wavetables.h"
#include "dsp/mfx.h"

extern const uint8_t pcm_image_pads[];
extern const uint32_t pcm_image_pads_len;

#define RATE 44100u
#define FP_OCT(k) ((uint8_t)((k) == 0 ? 64 : 18 * ((k) + 3) + 9))
#define FP_BIP(v) ((uint8_t)((v) + 64))
#define V_(f, v) (uint8_t)offsetof(EngineState, f), (uint8_t)(v)
#define V_END 0xFFu
#define V_OSC(i, w, sc, oc) V_(osc[i].wavebank, w), V_(osc[i].scan, sc), V_(osc[i].octave, FP_OCT(oc))
#define V_FLT(i, fl, drv, mo, cut, res, amt) V_(filter[i].flavour, FLT_##fl), V_(filter[i].drive, drv), \
    V_(filter[i].morph, mo), V_(filter[i].cutoff, cut), V_(filter[i].resonance, res), V_(filter[i].mod_amt, 2 * (amt))
#define V_ENV(i, a, d, s, r) V_(env[i].attack, a), V_(env[i].decay, d), V_(env[i].sustain, s), V_(env[i].release, r)
#define V_LFO(i, sh, rt) V_(lfo[i].shape, sh), V_(lfo[i].rate, rt)
#define V_SLOT(i, a, fn, b, amt) V_(mod[i].src_a, MSRC_##a), V_(mod[i].func, MFN_##fn), V_(mod[i].src_b, MSRC_##b), \
    V_(mod[i].amount, FP_BIP(amt))
#define V_AMP(drv, ds, rs) V_(amp.drive, drv), V_(amp.delay_send, ds), V_(amp.reverb_send, rs)
#define V_KT(i, kt) V_(filter_key_track[i], kt)
#define V_VIB(rt, dl) V_LFO(1, 0, rt), V_(lfo[1].delay, dl), V_(lfo[1].fade, FP_BIP(40)), V_(lfo[1].key_reset, 1)
#define V_MFX(t, k0, k1, k2, k3, wid) V_(mfx.type, MFX_##t), V_(mfx.knob[0], k0), V_(mfx.knob[1], k1), \
    V_(mfx.knob[2], k2), V_(mfx.knob[3], k3), V_(mfx.lr, wid)

/* pcm_image_pads (pads_image.py): the wave codes */
enum { P_CHOIR = 64, P_PAN, P_HORNS, P_STONE, P_RAIN, P_LOG, P_ANGKLUNG, P_CHIMES, P_LORE, P_MOON, P_SPIRIT };
enum { W_FOUND = 0, W_LEY = 1, W_STARS = 3, W_HALO = 4 };
enum { F_TRI = 0, F_SAW = 18, F_SQUARE = 36, F_P35 = 54 };

/* ---- VOX HEAVEN: the choir alone, wide. SPREAD over CHOIR, LFO1 drifting the vowel ooh -> aah, warm low-pass,
 * ENSEMBLE, a long CELESTIAL hall */
static const uint8_t PD_VOX[] = {
    V_(arch, VARCH_SPREAD), V_OSC(0, P_CHOIR, 30, 0), V_(osc[0].scan_mod_src, MSRC_LFO1), V_(osc[0].scan_mod_amt, 50),
    V_(oscmix.spread, 45),
    V_FLT(0, MULTI, 0, 0, 104, 10, 0), V_KT(0, 40),
    V_ENV(0, 40, 100, 120, 96), V_LFO(0, 0, 10),
    V_MFX(ENSEMBLE, 40, 30, 50, 70, 100),
    V_AMP(0, 20, 90), V_(analog, 24), V_END};

/* ---- PAN PIPE DREAM: the pan flute over a soft triangle an octave down. DUAL OSC, the flute's chiff up front (a
 * quick amp attack), a delayed vibrato, tape echo and a big room */
static const uint8_t PD_PAN[] = {
    V_(arch, VARCH_DUAL_OSC), V_OSC(0, P_PAN, 30, 0), V_OSC(1, W_FOUND, F_TRI, -1), V_(oscmix.balance, 44),
    V_(osc[0].pitch_mod_src, MSRC_LFO2), V_(osc[0].pitch_mod_amt, 6),
    V_FLT(0, MULTI, 0, 0, 110, 6, 0), V_KT(0, 64),
    V_ENV(0, 2, 90, 116, 84), V_VIB(70, 40),
    V_MFX(CHORUS, 30, 40, 50, 50, 64),
    V_AMP(0, 46, 84), V_(analog, 20), V_END};

/* ---- AUROCHS BRASS: the horn section's blat over a filtered saw. STACK: A HORNS (SCAN up a little toward
 * brassy), B Foundations SAW detuned through an SH-2 that ENV2 opens; CHORUS; DOLMEN */
static const uint8_t PD_HORNS[] = {
    V_(arch, VARCH_STACK), V_OSC(0, P_HORNS, 50, 0), V_OSC(1, W_FOUND, F_SAW, 0), V_(osc[1].fine, 69),
    V_FLT(0, MULTI, 0, 0, 108, 8, 0), V_FLT(1, SH2, 20, 0, 60, 30, 30), V_KT(1, 64),
    V_(fltmix.balance, 50), V_(fltmix.stack_width, 70),
    V_ENV(0, 6, 90, 112, 80), V_ENV(1, 30, 96, 80, 80),
    V_MFX(CHORUS, 24, 50, 60, 40, 64),
    V_AMP(0, 30, 80), V_(analog, 30), V_END};

/* ---- STONE CHOIR: the singing stone pressed into a choir. STACK: A SINGING STN (SCAN toward B, the pressed
 * hand), B CHOIR on its ooh; wide; slow swell; CELESTIAL */
static const uint8_t PD_STONE[] = {
    V_(arch, VARCH_STACK), V_OSC(0, P_STONE, 90, 0), V_OSC(1, P_CHOIR, 20, 0), V_(osc[1].fine, 66),
    V_FLT(0, MULTI, 0, 0, 100, 10, 0), V_FLT(1, MULTI, 0, 0, 96, 10, 0),
    V_(fltmix.balance, 56), V_(fltmix.stack_width, 90),
    V_ENV(0, 30, 100, 120, 96), V_ENV(1, 60, 100, 120, 96),
    V_MFX(ENSEMBLE, 30, 24, 40, 60, 100),
    V_AMP(0, 16, 92), V_(analog, 24), V_END};

/* ---- RAIN TEMPLE: the rainstick tipped over the moon hum. STACK: A MOONSONG (hum), B RAINSTICK (trickle) */
static const uint8_t PD_RAIN[] = {
    V_(arch, VARCH_STACK), V_OSC(0, P_MOON, 10, 0), V_OSC(1, P_RAIN, 30, 0),
    V_FLT(0, MULTI, 0, 0, 98, 10, 0), V_FLT(1, MULTI, 0, 0, 120, 0, 0),
    V_(fltmix.balance, 64), V_(fltmix.stack_width, 100),
    V_ENV(0, 50, 100, 120, 100), V_ENV(1, 2, 100, 120, 100),
    V_AMP(0, 20, 90), V_(analog, 20), V_END};

/* ---- LOG DRUM PAD: the struck drum ringing over a swelling triangle (DUAL OSC), the amp ENV1 holding the ring:
 * the 90s "mallet pad" */
static const uint8_t PD_LOG[] = {
    V_(arch, VARCH_DUAL_OSC), V_OSC(0, P_LOG, 40, 0), V_OSC(1, W_FOUND, F_TRI, 0), V_(oscmix.balance, 40),
    V_FLT(0, MULTI, 0, 0, 112, 6, 0),
    V_ENV(0, 0, 90, 110, 90),
    V_MFX(ENSEMBLE, 30, 30, 30, 50, 100),
    V_AMP(0, 40, 80), V_(analog, 20), V_END};

/* ---- LOG DRUM (decay): the same drum with ENV1 decaying it to silence: percussion, the envelope choosing the
 * length */
static const uint8_t PD_LOGDEC[] = {
    V_(arch, VARCH_SPREAD), V_OSC(0, P_LOG, 64, 0), V_(oscmix.spread, 0),
    V_FLT(0, MULTI, 0, 0, 116, 4, 0),
    V_ENV(0, 0, 76, 0, 70),
    V_AMP(0, 30, 60), V_END};

/* ---- BAMBOO BREATH: the angklung shaken over the moon hum an octave down. STACK: A ANGKLUNG, B MOONSONG */
static const uint8_t PD_ANGK[] = {
    V_(arch, VARCH_STACK), V_OSC(0, P_ANGKLUNG, 20, 0), V_OSC(1, P_MOON, 30, -1),
    V_FLT(0, MULTI, 0, 0, 112, 6, 0), V_FLT(1, MULTI, 0, 0, 90, 10, 0),
    V_(fltmix.balance, 52), V_(fltmix.stack_width, 80),
    V_ENV(0, 2, 100, 116, 90), V_ENV(1, 70, 100, 110, 100),
    V_AMP(0, 40, 86), V_(analog, 20), V_END};

/* ---- SLATE WIND: the chimes on their slate side over a Star Chart pad. STACK: A WIND CHIMES (SCAN at slate),
 * B wavetable Star Chart swelling, LFO3 moving its frame */
static const uint8_t PD_CHIMES[] = {
    V_(arch, VARCH_STACK), V_OSC(0, P_CHIMES, 120, 0), V_OSC(1, W_STARS, 40, 0),
    V_(osc[1].scan_mod_src, MSRC_LFO3), V_(osc[1].scan_mod_amt, 40),
    V_FLT(0, MULTI, 0, 0, 120, 0, 0), V_FLT(1, MULTI, 0, 0, 92, 14, 0),
    V_(fltmix.balance, 60), V_(fltmix.stack_width, 100),
    V_ENV(0, 2, 100, 120, 100), V_ENV(1, 70, 100, 110, 100), V_LFO(2, 0, 16),
    V_MFX(ENSEMBLE, 30, 24, 40, 50, 110),
    V_AMP(0, 30, 94), V_(analog, 20), V_END};

/* ---- LORE GARDEN: the cane chimes stirring over the choir's ooh. STACK: A LORE, B CHOIR */
static const uint8_t PD_LORE[] = {
    V_(arch, VARCH_STACK), V_OSC(0, P_LORE, 30, 0), V_OSC(1, P_CHOIR, 10, -1),
    V_FLT(0, MULTI, 0, 0, 118, 0, 0), V_FLT(1, MULTI, 0, 0, 92, 10, 0),
    V_(fltmix.balance, 62), V_(fltmix.stack_width, 100),
    V_ENV(0, 2, 100, 120, 100), V_ENV(1, 70, 100, 116, 100),
    V_MFX(ENSEMBLE, 30, 24, 40, 50, 110),
    V_AMP(0, 24, 92), V_(analog, 20), V_END};

/* ---- STAR SONG: the new wavetable Overtone Halo, new-age: SPREAD, LFO1 (a slow triangle) carrying the overtone
 * whistle up and down the series, the MOONSONG hum an octave under (STACK), ENSEMBLE, a long CELESTIAL hall */
static const uint8_t PD_HALO[] = {
    V_(arch, VARCH_STACK), V_OSC(0, W_HALO, 30, 0), V_(osc[0].scan_mod_src, MSRC_LFO1), V_(osc[0].scan_mod_amt, 70),
    V_OSC(1, P_MOON, 20, -1),
    V_FLT(0, MULTI, 0, 0, 112, 6, 0), V_FLT(1, MULTI, 0, 0, 90, 8, 0),
    V_(fltmix.balance, 50), V_(fltmix.stack_width, 90),
    V_ENV(0, 60, 100, 120, 100), V_ENV(1, 80, 100, 116, 100), V_LFO(0, 21, 8),
    V_MFX(ENSEMBLE, 30, 24, 50, 60, 110),
    V_AMP(0, 30, 96), V_(analog, 20), V_END};

/* ---- OVERTONE HALO alone: the bank under SPREAD, SCAN swept by LFO1, to hear it plain */
static const uint8_t PD_HALO1[] = {
    V_(arch, VARCH_SPREAD), V_OSC(0, W_HALO, 0, 0), V_(osc[0].scan_mod_src, MSRC_LFO1), V_(osc[0].scan_mod_amt, 127),
    V_(oscmix.spread, 40),
    V_FLT(0, MULTI, 0, 0, 120, 0, 0),
    V_ENV(0, 30, 100, 120, 90), V_LFO(0, 21, 10),
    V_MFX(ENSEMBLE, 30, 24, 40, 50, 100),
    V_AMP(0, 20, 80), V_(analog, 16), V_END};

typedef struct {
    const char *name;
    const uint8_t *set;
    uint8_t rtype, rdec, rdmp, dtime, dfb;
    uint8_t line;                        /* 1: a melody after the chords (shows the attacks) */
    int8_t shift;                        /* the chords' and line's transposition, semitones */
} Pad;
static const Pad PADS[] = {
    {"VOX_HEAVEN",      PD_VOX,    REVTYPE_CELESTIAL, 118, 50, 87, 40, 0, 0},
    {"PAN_PIPE_DREAM",  PD_PAN,    REVTYPE_TUMULUS,   110, 60, 79, 46, 1, 12},
    {"AUROCHS_BRASS",   PD_HORNS,  REVTYPE_DOLMEN,     96, 60, 79, 30, 1, 0},
    {"STONE_CHOIR",     PD_STONE,  REVTYPE_CELESTIAL, 120, 50, 87, 40, 0, 0},
    {"RAIN_TEMPLE",     PD_RAIN,   REVTYPE_TUMULUS,   120, 70, 87, 40, 0, 0},
    {"LOG_DRUM_PAD",    PD_LOG,    REVTYPE_TOMB,       80, 60, 71, 40, 1, 0},
    {"LOG_DRUM_DECAY",  PD_LOGDEC, REVTYPE_TOMB,       60, 70, 71, 30, 1, -12},
    {"BAMBOO_BREATH",   PD_ANGK,   REVTYPE_CELESTIAL, 112, 50, 79, 46, 1, 0},
    {"SLATE_WIND",      PD_CHIMES, REVTYPE_CELESTIAL, 124, 40, 87, 50, 0, 12},
    {"LORE_GARDEN",     PD_LORE,   REVTYPE_TUMULUS,   120, 60, 87, 40, 0, 12},
    {"STAR_SONG",       PD_HALO,   REVTYPE_CELESTIAL, 124, 40, 87, 50, 1, 0},
    {"OVERTONE_HALO",   PD_HALO1,  REVTYPE_CELESTIAL, 110, 50, 87, 40, 0, 0},
};

/* Am9 - Fmaj9 - Cadd9 - Gsus4, then a line over the last */
static const uint8_t CHORDS[4][5] = {{45, 52, 60, 67, 71}, {41, 48, 57, 64, 67}, {48, 55, 64, 67, 74}, {43, 50, 57, 60, 62}};
static const uint8_t LINE[] = {69, 72, 76, 74, 72, 69, 67, 69};

static int16_t *out;
static uint32_t frames, cap;

static void run(double sec)
{
    static int32_t buf[2 * 64];
    uint32_t n = (uint32_t)(sec * RATE);
    while (n) {
        uint32_t k = n < 64u ? n : 64u;
        synth_render_q15(buf, k);
        for (uint32_t i = 0; i < 2 * k && frames < cap; i++) {
            int32_t s = buf[i];
            out[2 * frames + (i & 1)] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
            if (i & 1) frames++;
        }
        n -= k;
    }
}
static void put_u32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 255, f); }

int main(int argc, char **argv)
{
    char path[512];
    if (argc < 2) { fprintf(stderr, "usage: render_pads OUTDIR\n"); return 2; }
    synth_init();
    if (pcm_store_attach(0, pcm_image_pads, pcm_image_pads_len) <= 0) { fprintf(stderr, "image rejected\n"); return 1; }
    {
        extern const uint8_t wt_area2_host[];
        extern const uint32_t wt_area2_host_len;
        wavetable_area_attach(0, wt_area2_host, wt_area2_host_len);
    }
    cap = 40u * RATE;
    out = malloc(cap * 4u);
    for (size_t p = 0; p < sizeof PADS / sizeof PADS[0]; p++) {
        const Pad *d = &PADS[p];
        EngineState *e = &engine_state[0];
        live_defaults();
        engine_defaults(e);
        for (const uint8_t *q = d->set; q[0] != V_END; q += 2)
            ((uint8_t *)e)[q[0]] = q[1];
        perf_state.perf_mode = PERF_SINGL_SYNTH;
        perf_state.voice_mode[0] = VMODE_POLY_REUSE;
        perf_state.reverb_type = d->rtype; perf_state.reverb_decay = d->rdec; perf_state.reverb_damp = d->rdmp;
        perf_state.delay_time = d->dtime; perf_state.delay_feedback = d->dfb;
        synth_restart();
        frames = 0;
        run(0.3);
        for (int c = 0; c < 4; c++) {
            for (int i = 0; i < 5; i++) synth_midi_3(0x90, (uint8_t)(CHORDS[c][i] + d->shift), 96);
            run(3.4);
            for (int i = 0; i < 5; i++) synth_midi_3(0x80, (uint8_t)(CHORDS[c][i] + d->shift), 0);
            run(0.1);
        }
        if (d->line) {
            run(1.0);
            for (size_t i = 0; i < sizeof LINE; i++) {
                synth_midi_3(0x90, (uint8_t)(LINE[i] + d->shift), 100);
                run(i == sizeof LINE - 1 ? 1.6 : 0.42);
                synth_midi_3(0x80, (uint8_t)(LINE[i] + d->shift), 0);
                run(0.04);
            }
        }
        run(4.0);
        int32_t peak = 1;
        for (uint32_t i = 0; i < 2 * frames; i++) if (abs(out[i]) > peak) peak = abs(out[i]);
        double g = 29000.0 / peak;                      /* about -1 dBFS */
        snprintf(path, sizeof path, "%s/%02u_%s.wav", argv[1], (unsigned)p + 1u, d->name);
        FILE *f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
        fwrite("RIFF", 1, 4, f); put_u32(f, 36u + frames * 4u); fwrite("WAVEfmt ", 1, 8, f);
        put_u32(f, 16); put_u32(f, 1u | 2u << 16); put_u32(f, RATE); put_u32(f, RATE * 4u); put_u32(f, 4u | 16u << 16);
        fwrite("data", 1, 4, f); put_u32(f, frames * 4u);
        for (uint32_t i = 0; i < 2 * frames; i++) {
            int32_t s = (int32_t)(out[i] * g);
            fputc(s & 255, f); fputc((s >> 8) & 255, f);
        }
        fclose(f);
        printf("%-16s peak %5.1f dBFS before gain, %5.1f s%s\n", d->name, 20.0 * __builtin_log10(peak / 32768.0),
               frames / (double)RATE, peak >= 32767 ? "  CLIPPED inside the engine" : "");
    }
    return 0;
}
