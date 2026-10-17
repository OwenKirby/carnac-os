/* Host test of the FM1 preset store (src/hardware/preset_store_fm1.c) on a simulated 1 MiB NOR (erase: 0xFF;
 * program: clears bits only), with X0X's storage.c hooks stood in: a fresh store over the old wavetable image reads
 * empty; every bank saves and loads back (the last, part-filled chunk too); saves take the two copies in turn; a save
 * cut off at any point (after the erase, during the payload, before the header) leaves the save before it; a chunk of
 * another layout version, or with a damaged payload, is left out.
 *   gcc -O1 -Wall -Wextra -Isrc -Isrc/dsp -Isrc/hardware src/host/test_preset_store.c src/dsp/wavetables_image.c \
 *       -o test_preset_store && ./test_preset_store
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PresetStructures.h"
#include "UIStateMachine.h"
#include "dsp/preset.h"
#include "dsp/flash_record.h"
#include "dsp/wavetables.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* ---- the flash and X0X's storage.c hooks */
static uint8_t nor[0x100000];
static int progs_left = -1;                          /* >= 0: the power is cut after that many more programs */
static int erases_left = -1;
static int cut;
static uint8_t st_buf[4096 - 256] __attribute__((aligned(4)));
static int in_store(uint32_t off, uint32_t n) { return off >= 0x97000u && off + n <= 0xE0000u; }
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if ((off & 0xFFFu) || !in_store(off, 0x1000u)) return -8;
    if (cut || erases_left == 0) { cut = 1; return -1; }
    if (erases_left > 0) erases_left--;
    memset(nor + off, 0xFF, 0x1000u);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!in_store(off, n)) return -8;
    if (cut || progs_left == 0) { cut = 1; return -1; }
    if (progs_left > 0) progs_left--;
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
static uint32_t st_crc32(const void *p, uint32_t n)  /* storage.c's, zlib CRC-32 */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *b++; c = (c >> 4) ^ T[c & 15u]; c = (c >> 4) ^ T[c & 15u]; }
    return ~c;
}

/* ---- the glue's pieces the fragment uses */
#define SYN_SLOTS 128u
EngineState patch_bank[PATCH_SLOTS];
static MasterPerformancePreset syn_perf_bank[SYN_SLOTS];
static SequencerLane syn_seq_bank[SYN_SLOTS][SEQ_NLANE];
static ArpState syn_arp_bank[SYN_SLOTS];
static uint8_t syn_used[UISAVE_COUNT][SYN_SLOTS / 8u];
static UIStateMachine sm;
static uint8_t flash_ok = 1;
static int done_calls, done_ok;
static void syn_lock(void) {}
static void syn_unlock(void) {}
void ui_sm_commit_done(UIStateMachine *m, uint8_t ok) { (void)m; done_calls++; done_ok = ok; }
static uint8_t syn_slot_used(uint8_t target, uint8_t slot)
{
    return (uint8_t)((syn_used[target][slot >> 3] >> (slot & 7u)) & 1u);
}

#include "preset_store_fm1.c"

extern const uint8_t wavetable_image_host[WT_STORE_IMAGE_LEN];

static uint8_t *slot_ptr(uint32_t k, uint32_t s) { return PS_KIND[k].bank + s * PS_KIND[k].size; }
static void fill(uint32_t k, uint32_t s, uint8_t v) { memset(slot_ptr(k, s), v, PS_KIND[k].size); }
/* slot s of bank k holds v in every byte; a patch that came back from flash: as v packed and unpacked (its two unread
 * bytes 0, its name 6 bits a character) */
