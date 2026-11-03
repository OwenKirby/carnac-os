/* A/B listening files: Foundations (bank 0's frames, osc_bank0_vco = 0) against VCO (the same oscillator with
 * osc_bank0_vco = 1: oscillators.c "VCO", the SH-2 VCO-1 computed live), both through the firmware's own
 * osc_prepare / osc_render_block at a 32-sample tick. Every pair plays Foundations first, then VCO.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/foundations_image_host.c src/host/render_vco_ab.c -lm -o ab.exe
 *   ./ab.exe listening/vco_ab [TONES_DIR]
 *
 * TONES_DIR (optional) gets single tones of each, for spectrum checks (analyse_vco_ab.py). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "oscillators.h"
#include "wavetables.h"

#define SR 44100
#define BLK 32

extern const uint8_t foundations_image_host[];
extern const uint32_t foundations_image_host_len;
int wavetable_bank_attach(const uint8_t *img, uint32_t size);

static const char *NAMES[8] = { "TRIANGLE", "SAW", "SQUARE", "PULSE35", "PULSE20", "PULSE10", "PULSEMIN", "SAW+SQUARE" };
static double ns_found, ns_vco; static long n_found, n_vco;

/* --------------------------------------------------------------- buffers --- */
typedef struct { float *x; int n, cap; } Buf;
static void put(Buf *b, float v)
{
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 1 << 16; b->x = realloc(b->x, sizeof(float) * b->cap); }
    b->x[b->n++] = v;
}
static void gap(Buf *b, float s) { for (int i = 0; i < (int)(s * SR); i++) put(b, 0.0f); }

static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }
static void wav(const char *dir, const char *name, const Buf *b)
{
    char path[512]; snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + b->n * 2); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, SR); put32(f, SR * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, b->n * 2);
    for (int i = 0; i < b->n; i++) {
        float v = b->x[i] * 0.7f;
        put16(f, (int)lrintf(v > 32767 ? 32767 : v < -32768 ? -32768 : v));
    }
    fclose(f);
}

/* ------------------------------------------------------------- the two --- */
/* hz(i) and morph(i) are read once per 32-sample block, as the synth's control tick */
typedef float (*Curve)(int i, void *ctx);

static void render_osc(Buf *b, int n, Curve hz, Curve morph, void *ctx, float phase, int vco)
{
    osc_bank0_vco = vco;
    WavetableOsc o; osc_init(&o); o.bank = 0;
    o.center_phase = (uint32_t)(phase * 4294967296.0);
    int32_t blk[BLK];
    clock_t c0 = clock();
    for (int i = 0; i < n; i += BLK) {
        int m = n - i < BLK ? n - i : BLK;
        o.morph_q12 = (int)(morph(i, ctx) * 4096.0f);
        if (o.morph_q12 > (7 << 12) - 1) o.morph_q12 = (7 << 12) - 1;
        osc_prepare(&o, (uint32_t)(hz(i, ctx) * 65536.0f), 0);
        osc_render_block(&o, blk, m);
        for (int k = 0; k < m; k++) put(b, (float)blk[k]);
    }
    double ns = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e9;
    if (vco) { ns_vco += ns; n_vco += n; } else { ns_found += ns; n_found += n; }
}

static void render_found(Buf *b, int n, Curve hz, Curve morph, void *ctx, float phase)
{ render_osc(b, n, hz, morph, ctx, phase, 0); }
static void render_vco(Buf *b, int n, Curve hz, Curve morph, void *ctx, float phase)
{ render_osc(b, n, hz, morph, ctx, phase, 1); }

static void fade(Buf *b, int from)
{
    int k = SR / 100, n = b->n - from;
    for (int i = 0; i < k && i < n; i++) { float g = (float)i / k; b->x[from + i] *= g; b->x[b->n - 1 - i] *= g; }
}

/* one tone of each, Foundations then VCO */
static void pair(Buf *b, int n, Curve hz, Curve morph, void *ctx, float pause)
{
    int s = b->n; render_found(b, n, hz, morph, ctx, 0.0f); fade(b, s); gap(b, pause);
    s = b->n; render_vco(b, n, hz, morph, ctx, 0.0f); fade(b, s); gap(b, pause * 2.0f);
}

