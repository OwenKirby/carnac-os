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

    /* a patch is stored packed (v7, engine_store_pack): every byte but the two unread ones and the name, the name at
     * 6 bits a character. A patch with every stored byte different and a name using the whole character map comes back
     * exactly; the two unread bytes come back 0 */
    {
        static EngineState e, back;
        uint8_t rec[FREC_SIZE_ENGINE];
        uint8_t *d = (uint8_t *)&e;
        CHECK(frec_size(FREC_ENGINE) == ENGINE_STORED_SIZE + FREC_HEADER, "ENGINE: size %u", frec_size(FREC_ENGINE));
        for (int pass = 0; pass < 3; pass++) {
            for (uint32_t i = 0; i < sizeof e; i++) d[i] = (uint8_t)(i * 31u + 7u + (uint32_t)pass * 101u);
            for (uint32_t i = 0; i < NAME_LEN; i++) e.name[i] = (uint8_t)((i * 3u + (uint32_t)pass * 13u) % CHAR_MAP_LEN);
            if (pass == 2) for (uint32_t i = 0; i < NAME_LEN; i++) e.name[i] = (uint8_t)(CHAR_MAP_LEN - 1u);
            e.osc[0].sync = 0;
            e.env[0].invert = 0;
            frec_pack(FREC_ENGINE, &e, rec);
            CHECK(rec[0] == FREC_VER_ENGINE, "ENGINE: version byte %u", rec[0]);
            memset(&back, 0xA5, sizeof back);
            CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_OK && memcmp(&back, &e, sizeof e) == 0,
                  "ENGINE: packed round trip differs (pass %d)", pass);
        }
        e.oscmix.atk_bend = 64;                          /* no bend: the last byte 0, as v7 wrote it before BEND */
        frec_pack(FREC_ENGINE, &e, rec);
        CHECK(rec[FREC_SIZE_ENGINE - 1u] == 0u, "ENGINE: BEND's centre not stored as 0");
        rec[FREC_SIZE_ENGINE - 1u] = 0u;
        frec_unpack(FREC_ENGINE, rec, &back);
        CHECK(back.oscmix.atk_bend == 64u, "ENGINE: a 0 in the last byte is not BEND's centre");
        e.osc[0].sync = 1;
        e.env[0].invert = 1;
        frec_pack(FREC_ENGINE, &e, rec);
        frec_unpack(FREC_ENGINE, rec, &back);
        CHECK(back.osc[0].sync == 0 && back.env[0].invert == 0 && back.osc[1].sync == e.osc[1].sync &&
              back.env[1].invert == e.env[1].invert, "ENGINE: the unread bytes");
        memset(&back, 0x5A, sizeof back);
        rec[0] = 0xFFu;
        CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_EMPTY, "ENGINE: erased slot not empty");
        rec[0] = 6u;
        CHECK(frec_unpack(FREC_ENGINE, rec, &back) == FREC_OLD_LAYOUT && back.arch == 0x5A, "ENGINE: v6 record accepted");
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