static int holds(uint32_t k, uint32_t s, uint8_t v)
{
    uint32_t i;
    for (i = 0; i < PS_KIND[k].size && slot_ptr(k, s)[i] == v; i++) {}
    if (i == PS_KIND[k].size) return 1;
    if (k != UISAVE_PATCH_PRESET) return 0;
    EngineState e, back;
    uint8_t rec[ENGINE_STORED_SIZE];
    memset(&e, v, sizeof e);
    engine_store_pack(&e, rec);
    engine_store_unpack(rec, &back);
    return memcmp(&back, slot_ptr(k, s), sizeof back) == 0;
}
static void wipe_ram(void)                           /* a power cycle: the banks back to a known filler */
{
    for (uint32_t k = 0; k < UISAVE_COUNT; k++)
        for (uint32_t s = 0; s < SYN_SLOTS; s++) fill(k, s, 0xEE);
    memset(syn_used, 0, sizeof syn_used);
}
/* a SAVE as syn_commit makes it, then the main loop's write: commit_done's ok */
static int save(uint32_t k, uint32_t s, uint8_t v)
{
    fill(k, s, v);
    syn_used[k][s >> 3] |= (uint8_t)(1u << (s & 7u));
    ps_dirty |= 1u << (PS_KIND[k].first + s / PS_PER(PS_KIND[k].stored));
    ps_commit_wait = 1;
    done_calls = 0;
    ps_tick();
    CHECK(done_calls == 1 && !ps_dirty && !ps_commit_wait, "commit_done once, nothing left marked");
    return done_ok;
}
static int used_count(void)
{
    int n = 0;
    for (uint32_t k = 0; k < UISAVE_COUNT; k++)
        for (uint32_t s = 0; s < SYN_SLOTS; s++) n += syn_slot_used((uint8_t)k, (uint8_t)s);
    return n;
}

