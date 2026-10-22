/* Host test of the oscillators' PCM waves (2026-10-21: src/dsp/pcm_osc.c) with the store images gen_pcm_waves.py makes
 * attached as the FM1 attaches its flash:
 *   1 the wave list: banks, the store's waves, the attacks; position <-> code round trips; names
 *   2 the block render and the one-sample render (DUAL OSC's path) give the same samples, through attacks, SCAN moves
 *     and the attack's end, for every wave
 *   3 every PCM wave and an attack-wave sound in every architecture, finite, with nothing stuck after the release
 *   4 a store wave whose area is gone plays bank 0 (the synth still sounds)
 *   5 the host cost of 8 voices on a PCM wave against the same on a bank (the FM1's own figure: PERF TEST)
 *   test_pcm_osc [wav dir]      also writes engine renders of each wave to <wav dir> (the real voice: filter, envelopes)
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/pcm_store.h"
#include "dsp/pcm_osc.h"
#include "dsp/oscillators.h"
#include "dsp/wavetables.h"
#include "dsp/attacks.h"

extern const uint8_t pcm_image_host0[], pcm_image_host1[];
extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void attach(void)
{
    CHECK(pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0) > 0, "area 0 attaches");
    CHECK(pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1) > 0, "area 1 attaches");
}

static uint8_t code_of(const char *name)
{
    char buf[13];
    for (uint32_t p = 0; p < wave_list_count(); p++)
        if (!strcmp(wave_name(wave_list_code(p), buf), name))
            return wave_list_code(p);
    return 0;
}

#define RATE 44100u
static int32_t buf[2 * 128];

/* frames of the engine; returns the peak and sum of squares, and whether every sample was finite and in range */
static double render(uint32_t frames, int32_t *peak, FILE *wav)
{
    double e = 0;
    while (frames) {
        uint32_t n = frames < 128u ? frames : 128u;
        synth_render_q15(buf, n);
        for (uint32_t i = 0; i < 2 * n; i++) {
            int32_t v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > *peak) *peak = v;
            e += (double)buf[i] * buf[i];
            if (wav) {
                int32_t s = buf[i] > 32767 ? 32767 : buf[i] < -32768 ? -32768 : buf[i];
                fputc(s & 255, wav); fputc((s >> 8) & 255, wav);
            }
        }
        frames -= n;
    }
    return e;
}

static void put_u32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 255, f); }
static FILE *wav_open(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/engine_%s.wav", dir, name);
    for (char *c = path + strlen(dir) + 1; *c; c++) if (*c == ' ' || *c == '+') *c = '_';
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
    put_u32(f, 16); put_u32(f, 1 | 2u << 16); put_u32(f, RATE); put_u32(f, RATE * 4); put_u32(f, 4 | 16u << 16);
    fwrite("data\0\0\0\0", 1, 8, f);
    return f;
}
static void wav_close(FILE *f)
{
    long n = ftell(f);
    fseek(f, 4, SEEK_SET); put_u32(f, (uint32_t)(n - 8));
    fseek(f, 40, SEEK_SET); put_u32(f, (uint32_t)(n - 44));
    fclose(f);
}

static void patch(uint8_t arch, uint8_t code, uint8_t scan)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = arch;
    e->osc[0].wavebank = e->osc[1].wavebank = code;
    e->osc[0].scan = e->osc[1].scan = scan;
    e->filter[0].cutoff = e->filter[1].cutoff = 110;
    e->env[0].attack = 0; e->env[0].sustain = 127; e->env[0].release = 30;
    e->amp.reverb_send = 0;
    synth_restart();
}

/* the same PcmOsc rendered by blocks and sample by sample, ticks in between, the SCAN moving: identical samples */
static void same_paths(uint8_t code, const char *name, int spread)
{
    static WavetableOsc a, b;
    int32_t ob[16], one;
    int diff = 0, sounded = 0;
    osc_init(&a);
    osc_select_wave(&a, code);
    a.pcm.trig = 1;
    b = a;
    for (int tick = 0; tick < 400; tick++) {
        uint32_t f = mtof_q16(48 + (tick / 100) * 7);
        a.morph_q12 = b.morph_q12 = (tick * 97) % (7 << 12);
        int sp = spread ? spread - 20 + (tick % 40) : 0;     /* across the low-spread crossfade and above it */
        osc_prepare(&a, f, sp);
        osc_prepare(&b, f, sp);
        osc_render_block(&a, ob, 16);
        for (int i = 0; i < 16; i++) {
            one = pcm_render_one(&b.pcm);         /* (osc_render plays banks only: the per-sample path) */
            diff += ob[i] != (int16_t)one;
            sounded |= ob[i] != 0;
        }
        if (tick == 200) a.pcm.trig = b.pcm.trig = 1;      /* a second note: the attack again */
    }
    CHECK(!diff, "%s: block and one-sample renders differ in %d samples", name, diff);
    CHECK(sounded, "%s: silent", name);
}

