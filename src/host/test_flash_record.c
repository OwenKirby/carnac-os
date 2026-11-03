/* Host test for the flash record framing (spec 6.4, src/dsp/flash_record.h): every kind packs to its version byte plus
 * the struct's bytes and round-trips; empty (0x00 / 0xFF) and other-version records are refused and leave the
 * destination untouched. Exit code 0 = pass. Test scaffolding only.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware src/dsp/preset.c src/host/test_flash_record.c -o test_flash_record */
#include <stdio.h>
#include <string.h>
#include "PresetStructures.h"
#include "preset.h"
#include "flash_record.h"
#include "filter.h"                   /* filter_multi_mode_name */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void roundtrip(FrecKind k, const char *name, const void *src, uint16_t payload)
{
    uint8_t rec[512], back[512], before[512];
    CHECK(frec_size(k) == payload + FREC_HEADER, "%s: size %u, want %u", name, frec_size(k), payload + FREC_HEADER);
    frec_pack(k, src, rec);
    CHECK(rec[0] == frec_version(k), "%s: version byte %u", name, rec[0]);
    CHECK(memcmp(rec + FREC_HEADER, src, payload) == 0, "%s: payload not the struct's bytes", name);

    memset(back, 0xA5, sizeof(back));
    CHECK(frec_unpack(k, rec, back) == FREC_OK, "%s: current version refused", name);
    CHECK(memcmp(back, src, payload) == 0, "%s: round trip differs", name);

    memset(before, 0x5A, sizeof(before));
    memcpy(back, before, sizeof(back));
    rec[0] = 0xFFu;
    CHECK(frec_unpack(k, rec, back) == FREC_EMPTY, "%s: erased slot not empty", name);
    rec[0] = 0x00u;
    CHECK(frec_unpack(k, rec, back) == FREC_EMPTY, "%s: zeroed slot not empty", name);
    rec[0] = (uint8_t)(frec_version(k) + 1u);
    CHECK(frec_unpack(k, rec, back) == FREC_OLD_LAYOUT, "%s: other version accepted", name);
    CHECK(frec_stored_version(rec) == (uint8_t)(frec_version(k) + 1u), "%s: stored version", name);
    CHECK(memcmp(back, before, sizeof(back)) == 0, "%s: refused record wrote the destination", name);
    printf("%-8s record %3u B (version %u + %u B)\n", name, frec_size(k), frec_version(k), payload);
}