/* --------------------------------------------------------------- curves --- */
typedef struct { float hz, morph, lfo_hz, lfo_ph, sweep_s; } Ctx;
static float c_hz(int i, void *c) { (void)i; return ((Ctx *)c)->hz; }
static float c_morph(int i, void *c) { (void)i; return ((Ctx *)c)->morph; }
static float c_pwm(int i, void *c)     /* SQUARE .. PULSE MIN and back: the SH-2's PWM from the LFO */
{
    Ctx *x = c; float t = (float)i / SR;
    return 2.0f + 4.0f * (0.5f - 0.5f * cosf(6.2831853f * x->lfo_hz * t + x->lfo_ph));
}
static float c_sweep(int i, void *c) { return 6.999f * (float)i / (((Ctx *)c)->sweep_s * SR); }

static double rms(const float *x, int n) { double s = 0; for (int i = 0; i < n; i++) s += (double)x[i] * x[i]; return sqrt(s / n); }

/* both levels, every wave at A2, and VCO's peak over scan sweeps at seven pitches */
static void match_gain(void)
{
    Ctx c = { 110.0f, 1.0f };
    printf("  wave         Foundations dB  VCO dB\n");
    for (int f = 0; f < 8; f++) {
        Buf p = {0}, q = {0}; c.morph = (float)f;
        render_found(&p, SR, c_hz, c_morph, &c, 0.0f); render_vco(&q, SR, c_hz, c_morph, &c, 0.0f);
        printf("  %-11s  %8.1f        %8.1f\n", NAMES[f], 20 * log10(rms(p.x + SR / 4, SR / 2) / 32768.0),
               20 * log10(rms(q.x + SR / 4, SR / 2) / 32768.0));
        free(p.x); free(q.x);
    }
    float peak = 0;
    for (float hz = 27.5f; hz < 8000.0f; hz *= 2.0f) {
        Buf q = {0}; Ctx s = { hz, 0, 0, 0, 2 };
        render_vco(&q, SR * 2, c_hz, c_sweep, &s, 0.0f);
        for (int i = 0; i < q.n; i++) if (fabsf(q.x[i]) > peak) peak = fabsf(q.x[i]);
        free(q.x);
    }
    printf("  VCO peak over scan sweeps 27.5 Hz .. 7 kHz: %.0f (int16 limit 32767)\n", peak);
}

/* --------------------------------------------------- a PWM string chord --- */
/* four notes, two oscillators each (+-6 cents), every oscillator's PWM on its own slow LFO, through one gentle
 * two-pole low-pass at 2.4 kHz: the same for both, so only the oscillator differs */
