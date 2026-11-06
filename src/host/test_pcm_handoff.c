/* Host test of the PCM HANDOFF engine (2026-11-05: whole-note waves, JD-800 style; pcm_osc.c), on a small store image
 * built here, so it does not depend on the wave images:
 *   1 the tails: on a hit (amp SUSTAIN 0) a loop with tail_a decays that many dB/s faster than the envelope alone
 *     (B, tail 0, the same envelope); a held note (SUSTAIN > 0) keeps its ring: no tail (2026-11-05)
 *   2 both attacks: at SCAN 0 only A's attack plays, at 127 only B's, between them both (two whole notes crossfaded)
 *   3 fast repeats: a voice retaken while it sounds is silent under its new attack (the loops fade over one tick and
 *     come back where the attack ends), not the old loops under a new strike (the "doubling")
 *   4 (beside them, the same day's change for retiring Foundations) SUB CURRENT on the VCO follows the VCO's own
 *     shape, with no Foundations image attached: not the sine bank 0's empty frames give
 * Every sample: square waves at 44100 Hz with root 60, played at note 60 (one sample a sample). Exit status: the
 * number of failed checks. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/pcm_store.h"
#include "dsp/pcm_osc.h"
#include "dsp/wavetables.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#define RATE 44100u
#define ATK 4410u                   /* 0.1 s attacks */
#define LOOP 4416u                  /* a whole number of 32-sample periods */

typedef struct { const char *name; uint8_t kind; uint32_t len; uint32_t period; int8_t amp; } Smp;

static uint8_t img[64 * 1024] __attribute__((aligned(4)));

/* the image: a HANDOFF wave, its loops A and B, its attacks A and B */
static uint32_t build(const Smp *la, const Smp *lb, const Smp *ka, const Smp *kb, uint8_t tail_a, uint8_t tail_b)
{
    const Smp *s[4] = {la, lb, ka, kb};
    uint32_t count = 5, off = PCM_HDR_SIZE + PCM_ENTRY_SIZE * count;
    memset(img, 0, sizeof img);
    PcmEntry *e = (PcmEntry *)(img + PCM_HDR_SIZE);
    memcpy(e[0].name, "HAND TEST", 9);
    e[0].kind = PCM_WAVE;
    e[0].u.w.loop_a = PCM_REF(0, 1); e[0].u.w.loop_b = PCM_REF(0, 2);
    e[0].u.w.atk_ref = PCM_REF(0, 3); e[0].u.w.atk_fw = PCM_REF_NONE; e[0].u.w.atk_ref_b = PCM_REF(0, 4);
    e[0].u.w.atk_level = 127; e[0].u.w.b_rate_q12 = 4096;
    e[0].u.w.flags = PCM_WF_HANDOFF; e[0].u.w.tail_a = tail_a; e[0].u.w.tail_b = tail_b;
    for (int i = 0; i < 4; i++) {
        PcmEntry *x = &e[1 + i];
        strncpy(x->name, s[i]->name, PCM_NAME_LEN);
        x->kind = s[i]->kind; x->root = 60;
        x->u.s.rate = RATE; x->u.s.off = off; x->u.s.len = s[i]->len;
        for (uint32_t k = 0; k < s[i]->len; k++)
            img[off + k] = (uint8_t)(int8_t)((k % s[i]->period) < s[i]->period / 2 ? s[i]->amp : -s[i]->amp);
        off += s[i]->len;
        if (s[i]->kind == PCM_LOOP) { img[off] = img[x->u.s.off]; off++; }   /* the guard byte */
    }
    PcmHdr *h = (PcmHdr *)img;
    h->magic = PCM_MAGIC; h->version = PCM_VERSION; h->count = (uint16_t)count; h->len = off;
    h->crc = wavetable_crc32(0, img + PCM_HDR_SIZE, off - PCM_HDR_SIZE);
    h->hcrc = wavetable_crc32(0, img, PCM_HDR_SIZE - 4u);
    return off;
}

static float out[3 * RATE];         /* mono */
static uint32_t frames;