int main(void)
{
    SequencerLane seq[SEQ_NLANE];
    ArpState arp;
    GlobalSystemConfig g = {3, 1, 7, 0};

    preset_defaults();
    engine_state[0].osc[0].scan = 99;
    engine_state[0].arch = VARCH_STACK;
    memset(seq, 0, sizeof(seq));
    seq[2].steps[5].note_buffer[0] = 64;
    seq[3].loop_end = 15;
    memset(&arp, 0, sizeof(arp));
    arp.sequence_length = 8;
    arp.steps[7].variation = 11;

    /* a patch is stored packed (v12, engine_store_pack): every byte at its width (ENG_W). Every field at its widest
     * value, then at random values in range, comes back exactly; each 1-bit switch on its own sets only itself; the
     * widths fit ENGINE_STORED_SIZE and say how much is left for fields added at the end */
    {
        static EngineState e, back;
        uint8_t rec[FREC_SIZE_ENGINE];
        uint8_t *d = (uint8_t *)&e;
        uint32_t bits = 0, seed = 12345u;
        for (uint32_t i = 0; i < sizeof ENG_W; i++) bits += ENG_W[i];
        CHECK(bits <= ENGINE_STORED_SIZE * 8u, "ENGINE: widths %u bits, more than %u bytes", (unsigned)bits,
              (unsigned)ENGINE_STORED_SIZE);
        CHECK(frec_size(FREC_ENGINE) == ENGINE_STORED_SIZE + FREC_HEADER, "ENGINE: size %u", frec_size(FREC_ENGINE));
        for (int pass = 0; pass < 66; pass++) {
            for (uint32_t i = 0; i < sizeof e; i++) {
                uint32_t top = (1u << ENG_W[i]) - 1u;
                seed = seed * 1103515245u + 12345u;
                d[i] = (uint8_t)(pass == 0 ? top : pass == 1 ? 0u : (seed >> 16) & top);
            }
            if (e.mfx.type >= MFX_COUNT) e.mfx.type = MFX_COUNT - 1u;   /* (unpack turns an unknown type OFF) */
            if (e.arch == VARCH_SINGLE) e.arch = VARCH_SPREAD;         /* (SINGLE loads as SPREAD) */
            frec_pack(FREC_ENGINE, &e, rec);
            CHECK(rec[0] == FREC_VER_ENGINE, "ENGINE: version byte %u", rec[0]);
            memset(&back, 0xA5, sizeof back);
            CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_OK && memcmp(&back, &e, sizeof e) == 0,
                  "ENGINE: packed round trip differs (pass %d)", pass);
            if (pass == 0)                              /* the bytes past the widths stay 0 */
                for (uint32_t j = (bits + 7u) / 8u; j < ENGINE_STORED_SIZE; j++)
                    CHECK(rec[FREC_HEADER + j] == 0u, "ENGINE: stored byte %u past the fields not 0", (unsigned)j);
        }
        for (uint32_t i = 0; i < sizeof e; i++) {       /* each switch alone */
            if (ENG_W[i] != 1u) continue;
            memset(&e, 0, sizeof e);
            e.arch = VARCH_SPREAD;
            d[i] = 1u;
            frec_pack(FREC_ENGINE, &e, rec);
            frec_unpack(FREC_ENGINE, rec, &back);
            CHECK(memcmp(&back, &e, sizeof e) == 0, "ENGINE: switch at byte %u alone", (unsigned)i);
        }
        CHECK(ENG_W[offsetof(EngineState, osc[1].sync)] == 1u && ENG_W[offsetof(EngineState, env[2].invert)] == 1u &&
              ENG_W[offsetof(EngineState, lfo[2].key_reset)] == 1u && ENG_W[offsetof(EngineState, osc[1].wavebank)] == 8u &&
              ENG_W[offsetof(EngineState, mod[3].src_b)] == EW_SRC && ENG_W[offsetof(EngineState, arch)] == 4u &&
              ENG_W[offsetof(EngineState, name)] == 6u && ENG_W[offsetof(EngineState, analog)] == EW_V &&
              ENG_W[offsetof(EngineState, lfo_depth_src)] == EW_SRC && ENG_W[offsetof(EngineState, mfx.type)] == 5u &&
              ENG_W[offsetof(EngineState, reserved) + ENGINE_RESERVED - 1u] == 8u &&
              ENG_W[offsetof(EngineState, send[0].dest)] == 8u && ENG_W[sizeof e - 1u] == EW_V, "ENGINE: ENG_W out of step");
        {                                               /* a SINGLE patch loads as SPREAD at SPREAD 0 (2026-10-26) */
            memset(&e, 0, sizeof e);
            e.oscmix.spread = 70;                       /* leftovers SINGLE ignored */
            e.oscmix.spread_mod_amt = 30;
            e.oscmix.spread_mod_src = MSRC_LFO1;
            frec_pack(FREC_ENGINE, &e, rec);
            frec_unpack(FREC_ENGINE, rec, &back);
            CHECK(back.arch == VARCH_SPREAD && back.oscmix.spread == 0 && back.oscmix.spread_mod_amt == 0 &&
                  back.oscmix.spread_mod_src == MSRC_LFO1, "ENGINE: SINGLE -> SPREAD at 0 (arch %u spread %u mod %u)",
                  back.arch, back.oscmix.spread, back.oscmix.spread_mod_amt);
            e.arch = VARCH_SPREAD;                      /* a SPREAD patch keeps its spread */
            frec_pack(FREC_ENGINE, &e, rec);
            frec_unpack(FREC_ENGINE, rec, &back);
            CHECK(back.arch == VARCH_SPREAD && back.oscmix.spread == 70 && back.oscmix.spread_mod_amt == 30,
                  "ENGINE: SPREAD patch lost its spread");
        }
        memset(&back, 0x5A, sizeof back);
        rec[0] = 0xFFu;
        CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_EMPTY, "ENGINE: erased slot not empty");
        rec[0] = 11u;
        CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_OLD_LAYOUT && back.arch == 0x5A, "ENGINE: v11 record accepted");
        {                                               /* an unknown MFX type loads as OFF */
            memset(&e, 0, sizeof e);
            e.arch = VARCH_SPREAD;
            e.mfx = (MfxParams){MFX_COUNT, {99, 50, 115, 60}, 40, {1, 30}};
            frec_pack(FREC_ENGINE, &e, rec);
            frec_unpack(FREC_ENGINE, rec, &back);
            CHECK(back.mfx.type == MFX_OFF && back.mfx.knob[0] == 99u, "ENGINE: an unknown MFX type loads as OFF");
        }
        printf("ENGINE   record %3u B (version %u + %u B packed: fields %u bits = %u B, %u B free; %u B in RAM)\n",
               frec_size(FREC_ENGINE), (unsigned)FREC_VER_ENGINE, (unsigned)ENGINE_STORED_SIZE, (unsigned)bits,
               (unsigned)((bits + 7u) / 8u), (unsigned)(ENGINE_STORED_SIZE - (bits + 7u) / 8u), (unsigned)sizeof(EngineState));
    }
    roundtrip(FREC_PERF, "PERF", &perf_state, sizeof(MasterPerformancePreset));
    roundtrip(FREC_SEQ, "SEQ", seq, sizeof(seq));
    roundtrip(FREC_ARP, "ARP", &arp, sizeof(arp));
    roundtrip(FREC_GLOBAL, "GLOBAL", &g, sizeof(g));
    CHECK(frec_size((FrecKind)FREC_KIND_COUNT) == 0u, "unknown kind has a size");
    CHECK(frec_unpack((FrecKind)FREC_KIND_COUNT, (const uint8_t *)"\x01", &g) == FREC_BAD_KIND, "unknown kind unpacked");
    printf("128 patches: %u B of flash\n", (unsigned)(PATCH_SLOTS * FREC_SIZE_ENGINE));

    printf(fails ? "\n%d check(s) failed\n" : "\nall checks passed\n", fails);
    return fails != 0;
}