static void strings(Buf *out, int vco)
{
    static const float notes[4] = { 48, 51, 55, 58 };          /* C3 Eb3 G3 Bb3 */
    int n = SR * 9;
    float *mix = calloc(n, sizeof(float));
    for (int v = 0; v < 8; v++) {
        Ctx c = { 440.0f * powf(2.0f, (notes[v / 2] - 69.0f + (v & 1 ? 0.06f : -0.06f)) / 12.0f), 0,
                  0.27f + 0.07f * v, 0.9f * v };
        Buf b = {0};
        if (vco) render_vco(&b, n, c_hz, c_pwm, &c, 0.13f * v);
        else render_found(&b, n, c_hz, c_pwm, &c, 0.13f * v);
        for (int i = 0; i < n; i++) mix[i] += b.x[i] * 0.22f;
        free(b.x);
    }
    /* RBJ low-pass, 2.4 kHz, Q 0.7 */
    float w = 6.2831853f * 2400.0f / SR, al = sinf(w) / 1.4f, cw = cosf(w), a0 = 1 + al;
    float b0 = (1 - cw) / 2 / a0, b1 = (1 - cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    int s = out->n;
    for (int i = 0; i < n; i++) {
        float y = b0 * mix[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = mix[i]; y2 = y1; y1 = y;
        float a = i < SR * 1.2f ? (float)i / (SR * 1.2f) : 1.0f;      /* a slow string attack */
        put(out, y * a);
    }
    fade(out, s);
    free(mix);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: render_vco_ab OUT_DIR [TONES_DIR]\n"); return 1; }
    const char *dir = argv[1];
    if (!wavetable_bank_attach(foundations_image_host, foundations_image_host_len)) {
        fprintf(stderr, "Foundations did not attach\n"); return 1;
    }
    match_gain();

    /* 1. every wave, held: Foundations then VCO */
    static const struct { float hz; const char *tag; } P[3] = { { 55, "A1" }, { 110, "A2" }, { 440, "A4" } };
    for (int p = 0; p < 3; p++) {
        Buf b = {0};
        for (int f = 0; f < 8; f++) { Ctx c = { P[p].hz, (float)f }; pair(&b, SR * 3 / 2, c_hz, c_morph, &c, 0.25f); }
        char name[64]; snprintf(name, sizeof name, "01_waves_%s", P[p].tag); wav(dir, name, &b); free(b.x);
    }
    /* 2. PWM from an LFO, SQUARE .. PULSE MIN: crossfaded frames against a moving edge */
    static const struct { float hz; const char *tag; } Q[3] = { { 55, "A1" }, { 110, "A2" }, { 220, "A3" } };
    for (int p = 0; p < 3; p++) {
        Buf b = {0}; Ctx c = { Q[p].hz, 0, 0.4f, 0 };
        pair(&b, SR * 8, c_hz, c_pwm, &c, 0.4f);
        char name[64]; snprintf(name, sizeof name, "02_pwm_lfo_%s", Q[p].tag); wav(dir, name, &b); free(b.x);
    }
    /* 3. the whole SCAN, 10 s */
    { Buf b = {0}; Ctx c = { 110, 0, 0, 0, 10 }; pair(&b, SR * 10, c_hz, c_sweep, &c, 0.4f); wav(dir, "03_scan_sweep_A2", &b); free(b.x); }
    /* 4. high notes: SAW then PULSE 10 up to C7, where the mip levels and the BLEP differ most */
    {
        Buf b = {0};
        static const float wv[2] = { 1, 5 };
        for (int w = 0; w < 2; w++)
            for (int a = 0; a < 2; a++) {
                int s = b.n;
                for (int note = 72; note <= 96; note += 3) {
                    Ctx c = { 440.0f * powf(2.0f, (note - 69) / 12.0f), wv[w] };
                    if (a) render_vco(&b, SR / 4, c_hz, c_morph, &c, 0.0f);
                    else render_found(&b, SR / 4, c_hz, c_morph, &c, 0.0f);
                }
                fade(&b, s); gap(&b, a ? 0.8f : 0.4f);
            }
        wav(dir, "04_high_run_saw_then_pulse10", &b); free(b.x);
    }
    /* 5. the PWM string chord */
    { Buf b = {0}; strings(&b, 0); gap(&b, 0.6f); strings(&b, 1); wav(dir, "05_pwm_strings_Cm7", &b); free(b.x); }

    if (argc > 2) {                          /* single tones for the spectrum checks */
        static const float hz[3] = { 110, 880, 2093 };
        for (int h = 0; h < 3; h++)
            for (int f = 0; f < 8; f++)
                for (int a = 0; a < 2; a++) {
                    Buf b = {0}; Ctx c = { hz[h], (float)f };
                    if (a) render_vco(&b, SR, c_hz, c_morph, &c, 0.0f); else render_found(&b, SR, c_hz, c_morph, &c, 0.0f);
                    char name[64]; snprintf(name, sizeof name, "%s_%d_%d", a ? "A" : "F", (int)hz[h], f);
                    wav(argv[2], name, &b); free(b.x);
                }
    }
    printf("host time per sample: Foundations %.1f ns, VCO %.1f ns (x86, a rough guide only)\n",
           ns_found / n_found, ns_vco / n_vco);
    printf("wrote %s\n", dir);
    return 0;
}
