/* Host test for the voice architectures (spec 4.0): renders each one, checks levels, stereo behaviour, voice limits,
 * layering, unison, the choke fade and the noise colours, and times the render per architecture. Exit code 0 = pass.
 * Test scaffolding only (it uses float and libc for the checks; the synth core it drives is integer-only).
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/test_voice_arch.c -lm -o test_voice_arch */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/voice_allocator.h"
#include "dsp/attacks.h"
#include "dsp/synth_voice.h"

static int32_t buf[64 * 2];
static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef struct { double rms_l, rms_r, rms_side; int32_t peak, max_step; } Stats;

static Stats render(uint32_t frames)
{
    Stats s = {0, 0, 0, 0, 0};
    double sl = 0, sr = 0, ss = 0;
    static int32_t prev_l = 0, prev_r = 0;   /* carried across calls: a block edge is not a jump */
    uint32_t n = 0;
    while (n < frames) {
        synth_render_q15(buf, 64);
        for (int i = 0; i < 64; i++) {
            int32_t l = buf[2 * i], r = buf[2 * i + 1];
            sl += (double)l * l; sr += (double)r * r; ss += (double)(l - r) * (l - r);
            if (abs(l) > s.peak) s.peak = abs(l);
            if (abs(r) > s.peak) s.peak = abs(r);
            if (abs(l - prev_l) > s.max_step) s.max_step = abs(l - prev_l);
            if (abs(r - prev_r) > s.max_step) s.max_step = abs(r - prev_r);
            prev_l = l; prev_r = r;
        }
        n += 64;
    }
    s.rms_l = sqrt(sl / n); s.rms_r = sqrt(sr / n); s.rms_side = sqrt(ss / n);
    return s;
}

static int sounding(int layer)
{
    int c = 0;
    for (int v = 0; v < POLYPHONY_VOICES; v++)
        if (!voice_is_free(&poly_bank[v]) && (layer < 0 || poly_bank[v].layer == layer)) c++;
    return c;
}

static void all_off(void)
{
    for (int n = 0; n < 128; n++) { synth_midi_3(0x80, (uint8_t)n, 0); synth_midi_3(0x81, (uint8_t)n, 0); }
    render(44100);
}

static const char *ARCH[] = {"SINGLE", "DUAL_OSC", "SPREAD", "DUAL_FILTER", "STACK", "BINAURAL", "STRIKE"};

static void base_patch(EngineState *e, uint8_t arch)
{
    e->arch = arch;
    e->env[0].attack = 0; e->env[0].sustain = 100; e->env[0].release = 20;
    e->env[1].attack = 0; e->env[1].sustain = 100; e->env[1].release = 20;
    e->oscmix.spread = 60;
    e->oscmix.xmod_amt = 20;
    e->fltmix.stack_width = 127;
    e->osc[1].wavebank = 3;
    /* LFO1 on the filter cutoff so BINAURAL's phase offset has something to shift */
    e->filter[0].mod_src = MSRC_LFO1;
    e->filter[0].mod_amt = 120;
    e->filter[0].cutoff = 70;
    e->filter[1].cutoff = 90;
    e->lfo[0].rate = 70;
}

/* STRIKE: one note on a STRIKE patch and the same on SINGLE, the left channel of each; their difference is the attack
 * alone (the attack adds to the mix and touches nothing else), so its length shows how fast it was read */
#define STRIKE_N 16384
static int32_t strike_l[STRIKE_N], single_l[STRIKE_N];
static int strike_vel = 60;                 /* the note's velocity */
static void (*strike_tweak)(EngineState *e); /* more settings on the patch, or NULL */
static void strike_take(uint8_t arch, uint8_t sample, uint8_t tune, uint8_t note, int32_t *out)
{
    synth_init();
    EngineState *e = &engine_state[0];
    base_patch(e, arch);
    e->env[0].attack = 100;                 /* a slow pad: the attack must not wait for it */
    e->amp.delay_send = e->amp.reverb_send = 0;
    e->oscmix.sub_level = e->oscmix.noise_level = 0;
    e->oscmix.atk_sample = sample;
    e->oscmix.atk_level = 60;
    e->oscmix.atk_tune = tune;
    if (strike_tweak) strike_tweak(e);
    synth_midi_3(0x90, note, (uint8_t)strike_vel);
    for (int n = 0; n < STRIKE_N; n += 64) {
        synth_render_q15(buf, 64);
        for (int i = 0; i < 64; i++) out[n + i] = buf[2 * i];
    }
}
/* samples until the last difference over 1 LSB; *early: the difference's rms over the first 20 ms */
static int strike_len(uint8_t sample, uint8_t tune, uint8_t note, double *early)
{
    strike_take(VARCH_STRIKE, sample, tune, note, strike_l);
    strike_take(VARCH_SINGLE, sample, tune, note, single_l);
    int last = -1;
    double e2 = 0;
    for (int i = 0; i < STRIKE_N; i++) {
        int32_t d = strike_l[i] - single_l[i];
        if (abs(d) > 1) last = i;
        if (i < 882) e2 += (double)d * d;
    }
    if (early) *early = sqrt(e2 / 882);
    return last + 1;
}

