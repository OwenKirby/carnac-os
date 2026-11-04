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
#include "dsp/tuning.h"
#include "dsp/mfx.h"

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

static const char *ARCH[] = {"SINGLE", "DUAL_OSC", "SPREAD", "DUAL_FILTER", "STACK", "BINAURAL", "STRIKE", "FULL", "DUAL_SPREAD"};

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
    e->filter[0].mod_amt = 112;
    e->filter[0].cutoff = 70;
    e->filter[1].cutoff = 90;
    e->lfo[0].rate = 70;
}


/* MOD SENDS (2026-11-03): a send sounds as the destination's own source set to the slot at the send's amount would.
 * The left channel of a held chord, the slot LFO1 alone (amount 100), each way; and an own source plus a send adds. */
#define SEND_N 8192
static int32_t send_out[2][SEND_N];
typedef void (*SendTweak)(EngineState *e, int way);   /* way 0: the old route (own source = MOD1), 1: a send */
static void send_take(uint8_t arch, SendTweak tw, int way, int32_t *out)
{
    synth_init();
    EngineState *e = &engine_state[0];
    base_patch(e, arch);
    e->filter[0].mod_src = MSRC_NONE;
    e->filter[0].mod_amt = 0;
    e->mod[0] = (ModSlotParams){MSRC_LFO1, MFN_ADD, MSRC_NONE, 100};
    e->lfo[0].rate = 90;
    tw(e, way);
    synth_midi_3(0x90, 48, 100);
    synth_midi_3(0x90, 55, 90);
    for (int n = 0; n < SEND_N; n += 64) {
        synth_render_q15(buf, 64);
        for (int i = 0; i < 64; i++) out[n + i] = buf[2 * i];
    }
}
static void send1(EngineState *e, uint8_t dest, int amt)
{
    e->send[0].dest = (uint8_t)(dest | 0u << MSEND_SLOT_SHIFT);
    e->send[0].amount = (uint8_t)(BIPOLAR_CENTER + amt);
}
static void tw_cut(EngineState *e, int way)
{
    if (way) send1(e, MDST_F1_CUTOFF, 50); else { e->filter[0].mod_src = MSRC_MODSLOT1; e->filter[0].mod_amt = 100; }
}
static void tw_pitch(EngineState *e, int way)
{
    if (way) send1(e, MDST_OSC1_PITCH, 50); else { e->osc[0].pitch_mod_src = MSRC_MODSLOT1; e->osc[0].pitch_mod_amt = 100; }
}
static void tw_morph(EngineState *e, int way)
{
    if (way) send1(e, MDST_F1_MORPH, -30); else { e->filter[0].morph_mod_src = MSRC_MODSLOT1; e->filter[0].morph_mod_amt = 64 - 30; }
}
static void tw_pan(EngineState *e, int way)
{
    if (way) send1(e, MDST_PAN, 40); else { e->amp.pan_mod_src = MSRC_MODSLOT1; e->amp.pan_mod_depth = 80; }
}
static void tw_scan(EngineState *e, int way)
{
    if (way) send1(e, MDST_OSC1_SCAN, 45); else { e->osc[0].scan_mod_src = MSRC_MODSLOT1; e->osc[0].scan_mod_amt = 90; }
}
static void tw_lfo2(EngineState *e, int way)   /* LFO2 on the cutoff, its speed swept by MOD1 */
{
    e->filter[0].mod_src = MSRC_LFO2; e->filter[0].mod_amt = 90; e->lfo[1].rate = 60;
    if (way) send1(e, MDST_LFO2_SPEED, 40); else { e->lfo[1].speed_mod_src = MSRC_MODSLOT1; e->lfo[1].speed_mod_amt = 64 + 40; }
}
static void tw_amp(EngineState *e, int way)
{
    if (way) send1(e, MDST_AMP_LEVEL, 50); else { e->amp.amp_mod_src = MSRC_MODSLOT1; e->amp.amp_mod_depth = 100; }
}
static void tw_both(EngineState *e, int way)   /* ENV2 on the cutoff a little, plus a send (way 1) or not (way 0) */
{
    e->filter[0].mod_src = MSRC_ENV2; e->filter[0].mod_amt = 20;
    if (way) send1(e, MDST_F1_CUTOFF, 50);
}
static void tw_mfx_k1(EngineState *e, int way)   /* STONE PHASER, MOD1 on its MANUAL (way 1) or not */
{
    e->mfx = (MfxParams){MFX_STONE_PHASER, {40, 0, 64, 127}, 0, {0, 0}};
    if (way) send1(e, MDST_MFX_K1, 60);
}
static void tw_mfx_ps(EngineState *e, int way)   /* PITCH SHIFT: a send to its PITCH (a list knob) does nothing */
{
    e->mfx = (MfxParams){MFX_PITCH_SHIFT, {12, 75, 0, 64}, 127, {30, 0}};
    if (way) send1(e, MDST_MFX_K1, 60);
}
static void tw_mod4(EngineState *e, int way)   /* MOD4 (2026-11-03) as MOD1: the same slot, the same send */
{
    if (way) {
        e->mod[3] = e->mod[0];
        e->mod[0] = (ModSlotParams){MSRC_NONE, MFN_ADD, MSRC_NONE, BIPOLAR_CENTER};
        e->send[0].dest = (uint8_t)(MDST_F1_CUTOFF | 3u << MSEND_SLOT_SHIFT);
        e->send[0].amount = (uint8_t)(BIPOLAR_CENTER + 50);
    } else {
        send1(e, MDST_F1_CUTOFF, 50);
    }
}
/* the destinations with no own source of their own (2026-11-03): way 1 a send, way 0 none; the sound must change */
static void tw_res(EngineState *e, int way) { e->filter[0].resonance = 40; if (way) send1(e, MDST_F1_RES, 60); }
static void tw_drive2(EngineState *e, int way)
{
    e->arch = VARCH_DUAL_FILTER;
    e->filter[1].drive = 10;
    if (way) send1(e, MDST_F2_DRIVE, 60);
}
static void tw_dly(EngineState *e, int way) { e->amp.delay_send = 20; if (way) send1(e, MDST_DLY_SEND, 60); }
static void tw_rev(EngineState *e, int way) { e->amp.reverb_send = 20; if (way) send1(e, MDST_REV_SEND, 60); }
static void send_differs(const char *name, SendTweak tw)
{
    send_take(VARCH_DUAL_OSC, tw, 0, send_out[0]);
    send_take(VARCH_DUAL_OSC, tw, 1, send_out[1]);
    int diff = memcmp(send_out[0], send_out[1], sizeof send_out[0]) != 0;
    CHECK(diff, "MOD SEND %s: changed nothing", name);
    if (diff) printf("MOD SEND %-12s moves the sound\n", name);
}
static void send_same(const char *name, uint8_t arch, SendTweak tw)
{
    send_take(arch, tw, 0, send_out[0]);
    send_take(arch, tw, 1, send_out[1]);
    int same = memcmp(send_out[0], send_out[1], sizeof send_out[0]) == 0;
    CHECK(same, "MOD SEND %s: does not sound as the own-source route", name);
    if (same) printf("MOD SEND %-10s = the own-source route\n", name);
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
    CHECK(layer_voice_limit(PERF_DUAL_SYNTH, &engine_state[0], &engine_state[1], VMODE_POLY_REUSE, VMODE_POLY_REUSE, 0) == 3,
          "limit");
    /* both SINGLE: the spec's 4 + 4 */
    all_off();
    engine_state[1].arch = VARCH_SINGLE;
    for (int i = 0; i < 6; i++) { synth_midi_3(0x90, (uint8_t)(48 + i), 100); synth_midi_3(0x91, (uint8_t)(60 + i), 100); }
    render(2048);
    CHECK(sounding(0) == 4 && sounding(1) == 4, "DUAL SINGLE+SINGLE: %d / %d, want 4 / 4", sounding(0), sounding(1));

    /* a MONO layer takes one voice and gives the rest to the other: SINGLE + SINGLE 1 + 7, either way round (DUAL
     * and SPLIT); SINGLE + STACK (cap 6) with layer 1 mono: 1 + floor(6 * 7 / 8) = 5 */
    for (int pm = PERF_DUAL_SYNTH; pm <= PERF_SPLIT_SYNTH; pm++)
        for (int ml = 0; ml < 2; ml++) {
            synth_init();
            perf_state.perf_mode = (uint8_t)pm;
            perf_state.split_note = 60;
            perf_state.split_fade = 0;
            base_patch(&engine_state[0], VARCH_SINGLE);
            base_patch(&engine_state[1], VARCH_SINGLE);
            perf_state.voice_mode[ml] = VMODE_MONO_RETRIG;
            for (int i = 0; i < 8; i++) {
                synth_midi_3(0x90, (uint8_t)(36 + i), 100);
                synth_midi_3(pm == PERF_DUAL_SYNTH ? 0x91 : 0x90, (uint8_t)(72 + i), 100);
            }
            render(2048);
            CHECK(sounding(ml) == 1 && sounding(1 - ml) == 7, "%s mono L%d: %d / %d, want 1 / 7",
                  pm == PERF_DUAL_SYNTH ? "DUAL" : "SPLIT", ml + 1, sounding(ml), sounding(1 - ml));
        }
    CHECK(layer_voice_limit(PERF_DUAL_SYNTH, &engine_state[0], &engine_state[1], VMODE_MONO_LEGATO, VMODE_MONO_RETRIG, 1) == 1,
          "both mono");
    base_patch(&engine_state[1], VARCH_STACK);
    CHECK(layer_voice_limit(PERF_DUAL_SYNTH, &engine_state[0], &engine_state[1], VMODE_MONO_LEGATO, VMODE_UNISON_STACK, 1) == 5,
          "mono SINGLE + STACK");

    /* 4b. a full layer steals by source (2026-10-26): ARP below SEQ below the keys, never upward; inside the ARP / SEQ
     * the outer notes are kept, the keys lose their oldest */
    synth_init();
    base_patch(&engine_state[0], VARCH_SINGLE);
    {
        static const uint8_t SRC[8] = {VSRC_SEQ, VSRC_ARP, VSRC_ARP, VSRC_ARP, VSRC_ARP, VSRC_ARP, VSRC_ARP, VSRC_ARP};
        static const uint8_t AGE[8] = {90, 10, 80, 70, 20, 30, 40, 50};
        for (int v = 0; v < 8; v++) {
            SynthVoiceNode *n = &poly_bank[v];
            n->is_gate_open = 1; n->silent = 0; n->choke = 0; n->layer = 0;
            n->src = SRC[v]; n->active_note = v == 0 ? 36 : 58 + v; n->age_counter = AGE[v];
        }
        /* ARP notes 59..65: 59 (voice 1) and 65 (voice 7) kept, the oldest of the rest is voice 2 (age 80) */
        CHECK(allocate_voice_slot(0, VSRC_ARP) == 2, "ARP over ARP: took %d, want the oldest inner voice 2",
              allocate_voice_slot(0, VSRC_ARP));
        CHECK(allocate_voice_slot(0, VSRC_SEQ) == 2, "SEQ over SEQ + ARP: took %d, want ARP voice 2",
              allocate_voice_slot(0, VSRC_SEQ));
        for (int v = 0; v < 8; v++) poly_bank[v].src = VSRC_SEQ;
        CHECK(allocate_voice_slot(0, VSRC_ARP) == -1, "ARP over a full SEQ layer: not dropped");
        CHECK(allocate_voice_slot(0, VSRC_KEYS) == 2, "keys over SEQ: took %d, want SEQ's oldest inner voice 2",
              allocate_voice_slot(0, VSRC_KEYS));
        for (int v = 0; v < 8; v++) poly_bank[v].src = VSRC_KEYS;
        CHECK(allocate_voice_slot(0, VSRC_SEQ) == -1, "SEQ over a full keys layer: not dropped");
        CHECK(allocate_voice_slot(0, VSRC_KEYS) == 0, "keys over keys: took %d, want the oldest, voice 0",
              allocate_voice_slot(0, VSRC_KEYS));
        for (int v = 0; v < 8; v++) poly_bank[v].is_gate_open = 0;
    }

    /* 5. choke: lower the limit under held notes (SINGLE -> STACK), the extra voices fade without a click */
    synth_init();
    base_patch(&engine_state[0], VARCH_SINGLE);
    engine_state[0].filter[0].mod_amt = 0;
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
    engine_state[0].filter[0].mod_amt = 0;
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
        engine_state[0].filter[0].mod_amt = 0;
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
        engine_state[0].filter[0].mod_amt = 0;
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

        /* (b) SINGLE, white noise at full level: the colour mod darkens it towards pink, the level drops (~4 dB
         * unfiltered). The filter out of the way: the SH-2's HP24 at 20 Hz (2026-10-21: the default MULTI LP12's two
         * real poles at 14 kHz took more of white's top octave than pink's) */
        synth_init();
        base_patch(&engine_state[0], VARCH_SINGLE);
        engine_state[0].filter[0].mod_amt = 0;
        engine_state[0].filter[0].flavour = FLT_SH2;
        engine_state[0].filter[0].morph = 127;
        engine_state[0].filter[0].cutoff = 0;
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
        engine_state[0].filter[0].mod_amt = 0;
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

    /* GLOBAL > MASTER TUNE (2026-10-17): +50 cents moves the pitch (the zero-crossings come sooner), 0 changes nothing */
    {
        int32_t z[2], prev;
        for (int t = 0; t < 2; t++) {
            synth_init();
            base_patch(&engine_state[0], VARCH_SINGLE);
            engine_state[0].filter[0].mod_amt = 0;   /* (no LFO on the cutoff: the pitch alone) */
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

    /* GLOBAL > TUNING and ROOT (2026-10-29): MAQAM RAST on C lowers E a quarter tone and leaves C alone; turned to D
     * the flat 3rd is F#; a USER table plays its key's cents; DELAY SYNC and DELAY TYPE keep their bit 0 */
    {
        CHECK(tuning_key_cents(TUNING_RAST, 0, 64) == -50 && tuning_key_cents(TUNING_RAST, 0, 60) == 0 &&
              tuning_key_cents(TUNING_RAST, 2, 66) == -50 && tuning_key_cents(TUNING_RAST, 2, 64) == 0 &&
              tuning_key_cents(TUNING_JUST, 0, 64) == -14 && tuning_key_cents(TUNING_EQUAL, 5, 64) == 0,
              "the built-in tunings, turned to ROOT");
        tuning_user[3][64] = 77;
        CHECK(tuning_key_cents(TUNING_USER1 + 3, 0, 64) == 77, "USER 4 key 64");
        tuning_user[3][64] = 0;
        int32_t z[4], prev;
        for (int t = 0; t < 4; t++) {
            synth_init();
            base_patch(&engine_state[0], VARCH_SINGLE);
            engine_state[0].filter[0].mod_amt = 0;
            perf_state.delay_sync = (uint8_t)(t & 1 ? TUNING_RAST << 1 : 0);
            perf_state.delay_type = 0;
            synth_midi_3(0x90, t < 2 ? 64 : 60, 100);   /* E4 equal, E4 RAST, C4 equal, C4 RAST */
            render(4096);
            z[t] = 0;
            prev = 0;
            for (int k = 0; k < 689; k++) {
                render(64);
                for (int j = 0; j < 64; j++) {
                    z[t] += (prev < 0) != (buf[j * 2] < 0);
                    prev = buf[j * 2];
                }
            }
            all_off();
        }
        CHECK(DELAY_SYNC(&perf_state) == 0 && PERF_TUNING(&perf_state) == TUNING_RAST, "TUNING beside DELAY SYNC");
        perf_state.delay_sync = 0;
        /* E4 330 Hz: 50 cents down is ~ 9.5 Hz, ~ 19 crossings a second */
        CHECK(z[1] < z[0] - 10, "RAST: E4 a quarter tone flat (%d vs %d crossings)", (int)z[1], (int)z[0]);
        CHECK(z[3] == z[2], "RAST: C4 in tune (%d vs %d crossings)", (int)z[3], (int)z[2]);
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

    /* VEL > LEVEL (2026-10-26): ENV1's sets how much quieter a soft note is (0: none); ENV2's scales ENV2 for the note.
     * DEPTH MOD: 127 on the mod wheel, the wheel down stops LFO2, up gives it full; an LFO as the source reads as none */
    {
        double rms[2][2];
        for (int amt = 0; amt < 2; amt++)
            for (int vi = 0; vi < 2; vi++) {
                synth_init();
                base_patch(&engine_state[0], VARCH_SINGLE);
                engine_state[0].filter[0].mod_amt = 0;
                engine_state[0].env_vel_level[0] = amt ? 127u : 0u;
                synth_midi_3(0x90, 60, vi ? 127 : 20);
                render(2048);
                rms[amt][vi] = render(8192).rms_l;
                all_off();
            }
        CHECK(fabs(rms[0][0] - rms[0][1]) < 0.01 * rms[0][1], "VEL > LEVEL 0: velocity moved the level (%.0f vs %.0f)",
              rms[0][0], rms[0][1]);
        CHECK(rms[1][0] < 0.1 * rms[1][1] && rms[1][1] > 0.99 * rms[0][1], "VEL > LEVEL 127: soft %.0f, loud %.0f",
              rms[1][0], rms[1][1]);

        synth_init();
        base_patch(&engine_state[0], VARCH_SINGLE);
        engine_state[0].env_vel_level[1] = 127u;
        engine_state[0].lfo_depth_src[1] = MSRC_MODWHEEL;
        engine_state[0].lfo_depth_amt[1] = 127u;
        engine_state[0].lfo_depth_src[2] = MSRC_LFO1;  /* refused: reads as none */
        engine_state[0].lfo_depth_amt[2] = 127u;
        engine_state[0].lfo[1].rate = 100;
        synth_midi_3(0xB0, 1, 0);
        synth_midi_3(0x90, 60, 64);
        int32_t lfo_peak = 0;
        for (int i = 0; i < 64; i++) {
            render(256);
            if (abs(poly_bank[0].lfo_q15[0]) > lfo_peak) lfo_peak = abs(poly_bank[0].lfo_q15[0]);
        }
        int32_t want_env = 32768 - (((32768 - ((64 * 64 * 16645) >> 13)) * (127 * 258)) >> 15);
        CHECK(poly_bank[0].env_vel_q15[1] == want_env && poly_bank[0].env_vel_q15[2] == 32768,
              "VEL > LEVEL: ENV2 gain %d (want %d), ENV3 %d", (int)poly_bank[0].env_vel_q15[1], (int)want_env,
              (int)poly_bank[0].env_vel_q15[2]);
        CHECK(poly_bank[0].lfo_gain_q15[1] < 100 && lfo_peak < 100 && poly_bank[0].lfo_gain_q15[2] == 32768,
              "DEPTH MOD, wheel down: LFO2 gain %d, peak %d; LFO3 on LFO1 %d", (int)poly_bank[0].lfo_gain_q15[1],
              (int)lfo_peak, (int)poly_bank[0].lfo_gain_q15[2]);
        synth_midi_3(0xB0, 1, 127);
        lfo_peak = 0;
        for (int i = 0; i < 64; i++) {
            render(256);
            if (abs(poly_bank[0].lfo_q15[0]) > lfo_peak) lfo_peak = abs(poly_bank[0].lfo_q15[0]);
        }
        CHECK(poly_bank[0].lfo_gain_q15[1] > 32000 && lfo_peak > 20000, "DEPTH MOD, wheel up: LFO2 gain %d, peak %d",
              (int)poly_bank[0].lfo_gain_q15[1], (int)lfo_peak);
        all_off();
    }

    {   /* MOD SENDS: each kind of destination; an own source plus a send adds */
        send_same("F1 CUTOFF", VARCH_DUAL_OSC, tw_cut);
        send_same("OSC1 PITCH", VARCH_DUAL_OSC, tw_pitch);
        send_same("F1 MORPH", VARCH_DUAL_OSC, tw_morph);
        send_same("PAN", VARCH_DUAL_OSC, tw_pan);
        send_same("OSC1 SCAN", VARCH_DUAL_OSC, tw_scan);
        send_same("LFO2 SPEED", VARCH_DUAL_OSC, tw_lfo2);
        send_same("AMP LEVEL", VARCH_DUAL_OSC, tw_amp);
        send_same("BIN PITCH", VARCH_BINAURAL, tw_pitch);
        send_same("MFX PITCH", VARCH_DUAL_OSC, tw_mfx_ps);   /* (way 0: no send; a list knob takes none) */
        send_take(VARCH_DUAL_OSC, tw_mfx_k1, 0, send_out[0]);
        send_take(VARCH_DUAL_OSC, tw_mfx_k1, 1, send_out[1]);
        CHECK(memcmp(send_out[0], send_out[1], sizeof send_out[0]) != 0, "MOD SEND: MOD1 on the phaser's MANUAL changed nothing");
        send_take(VARCH_DUAL_OSC, tw_both, 0, send_out[0]);
        send_take(VARCH_DUAL_OSC, tw_both, 1, send_out[1]);
        CHECK(memcmp(send_out[0], send_out[1], sizeof send_out[0]) != 0, "MOD SEND: a send on top of ENV2 changed nothing");
        send_same("MOD4", VARCH_DUAL_OSC, tw_mod4);
        send_differs("F1 RESONANCE", tw_res);
        send_differs("F2 DRIVE", tw_drive2);
        send_differs("DELAY SEND", tw_dly);
        send_differs("REVERB SEND", tw_rev);
        all_off();
    }

    {   /* GLIDE TIME's sends: a glide under way goes faster with a negative send, slower with a positive one */
        int32_t left[3];
        for (int k = 0; k < 3; k++) {
            synth_init();
            base_patch(&engine_state[0], VARCH_DUAL_OSC);
            engine_state[0].mod[0] = (ModSlotParams){MSRC_VELOCITY, MFN_ADD, MSRC_NONE, 127};
            if (k) send1(&engine_state[0], MDST_GLIDE, k == 1 ? -50 : 50);
            perf_state.porta_time[0] = 70;
            perf_state.porta_mode[0] = PORTA_CONST_RATE;
            synth_midi_3(0x90, 48, 127);
            render(4410);                            /* (past the chord window: the next note glides from this) */
            synth_midi_3(0x90, 72, 127);
            render(4096);
            left[k] = 0;
            for (int v = 0; v < POLYPHONY_VOICES; v++)
                if (poly_bank[v].is_gate_open && poly_bank[v].active_note == 72) left[k] = abs(poly_bank[v].glide_q16);
            all_off();
        }
        printf("GLIDE TIME send: glide left %d (none), %d (-50), %d (+50)\n", (int)left[0], (int)left[1], (int)left[2]);
        CHECK(left[0] > 0 && left[1] < left[0] && left[2] > left[0], "GLIDE TIME: the send did not change the glide's speed");
        perf_state.porta_time[0] = 0;
    }

    {   /* RANDOM: a value per note; POLY AT: the note's own pressure, else the channel's */
        synth_init();
        base_patch(&engine_state[0], VARCH_DUAL_OSC);
        synth_midi_3(0x90, 48, 100);
        synth_midi_3(0x90, 55, 100);
        synth_midi_3(0xD0, 30, 0);
        synth_midi_3(0xA0, 48, 110);
        render(64);
        int a = -1, b = -1;
        for (int v = 0; v < POLYPHONY_VOICES; v++) {
            if (poly_bank[v].is_gate_open && poly_bank[v].active_note == 48) a = v;
            if (poly_bank[v].is_gate_open && poly_bank[v].active_note == 55) b = v;
        }
        CHECK(a >= 0 && b >= 0, "RANDOM / POLY AT: the two notes did not start");
        if (a >= 0 && b >= 0) {
            CHECK(poly_bank[a].rand_q15 != poly_bank[b].rand_q15, "RANDOM: two notes drew the same value");
            CHECK(poly_bank[a].poly_seen && poly_bank[a].poly_at == 110 && !poly_bank[b].poly_seen,
                  "POLY AT: the pressure did not reach its own note only");
        }
        all_off();
    }

    {   /* VEL > ATTACK: a hard note's attack faster (a negative amount), its decay and release untouched */
        int32_t lv[2];
        for (int k = 0; k < 2; k++) {
            synth_init();
            base_patch(&engine_state[0], VARCH_DUAL_OSC);
            engine_state[0].env[0].attack = 80;
            engine_state[0].env[0].vel_to_attack = BIPOLAR_CENTER - 40;
            synth_midi_3(0x90, 60, k ? 127 : 20);
            render(4410);
            lv[k] = poly_bank[0].amp_env.level_q15;
            all_off();
        }
        printf("VEL > ATTACK: ENV1 after 100 ms %d (soft), %d (hard)\n", (int)lv[0], (int)lv[1]);
        CHECK(lv[1] > lv[0] + 2000, "VEL > ATTACK: a hard note's attack is not faster");
    }

    printf(fails ?"\n%d FAILED\n" : "\nall checks passed\n", fails);
    return fails != 0;
}