static void run(double sec)
{
    static int32_t buf[2 * 64];
    uint32_t n = (uint32_t)(sec * RATE);
    while (n) {
        uint32_t k = n < 64u ? n : 64u;
        synth_render_q15(buf, k);
        for (uint32_t i = 0; i < k && frames < sizeof out / sizeof out[0]; i++)
            out[frames++] = 0.5f * (float)(buf[2 * i] + buf[2 * i + 1]);
        n -= k;
    }
}

static void patch(uint8_t scan)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    engine_defaults(e);
    e->arch = VARCH_SPREAD;
    e->oscmix.spread = 0;
    e->osc[0].wavebank = WAVE_PCM_FIRST;   /* area 0's first wave: ours */
    e->osc[0].scan = scan;
    e->filter[0].cutoff = 127; e->filter[0].resonance = 0; e->filter[0].mod_amt = 0;
    e->env[0].attack = 0; e->env[0].decay = 64; e->env[0].sustain = 127; e->env[0].release = 60;
    e->analog = 0;
    e->amp.delay_send = 0; e->amp.reverb_send = 0;
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    perf_state.voice_mode[0] = VMODE_POLY_REUSE;
    synth_restart();
    run(0.05);
    frames = 0;
}

static double rms(double t0, double t1)
{
    uint32_t a = (uint32_t)(t0 * RATE), b = (uint32_t)(t1 * RATE);
    double s = 0;
    for (uint32_t i = a; i < b && i < frames; i++) s += (double)out[i] * out[i];
    return sqrt(s / (b - a)) + 1e-9;
}

/* the power at f Hz over [t0, t1) s (Goertzel) */
static double power(double f, double t0, double t1)
{
    uint32_t a = (uint32_t)(t0 * RATE), b = (uint32_t)(t1 * RATE);
    double c = 2.0 * cos(2.0 * M_PI * f / RATE), s1 = 0, s2 = 0;
    for (uint32_t i = a; i < b && i < frames; i++) {
        double s0 = out[i] + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)(b - a) * (b - a));
}

