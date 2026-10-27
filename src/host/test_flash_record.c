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

/* what engine_from_v9 makes of a v9 patch's v10 bytes and cutoff amounts */
static void want_from_v9(EngineState *w)
{
    for (uint32_t i = 0; i < NENV; i++) w->env_vel_level[i] = i == 0u ? (uint8_t)ENV1_VEL_LEVEL_V9 : 0u;
    for (uint32_t i = 0; i < NLFO; i++) { w->lfo_depth_src[i] = MSRC_MODWHEEL; w->lfo_depth_amt[i] = 0u; }
    for (uint32_t f = 0; f < NFILTER; f++) {
        uint8_t a = w->filter[f].mod_amt;
        w->filter[f].mod_amt = a <= 64u ? 0u : a >= 127u ? 126u : (uint8_t)((a - 64u) * 2u);
    }
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
            e.analog = (uint8_t)(40u + (uint32_t)pass * 43u);   /* 0..127, in the last stored byte */
            for (uint32_t f = 0; f < NFILTER; f++) {     /* sources fit 4 bits; KEY TRACK in their high nibbles */
                e.filter[f].mod_src &= 15u;
                e.filter[f].morph_mod_src &= 15u;
                e.filter_key_track[f] = (uint8_t)(pass == 2 ? VAL_MAX - f : (f * 77u + (uint32_t)pass * 45u) & VAL_MAX);
            }
            for (uint32_t f = 0; f < ENG_NEFLAG; f++)   /* v10: the envelope flags 0 / 1, as bits */
                d[engine_eflag_ofs(f)] = (uint8_t)(((f * 5u + (uint32_t)pass) >> 1) & 1u);
            for (uint32_t l = 0; l < NLFO; l++) {        /* v10: DEPTH MOD SRC in SPD MOD SRC's high nibble */
                e.lfo[l].speed_mod_src &= 15u;
                e.lfo_depth_src[l] = (uint8_t)((l * 7u + (uint32_t)pass * 3u) & 15u);
            }
            frec_pack(FREC_ENGINE, &e, rec);
            CHECK(rec[FREC_SIZE_ENGINE - 1u] == e.analog, "ENGINE: ANALOG not in the last stored byte (pass %d)", pass);
            CHECK(rec[0] == FREC_VER_ENGINE, "ENGINE: version byte %u", rec[0]);
            memset(&back, 0xA5, sizeof back);
            CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_OK && memcmp(&back, &e, sizeof e) == 0,
                  "ENGINE: packed round trip differs (pass %d)", pass);
        }
        for (uint32_t f = 0; f < ENG_NFLAG; f++) {      /* each flag on its own: its own bit, the others stay 0 */
            memset(&e, 0, sizeof e);
            e.arch = VARCH_SPREAD;                      /* (all zero is SINGLE, which loads as SPREAD) */
            ((uint8_t *)&e)[engine_flag_ofs(f)] = 1u;
            frec_pack(FREC_ENGINE, &e, rec);
            frec_unpack(FREC_ENGINE, rec, &back);
            CHECK(memcmp(&back, &e, sizeof e) == 0, "ENGINE: LFO flag %u alone", (unsigned)f);
        }
        for (uint32_t f = 0; f < ENG_NEFLAG; f++) {     /* v10: each envelope flag on its own */
            memset(&e, 0, sizeof e);
            e.arch = VARCH_SPREAD;                      /* (all zero is SINGLE, which loads as SPREAD) */
            ((uint8_t *)&e)[engine_eflag_ofs(f)] = 1u;
            frec_pack(FREC_ENGINE, &e, rec);
            frec_unpack(FREC_ENGINE, rec, &back);
            CHECK(memcmp(&back, &e, sizeof e) == 0, "ENGINE: envelope flag %u alone", (unsigned)f);
        }
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
        CHECK(engine_eflag_ofs(6) == offsetof(EngineState, env[1].invert) &&
              engine_eflag_ofs(7) == offsetof(EngineState, env[2].invert) &&
              engine_eflag_ofs(5) == offsetof(EngineState, env[2].legato), "ENGINE: the envelope flags' bytes");
        /* a stored v9 patch -> v10: every byte where it was, the v10 bytes as v9 played, the cutoff amount doubled from
         * its centre (a negative one 0) */
        {
            EngineState want;
            uint8_t st9[ENGINE_STORED_SIZE], *w = (uint8_t *)&want;
            for (uint32_t i = 0; i < sizeof want; i++) w[i] = (uint8_t)(i * 11u + 3u);
            want.osc[0].sync = 0u; want.env[0].invert = 0u;
            for (uint32_t f = 0; f < NFILTER; f++) {
                want.filter[f].mod_src &= 15u; want.filter[f].morph_mod_src &= 15u;
                want.filter_key_track[f] = (uint8_t)(30u + f * 50u);
            }
            for (uint32_t l = 0; l < NLFO; l++) { want.lfo[l].clock_sync = (uint8_t)(l & 1u); want.lfo[l].key_reset = 1u; }
            for (uint32_t i = 0; i < NAME_LEN; i++) want.name[i] = (uint8_t)((i * 3u + 1u) % CHAR_MAP_LEN);
            want.analog = 77u;
            want.filter[0].mod_amt = 100u;   /* +36 -> 72 */
            want.filter[1].mod_amt = 20u;    /* -44 -> 0 */
            engine_store_pack_v9(&want, st9);
            want_from_v9(&want);
            CHECK(want.filter[0].mod_amt == 72u && want.filter[1].mod_amt == 0u, "ENGINE: v9 cutoff amounts");
            memset(&back, 0xA5, sizeof back);
            engine_from_v9_stored(st9, &back);
            CHECK(memcmp(&back, &want, sizeof want) == 0, "ENGINE: a v9 patch to v10");
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
                        r == offsetof(FilterParams, morph_mod_amt) || r == offsetof(FilterParams, mod_amt))
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
            for (uint32_t f = 0; f < NFILTER; f++) {     /* v8 sources fit 4 bits, so its KEY TRACK reads 0 */
                want.filter[f].mod_src &= 15u; want.filter[f].morph_mod_src &= 15u; want.filter_key_track[f] = 0u;
            }
            for (uint32_t l = 0; l < NLFO; l++) { want.lfo[l].clock_sync = 1u; want.lfo[l].key_reset = (uint8_t)(l & 1u); }
            for (uint32_t i = 0; i < NAME_LEN; i++) want.name[i] = (uint8_t)((i * 5u + 2u) % CHAR_MAP_LEN);
            want.filter[0].flavour = 3u; want.filter[0].morph = 40u; want.filter[0].drive = 0u;
            want.filter[1].flavour = 4u; want.filter[1].drive = 0u;
            engine_store_pack_v9(&want, st9);
            for (uint32_t i = 0; i < ENGINE_STORED_SIZE; i++)
                if (i != ENG_ST_DRIVE0 && i != ENG_ST_DRIVE1) st8[j++] = st9[i];
            while (j < ENGINE_STORED_SIZE) st8[j++] = 0u;
            want.filter[0].flavour = FLT_MS20; want.filter[0].drive = 40u; want.filter[0].morph = (uint8_t)VAL_MAX;
            want.filter[0].morph_mod_amt = BIPOLAR_CENTER;
            want.filter[1].flavour = FLT_FORMANT;
            want_from_v9(&want);
            memset(&back, 0xA5, sizeof back);
            engine_from_v8(st8, &back);
            CHECK(memcmp(&back, &want, sizeof want) == 0, "ENGINE: a v8 patch to v10");
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