int main(void)
{
    /* the flash as an FM1 left by the old firmware: the wavetable image at 0xA0000, the rest erased */
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor + WT_STORE_BASE, wavetable_image_host, WT_STORE_IMAGE_LEN);
    printf("chunks: arp %u, seq %u, patch %u, perf %u = %u (%u KiB)\n", (unsigned)PS_N_ARP, (unsigned)PS_N_SEQ,
           (unsigned)PS_N_PATCH, (unsigned)PS_N_PERF, (unsigned)PS_NCHUNK, (unsigned)(PS_NCHUNK * 8u));
    wipe_ram();
    ps_load();
    CHECK(used_count() == 0 && holds(UISAVE_PATCH_PRESET, 0, 0xEE), "the old wavetable image read as presets");

    /* every bank: its first slot, one in the middle, its last (the part-filled last chunk) */
    static const uint8_t SL[3] = {0, 61, 127};
    for (uint32_t k = 0; k < UISAVE_COUNT; k++)
        for (int i = 0; i < 3; i++)
            CHECK(save(k, SL[i], (uint8_t)(0x10 + k * 16 + i)), "save bank %u slot %u", (unsigned)k, SL[i]);
    wipe_ram();
    ps_load();
    CHECK(used_count() == 12, "after a power cycle %d slots used, not 12", used_count());
    for (uint32_t k = 0; k < UISAVE_COUNT; k++)
        for (int i = 0; i < 3; i++)
            CHECK(syn_slot_used((uint8_t)k, SL[i]) && holds(k, SL[i], (uint8_t)(0x10 + k * 16 + i)),
                  "bank %u slot %u did not come back", (unsigned)k, SL[i]);
    CHECK(holds(UISAVE_PATCH_PRESET, 1, 0xEE), "a slot never saved was overwritten");

    /* the two copies in turn: a chunk's headers' seq 1 and 2 after two saves, then 3 over the first */
    uint32_t ch = PS_KIND[UISAVE_ARP_SEQ].first;
    PsHdr a, b;
    st_read(ps_sector(ch, 0), &a, sizeof a);
    st_read(ps_sector(ch, 1), &b, sizeof b);
    CHECK(a.magic == PS_MAGIC && b.magic == PS_MAGIC && ((a.seq == 1 && b.seq == 2) || (a.seq == 2 && b.seq == 1)),
          "arp chunk 0's copies after 2 saves: seq %u / %u", a.seq, b.seq);

    /* a save cut off: after the erase, after part of the payload, before the header; each leaves the save before */
    for (int at = 0; at < 3; at++) {
        cut = 0;
        progs_left = at == 0 ? 0 : at == 1 ? 3 : (int)((PS_USED_BYTES + PS_PER(PS_KIND[UISAVE_SEQ_SETTINGS].size) *
                                                         PS_KIND[UISAVE_SEQ_SETTINGS].size + 255u) / 256u);
        CHECK(!save(UISAVE_SEQ_SETTINGS, 61, 0x77), "a save cut off (case %d) reported SAVED", at);
        progs_left = -1;
        cut = 0;
        wipe_ram();
        ps_load();
        CHECK(syn_slot_used(UISAVE_SEQ_SETTINGS, 61) && holds(UISAVE_SEQ_SETTINGS, 61, 0x10 + 16 + 1),
              "a save cut off (case %d) lost the save before it", at);
        CHECK(used_count() == 12, "a save cut off (case %d): %d slots used", at, used_count());
    }
    /* .. and the next save goes through */
    CHECK(save(UISAVE_SEQ_SETTINGS, 61, 0x78), "the save after the cut");
    wipe_ram();
    ps_load();
    CHECK(holds(UISAVE_SEQ_SETTINGS, 61, 0x78), "the save after the cut did not land");

    /* many saves of one chunk: always the last one back */
    for (int i = 0; i < 40; i++)
        CHECK(save(UISAVE_PATCH_PRESET, 5, (uint8_t)(0x30 + i)), "save %d", i);
    wipe_ram();
    ps_load();
    CHECK(holds(UISAVE_PATCH_PRESET, 5, 0x30 + 39) && holds(UISAVE_PATCH_PRESET, 0, 0x10 + 2 * 16),
          "after 40 saves: the last one, and its chunk's other slot, back");

    /* another layout version: the chunk is left out (its slots empty, not misread) */
    uint32_t pch = PS_KIND[UISAVE_WHOLE_PERF].first;
    for (uint32_t c = 0; c < 2; c++) {
        PsHdr h;
        st_read(ps_sector(pch, c), &h, sizeof h);
        if (h.magic != PS_MAGIC) continue;
        memset(nor + ps_sector(pch, c), 0xFF, 32);  /* (a header rewritten as an old firmware would have it) */
        h.ver = (uint8_t)(FREC_VER_PERF - 1u);
        h.hcrc = st_crc32(&h, sizeof h - 4u);
        memcpy(nor + ps_sector(pch, c), &h, sizeof h);
    }
    wipe_ram();
    ps_load();
    CHECK(!syn_slot_used(UISAVE_WHOLE_PERF, 0) && !syn_slot_used(UISAVE_WHOLE_PERF, 61) &&
          syn_slot_used(UISAVE_WHOLE_PERF, 127), "a chunk of another layout loaded (or the next chunk lost)");

    /* a damaged payload in the newest copy: the copy before it is used */
    uint32_t sch = PS_KIND[UISAVE_PATCH_PRESET].first;
    {
        PsHdr h0, h1;
        st_read(ps_sector(sch, 0), &h0, sizeof h0);
        st_read(ps_sector(sch, 1), &h1, sizeof h1);
        uint32_t newest = h1.seq > h0.seq ? 1u : 0u;
        nor[ps_sector(sch, newest) + 256 + PS_USED_BYTES + 5 * sizeof(EngineState)] ^= 0x01;   /* a bit flipped */
    }
    wipe_ram();
    ps_load();
    CHECK(holds(UISAVE_PATCH_PRESET, 5, 0x30 + 38), "a damaged newest copy: not the save before it");

    /* patch chunks in the v5 layout (170-byte patches, before STRIKE) and the v6 one (173, STRIKE's first three attack
     * bytes, stored as in RAM), as the firmware before each wrote them: their slots load through engine_from_v5 / _v6,
     * and the next save writes the chunk as v7 (packed) */
    for (uint32_t ver = 5u; ver <= 6u; ver++) {
        uint32_t c = ver - 4u, chunk = PS_KIND[UISAVE_PATCH_PRESET].first + c, first, n = ps_slots(&PS_KIND[UISAVE_PATCH_PRESET], c, &first);
        uint32_t size = ver == 5u ? ENGINE_V5_SIZE : ENGINE_V6_SIZE, len = PS_USED_BYTES + n * size, i, j;
        static uint8_t pay[4096];
        PsHdr h;
        memset(pay, 0, sizeof pay);
        pay[0] = 0x01u;                                  /* the chunk's first and last slots used */
        pay[(n - 1u) >> 3] |= (uint8_t)(1u << ((n - 1u) & 7u));
        for (i = 0; i < n; i++)
            for (j = 0; j < size; j++)
                pay[PS_USED_BYTES + i * size + j] = (uint8_t)(j * 7u + i);
        st_erase(ps_sector(chunk, 0u));
        st_erase(ps_sector(chunk, 1u));
        memcpy(nor + ps_sector(chunk, 0u) + PS_PAYLOAD_OFF, pay, len);
        memset(&h, 0xFF, sizeof h);
        h.magic = PS_MAGIC;
        h.kind = UISAVE_PATCH_PRESET;
        h.chunk = (uint8_t)c;
        h.ver = (uint8_t)ver;
        h.seq = 1u;
        h.len = len;
        h.crc = st_crc32(pay, len);
        h.hcrc = st_crc32(&h, sizeof h - 4u);
        memcpy(nor + ps_sector(chunk, 0u), &h, sizeof h);
        wipe_ram();
        ps_load();
        uint32_t last = first + n - 1u;
        CHECK(syn_slot_used(UISAVE_PATCH_PRESET, (uint8_t)first) && syn_slot_used(UISAVE_PATCH_PRESET, (uint8_t)last) &&
              !syn_slot_used(UISAVE_PATCH_PRESET, (uint8_t)(first + 1u)), "v%u chunk: the used slots", (unsigned)ver);
        /* where each old byte must land: the v5 bytes after the attack's place move up 3 + 6, the v6 ones after
         * atk_filter's place up 6; the attack's new bytes are their defaults */
        static const uint8_t def7[6] = ATK_V7_DEFAULTS;
        int ok = 1;
        for (i = 0; i < 2; i++) {
            uint32_t s = i ? last : first, k = i ? n - 1u : 0u;
            const uint8_t *e = slot_ptr(UISAVE_PATCH_PRESET, s);
            uint32_t ins = ver == 5u ? ENGINE_V5_INSERT : ENGINE_V6_INSERT, add = ver == 5u ? 9u : 6u;
            for (j = 0; j < ins; j++)
                ok &= e[j] == (uint8_t)(j * 7u + k);
            if (ver == 5u)
                ok &= e[ENGINE_V5_INSERT] == 0u && e[ENGINE_V5_INSERT + 1u] == ATK_LEVEL_DEFAULT && e[ENGINE_V5_INSERT + 2u] == 64u;
            ok &= memcmp(e + ENGINE_V6_INSERT, def7, 6) == 0;
            for (j = ins; j < size; j++)
                ok &= e[j + add] == (uint8_t)(j * 7u + k);
        }
        CHECK(ok, "v%u chunk: a patch's bytes not where the migration puts them", (unsigned)ver);
        const EngineState *e = (const EngineState *)(const void *)slot_ptr(UISAVE_PATCH_PRESET, first);
        CHECK(e->name[NAME_LEN - 1] == (uint8_t)((size - 1u) * 7u) && e->oscmix.atk_track == ATKTRK_AUTO,
              "v%u chunk: the name / the attack's last byte", (unsigned)ver);
        /* a save in that chunk writes it as v7: the migrated slots come back as their packed form */
        EngineState keep;
        uint8_t rec[ENGINE_STORED_SIZE];
        engine_store_pack((const EngineState *)(const void *)slot_ptr(UISAVE_PATCH_PRESET, last), rec);
        engine_store_unpack(rec, &keep);
        CHECK(save(UISAVE_PATCH_PRESET, first + 1u, 0x66), "v%u chunk: save into it", (unsigned)ver);
        wipe_ram();
        ps_load();
        CHECK(holds(UISAVE_PATCH_PRESET, first + 1u, 0x66) &&
              memcmp(&keep, slot_ptr(UISAVE_PATCH_PRESET, last), sizeof keep) == 0, "v%u chunk: not the same after a v7 save",
              (unsigned)ver);
        PsHdr h2;
        CHECK(ps_current(chunk, UISAVE_PATCH_PRESET, (uint8_t)c, &h2) >= 0 && h2.ver == FREC_VER_ENGINE,
              "v%u chunk: the save did not write v7", (unsigned)ver);
    }

    /* no flash: a SAVE reports failure (the slot stays in RAM) */
    flash_ok = 0;
    CHECK(!save(UISAVE_ARP_SEQ, 3, 0x55), "no flash reported SAVED");
    flash_ok = 1;

    printf(failures ? "%d preset store checks failed\n" : "preset store tests: all passed\n", failures);
    return failures;
}