/* the rms of STRIKE minus SINGLE (the attack alone) over samples [from, to) of the last strike_len / strike_diff */
static double strike_rms(int from, int to)
{
    double e2 = 0;
    for (int i = from; i < to; i++) {
        double d = (double)(strike_l[i] - single_l[i]);
        e2 += d * d;
    }
    return sqrt(e2 / (to - from));
}
static void strike_diff(uint8_t sample, uint8_t note)
{
    strike_take(VARCH_STRIKE, sample, 64, note, strike_l);
    strike_take(VARCH_SINGLE, sample, 64, note, single_l);
}
static void tw_decay(EngineState *e) { e->oscmix.atk_decay = 20; }   /* a ~10 ms time constant */
static void tw_start(EngineState *e) { e->oscmix.atk_start = 127; }
static void tw_vel0(EngineState *e) { e->oscmix.atk_vel = 0; }
static void tw_fixed(EngineState *e) { e->oscmix.atk_track = ATKTRK_FIXED; }
static void tw_track(EngineState *e) { e->oscmix.atk_track = ATKTRK_TRACK; }
static void tw_dark(EngineState *e) { e->env[0].attack = 0; e->filter[0].cutoff = 15; e->filter[0].mod_amt = 64; }
static void tw_dark_filt(EngineState *e) { tw_dark(e); e->oscmix.atk_filter = 127; }
static void tw_slow_filt(EngineState *e) { e->oscmix.atk_filter = 127; }
static void tw_bend(EngineState *e) { e->oscmix.atk_bend = 64 + 24; }   /* +12 semitones */

/* the attack's read step (Q16) the voice playing a STRIKE note was given */
static uint32_t strike_step(uint8_t sample, uint8_t tune, uint8_t note)
{
    strike_take(VARCH_STRIKE, sample, tune, note, strike_l);
    for (int v = 0; v < POLYPHONY_VOICES; v++)
        if (poly_bank[v].is_gate_open) return poly_bank[v].atk_step;
    return 0;
}