/* SPREAD on a PCM wave: the four detuned playheads change the sound (not the one playhead as at spread 0), at a
 * like level */
static void spread_detunes(uint8_t code, const char *name)
{
    static WavetableOsc a, b;
    int32_t oa[16], ob[16];
    double ea = 0, eb = 0, ed = 0;
    osc_init(&a);
    osc_select_wave(&a, code);
    b = a;
    for (int tick = 0; tick < 600; tick++) {
        osc_prepare(&a, mtof_q16(57), 0);
        osc_prepare(&b, mtof_q16(57), 90);
        osc_render_block(&a, oa, 16);
        osc_render_block(&b, ob, 16);
        if (tick < 100) continue;                           /* the playheads drift apart first */
        for (int i = 0; i < 16; i++) {
            ea += (double)oa[i] * oa[i];
            eb += (double)ob[i] * ob[i];
            ed += (double)(ob[i] - oa[i]) * (ob[i] - oa[i]);
        }
    }
    CHECK(ed > 0.2 * ea, "%s: SPREAD 90 barely changes the sound (diff %.3g of %.3g)", name, ed, ea);
    CHECK(eb > 0.1 * ea && eb < 1.5 * ea, "%s: SPREAD 90 level %.3g against %.3g", name, eb, ea);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    char nb[13];
    synth_init();
    attach();

    /* 1 the list */
    uint32_t nw = pcm_wave_count();
    CHECK(nw >= 7, "the store holds the waves (%u)", (unsigned)nw);
    CHECK(wave_list_count() == NUM_BANKS + nw + ATK_COUNT, "list: %u = banks + waves + attacks",
          (unsigned)wave_list_count());
    for (uint32_t p = 0; p < wave_list_count(); p++)
        CHECK(wave_list_pos(wave_list_code(p)) == p, "list position %u round-trips", (unsigned)p);
    CHECK(wave_list_code(NUM_BANKS) == WAVE_PCM_FIRST && wave_list_code(NUM_BANKS + nw) == WAVE_ATK_FIRST,
          "PCM waves after the banks, then the attacks");
    CHECK(!strcmp(wave_name(WAVE_PCM_FIRST, nb), "WOOD CHIMES") && !strcmp(wave_name(WAVE_ATK_FIRST, nb), "BONE CHIFF"),
          "names: %s, %s", wave_name(WAVE_PCM_FIRST, nb), wave_name(WAVE_ATK_FIRST, nb));
    CHECK(wave_kind(5) == 0 && wave_kind(WAVE_ATK_FIRST + ATK_COUNT) == -1 && wave_kind((uint8_t)(WAVE_PCM_FIRST + nw)) == -1,
          "kinds: a bank, past the attacks, past the waves");

    /* 2 block = one sample */
    for (uint32_t w = 0; w < nw; w++) {
        same_paths((uint8_t)(WAVE_PCM_FIRST + w), wave_name((uint8_t)(WAVE_PCM_FIRST + w), nb), 0);
        same_paths((uint8_t)(WAVE_PCM_FIRST + w), wave_name((uint8_t)(WAVE_PCM_FIRST + w), nb), 40);
        same_paths((uint8_t)(WAVE_PCM_FIRST + w), wave_name((uint8_t)(WAVE_PCM_FIRST + w), nb), 110);
        spread_detunes((uint8_t)(WAVE_PCM_FIRST + w), wave_name((uint8_t)(WAVE_PCM_FIRST + w), nb));
    }
    same_paths(WAVE_ATK_FIRST + 2, "HORN BLAT (an attack-wave)", 0);
    same_paths(WAVE_ATK_FIRST + 2, "HORN BLAT (an attack-wave)", 80);

    /* 3 every wave in every architecture */
    for (uint8_t arch = 0; arch < VARCH_COUNT; arch++) {
        for (uint32_t w = 0; w <= nw; w++) {
            uint8_t code = w < nw ? (uint8_t)(WAVE_PCM_FIRST + w) : (uint8_t)(WAVE_ATK_FIRST + 6);   /* + LITHOPHONE */
            int32_t peak = 0, tail_peak = 0;
            patch(arch, code, 64);
            for (int k = 0; k < 3; k++) synth_midi_3(0x90, (uint8_t)(48 + 7 * k), 100);
            double e = render(RATE / 2, &peak, NULL);
            for (int k = 0; k < 3; k++) synth_midi_3(0x80, (uint8_t)(48 + 7 * k), 0);
            render(RATE * 3, &tail_peak, NULL);                /* the release, then nothing */
            tail_peak = 0;
            render(RATE / 10, &tail_peak, NULL);
            CHECK(e > 1e9 && peak <= 32768 * 4, "arch %u, %s: energy %.3g, peak %d", arch, wave_name(code, nb), e, peak);
            CHECK(tail_peak < 64, "arch %u, %s: still sounding after the release (%d)", arch, wave_name(code, nb),
                  tail_peak);
        }
    }

    /* 4 an area gone: its waves play bank 0 */
    {
        int32_t peak = 0;
        uint8_t code = code_of("STRINGS");
        pcm_store_detach(0);
        pcm_store_detach(1);
        patch(VARCH_SINGLE, code, 0);
        synth_midi_3(0x90, 60, 100);
        double e = render(RATE / 4, &peak, NULL);
        CHECK(wave_kind(code) == -1 && e > 1e9, "no store: STRINGS (code %u) plays bank 0 (energy %.3g)", code, e);
        attach();
    }

    /* 5 cost: 8 voices on a bank, then on CHOIR at SCAN 64 (two loops read), then WOOD CHIMES struck again and again */
    {
        const char *what[3] = {"bank 0", "CHOIR, SCAN 64", "WOOD CHIMES re-struck"};
        double ns[3];
        for (int t = 0; t < 3; t++) {
            int32_t peak = 0;
            patch(VARCH_SINGLE, t == 0 ? 0 : code_of(t == 1 ? "CHOIR" : "WOOD CHIMES"), 64);
            for (int k = 0; k < 8; k++) synth_midi_3(0x90, (uint8_t)(48 + 3 * k), 100);
            clock_t c0 = clock();
            for (int r = 0; r < 40; r++) {
                if (t == 2)
                    for (int k = 0; k < 8; k++) { synth_midi_3(0x80, (uint8_t)(48 + 3 * k), 0); synth_midi_3(0x90, (uint8_t)(48 + 3 * k), 100); }
                render(RATE / 8, &peak, NULL);
            }
            ns[t] = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e9 / (40.0 * RATE / 8) / 8.0;
        }
        printf("host cost per voice-sample: %s %.1f ns, %s %.1f ns, %s %.1f ns\n", what[0], ns[0], what[1], ns[1],
               what[2], ns[2]);
    }

    /* renders to listen to: the real voice */
    if (dir) {
        static const struct { const char *name; uint8_t arch; uint8_t scan; const uint8_t notes[4]; uint8_t a, r; } R[] = {
            {"WOOD CHIMES", VARCH_SINGLE, 30, {55, 60, 67, 0}, 0, 70},
            {"CLAY CHIMES", VARCH_SINGLE, 30, {55, 60, 67, 0}, 0, 70},
            {"SHELL CHIMES", VARCH_SINGLE, 30, {60, 67, 72, 0}, 0, 70},
            {"STRINGS", VARCH_SINGLE, 64, {48, 55, 60, 64}, 40, 70},
            {"CHOIR", VARCH_SINGLE, 64, {45, 52, 57, 60}, 50, 75},
            {"HORNS", VARCH_SINGLE, 64, {41, 48, 53, 57}, 10, 60},
            {"CHOIR+WOOD", VARCH_SINGLE, 64, {48, 55, 60, 63}, 50, 80},
        };
        for (uint32_t i = 0; i < sizeof R / sizeof R[0]; i++) {
            FILE *f = wav_open(dir, R[i].name);
            int32_t peak = 0;
            if (!f) break;
            patch(R[i].arch, code_of(R[i].name), R[i].scan);
            engine_state[0].env[0].attack = R[i].a;
            engine_state[0].env[0].release = R[i].r;
            engine_state[0].amp.reverb_send = 60;
            for (int k = 0; k < 4; k++) if (R[i].notes[k]) synth_midi_3(0x90, R[i].notes[k], 100);
            render(RATE * 3, &peak, f);
            for (int k = 0; k < 4; k++) if (R[i].notes[k]) synth_midi_3(0x80, R[i].notes[k], 0);
            render(RATE * 2, &peak, f);
            wav_close(f);
        }
        /* Universe: DUAL OSC, CHOIR on OSC1 and WOOD CHIMES on OSC2 */
        FILE *f = wav_open(dir, "DUALOSC CHOIR + WOOD CHIMES");
        if (f) {
            int32_t peak = 0;
            patch(VARCH_DUAL_OSC, code_of("CHOIR"), 64);
            engine_state[0].osc[1].wavebank = code_of("WOOD CHIMES");
            engine_state[0].oscmix.xmod_amt = 0;
            engine_state[0].env[0].attack = 50; engine_state[0].env[0].release = 80;
            engine_state[0].amp.reverb_send = 70;
            for (int k = 0; k < 4; k++) synth_midi_3(0x90, (uint8_t)((const uint8_t[]){48, 55, 60, 63})[k], 100);
            render(RATE * 4, &peak, f);
            for (int k = 0; k < 4; k++) synth_midi_3(0x80, (uint8_t)((const uint8_t[]){48, 55, 60, 63})[k], 0);
            render(RATE * 3, &peak, f);
            wav_close(f);
        }
        printf("wrote engine renders to %s\n", dir);
    }

    printf(failures ? "%d FAILED\n" : "PCM oscillator: list, paths, every architecture, fallback all hold\n", failures);
    return failures;
}
