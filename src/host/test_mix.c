/* Host test: the MIX page (2026-11-02, dsp/fx.h fx_live; synth_runtime.c mix_targets, fx.c LOOP TONE):
 *  1. BALANCE (DUAL): a note on each layer. At 0 the output is layer 1's alone (as with layer 2 not playing, within
 *     0.2 dB), at 127 layer 2's alone; at 32 layer 2 sits 12 dB down (the square law).
 *  2. DRY/WET (SINGL): at 127, no sends: silent; with a delay send the echoes still sound (the dry cut after the
 *     sends); at 0 with sends, the output is the dry alone (as with no sends, within 0.2 dB).
 *  3. LOOP TONE (the delay alone, a short burst, FEEDBACK 90): the first echo is untouched (its treble and bass within
 *     0.5 dB of the middle's); the later repeats lose treble below the middle (DARK, >= 6 dB less at 3 kHz) and
 *     bass above it (THIN, >= 4 dB less at 80 Hz).
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(DSP) src/host/foundations_image_host.c src/host/test_mix.c -lm */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/fx.h"
#include "dsp/wavetables.h"

#define SR 44100
static int fails;
static int32_t buf[64];

static void setup(uint8_t mode, uint8_t dly_send, uint8_t rev_send)
{
    live_defaults();
    perf_state.perf_mode = mode;
    perf_state.delay_feedback = 60;
    for (int l = 0; l < 2; l++) {
        EngineState *e = &engine_state[l];
        e->env[0].attack = 0; e->env[0].sustain = 120; e->env[0].release = 20;
        e->amp.delay_send = dly_send; e->amp.reverb_send = rev_send;
    }
    synth_restart();
}

/* notes on the layers' channels (ch1 / ch2: note on1 / on2, 0 none) held for `hold` s, then `tail` s more; the RMS
 * (dB, Q15 full scale) of the output from `from` s on */
static double run(int on1, int on2, double hold, double tail, double from)
{
    double e = 0.0;
    long n = 0;
    if (on1) synth_midi_3(0x90, (uint8_t)on1, 100);
    if (on2) synth_midi_3(0x91, (uint8_t)on2, 100);
    for (int s = 0; s < (int)((hold + tail) * SR); s += 32) {
        if (s >= (int)(hold * SR) && s < (int)(hold * SR) + 32) {
            if (on1) synth_midi_3(0x80, (uint8_t)on1, 0);
            if (on2) synth_midi_3(0x81, (uint8_t)on2, 0);
        }
        synth_render_q15(buf, 32);
        if (s >= (int)(from * SR))
            for (int i = 0; i < 64; i++) { e += (double)buf[i] * buf[i]; n++; }
    }
    return 10.0 * log10(e / (double)(n ? n : 1) + 1e-12) - 90.3;
}

static void check(int ok, const char *what, double a, double b)
{
    printf("%-56s %7.2f %7.2f%s\n", what, a, b, ok ? "" : "  << FAIL");
    if (!ok) fails++;
}