int main(void)
{
    synth_init();
    const Smp sq_loop = {"LOOP", PCM_LOOP, LOOP, 32, 100}, quiet_loop = {"QUIET", PCM_LOOP, LOOP, 32, 0},
              quiet_atk = {"QUIET K", PCM_ONESHOT, ATK, 32, 0},
              hi_atk = {"HI K", PCM_ONESHOT, ATK, 8, 100}, lo_atk = {"LO K", PCM_ONESHOT, ATK, 128, 100};

    /* 1 the tails: A decays 40 dB/s after its attack, B (tail 0) only as the envelope does */
    uint32_t len = build(&sq_loop, &sq_loop, &quiet_atk, &quiet_atk, 40, 0);
    CHECK(pcm_store_attach(0, img, len) > 0, "the tail image attaches");
    double dd[2][2];
    for (int held = 0; held < 2; held++)
        for (int side = 0; side < 2; side++) {
            patch(side ? 127 : 0);
            engine_state[0].env[0].sustain = held ? 127 : 0;
            engine_state[0].env[0].decay = 127;          /* (a hit: the slowest decay, so the tail stands out) */
            synth_midi_3(0x90, 60, 100);
            run(1.0);
            dd[held][side] = 20.0 * log10(rms(0.8, 0.9) / rms(0.3, 0.4));
            CHECK(rms(0.3, 0.4) > 300.0, "the loop sounds after the attack (%.0f)", rms(0.3, 0.4));
            synth_midi_3(0x80, 60, 0);
            run(1.5);
        }
    CHECK(fabs(dd[0][0] - dd[0][1] + 20.0) < 3.0, "a hit: tail 40 dB/s adds %.1f dB over 0.5 s (want -20)",
          dd[0][0] - dd[0][1]);
    CHECK(fabs(dd[1][0]) < 1.5 && fabs(dd[1][1]) < 1.5, "held: the ring holds (%.1f, %.1f dB over 0.5 s)",
          dd[1][0], dd[1][1]);

    /* 2 both attacks: A's at 5512 Hz, B's at 345 Hz, the loops silent */
    len = build(&quiet_loop, &quiet_loop, &hi_atk, &lo_atk, 0, 0);
    CHECK(pcm_store_attach(0, img, len) > 0, "the attack image attaches");
    double pa[3], pb[3];
    static const uint8_t SC[3] = {0, 64, 127};
    for (int s = 0; s < 3; s++) {
        patch(SC[s]);
        synth_midi_3(0x90, 60, 100);
        run(0.2);
        pa[s] = power(RATE / 8.0, 0.005, 0.09);
        pb[s] = power(RATE / 128.0, 0.005, 0.09);
        CHECK(rms(0.12, 0.19) < 50.0, "SCAN %u: silent after the attacks (%.0f)", SC[s], rms(0.12, 0.19));
        synth_midi_3(0x80, 60, 0);
        run(1.5);
    }
    CHECK(pa[0] > 1e4 && pb[2] > 1e4, "the attacks sound (A %.3g at SCAN 0, B %.3g at 127)", pa[0], pb[2]);
    CHECK(pb[0] < 0.01 * pb[2], "SCAN 0: B's attack is out (%.3g of %.3g)", pb[0], pb[2]);
    CHECK(pa[2] < 0.01 * pa[0], "SCAN 127: A's attack is out (%.3g of %.3g)", pa[2], pa[0]);
    CHECK(pa[1] > 0.1 * pa[0] && pb[1] > 0.1 * pb[2], "SCAN 64: both attacks (A %.2f, B %.2f of alone)",
          pa[1] / pa[0], pb[1] / pb[2]);

    /* 3 fast repeats: the attack silent, the loop a square; retaken at 0.5 s, the loops fade under the new attack */
    len = build(&sq_loop, &sq_loop, &quiet_atk, &quiet_atk, 0, 0);
    CHECK(pcm_store_attach(0, img, len) > 0, "the repeat image attaches");
    patch(0);
    synth_midi_3(0x90, 60, 100);
    run(0.5);
    synth_midi_3(0x80, 60, 0);
    run(0.02);
    synth_midi_3(0x90, 60, 100);           /* retaken: the voice still sounds in its release */
    run(0.3);
    double held = rms(0.3, 0.45), under = rms(0.53, 0.61), back = rms(0.66, 0.75);
    CHECK(under < 0.05 * held, "retaken: the loops under the new attack at %.3f of the held level", under / held);
    CHECK(back > 0.5 * held, "retaken: the loops back after the attack (%.2f of the held level)", back / held);

    /* 4 SUB CURRENT on the VCO (bank 0, no wavetable area attached): against SUB SINE, a saw an octave down */
    double e[2] = {0, 0}, d = 0;
    static float cur[RATE / 2];
    for (int w = 0; w < 2; w++) {
        patch(0);
        EngineState *es = &engine_state[0];
        es->osc[0].wavebank = 0; es->osc[0].scan = 18;          /* the VCO at SAW */
        es->oscmix.sub_level = 127; es->oscmix.sub_waveform = w ? SUBWAVE_SINE : SUBWAVE_CURRENT;
        synth_restart();
        run(0.05);
        frames = 0;
        synth_midi_3(0x90, 48, 100);
        run(0.5);
        synth_midi_3(0x80, 48, 0);
        run(1.5);
        for (uint32_t i = 0; i < RATE / 2; i++) {
            if (!w) cur[i] = out[i];
            else d += (double)(cur[i] - out[i]) * (cur[i] - out[i]);
            e[w] += (double)out[i] * out[i];
        }
    }
    CHECK(e[0] > 1e9 && e[1] > 1e9, "the VCO with its sub sounds (%.3g, %.3g)", e[0], e[1]);
    CHECK(d > 0.02 * e[1], "SUB CURRENT on the VCO differs from SUB SINE (%.4f of its energy)", d / e[1]);

    pcm_store_detach(0);
    printf(failures ? "test_pcm_handoff: %d failed\n" : "PCM handoff: tails, both attacks, fast repeats, the VCO's SUB CURRENT all hold\n",
           failures);
    return failures;
}