int main(void)
{
    /* 1. each architecture, 8 notes held: voices <= cap, sound present, stereo where expected; CPU time */
    printf("%-12s %4s %4s %8s %8s %8s %8s %10s\n", "arch", "cap", "vox", "rmsL", "rmsR", "side", "peak", "ns/sample");
    double base_ns = 0;
    for (uint8_t a = 0; a < VARCH_COUNT; a++) {
        synth_init();
        base_patch(&engine_state[0], a);
        for (int i = 0; i < 8; i++) synth_midi_3(0x90, (uint8_t)(48 + i * 3), 100);
        render(2048);
        int vox = sounding(0);
        uint8_t cap = varch_voice_cap(&engine_state[0]);
        clock_t t0 = clock();
        Stats s = render(44100 * 4);
        double ns = (double)(clock() - t0) * 1e9 / CLOCKS_PER_SEC / (44100.0 * 4);
        if (a == 0) base_ns = ns;
        printf("%-12s %4u %4d %8.0f %8.0f %8.0f %8d %7.0f (%.2fx per voice vs SINGLE)\n", ARCH[a], cap, vox,
               s.rms_l, s.rms_r, s.rms_side, s.peak, ns, (ns / vox) / (base_ns / 8));
        CHECK(vox == cap, "%s: %d voices sounding, cap %u", ARCH[a], vox, cap);
        CHECK(s.rms_l > 300 && s.rms_r > 300, "%s: too quiet", ARCH[a]);
        CHECK(s.peak <= 32767, "%s: over full scale", ARCH[a]);
        if (a == VARCH_STACK || a == VARCH_BINAURAL)
            CHECK(s.rms_side > 0.2 * s.rms_l, "%s: no stereo difference (side %.0f)", ARCH[a], s.rms_side);
        else
            CHECK(s.rms_side < 0.05 * s.rms_l + 1, "%s: unexpected stereo difference", ARCH[a]);
        all_off();
        CHECK(sounding(-1) == 0, "%s: voices still sounding after release", ARCH[a]);
    }

    /* 2. BINAURAL width 0 sums the ears: no side signal */
    synth_init();
    base_patch(&engine_state[0], VARCH_BINAURAL);
    engine_state[0].fltmix.bin_width = 0;
    synth_midi_3(0x90, 60, 100);
    render(2048);
    {
        Stats s = render(44100);
        CHECK(s.rms_side < 0.02 * s.rms_l + 2, "BINAURAL width 0 still has side %.0f vs %.0f", s.rms_side, s.rms_l);
    }

    /* 3. STACK: ENV2 alone keeps stack B sounding after ENV1 has finished */
    synth_init();
    base_patch(&engine_state[0], VARCH_STACK);
    engine_state[0].env[0].release = 0;
    engine_state[0].env[1].release = 90;
    synth_midi_3(0x90, 60, 100);
    render(4410);
    synth_midi_3(0x80, 60, 0);
    render(4410);
    CHECK(engine_state[0].env[0].release == 0 && poly_bank[0].amp_env.level_q15 == 0, "STACK: ENV1 still up");
    CHECK(sounding(0) == 1, "STACK: B path stopped with ENV1 (sounding %d)", sounding(0));
    {
        Stats s = render(4410);
        CHECK(s.rms_l + s.rms_r > 50, "STACK: B release silent");
    }

    /* 4. layering: DUAL with SINGLE (cap 8) + STACK (cap 6): each layer gets min / 2 = 3 */
    synth_init();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    base_patch(&engine_state[0], VARCH_SINGLE);
    base_patch(&engine_state[1], VARCH_STACK);
    for (int i = 0; i < 6; i++) { synth_midi_3(0x90, (uint8_t)(48 + i), 100); synth_midi_3(0x91, (uint8_t)(60 + i), 100); }
    render(2048);
    CHECK(sounding(0) == 3 && sounding(1) == 3, "DUAL SINGLE+STACK: layer voices %d / %d, want 3 / 3", sounding(0), sounding(1));
    CHECK(layer_voice_limit(PERF_DUAL_SYNTH, &engine_state[0], &engine_state[1], 0) == 3, "limit");
    /* both SINGLE: the spec's 4 + 4 */
    all_off();
    engine_state[1].arch = VARCH_SINGLE;
    for (int i = 0; i < 6; i++) { synth_midi_3(0x90, (uint8_t)(48 + i), 100); synth_midi_3(0x91, (uint8_t)(60 + i), 100); }
    render(2048);
    CHECK(sounding(0) == 4 && sounding(1) == 4, "DUAL SINGLE+SINGLE: %d / %d, want 4 / 4", sounding(0), sounding(1));

    /* 5. choke: lower the limit under held notes (SINGLE -> STACK), the extra voices fade without a click */
    synth_init();
    base_patch(&engine_state[0], VARCH_SINGLE);
    engine_state[0].filter[0].mod_amt = BIPOLAR_CENTER;
    for (int i = 0; i < 8; i++) synth_midi_3(0x90, (uint8_t)(48 + i * 2), 100);
    render(4096);
    {
        Stats before = render(4096);
        engine_state[0].arch = VARCH_STACK;           /* architecture change: every voice of the layer chokes */
        Stats during = render(512);
        CHECK(during.max_step < before.max_step * 2 + 2000, "choke click: step %d vs %d", during.max_step, before.max_step);
        render(512);
        CHECK(sounding(0) == 0, "choke: %d voices left after the fade", sounding(0));
        for (int i = 0; i < 8; i++) synth_midi_3(0x80, (uint8_t)(48 + i * 2), 0);
        for (int i = 0; i < 8; i++) synth_midi_3(0x90, (uint8_t)(48 + i * 2), 100);
        render(2048);
        CHECK(sounding(0) == 6, "after the change STACK plays %d voices, want 6", sounding(0));
    }
    /* formant keeps the cap (measured no dearer): STACK stays at 6 */
    engine_state[0].filter[1].flavour = FLT_FORMANT;
    render(2048);
    CHECK(sounding(0) == 6, "STACK + FORMANT: %d voices, want 6", sounding(0));

    /* 6. unison: stack size = limit, detune spreads symmetrically, level close to one voice */
    synth_init();
    base_patch(&engine_state[0], VARCH_SINGLE);
    engine_state[0].filter[0].mod_amt = BIPOLAR_CENTER;
    synth_midi_3(0x90, 60, 100);
    render(2048);
    Stats one = render(44100);
    all_off();
    perf_state.voice_mode[0] = VMODE_UNISON_STACK;
    perf_state.unison_detune[0] = 127;
    synth_midi_3(0x90, 60, 100);
    render(2048);
    CHECK(sounding(0) == 8, "UNISON stacked %d voices, want 8", sounding(0));
    CHECK(poly_bank[0].detune_cents == -50 && poly_bank[7].detune_cents == 50, "UNISON detune %d .. %d",
          poly_bank[0].detune_cents, poly_bank[7].detune_cents);
    Stats uni = render(44100);
    printf("unison 8 @127: rms %.0f vs one voice %.0f (%.1f dB)\n", uni.rms_l, one.rms_l, 20 * log10(uni.rms_l / one.rms_l));
    CHECK(uni.rms_l < one.rms_l * 2.0 && uni.rms_l > one.rms_l * 0.5, "UNISON level off");
    all_off();
    perf_state.unison_detune[0] = 0;
    synth_midi_3(0x90, 60, 100);
    render(256);
    for (int v = 0; v < 8; v++) CHECK(poly_bank[v].detune_cents == 0, "UNISON detune 0 not 0");
    /* stereo spread: the pan offsets are a symmetric set out to +-UNISON_PAN_OUTER, every voice on its own spot */
    {
        int sum = 0, lo = 0, hi = 0, distinct = 1;
        for (int v = 0; v < 8; v++) {
            int p = poly_bank[v].unison_pan;
            sum += p;
            if (p < lo) lo = p;
            if (p > hi) hi = p;
            for (int w = 0; w < v; w++) if (poly_bank[w].unison_pan == p) distinct = 0;
        }
        CHECK(sum == 0 && lo == -UNISON_PAN_OUTER && hi == UNISON_PAN_OUTER && distinct,
              "UNISON pan offsets: sum %d, %d .. %d, distinct %d", sum, lo, hi, distinct);
        CHECK(one.rms_side < one.rms_l * 0.05, "one voice should be about centred (pan 64 is half a step off) (side %.0f)", one.rms_side);
        CHECK(uni.rms_side > uni.rms_l * 0.1, "UNISON stack has no stereo width (side %.0f vs %.0f)", uni.rms_side,
              uni.rms_l);
    }
    /* random start phases: at detune 0 every voice runs at one pitch, so a stack started from silence must still
     * hold different phases; a retrigger while it sounds must keep them (no jump, no click) */
    {
        uint32_t d0[8];
        int distinct = 1, kept = 1;
        for (int v = 0; v < 8; v++) d0[v] = poly_bank[v].osc1.center_phase - poly_bank[0].osc1.center_phase;
        for (int v = 1; v < 8; v++)
            for (int w = 0; w < v; w++)
                if (d0[v] == d0[w]) distinct = 0;
        CHECK(distinct, "UNISON voices started in phase");
        synth_midi_3(0x80, 60, 0);
        render(64);                                   /* releasing, still sounding */
        synth_midi_3(0x90, 60, 100);
        render(64);
        for (int v = 0; v < 8; v++)
            if (poly_bank[v].osc1.center_phase - poly_bank[0].osc1.center_phase != d0[v]) kept = 0;
        CHECK(kept, "UNISON retrigger while sounding moved the voices' phases");
    }
    all_off();
    /* poly voices after a unison stack: back to the centre */
    perf_state.voice_mode[0] = VMODE_POLY_RESET;
    synth_midi_3(0x90, 60, 100);
    render(256);
    for (int v = 0; v < 8; v++)
        if (!voice_is_free(&poly_bank[v])) CHECK(poly_bank[v].unison_pan == 0, "poly voice kept a unison pan");
    all_off();
    perf_state.voice_mode[0] = VMODE_UNISON_STACK;
    engine_state[0].arch = VARCH_STACK;               /* unison follows the architecture's limit */
    synth_midi_3(0x90, 60, 100);
    render(2048);
    CHECK(sounding(0) == 6, "UNISON on STACK stacked %d, want 6", sounding(0));

    /* 7. the noise colour morph makes sound and stays in range at its three colours and between them */
    static const uint8_t colours[] = {NOISE_WHITE, 32, NOISE_PINK, 96, NOISE_BROWN};
    for (unsigned ci = 0; ci < sizeof colours; ci++) {
        uint8_t col = colours[ci];
        synth_init();
        base_patch(&engine_state[0], VARCH_SINGLE);
        engine_state[0].filter[0].mod_amt = BIPOLAR_CENTER;
        engine_state[0].filter[0].cutoff = 127;
        engine_state[0].oscmix.balance = 0;
        engine_state[0].oscmix.noise_level = 127;
        engine_state[0].oscmix.noise_colour = col;
        engine_state[0].osc[0].scan = 0;
        synth_midi_3(0x90, 60, 100);
        render(2048);
        Stats s = render(44100);
        printf("noise colour %u: rms %.0f peak %d\n", col, s.rms_l, s.peak);
        CHECK(s.rms_l > 500, "noise colour %u silent", col);
    }

    /* 8. the new mod routes, driven by the mod wheel: the filter type morph (every architecture), the noise colour,
     * BINAURAL's CUTOFF L/R. Each is compared at wheel 0 and wheel 127. */
    {
        Stats lo, hi;
        /* (a) SINGLE, a low LP cutoff: the morph mod opens it towards BP / HP, the level changes */
        synth_init();
        base_patch(&engine_state[0], VARCH_SINGLE);
        engine_state[0].filter[0].mod_amt = BIPOLAR_CENTER;
        engine_state[0].filter[0].cutoff = 40;
        engine_state[0].filter[0].morph = 0;
        engine_state[0].filter[0].morph_mod_src = MSRC_MODWHEEL;
        engine_state[0].filter[0].morph_mod_amt = 127;
        synth_midi_3(0xB0, 1, 0);
        synth_midi_3(0x90, 60, 100);
        render(4096);
        lo = render(22050);
        synth_midi_3(0xB0, 1, 127);
        render(4096);
        hi = render(22050);
        printf("filter morph mod: rms %.0f (wheel 0) -> %.0f (wheel 127)\n", lo.rms_l, hi.rms_l);
        CHECK(fabs(hi.rms_l - lo.rms_l) > 0.1 * lo.rms_l, "filter morph mod made no difference");
        all_off();

        /* (b) SINGLE, white noise at full level: the colour mod darkens it towards pink, the level drops (about 0.7 dB
         * through the open 14 kHz lowpass; ~4 dB unfiltered) */
        synth_init();
        base_patch(&engine_state[0], VARCH_SINGLE);
        engine_state[0].filter[0].mod_amt = BIPOLAR_CENTER;
        engine_state[0].filter[0].cutoff = 127;
        engine_state[0].oscmix.noise_level = 127;
        engine_state[0].oscmix.noise_colour = NOISE_WHITE;
        engine_state[0].oscmix.noise_col_mod_src = MSRC_MODWHEEL;
        engine_state[0].oscmix.noise_col_mod_amt = 127;
        synth_midi_3(0xB0, 1, 0);
        synth_midi_3(0x90, 60, 100);
        render(4096);
        lo = render(22050);
        synth_midi_3(0xB0, 1, 127);
        render(4096);
        hi = render(22050);
        printf("noise colour mod: rms %.0f (wheel 0) -> %.0f (wheel 127)\n", lo.rms_l, hi.rms_l);
        /* 0.95, was 0.85 (2026-10-15): the float TPT lowpass at cutoff 127 (14 kHz) truly rolls off white noise's top
         * octave, which the fixed-point 2x Chamberlin let through, so white reads ~1 dB lower and the gap to pink shrinks.
         * 0.98 (2026-10-15): the SVF is flat (Q 0.707) at resonance 0, no longer lifting white's top octave (measured
         * 0.964, deterministic) */
        CHECK(hi.rms_l < 0.98 * lo.rms_l, "noise colour mod did not darken the noise");
        all_off();

        /* (c) BINAURAL: CUTOFF L/R mod opens one ear and closes the other; with no LFO on the cutoff the ears match
         * at wheel 0 */
        synth_init();
        base_patch(&engine_state[0], VARCH_BINAURAL);
        engine_state[0].filter[0].mod_amt = BIPOLAR_CENTER;
        engine_state[0].filter[0].cutoff = 60;
        engine_state[0].oscmix.bin_beat = 0;
        engine_state[0].fltmix.bin_cutoff_mod_src = MSRC_MODWHEEL;
        engine_state[0].fltmix.bin_cutoff_mod_amt = 127;
        synth_midi_3(0xB0, 1, 0);
        synth_midi_3(0x90, 60, 100);
        render(4096);
        lo = render(22050);
        synth_midi_3(0xB0, 1, 127);
        render(4096);
        hi = render(22050);
        printf("cutoff L/R mod: L/R rms %.0f / %.0f (wheel 0) -> %.0f / %.0f (wheel 127)\n", lo.rms_l, lo.rms_r, hi.rms_l, hi.rms_r);
        CHECK(fabs(lo.rms_l - lo.rms_r) < 0.05 * lo.rms_l, "ears differ with no L/R offset");
        CHECK(hi.rms_r > 1.15 * hi.rms_l, "cutoff L/R mod did not open the right ear over the left");
        all_off();
    }

    /* 8. poly portamento: a new note slides in from the last note played; a chord fans out from the note before it;
     * PORTA TIME 0 is no glide; LEGATO TIED glides only while another key is down */
    {
        synth_init();
        base_patch(&engine_state[0], VARCH_SINGLE);
        static const uint8_t modes[] = {VMODE_POLY_REUSE, VMODE_POLY_RESET};
        for (unsigned mi = 0; mi < sizeof modes; mi++) {
            all_off();
            perf_state.voice_mode[0] = modes[mi];
            perf_state.porta_mode[0] = PORTA_CONST_TIME;
            perf_state.porta_time[0] = 40;
            synth_midi_3(0x90, 60, 100);
            render(256);
            synth_midi_3(0x80, 60, 0);
            render(2048);
            synth_midi_3(0x90, 67, 100);                   /* a fifth up: starts 700 cents below its target */
            int v67 = -1;
            for (int v = 0; v < 8; v++)
                if (poly_bank[v].is_gate_open && poly_bank[v].active_note == 67) v67 = v;
            CHECK(v67 >= 0 && poly_bank[v67].glide_q16 == -700 * 65536, "poly mode %u: glide %d, want -700 cents",
                  modes[mi], v67 >= 0 ? poly_bank[v67].glide_q16 / 65536 : 0);
            render(44100);
            CHECK(v67 >= 0 && poly_bank[v67].glide_q16 == 0, "poly mode %u: glide did not finish", modes[mi]);
            synth_midi_3(0x80, 67, 0);
            render(2048);
            synth_midi_3(0x90, 72, 100);                   /* a chord: both notes from 67 */
            synth_midi_3(0x90, 76, 100);
            int ok = 0;
            for (int v = 0; v < 8; v++) {
                const SynthVoiceNode *n = &poly_bank[v];
                if (n->is_gate_open && n->active_note == 72 && n->glide_q16 == -500 * 65536) ok++;
                if (n->is_gate_open && n->active_note == 76 && n->glide_q16 == -900 * 65536) ok++;
            }
            CHECK(ok == 2, "poly mode %u: chord did not fan out from the last note", modes[mi]);
        }
        all_off();
        perf_state.porta_time[0] = 0;
        synth_midi_3(0x90, 60, 100);
        synth_midi_3(0x80, 60, 0);
        render(2048);
        synth_midi_3(0x90, 64, 100);
        for (int v = 0; v < 8; v++) CHECK(!poly_bank[v].is_gate_open || poly_bank[v].glide_q16 == 0, "porta time 0 still glides");
        all_off();
        perf_state.porta_time[0] = 40;
        perf_state.porta_mode[0] = PORTA_LEGATO_TIED;
        synth_midi_3(0x90, 60, 100);
        synth_midi_3(0x80, 60, 0);
        render(2048);
        synth_midi_3(0x90, 64, 100);                       /* detached: no glide */
        for (int v = 0; v < 8; v++) CHECK(!poly_bank[v].is_gate_open || poly_bank[v].glide_q16 == 0, "LEGATO TIED glided a detached note");
        render(2048);
        synth_midi_3(0x90, 67, 100);                       /* over the held 64: glides */
        int tied = 0;
        for (int v = 0; v < 8; v++)
            if (poly_bank[v].is_gate_open && poly_bank[v].active_note == 67 && poly_bank[v].glide_q16 == -300 * 65536) tied = 1;
        CHECK(tied, "LEGATO TIED did not glide an overlapping note");
        all_off();
        perf_state.porta_time[0] = 0;
        perf_state.porta_mode[0] = PORTA_CONST_RATE;
        perf_state.voice_mode[0] = VMODE_POLY_REUSE;
    }

    /* STRIKE (2026-10-16): the attack sounds at once over a slow pad, stops, and leaves the rest of the voice as SINGLE
     * plays it; a pitched attack is read faster for a higher note, an unpitched one is not; TUNE transposes either */
    {
        double early;
        const int BOW = 16, FLINT = 3;
        CHECK(attack_info[BOW].root != ATK_UNPITCHED && attack_info[FLINT].root == ATK_UNPITCHED, "attack table order");
        int at_root = strike_len(BOW, 64, attack_info[BOW].root, &early);
        double expect = (attack_info[BOW].length - 1) * 65536.0 / attack_info[BOW].rate_q16;
        printf("STRIKE BOW PLUCK at its root: %d samples (expected ~%.0f), first 20 ms rms %.0f\n", at_root, expect, early);
        CHECK(fabs(at_root - expect) < 0.02 * expect, "STRIKE: the attack's length at its root %d, not ~%.0f", at_root, expect);
        CHECK(early > 200, "STRIKE: the attack is not heard over a slow ENV1 (rms %.0f)", early);
        int octave_up = strike_len(BOW, 64, attack_info[BOW].root + 12, NULL);
        CHECK(fabs(octave_up * 2.0 / at_root - 1.0) < 0.02, "STRIKE: a pitched attack an octave up lasts %d, not half of %d",
              octave_up, at_root);
        /* unpitched: the read rate is the sound's own at any key (its tail's end, measured as above, wobbles by a few
         * samples with the pad under it, so the rate is read from the voice), TUNE +12 doubles it */
        uint32_t st_lo = strike_step(FLINT, 64, 48), st_hi = strike_step(FLINT, 64, 72), st_up = strike_step(FLINT, 64 + 12, 48);
        CHECK(st_lo == st_hi && st_lo > 0, "STRIKE: an unpitched attack follows the key (step %u vs %u)", st_lo, st_hi);
        CHECK(fabs(st_lo - (double)attack_info[FLINT].rate_q16) <= 1.0, "STRIKE: unpitched step %u, its rate %u", st_lo,
              attack_info[FLINT].rate_q16);
        CHECK(fabs(st_up / (2.0 * st_lo) - 1.0) < 0.002, "STRIKE: TUNE +12 step %u, not twice %u", st_up, st_lo);
        /* the ATTACK page's rows and knobs (2026-10-16, FREC_VER_ENGINE 7) */
        strike_diff(BOW, 60);
        double full_late = strike_rms(1500, 4000);
        strike_tweak = tw_decay; strike_diff(BOW, 60); strike_tweak = NULL;
        double dec_late = strike_rms(1500, 4000);
        printf("STRIKE DECAY 20: attack rms 34..90 ms %.0f (127: %.0f)\n", dec_late, full_late);
        CHECK(dec_late < 0.05 * full_late, "STRIKE: DECAY 20 left %.0f of %.0f", dec_late, full_late);
        strike_tweak = tw_start;
        int half = strike_len(BOW, 64, attack_info[BOW].root, NULL);
        strike_tweak = NULL;
        CHECK(fabs(half * 2.0 / at_root - 1.0) < 0.03, "STRIKE: START 127 lasts %d, not half of %d", half, at_root);
        double lo, hi;
        strike_tweak = tw_vel0;
        strike_vel = 30; strike_diff(BOW, 60); lo = strike_rms(0, 882);
        strike_vel = 120; strike_diff(BOW, 60); hi = strike_rms(0, 882);
        CHECK(fabs(lo / hi - 1.0) < 0.03, "STRIKE: VELOCITY 0 still follows velocity (%.0f vs %.0f)", lo, hi);
        strike_tweak = NULL;
        strike_vel = 30; strike_diff(BOW, 60); lo = strike_rms(0, 882);
        strike_vel = 120; strike_diff(BOW, 60); hi = strike_rms(0, 882);
        strike_vel = 60;
        CHECK(lo < 0.5 * hi, "STRIKE: VELOCITY 127 ignores velocity (%.0f vs %.0f)", lo, hi);
        strike_tweak = tw_fixed;
        uint32_t fx_lo = strike_step(BOW, 64, 48), fx_hi = strike_step(BOW, 64, 72);
        strike_tweak = tw_track;
        uint32_t tr_lo = strike_step(FLINT, 64, 60), tr_hi = strike_step(FLINT, 64, 72);
        strike_tweak = NULL;
        CHECK(fx_lo == fx_hi && fx_lo == attack_info[BOW].rate_q16, "STRIKE: KEY TRACK FIXED follows the key (%u, %u)", fx_lo, fx_hi);
        CHECK(fabs(tr_lo - (double)attack_info[FLINT].rate_q16) <= 2.0 && fabs(tr_hi / (2.0 * tr_lo) - 1.0) < 0.002,
              "STRIKE: KEY TRACK TRACK on an unpitched sound (%u at C4, %u an octave up)", tr_lo, tr_hi);
        /* TO FILTER: through a dark filter the attack loses most of itself; and it then follows ENV1 (a slow one hides it) */
        double open, shut, slow;
        strike_tweak = tw_dark; strike_diff(BOW, 60); open = strike_rms(0, 882);
        strike_tweak = tw_dark_filt; strike_diff(BOW, 60); shut = strike_rms(0, 882);
        strike_tweak = tw_slow_filt; strike_diff(BOW, 60); slow = strike_rms(0, 882);
        strike_tweak = NULL;
        printf("STRIKE TO FILTER: first 20 ms rms %.0f unfiltered, %.0f through a dark FILTER1, %.0f under a slow ENV1\n",
               open, shut, slow);
        CHECK(shut < 0.5 * open, "STRIKE: TO FILTER 127 through a dark filter %.0f, unfiltered %.0f", shut, open);
        CHECK(slow < 0.1 * open, "STRIKE: TO FILTER 127 under a slow ENV1 %.0f, unfiltered %.0f", slow, open);

        /* BEND +12: the attack starts an octave up (read twice as fast) and glides back to its own rate */
        strike_tweak = tw_bend;
        uint32_t bent_end = strike_step(FLINT, 64, 60);
        synth_init();
        base_patch(&engine_state[0], VARCH_STRIKE);
        engine_state[0].oscmix.atk_sample = FLINT;
        tw_bend(&engine_state[0]);
        synth_midi_3(0x90, 60, 60);
        synth_render_q15(buf, 64);                          /* 4 ticks */
        uint32_t bent0 = 0;
        for (int v = 0; v < POLYPHONY_VOICES; v++)
            if (poly_bank[v].is_gate_open) bent0 = poly_bank[v].atk_step;
        strike_tweak = NULL;
        double r0 = bent0 / (double)attack_info[FLINT].rate_q16;
        printf("STRIKE BEND +12: step x%.3f after 4 ticks, x%.3f at the end\n", r0,
               bent_end / (double)attack_info[FLINT].rate_q16);
        CHECK(r0 > 1.85 && r0 < 2.0, "STRIKE: BEND +12 starts at x%.3f, not ~x2", r0);
        /* (FLINT is 60 ms: it ends mid-glide, and its step stays where the glide had got to) */
        double r1 = bent_end / (double)attack_info[FLINT].rate_q16;
        CHECK(r1 > 1.0 && r1 < 0.75 * r0, "STRIKE: BEND did not glide toward the pitch (x%.3f -> x%.3f)", r0, r1);

        /* BEND MOD +24 from VELOCITY with no BEND: a hard note starts ~an octave up, a soft one ~a half octave up */
        double rv[2];
        for (int k = 0; k < 2; k++) {
            synth_init();
            base_patch(&engine_state[0], VARCH_STRIKE);
            engine_state[0].oscmix.atk_sample = FLINT;
            engine_state[0].oscmix.atk_bend_mod_src = MSRC_VELOCITY;
            engine_state[0].oscmix.atk_bend_mod_amt = 64 + 24;
            synth_midi_3(0x90, 60, k ? 127 : 64);
            synth_render_q15(buf, 64);
            rv[k] = 0;
            for (int v = 0; v < POLYPHONY_VOICES; v++)
                if (poly_bank[v].is_gate_open) rv[k] = poly_bank[v].atk_step / (double)attack_info[FLINT].rate_q16;
            all_off();
        }
        printf("STRIKE BEND MOD +12 by velocity: x%.3f at 64, x%.3f at 127\n", rv[0], rv[1]);
        CHECK(rv[1] > 1.85 && rv[1] < 2.02 && rv[0] > 1.3 && rv[0] < 1.45, "STRIKE: BEND MOD by velocity x%.3f / x%.3f",
              rv[0], rv[1]);

        /* after the attack the voice is SINGLE's, sample for sample */
        strike_take(VARCH_STRIKE, BOW, 64, 60, strike_l);
        strike_take(VARCH_SINGLE, BOW, 64, 60, single_l);
        CHECK(memcmp(strike_l + STRIKE_N / 2, single_l + STRIKE_N / 2, sizeof(int32_t) * (STRIKE_N / 2)) == 0,
              "STRIKE: after the attack the voice differs from SINGLE");
        all_off();
    }

    /* GLOBAL > MASTER TUNE (2026-10-17): +50 cents moves the pitch (the zero-crossings come sooner), 0 changes nothing */
    {
        int32_t z[2], prev;
        for (int t = 0; t < 2; t++) {
            synth_init();
            base_patch(&engine_state[0], VARCH_SINGLE);
            engine_state[0].filter[0].mod_amt = 64;   /* (no LFO on the cutoff: the pitch alone) */
            instrument.master_tune = t ? MASTER_TUNE_MAX : 64u;
            synth_midi_3(0x90, 57, 100);
            render(4096);
            z[t] = 0;
            prev = 0;
            for (int k = 0; k < 689; k++) {             /* one second, ~ 220 cycles of A3 */
                render(64);
                for (int j = 0; j < 64; j++) {
                    z[t] += (prev < 0) != (buf[j * 2] < 0);
                    prev = buf[j * 2];
                }
            }
            all_off();
        }
        instrument.master_tune = 64u;
        CHECK(z[1] > z[0], "MASTER TUNE +50 cents: more zero-crossings (%d vs %d)", (int)z[1], (int)z[0]);
    }

    /* MOD slot A*B with a source missing (2026-10-17): the missing one is the product's 1, so one source passes
     * through; both missing give 0. LFO1 is the layer-level source that runs with no voice. */
    {
        static const struct { uint8_t a, b; int live; const char *what; } C[] = {
            {MSRC_LFO1, MSRC_NONE, 1, "A only"}, {MSRC_NONE, MSRC_LFO1, 1, "B only"}, {MSRC_NONE, MSRC_NONE, 0, "none"},
        };
        for (unsigned k = 0; k < sizeof(C) / sizeof(C[0]); k++) {
            int32_t peak = 0;
            synth_init();
            base_patch(&engine_state[0], VARCH_SINGLE);
            engine_state[0].lfo[0].rate = 100;
            engine_state[0].mod[0].src_a = C[k].a;
            engine_state[0].mod[0].src_b = C[k].b;
            engine_state[0].mod[0].func = MFN_MUL;
            engine_state[0].mod[0].amount = 127;
            synth_mod_watch = 0;
            for (int i = 0; i < 64; i++) {
                int32_t o;
                render(256);
                o = synth_mod_layer_out(0, 0);
                if (o < 0) o = -o;
                if (o > peak) peak = o;
            }
            synth_mod_watch = -1;
            CHECK(C[k].live ? peak > 8000 : peak == 0, "MOD slot a*b, %s: peak %d", C[k].what, (int)peak);
        }
    }

    printf(fails ?"\n%d FAILED\n" : "\nall checks passed\n", fails);
    return fails != 0;
}
