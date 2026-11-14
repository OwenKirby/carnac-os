/* Host test: the priced voice pool (PresetStructures.h varch_voice_cost / varch_voice_cap / layer_voice_limit) and the
 * allocator's placement and source tails (voice_allocator.c). Exit code 0 = pass. Built by run_tests.sh with the DSP. */
#include <stdio.h>
#include <string.h>
#include "PresetStructures.h"
#include "voice_allocator.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/mfx.h"

#define P VMODE_POLY_REUSE
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static EngineState mk(uint8_t arch, int spread, uint8_t flt, uint8_t pcm)
{
    EngineState e;
    memset(&e, 0, sizeof e);
    e.arch = arch;
    e.oscmix.spread = (uint8_t)spread;
    e.filter[0].flavour = e.filter[1].flavour = flt;
    e.osc[0].wavebank = e.osc[1].wavebank = pcm ? 70u : 2u;
    return e;
}

static unsigned lim(const EngineState *a, const EngineState *b, uint8_t pm, uint8_t vm1, uint8_t vm2, uint8_t l)
{
    return layer_voice_limit(pm, a, b, vm1, vm2, l);
}

int main(void)
{
    EngineState e;
    /* costs, tenths of a plain voice */
    e = mk(VARCH_SPREAD, 0, FLT_MULTI, 0);  CHECK(varch_voice_cost(&e) == 10, "SPREAD 0 %u", varch_voice_cost(&e));
    e = mk(VARCH_SPREAD, 40, FLT_MULTI, 0); CHECK(varch_voice_cost(&e) == 13, "SPREAD %u", varch_voice_cost(&e));
    e = mk(VARCH_STACK, 0, FLT_MULTI, 0);   CHECK(varch_voice_cost(&e) == 15, "STACK %u", varch_voice_cost(&e));
    e = mk(VARCH_STACK, 0, FLT_SH2, 0);     CHECK(varch_voice_cost(&e) == 19, "STACK SH-2 x2 %u", varch_voice_cost(&e));
    e = mk(VARCH_STACK, 0, FLT_SH2, 1);     CHECK(varch_voice_cost(&e) == 23, "STACK SH-2 x2 PCM x2 %u", varch_voice_cost(&e));
    e = mk(VARCH_BINAURAL, 0, FLT_SH2, 0);  CHECK(varch_voice_cost(&e) == 20, "BINAURAL SH-2 twice %u", varch_voice_cost(&e));
    e = mk(VARCH_SPREAD, 40, FLT_MULTI, 1); CHECK(varch_voice_cost(&e) == 14, "SPREAD PCM %u", varch_voice_cost(&e));

    /* the cap of one patch alone: what one core carries (56 tenths) */
    static const struct { uint8_t arch; int spread; uint8_t flt, pcm; unsigned want; const char *what; } C[] = {
        {VARCH_SPREAD, 0, FLT_MULTI, 0, 8, "SINGLE"},           {VARCH_SPREAD, 40, FLT_MULTI, 0, 8, "SPREAD"},
        {VARCH_SPREAD, 40, FLT_SH2, 0, 6, "SPREAD SH-2"},       {VARCH_SPREAD, 40, FLT_MULTI, 1, 8, "SPREAD PCM"},
        {VARCH_DUAL_OSC, 0, FLT_MULTI, 0, 8, "DUAL OSC"},       {VARCH_DUAL_OSC, 0, FLT_MULTI, 1, 6, "DUAL OSC PCM"},
        {VARCH_DUAL_FILTER, 0, FLT_MULTI, 0, 8, "DUAL FILTER"}, {VARCH_DUAL_FILTER, 0, FLT_SH2, 0, 6, "DUAL FILTER SH-2"},
        {VARCH_STACK, 0, FLT_MULTI, 0, 6, "STACK"},             {VARCH_STACK, 0, FLT_SH2, 0, 4, "STACK SH-2"},
        {VARCH_STACK, 0, FLT_SH2, 1, 4, "STACK SH-2 PCM"},      {VARCH_BINAURAL, 0, FLT_MULTI, 0, 6, "BINAURAL"},
        {VARCH_BINAURAL, 0, FLT_SH2, 0, 4, "BINAURAL SH-2"},    {VARCH_FULL, 0, FLT_MULTI, 0, 6, "FULL"},
        {VARCH_FULL, 0, FLT_SH2, 0, 4, "FULL SH-2"},
    };
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
        e = mk(C[i].arch, C[i].spread, C[i].flt, C[i].pcm);
        CHECK(varch_voice_cap(&e) == C[i].want, "%s: cap %u, want %u (cost %u)", C[i].what, varch_voice_cap(&e), C[i].want,
              varch_voice_cost(&e));
    }

    /* the MFX is priced by type (2026-11-11): the price table covers every type, and one layer's costs a fixed part + half */
    CHECK(MFX_COUNT == 13, "the MFX price table (PresetStructures.h mfx_full_cost) has 13 entries, MFX_COUNT is %d", (int)MFX_COUNT);
    CHECK(mfx_full_cost(MFX_OFF) == 0 && mfx_full_cost(MFX_STONE_PHASER) == 8 && mfx_full_cost(MFX_METAL) == 20 &&
          mfx_full_cost(MFX_ENS_PHASER) == 13 && mfx_full_cost(MFX_BBD_ECHO) == 8, "MFX full costs");
    {
        EngineState ph = mk(VARCH_SPREAD, 0, FLT_MULTI, 0), br = ph, no = ph;
        ph.mfx.type = MFX_STONE_PHASER;
        br.mfx.type = MFX_METAL;
        CHECK(voice_overhead(&ph, &no, 1) == 7 && voice_overhead(&no, &ph, 1) == 7, "one phaser: 3 + 4 (%u)", voice_overhead(&ph, &no, 1));
        CHECK(voice_overhead(&br, &no, 1) == 13, "one BRONZE AGE: 3 + 10 (%u)", voice_overhead(&br, &no, 1));
        CHECK(voice_overhead(&ph, &ph, 1) == 8, "two phasers: the dearer (%u)", voice_overhead(&ph, &ph, 1));
        CHECK(voice_overhead(&br, &br, 1) == 20, "two BRONZE AGE: 20 (%u)", voice_overhead(&br, &br, 1));
        CHECK(voice_overhead(&ph, &br, 1) == 22 && voice_overhead(&br, &ph, 1) == 22,
              "a phaser beside a BRONZE AGE costs the BRONZE AGE's full price + 2 (%u)", voice_overhead(&ph, &br, 1));
        CHECK(voice_overhead(&br, &br, 0) == 13, "SINGL: only layer 1's MFX runs (%u)", voice_overhead(&br, &br, 0));
        /* a BRONZE AGE on STACK (15): 43 a core left, 2 a core: 4 voices; SINGLE (10) 4 a core still fits */
        e = mk(VARCH_STACK, 0, FLT_MULTI, 0); e.mfx.type = MFX_METAL;
        CHECK(varch_voice_cap(&e) == 4, "STACK + BRONZE AGE: cap %u, want 4", varch_voice_cap(&e));
        e = mk(VARCH_SPREAD, 0, FLT_MULTI, 0); e.mfx.type = MFX_METAL;
        CHECK(varch_voice_cap(&e) == 8, "SINGLE + BRONZE AGE: cap %u, want 8", varch_voice_cap(&e));
        /* two STACK layers with a BRONZE AGE each: 36 a core left, 2 + 2 */
        EngineState sb = mk(VARCH_STACK, 0, FLT_MULTI, 0);
        sb.mfx.type = MFX_METAL;
        CHECK(lim(&sb, &sb, PERF_DUAL_SYNTH, P, P, 0) == 2 && lim(&sb, &sb, PERF_DUAL_SYNTH, P, P, 1) == 2,
              "STACK + STACK, BRONZE AGE on both: 2 + 2 (%u + %u)", lim(&sb, &sb, PERF_DUAL_SYNTH, P, P, 0),
              lim(&sb, &sb, PERF_DUAL_SYNTH, P, P, 1));
    }
    /* a phaser takes 7 tenths off a core: SPREAD (13) 4 a core is 52, over the 49 left: 6 voices; SINGLE and STACK keep theirs */
    e = mk(VARCH_SPREAD, 40, FLT_MULTI, 0); e.mfx.type = 1;
    CHECK(varch_voice_cap(&e) == 6, "SPREAD + MFX: cap %u, want 6", varch_voice_cap(&e));
    e = mk(VARCH_SPREAD, 0, FLT_MULTI, 0); e.mfx.type = 1;
    CHECK(varch_voice_cap(&e) == 8, "SINGLE + MFX: cap %u, want 8", varch_voice_cap(&e));
    e = mk(VARCH_STACK, 0, FLT_MULTI, 0); e.mfx.type = 1;
    CHECK(varch_voice_cap(&e) == 6, "STACK + MFX: cap %u, want 6", varch_voice_cap(&e));
    e = mk(VARCH_DUAL_FILTER, 0, FLT_MULTI, 0); e.mfx.type = 1;
    CHECK(varch_voice_cap(&e) == 6, "DUAL FILTER + MFX: cap %u, want 6", varch_voice_cap(&e));

    /* two layers share the pool by price, each core's share inside 56, both layers at least 1 */
    EngineState sg = mk(VARCH_SPREAD, 0, FLT_MULTI, 0), st = mk(VARCH_STACK, 0, FLT_MULTI, 0), ss = mk(VARCH_STACK, 0, FLT_SH2, 1);
    CHECK(lim(&sg, &sg, PERF_DUAL_SYNTH, P, P, 0) == 4 && lim(&sg, &sg, PERF_DUAL_SYNTH, P, P, 1) == 4, "SINGLE + SINGLE 4 + 4");
    CHECK(lim(&sg, &st, PERF_DUAL_SYNTH, P, P, 0) == 4 && lim(&sg, &st, PERF_DUAL_SYNTH, P, P, 1) == 4, "SINGLE + STACK 4 + 4");
    CHECK(lim(&st, &st, PERF_DUAL_SYNTH, P, P, 0) == 3 && lim(&st, &st, PERF_DUAL_SYNTH, P, P, 1) == 3,
          "STACK + STACK 3 + 3 (%u + %u)", lim(&st, &st, PERF_DUAL_SYNTH, P, P, 0), lim(&st, &st, PERF_DUAL_SYNTH, P, P, 1));
    unsigned a = lim(&sg, &ss, PERF_DUAL_SYNTH, P, P, 0), b = lim(&sg, &ss, PERF_DUAL_SYNTH, P, P, 1);
    CHECK(a + b <= VOICE_POOL && a >= 1 && b >= 1 && voice_core_cost(10, (uint8_t)a, 23, (uint8_t)b, 0) <= VOICE_CORE_COST &&
          voice_core_cost(10, (uint8_t)a, 23, (uint8_t)b, 1) <= VOICE_CORE_COST, "SINGLE + dear STACK fits the cores (%u + %u)", a, b);
    CHECK(a >= b, "the cheap layer gets at least what the dear one does (%u + %u)", a, b);
    /* mono layers: one voice, the rest to the other layer as far as the cores carry it */
    CHECK(lim(&sg, &sg, PERF_DUAL_SYNTH, VMODE_MONO_RETRIG, P, 0) == 1 && lim(&sg, &sg, PERF_DUAL_SYNTH, VMODE_MONO_RETRIG, P, 1) == 7,
          "mono + SINGLE 1 + 7");
    CHECK(lim(&sg, &sg, PERF_DUAL_SYNTH, P, VMODE_MONO_LEGATO, 0) == 7 && lim(&sg, &sg, PERF_DUAL_SYNTH, P, VMODE_MONO_LEGATO, 1) == 1,
          "SINGLE + mono 7 + 1");
    CHECK(lim(&sg, &sg, PERF_DUAL_SYNTH, VMODE_MONO_RETRIG, VMODE_MONO_RETRIG, 0) == 1 &&
          lim(&sg, &sg, PERF_DUAL_SYNTH, VMODE_MONO_RETRIG, VMODE_MONO_RETRIG, 1) == 1, "both mono");
    CHECK(lim(&sg, &ss, PERF_SINGL_SYNTH, P, P, 1) == 0 && lim(&sg, &ss, PERF_SINGL_SYNTH, P, P, 0) == 8, "SINGL: layer 1 alone");
    /* every pair of architectures and filters: inside the cores, never below one, never past the pool */
    for (uint8_t a1 = 0; a1 < VARCH_COUNT; a1++)
        for (uint8_t a2 = 0; a2 < VARCH_COUNT; a2++)
            for (uint8_t f = 0; f < 2; f++)
                for (uint8_t p = 0; p < 2; p++) {
                    EngineState x = mk(a1, 30, f ? FLT_SH2 : FLT_MULTI, p), y = mk(a2, 30, FLT_SH2, p);
                    unsigned n0 = lim(&x, &y, PERF_DUAL_SYNTH, P, P, 0), n1 = lim(&x, &y, PERF_DUAL_SYNTH, P, P, 1);
                    CHECK(n0 >= 1 && n1 >= 1 && n0 + n1 <= VOICE_POOL, "pair %u/%u: %u + %u", a1, a2, n0, n1);
                }

    /* placement: layer 2 at the top of the pool, so odd counts split the cores */
    synth_init();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    engine_state[0] = st;
    engine_state[1] = st;
    int first, count;
    pool_range(0, &first, &count);
    CHECK(first == 0 && count == 3, "L1 range %d + %d", first, count);
    pool_range(1, &first, &count);
    CHECK(first == 5 && count == 3, "L2 range %d + %d (the top three: 5 6 7)", first, count);
    CHECK(voice_core_cost(15, 3, 15, 3, 0) == 45 && voice_core_cost(15, 3, 15, 3, 1) == 45, "3 + 3 splits the cores 45 / 45");

    /* source tails: the ARP, SEQ and REC loop keep SRC_TAILS released notes a layer; a third takes the oldest of its own */
    synth_init();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    engine_state[0] = sg;
    for (int v = 0; v < 8; v++) {
        poly_bank[v].src = VSRC_KEYS;
        poly_bank[v].is_gate_open = 0;
        poly_bank[v].silent = 1;
        poly_bank[v].choke = 0;
        poly_bank[v].age_counter = 0;
    }
    for (int v = 0; v < 2; v++) {                   /* two ARP tails, ages 5 and 9 */
        poly_bank[v].src = VSRC_ARP;
        poly_bank[v].silent = 0;
        poly_bank[v].age_counter = v ? 9u : 5u;
    }
    CHECK(allocate_voice_slot(0, VSRC_ARP) == 1, "an ARP note past its tails takes its oldest tail (voice 1), took %d",
          allocate_voice_slot(0, VSRC_ARP));
    CHECK(allocate_voice_slot(0, VSRC_KEYS) >= 2, "a key takes a free voice, not the ARP's tails (took %d)",
          allocate_voice_slot(0, VSRC_KEYS));
    CHECK(allocate_voice_slot(0, VSRC_SEQ) >= 2, "the SEQ is not bound by the ARP's tails (took %d)", allocate_voice_slot(0, VSRC_SEQ));
    poly_bank[1].silent = 1;                        /* one tail: a free voice is taken */
    CHECK(allocate_voice_slot(0, VSRC_ARP) >= 1 && allocate_voice_slot(0, VSRC_ARP) != 0, "below SRC_TAILS a free voice is taken");

    /* round-robin (2026-11-13): free voices are taken in turn, wrapping, never the lowest one again and again */
    voice_allocator_init();
    for (int v = 0; v < 8; v++) {
        poly_bank[v].src = VSRC_KEYS;
        poly_bank[v].is_gate_open = 0;
        poly_bank[v].silent = 1;
        poly_bank[v].choke = 0;
        poly_bank[v].age_counter = 0;
    }
    pool_range(0, &first, &count);
    CHECK(first == 0 && count == 8, "SINGLE alone: 8 voices from 0 (%d + %d)", first, count);
    for (int k = 0; k < 2 * count; k++) {            /* staccato: each note's tail ends before the next */
        int got = allocate_voice_slot(0, VSRC_KEYS);
        CHECK(got == k % count, "staccato note %d took voice %d, want %d", k, got, k % count);
    }
    voice_allocator_init();                          /* held chord: every free voice before any steal */
    int seen = 0;
    for (int k = 0; k < count; k++) {
        int got = allocate_voice_slot(0, VSRC_KEYS);
        CHECK(got >= 0 && !(seen & 1 << got), "held note %d took voice %d, already used", k, got);
        if (got >= 0) {
            seen |= 1 << got;
            poly_bank[got].is_gate_open = 1;
            poly_bank[got].silent = 0;
            poly_bank[got].age_counter = (uint32_t)(count - k);   /* the first note is the oldest */
        }
    }
    CHECK(seen == 0xFF, "all 8 voices used before a steal (0x%02X)", seen);
    CHECK(allocate_voice_slot(0, VSRC_KEYS) == 0, "the 9th note steals the oldest held, voice 0");
    poly_bank[3].is_gate_open = 0;                   /* voice 3 releases (still ringing): it goes before any held note */
    CHECK(allocate_voice_slot(0, VSRC_KEYS) == 3, "a ringing release is stolen before a held note");
    poly_bank[5].silent = 1;                         /* voice 5 frees, after the last pick (7): wraps to it */
    poly_bank[5].is_gate_open = 0;
    CHECK(allocate_voice_slot(0, VSRC_KEYS) == 5, "a free voice before a ringing one, found past the wrap");
    pool_range(1, &first, &count);
    CHECK(count == 0 || allocate_voice_slot(1, VSRC_KEYS) == -1 || allocate_voice_slot(1, VSRC_KEYS) >= first,
          "layer 2 stays in its own range");

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("voice pool: priced caps, pairs inside the cores, top-aligned layer 2, source tails\n");
    return 0;
}