/* the energy (dB) of x[0..n) in a band at f (RBJ band-pass, Q 1) within [t0, t1) s */
static double band(const double *x, int n, double f, double t0, double t1)
{
    double w = 2 * M_PI * f / SR, al = sin(w) / 2.0, a0 = 1 + al, a1 = -2 * cos(w), a2 = 1 - al;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0;
    for (int i = 0; i < n; i++) {
        double y = (al * x[i] - al * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = x[i]; y2 = y1; y1 = y;
        if (i >= (int)(t0 * SR) && i < (int)(t1 * SR)) e += y * y;
    }
    return 10 * log10(e + 1);
}

/* the delay alone over a burst, LOOP TONE t: the treble (the 3 kHz band) and bass (the 80 Hz band), dB, in the first
 * echo's window and the later repeats' */
static void tone_bands(uint8_t t, double *tr1, double *ba1, double *tr3, double *ba3)
{
    static MasterPerformancePreset p;
    static double out[SR * 2];
    memset(&p, 0, sizeof p);
    p.delay_time = 59; p.delay_feedback = 90;
    if (t > 64u) p.delay_pingpong = (uint8_t)(((t - 64u) * 127u / 63u) << 1);   /* (as fx_tone_sync writes it) */
    fx_live[FX_L_TONE] = t;
    fx_init();
    unsigned seed = 3;
    int32_t d[128], r[128], w[128];
    for (int s = 0; s < SR * 2; s += 128) {
        for (int i = 0; i < 128; i++) {
            seed = seed * 1103515245u + 12345u;
            d[i] = s + i < SR / 50 ? (int16_t)(seed >> 16) / 2 : 0;           /* 20 ms of noise */
        }
        fx_delay_block(&p, d, r, w, 128);
        for (int i = 0; i < 128; i++) out[s + i] = r[i];
    }
    *tr1 = band(out, SR * 2, 3000, 0.3, 0.7); *ba1 = band(out, SR * 2, 80, 0.3, 0.7);
    *tr3 = band(out, SR * 2, 3000, 1.2, 2.0); *ba3 = band(out, SR * 2, 80, 1.2, 2.0);
    fx_live[FX_L_TONE] = 64u;
}

int main(void)
{
    synth_init();
    {
        extern const uint8_t foundations_image_host[];
        extern const uint32_t foundations_image_host_len;
        wavetable_bank_attach(foundations_image_host, foundations_image_host_len);
    }
    double a, b;
    /* 1. BALANCE */
    fx_live[FX_L_BALANCE] = 64u;
    setup(PERF_DUAL_SYNTH, 0, 0); a = run(60, 0, 1.0, 0.0, 0.3);
    fx_live[FX_L_BALANCE] = 0u;
    setup(PERF_DUAL_SYNTH, 0, 0); b = run(60, 67, 1.0, 0.0, 0.3);
    check(fabs(a - b) < 0.2, "BALANCE 0: layer 1 alone (dB: L1 only at 64, both at 0)", a, b);
    fx_live[FX_L_BALANCE] = 64u;
    setup(PERF_DUAL_SYNTH, 0, 0); a = run(0, 67, 1.0, 0.0, 0.3);
    fx_live[FX_L_BALANCE] = 127u;
    setup(PERF_DUAL_SYNTH, 0, 0); b = run(60, 67, 1.0, 0.0, 0.3);
    check(fabs(a - b) < 0.2, "BALANCE 127: layer 2 alone (L2 only at 64, both at 127)", a, b);
    fx_live[FX_L_BALANCE] = 32u;
    setup(PERF_DUAL_SYNTH, 0, 0); b = run(0, 67, 1.0, 0.0, 0.3);
    check(fabs((a - b) - 12.04) < 0.3, "BALANCE 32: layer 2 12 dB down (L2 at 64, at 32)", a, b);
    /* 2. DRY/WET */
    fx_live[FX_L_BALANCE] = 127u;
    setup(PERF_SINGL_SYNTH, 0, 0); a = run(60, 0, 1.0, 0.0, 0.3);
    check(a < -90.0, "DRY/WET 127, no sends: silent (dB)", a, -90.0);
    setup(PERF_SINGL_SYNTH, 90, 0); a = run(60, 0, 1.0, 1.5, 0.3);
    check(a > -60.0, "DRY/WET 127, a delay send: the echoes sound (dB)", a, -60.0);
    fx_live[FX_L_BALANCE] = 64u;
    setup(PERF_SINGL_SYNTH, 0, 0); a = run(60, 0, 1.0, 0.0, 0.3);
    fx_live[FX_L_BALANCE] = 0u;
    setup(PERF_SINGL_SYNTH, 90, 60); b = run(60, 0, 1.0, 0.0, 0.3);
    check(fabs(a - b) < 0.2, "DRY/WET 0 with sends: the dry alone (no sends at 64, sends at 0)", a, b);
    fx_live[FX_L_BALANCE] = 64u;
    /* 3. LOOP TONE */
    {
        double t1, b1, t3, b3, d1, db1, d3, db3, h1, hb1, h3, hb3;
        tone_bands(64, &t1, &b1, &t3, &b3);
        tone_bands(16, &d1, &db1, &d3, &db3);
        tone_bands(112, &h1, &hb1, &h3, &hb3);
        check(fabs(d1 - t1) < 0.5 && fabs(db1 - b1) < 0.5, "LOOP TONE 16: the first echo's treble as at 64", d1, t1);
        check(t3 - d3 >= 6.0, "LOOP TONE 16: later repeats' treble down (64, 16)", t3, d3);
        check(fabs(h1 - t1) < 0.5 && fabs(hb1 - b1) < 0.5, "LOOP TONE 112: the first echo's bass as at 64", hb1, b1);
        check(b3 - hb3 >= 4.0, "LOOP TONE 112: later repeats' bass down (64, 112)", b3, hb3);
    }
    printf(fails ? "%d MIX checks failed\n" : "all MIX checks passed\n", fails);
    return fails != 0;
}
