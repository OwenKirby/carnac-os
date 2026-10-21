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

    /* a patch is stored packed (v8, engine_store_pack): every byte but the two unread ones, the six LFO flags and the
     * name, then the flags as bits, then the name at 6 bits a character. A patch with every stored byte different and a
     * name using the whole character map comes back exactly; the two unread bytes come back 0 */
    {
        static EngineState e, back;
        uint8_t rec[FREC_SIZE_ENGINE];
        uint8_t *d = (uint8_t *)&e;
        CHECK(frec_size(FREC_ENGINE) == ENGINE_STORED_SIZE + FREC_HEADER, "ENGINE: size %u", frec_size(FREC_ENGINE));
        for (int pass = 0; pass < 3; pass++) {
            for (uint32_t i = 0; i < sizeof e; i++) d[i] = (uint8_t)(i * 31u + 7u + (uint32_t)pass * 101u);
            for (uint32_t i = 0; i < NAME_LEN; i++) e.name[i] = (uint8_t)((i * 3u + (uint32_t)pass * 13u) % CHAR_MAP_LEN);
            if (pass == 2) for (uint32_t i = 0; i < NAME_LEN; i++) e.name[i] = (uint8_t)(CHAR_MAP_LEN - 1u);
            for (uint32_t l = 0; l < NLFO; l++) {        /* every flag pattern across the passes */
                e.lfo[l].clock_sync = (uint8_t)(((l + (uint32_t)pass) >> 0) & 1u);
                e.lfo[l].key_reset = (uint8_t)(((l * 2u + (uint32_t)pass) >> 1) & 1u);
            }
            e.osc[0].sync = 0;
            e.env[0].invert = 0;
            frec_pack(FREC_ENGINE, &e, rec);
            CHECK(rec[0] == FREC_VER_ENGINE, "ENGINE: version byte %u", rec[0]);
            memset(&back, 0xA5, sizeof back);
            CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_OK && memcmp(&back, &e, sizeof e) == 0,
                  "ENGINE: packed round trip differs (pass %d)", pass);
        }
        for (uint32_t f = 0; f < ENG_NFLAG; f++) {      /* each flag on its own: its own bit, the others stay 0 */
            memset(&e, 0, sizeof e);
            ((uint8_t *)&e)[engine_flag_ofs(f)] = 1u;
            frec_pack(FREC_ENGINE, &e, rec);
            frec_unpack(FREC_ENGINE, rec, &back);
            CHECK(memcmp(&back, &e, sizeof e) == 0, "ENGINE: LFO flag %u alone", (unsigned)f);
        }
        e.osc[0].sync = 1;
        e.env[0].invert = 1;
        e.osc[1].sync = 1;
        e.env[1].invert = 1;
        frec_pack(FREC_ENGINE, &e, rec);
        frec_unpack(FREC_ENGINE, rec, &back);
        CHECK(back.osc[0].sync == 0 && back.env[0].invert == 0 && back.osc[1].sync == e.osc[1].sync &&
              back.env[1].invert == e.env[1].invert, "ENGINE: the unread bytes");
        /* a v7 patch (BEND last, no BEND MOD), packed here as v7 wrote it from its 179-byte RAM layout: each byte lands
         * where engine_from_v7 then engine_from_v8_ram say (BEND MOD inserted after BEND, each filter's DRIVE after its
         * last byte), BEND MOD gets VELOCITY / centre, the LFO flags come back as bytes; FILTER1 an MS-20 LOW (its morph
         * the DRIVE now), FILTER2 an SVF (MULTI now) */
        {
            static uint8_t v7[ENGINE_V7_RAM], old[ENGINE_STORED_SIZE];
            uint32_t u1 = (uint32_t)offsetof(EngineState, osc[0].sync), u2 = (uint32_t)offsetof(EngineState, env[0].invert) - 4u;
            uint32_t bend = (uint32_t)offsetof(EngineState, oscmix.atk_bend), nm = ENGINE_V7_RAM - NAME_LEN, j = 0, acc = 0, bits = 0;
            uint32_t f0 = (uint32_t)offsetof(EngineState, filter[0]) - 2u, f1 = f0 + (uint32_t)sizeof(FilterParams) - 1u;
            uint32_t d0 = (uint32_t)offsetof(EngineState, filter[0].drive);
            for (uint32_t i = 0; i < ENGINE_V7_RAM; i++) v7[i] = (uint8_t)(i * 13u + 5u);
            v7[u1] = v7[u2] = 0u;
            v7[f0 + offsetof(FilterParams, flavour)] = 2u;   /* v8's MS-20 LOW */
            v7[f0 + offsetof(FilterParams, morph)] = 90u;
            v7[f1 + offsetof(FilterParams, flavour)] = 0u;   /* v8's SVF */
            v7[f1 + offsetof(FilterParams, morph)] = 127u;
            for (uint32_t i = 0; i < NAME_LEN; i++) v7[nm + i] = (uint8_t)((i * 7u + 3u) % CHAR_MAP_LEN);
            for (uint32_t i = 0; i < ENGINE_V7_RAM; i++)
                if (i != u1 && i != u2 && i != bend && i < nm) old[j++] = v7[i];
            for (uint32_t i = 0; i < NAME_LEN; i++) {
                acc |= (uint32_t)v7[nm + i] << bits;
                for (bits += 6u; bits >= 8u; bits -= 8u, acc >>= 8) old[j++] = (uint8_t)acc;
            }
            while (j < ENGINE_STORED_SIZE - 1u) old[j++] = 0u;
            old[ENGINE_STORED_SIZE - 1u] = (uint8_t)(v7[bend] - 64u);
            memset(&back, 0xA5, sizeof back);
            engine_from_v7(old, &back);
            const uint8_t *b = (const uint8_t *)&back;
            int ok = 1;
            for (uint32_t i = 0; i < ENGINE_V7_RAM; i++) {
                uint32_t k = i <= bend ? i : i + 2u;                                  /* v8 */
                k = k < d0 ? k : k < d0 + (uint32_t)sizeof(FilterParams) - 1u ? k + 1u : k + 2u;   /* v9 */
                uint32_t r = k - (uint32_t)offsetof(EngineState, filter[0]);
                if (k >= (uint32_t)offsetof(EngineState, filter[0]) && r < 2u * sizeof(FilterParams)) {
                    r %= (uint32_t)sizeof(FilterParams);
                    if (r == offsetof(FilterParams, flavour) || r == offsetof(FilterParams, morph) ||
                        r == offsetof(FilterParams, morph_mod_amt))
                        continue;                                                     /* converted: below */
                }
                ok &= b[k] == v7[i];
            }
            CHECK(ok, "ENGINE: a v7 patch's bytes not where they were");
            CHECK(back.oscmix.atk_bend_mod_src == MSRC_VELOCITY && back.oscmix.atk_bend_mod_amt == 64u,
                  "ENGINE: v7 -> BEND MOD defaults");
            CHECK(back.filter[0].flavour == FLT_MS20 && back.filter[0].drive == 90u && back.filter[0].morph == 0u &&
                  back.filter[0].morph_mod_amt == BIPOLAR_CENTER, "ENGINE: v8's MS-20 LOW -> the MS-20, its morph the DRIVE");
            CHECK(back.filter[1].flavour == FLT_MULTI && back.filter[1].morph == 91u && back.filter[1].drive == 0u &&
                  back.filter[1].morph_mod_amt == v7[f1 + offsetof(FilterParams, morph_mod_amt)],
                  "ENGINE: v8's SVF (HP) -> MULTI HP12");
        }
        /* a stored v8 patch (today's stored form without the two DRIVE bytes, then 2 spare 0s): engine_from_v8 gives
         * the patch back, DRIVE 0 but where the filter conversion sets it: FILTER1 an MS-20 HIGH (MS-20, HP, its old
         * morph the DRIVE), FILTER2 FORMANT (4 -> 3) */
        {
            EngineState want;
            uint8_t st9[ENGINE_STORED_SIZE], st8[ENGINE_STORED_SIZE], *w = (uint8_t *)&want;
            uint32_t j = 0;
            for (uint32_t i = 0; i < sizeof want; i++) w[i] = (uint8_t)(i * 7u + 1u);
            want.osc[0].sync = 0u; want.env[0].invert = 0u;
            for (uint32_t l = 0; l < NLFO; l++) { want.lfo[l].clock_sync = 1u; want.lfo[l].key_reset = (uint8_t)(l & 1u); }
            for (uint32_t i = 0; i < NAME_LEN; i++) want.name[i] = (uint8_t)((i * 5u + 2u) % CHAR_MAP_LEN);
            want.filter[0].flavour = 3u; want.filter[0].morph = 40u; want.filter[0].drive = 0u;
            want.filter[1].flavour = 4u; want.filter[1].drive = 0u;
            engine_store_pack(&want, st9);
            for (uint32_t i = 0; i < ENGINE_STORED_SIZE; i++)
                if (i != ENG_ST_DRIVE0 && i != ENG_ST_DRIVE1) st8[j++] = st9[i];
            while (j < ENGINE_STORED_SIZE) st8[j++] = 0u;
            want.filter[0].flavour = FLT_MS20; want.filter[0].drive = 40u; want.filter[0].morph = (uint8_t)VAL_MAX;
            want.filter[0].morph_mod_amt = BIPOLAR_CENTER;
            want.filter[1].flavour = FLT_FORMANT;
            memset(&back, 0xA5, sizeof back);
            engine_from_v8(st8, &back);
            CHECK(memcmp(&back, &want, sizeof want) == 0, "ENGINE: a v8 patch to v9");
        }
        CHECK(filter_svf_to_multi(0u) == 36u && filter_svf_to_multi(64u) == 73u && filter_svf_to_multi(127u) == 91u &&
              !strcmp(filter_multi_mode_name(36), "LP12") && !strcmp(filter_multi_mode_name(73), "BP") &&
              !strcmp(filter_multi_mode_name(91), "HP12"), "ENGINE: the SVF's LP / BP / HP on MULTI's LP12 / BP / HP12");
        memset(&back, 0x5A, sizeof back);
        rec[0] = 0xFFu;
        CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_EMPTY, "ENGINE: erased slot not empty");
        rec[0] = 8u;
        CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_OLD_LAYOUT && back.arch == 0x5A, "ENGINE: v8 record accepted");
        printf("ENGINE   record %3u B (version %u + %u B packed, %u B in RAM)\n", frec_size(FREC_ENGINE),
               (unsigned)FREC_VER_ENGINE, (unsigned)ENGINE_STORED_SIZE, (unsigned)sizeof(EngineState));
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
