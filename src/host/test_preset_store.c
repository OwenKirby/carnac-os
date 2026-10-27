/* Host test of the FM1 preset store (src/hardware/preset_store_fm1.c) on a simulated 1 MiB NOR (erase: 0xFF;
 * program: clears bits only), with X0X's storage.c hooks stood in: a fresh store over the old wavetable image reads
 * empty; every bank saves and loads back (the last, part-filled chunk too); saves take the two copies in turn; a save
 * cut off at any point (after the erase, during the payload, before the header) leaves the save before it; a chunk of
 * another layout version, or with a damaged payload, is left out. The performances (2026-10-17: whole, in flash only)
 * read back slot by slot and a save keeps the chunk's others; the 128-slot layout's sequence chunks are left out. Then
 * the factory bank (src/hardware/factory_presets.c): every sequence, arp pattern and performance in range and in key.
 *   gcc -O1 -Wall -Wextra -Isrc -Isrc/dsp -Isrc/hardware src/host/test_preset_store.c src/dsp/preset.c \
 *       src/dsp/seq_scale.c src/dsp/arp_chords.c src/dsp/wavetables_image.c -o test_preset_store && ./test_preset_store
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PresetStructures.h"
#include "UIStateMachine.h"
#include "dsp/preset.h"
#include "dsp/flash_record.h"
#include "dsp/wavetables.h"
#include "dsp/midi_clock.h"
#include "dsp/seq_scale.h"
#include "dsp/attacks.h"         /* ATK_COUNT: the factory bank's attack sounds */
#include "dsp/arp_chords.h"

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

/* ---- the glue's pieces the fragments use (patch_bank: preset.c's) */
static SequencerLane syn_seq_bank[BANK_SLOTS][SEQ_NLANE];
static ArpState syn_arp_bank[BANK_SLOTS];
static uint8_t syn_perf_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_seq_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_arp_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_used[UISAVE_COUNT][PATCH_SLOTS / 8u];
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
#include "factory_presets.c"

extern const uint8_t wavetable_image_host[WT_STORE_IMAGE_LEN];

#define PERF UISAVE_WHOLE_PERF
static uint8_t *slot_ptr(uint32_t k, uint32_t s) { return PS_KIND[k].bank + s * PS_KIND[k].size; }
static void fill(uint32_t k, uint32_t s, uint8_t v)
{
    if (k == PERF) memset(&ps_perf_save, v, sizeof ps_perf_save);
    else memset(slot_ptr(k, s), v, PS_KIND[k].size);
}
/* a performance of v in every byte as it comes back from flash (its patches packed and unpacked) */
static void perf_expect(uint8_t v, WholePerformance *w)
{
    static uint8_t rec[WHOLE_PERF_STORED_SIZE];
    memset(w, v, sizeof *w);
    whole_perf_pack(w, rec);
    whole_perf_unpack(rec, w);
}
/* slot s of bank k holds v in every byte; a patch that came back from flash: as v packed and unpacked (its two unread
 * bytes 0, its name 6 bits a character); a performance: read from flash, and its name known */
static int holds(uint32_t k, uint32_t s, uint8_t v)
{
    uint32_t i;
    if (k == PERF) {
        WholePerformance got, want;
        uint8_t name[NAME_LEN];
        memset(name, v, sizeof name);
        perf_expect(v, &want);
        return ps_perf_read(s, &got) == 0 && memcmp(&got, &want, sizeof got) == 0 &&
               memcmp(syn_perf_name[s], name, NAME_LEN) == 0;
    }
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
        if (k != PERF)
            for (uint32_t s = 0; s < PS_KIND[k].slots; s++) fill(k, s, 0xEE);
    memset(syn_perf_name, 0xEE, sizeof syn_perf_name);
    memset(syn_seq_name, 0xEE, sizeof syn_seq_name);
    memset(syn_arp_name, 0xEE, sizeof syn_arp_name);
    memset(ps_names_used, 0, sizeof ps_names_used);
    memset(&ps_perf_save, 0xEE, sizeof ps_perf_save);
    memset(syn_used, 0, sizeof syn_used);
}
/* a SAVE as syn_commit makes it, then the main loop's write: commit_done's ok */
static int save(uint32_t k, uint32_t s, uint8_t v)
{
    fill(k, s, v);
    if (k == PERF) {
        ps_perf_save_slot = (uint8_t)s;
        memcpy(syn_perf_name[s], ps_perf_save.perf.name, NAME_LEN);
    }
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
        for (uint32_t s = 0; s < PS_KIND[k].slots; s++) n += syn_slot_used((uint8_t)k, (uint8_t)s);
    return n;
}
/* a chunk header as a firmware wrote it, over an erased chunk (copy 0) with the payload pay */
static void put_chunk(uint32_t chunk, uint8_t kind, uint8_t c, uint8_t ver, const uint8_t *pay, uint32_t len)
{
    PsHdr h;
    st_erase(ps_sector(chunk, 0u));
    st_erase(ps_sector(chunk, 1u));
    memcpy(nor + ps_sector(chunk, 0u) + PS_PAYLOAD_OFF, pay, len);
    memset(&h, 0xFF, sizeof h);
    h.magic = PS_MAGIC;
    h.kind = kind;
    h.chunk = c;
    h.ver = ver;
    h.seq = 1u;
    h.len = len;
    h.crc = st_crc32(pay, len);
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + ps_sector(chunk, 0u), &h, sizeof h);
}

/* the sequence and arp names (2026-10-25): chunk 1, written only when a name changed; a power cycle brings back the
 * names saved and leaves the others (the factory's) alone; a names chunk of another version is left out */
static void test_names(void)
{
    uint8_t nq[NAME_LEN], na[NAME_LEN], fac[NAME_LEN];
    uint32_t s;
    memset(nq, 5, sizeof nq);
    memset(na, 9, sizeof na);
    wipe_ram();
    ps_load();
    CHECK(syn_seq_name[0][0] == 0xEE, "a names table read from a store that has none");
    ps_name_save(UISAVE_SEQ_SETTINGS, 4, syn_seq_name[4]);
    CHECK(!ps_dirty, "a SAVE with the same name marked the names chunk");
    ps_name_save(UISAVE_SEQ_SETTINGS, 4, nq);
    CHECK(save(UISAVE_SEQ_SETTINGS, 4, 0x31), "save sequence 4");
    ps_name_save(UISAVE_ARP_SEQ, BANK_SLOTS - 1u, na);
    CHECK(save(UISAVE_ARP_SEQ, BANK_SLOTS - 1u, 0x32), "save the last arp");
    wipe_ram();
    memset(fac, 0x11, sizeof fac);
    for (s = 0; s < BANK_SLOTS; s++) {
        memcpy(syn_seq_name[s], fac, NAME_LEN);       /* the factory's names, as factory_install leaves them */
        memcpy(syn_arp_name[s], fac, NAME_LEN);
    }
    ps_load();
    CHECK(memcmp(syn_seq_name[4], nq, NAME_LEN) == 0 && memcmp(syn_arp_name[BANK_SLOTS - 1u], na, NAME_LEN) == 0,
          "the saved names did not come back");
    CHECK(memcmp(syn_seq_name[3], fac, NAME_LEN) == 0 && memcmp(syn_arp_name[4], fac, NAME_LEN) == 0,
          "a name never saved was overwritten");
    CHECK(holds(UISAVE_SEQ_SETTINGS, 4, 0x31), "sequence 4 itself did not come back");
    {   /* another version: left out */
        uint8_t pay[PS_NAMES_LEN];
        memset(pay, 0xFF, sizeof pay);
        put_chunk(PS_NAMES_CHUNK, PS_NAMES_KIND, 0u, PS_NAMES_VER + 1u, pay, sizeof pay);
        memset(syn_seq_name, 0x11, sizeof syn_seq_name);
        ps_load();
        CHECK(syn_seq_name[4][0] == 0x11, "a names chunk of another version was read");
    }
}

/* the factory bank: every slot in range, in key, and whole */
static void test_factory(void)
{
    static const struct { uint8_t g; const char *name; } G[] = {
        {FG_TRAD_MAJ, "Trad_Maj"}, {FG_TRAD_MIN_1, "Trad_Min_1"}, {FG_TRAD_MIN_2, "Trad_Min_2"}, {FG_OCT, "Oct_Stack"},
        {FG_4TH, "4th_Stack"}, {FG_5TH, "5th_Stack"}, {FG_MIN, "Utility_Min"}, {FG_M7, "Utility_M7"},
        {FG_MIN7, "Utility_m7"}, {FG_MIN9, "Utility_m9"}, {FG_POP_INV, "Pop_Inversions"}, {FG_CINE_1, "Cinematic_1"},
        {FG_CINE_2, "Cinematic_2"}, {FG_CINE_3, "Cinematic_3"}, {FG_CINE_5, "Cinematic_5"}, {FG_CINE_6, "Cinematic_6"},
        {FG_CLASSICAL_1, "Classical_1"}, {FG_MODERN_1, "Modern_1"}};
    uint32_t s, l, i, arp_led = 0;
    for (i = 0; i < sizeof G / sizeof G[0]; i++)
        CHECK(strcmp(arp_genre_names[G[i].g], G[i].name) == 0, "genre %u is %s, not %s", G[i].g,
              arp_genre_names[G[i].g], G[i].name);
    memset(syn_used, 0, sizeof syn_used);
    factory_install();
    CHECK(used_count() == (int)(FP_N + 3u * FP_NPERF), "factory_install marked %d slots, not %u", used_count(),
          FP_N + 3u * FP_NPERF);
    for (s = 0; s < FP_N; s++) {
        {   /* the patch's settings: each a byte of the patch, once, and the values in range */
            const uint8_t *q = FV_TAB[s].set;
            uint8_t seen[sizeof(EngineState)] = {0};
            const EngineState *e = &patch_bank[s];
            uint32_t k;
            for (; q[0] != V_END; q += 2) {
                CHECK(q[0] < sizeof(EngineState) && !seen[q[0]], "patch %u: offset %u twice or out of range", s, q[0]);
                if (q[0] < sizeof(EngineState)) seen[q[0]] = 1;
            }
            CHECK(e->arch < VARCH_COUNT && e->filter[0].flavour < FLT_COUNT && e->filter[1].flavour < FLT_COUNT &&
                  e->amp.drive_model < DRIVE_COUNT && e->oscmix.atk_sample < ATK_COUNT &&
                  e->oscmix.atk_track < ATKTRK_COUNT && e->oscmix.xmod_type < XMOD_COUNT, "patch %u out of range", s);
            for (k = 0; k < NOSC; k++)
                CHECK(e->osc[k].wavebank < 16u || (e->osc[k].wavebank >= 32u && e->osc[k].wavebank < 32u + ATK_COUNT) ||
                      (e->osc[k].wavebank >= 64u && e->osc[k].wavebank < 74u), "patch %u osc %u: wave %u", s, k,
                      e->osc[k].wavebank);
            for (k = 0; k < NMODSLOT; k++)
                CHECK(e->mod[k].src_a < MSRC_COUNT && e->mod[k].src_b < MSRC_COUNT && e->mod[k].func < MFN_COUNT,
                      "patch %u slot %u", s, k);
        }
    }
    for (s = 0; s < FP_NPERF; s++) {
        WholePerformance w, back;
        static uint8_t rec[WHOLE_PERF_STORED_SIZE];
        const FactoryArp *fa = &FA_TAB[s];
        CHECK(factory_perf(s, &w) == 0, "factory_perf %u", s);
        CHECK(memcmp(syn_seq_name[s], syn_perf_name[s], NAME_LEN) == 0 && memcmp(syn_arp_name[s], syn_perf_name[s], NAME_LEN) == 0,
              "factory sequence / arp %u: not named after its performance", s);
        CHECK(memcmp(&w.engine[1], &patch_bank[w.perf.layer_patch[1]], sizeof(EngineState)) == 0 &&
              w.perf.perf_mode < PERF_COUNT, "performance %u: its layer 2", s);
        CHECK(memcmp(&w.engine[0], &patch_bank[FP_TAB[s].patch], sizeof(EngineState)) == 0 &&
              memcmp(w.seq, syn_seq_bank[s], sizeof w.seq) == 0, "performance %u: not its patch and sequence", s);
        CHECK(memcmp(w.perf.name, syn_perf_name[s], NAME_LEN) == 0, "performance %u: its name", s);
        CHECK(fa->maq ? memcmp(&w.arp, &syn_arp_bank[s], sizeof w.arp) == 0 && w.perf.arp_link == s
                      : w.arp.steps[0].chord_root == ARP_CHORD_EMPTY && w.perf.arp_link == 127u,
              "performance %u: its arp pattern (ARP-led %u)", s, fa->maq);
        CHECK((FS_TAB[s].prescale == 0u) == (fa->maq != 0u), "slot %u: an ARP-led performance has an arp bass", s);
        CHECK(w.perf.int_tempo + MIDI_CLOCK_BPM_BASE == fa->bpm && fa->bpm >= 60u && fa->bpm <= 130u, "slot %u tempo", s);
        arp_led += fa->maq;
        whole_perf_pack(&w, rec);
        whole_perf_unpack(rec, &back);
        whole_perf_pack(&back, rec);
        whole_perf_unpack(rec, &w);
        CHECK(memcmp(&w, &back, sizeof w) == 0, "performance %u: not the same through flash twice", s);
        /* the sequence: every lane in range; lane 1's notes in its scale (the genre's, as the MAQ reads it) */
        for (l = 0; l < SEQ_NLANE; l++) {
            const SequencerLane *ln = &syn_seq_bank[s][l];
            CHECK(ln->event_type < SEQEV_COUNT && ln->target_mask <= seq_target_max(ln->event_type) &&
                  ln->prescale >= SEQ_PRESCALE_MIN && ln->prescale <= SEQ_PRESCALE_MAX &&
                  ln->playback_mode < SEQ_MODE_COUNT && ln->scale < SEQSCALE_COUNT && ln->loop_end == SEQ_NSTEP - 1u,
                  "sequence %u lane %u out of range", s, l);
            CHECK((l == 0u) == (ln->event_type == SEQEV_NOTE), "sequence %u lane %u: NOTE only on lane 1", s, l);
            for (i = 0; i < SEQ_NSTEP; i++) {
                const SequencerStep *st = &ln->steps[i];
                uint8_t n = st->note_buffer[0];
                uint16_t mask = seq_lane_scale_mask(ln, syn_arp_bank[s].current_genre_bank);
                CHECK(st->value <= VAL_MAX && (n <= VAL_MAX || n == SEQ_NOTE_TIE), "sequence %u lane %u step %u", s, l, i);
                if (l == 0u && n != SEQ_NOTE_TIE && !(st->flags & SEQ_FLAG_MUTE))
                    CHECK(seq_scale_snap(n, mask) == n, "sequence %u step %u: note %u not in its scale", s, i, n);
            }
        }
        /* the arp pattern */
        const ArpState *a = &syn_arp_bank[s];
        CHECK(a->current_genre_bank < ARP_GENRE_COUNT && a->gate_length <= VAL_MAX && a->sequence_length == ARP_NSTEP &&
              ARP_LAYER(a) == ARPLAYER_L2 && ARP_TIMING(a) == fa->div && a->transpose_maq_option == fa->maq,
              "arp %u out of range", s);
        CHECK(ARP_ROOT_IS_CHORD(a->steps[0].chord_root), "arp %u starts on a chord", s);
        for (i = 0; i < ARP_NSTEP; i++) {
            const ArpChordStep *c = &a->steps[i];
            CHECK((ARP_ROOT_IS_CHORD(c->chord_root) || c->chord_root == ARP_CHORD_TIE) && c->octave_range < ARP_NOCT &&
                  c->style < ARPSTYLE_COUNT && c->variation < ARP_NVAR && c->accent <= 1u && c->rate < ARPRATE_COUNT,
                  "arp %u step %u out of range", s, i);
        }
    }
    printf("factory: %u patches, %u performances, %u ARP-led, %u SEQ-led\n", FP_N, FP_NPERF, arp_led,
           FP_NPERF - arp_led);
    CHECK(factory_perf(FP_NPERF, &(WholePerformance){0}) == -1, "factory_perf past the bank");
}

int main(void)
{
    /* the flash as an FM1 left by the old firmware: the wavetable image at 0xA0000, the rest erased */
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor + WT_STORE_BASE, wavetable_image_host, WT_STORE_IMAGE_LEN);
    printf("chunks: arp %u, seq %u, perf %u, patch %u in %u (%u KiB)\n", (unsigned)PS_N_ARP, (unsigned)PS_N_SEQ,
           (unsigned)PS_N_PERF, (unsigned)PS_N_PATCH, (unsigned)PS_NCHUNK, (unsigned)(PS_NCHUNK * 8u));
    wipe_ram();
    ps_load();
    CHECK(used_count() == 0 && holds(UISAVE_PATCH_PRESET, 0, 0xEE), "the old wavetable image read as presets");

    /* every bank: its first slot, one in the middle, its last (the part-filled last chunk) */
    static const uint8_t SL[2][3] = {{0, 13, BANK_SLOTS - 1u}, {0, 61, PATCH_SLOTS - 1u}};
#define SLOT(k, i) SL[(k) == UISAVE_PATCH_PRESET][i]
    for (uint32_t k = 0; k < UISAVE_COUNT; k++)
        for (int i = 0; i < 3; i++)
            CHECK(save(k, SLOT(k, i), (uint8_t)(0x10 + k * 16 + i)), "save bank %u slot %u", (unsigned)k, SLOT(k, i));
    wipe_ram();
    ps_load();
    CHECK(used_count() == 12, "after a power cycle %d slots used, not 12", used_count());
    for (uint32_t k = 0; k < UISAVE_COUNT; k++)
        for (int i = 0; i < 3; i++)
            CHECK(syn_slot_used((uint8_t)k, SLOT(k, i)) && holds(k, SLOT(k, i), (uint8_t)(0x10 + k * 16 + i)),
                  "bank %u slot %u did not come back", (unsigned)k, SLOT(k, i));
    CHECK(holds(UISAVE_PATCH_PRESET, 1, 0xEE), "a slot never saved was overwritten");
    {
        WholePerformance w;
        CHECK(ps_perf_read(1, &w) == -1 && ps_perf_read(BANK_SLOTS, &w) == -1, "a performance never saved read back");
    }

    /* the two copies in turn: the arp chunk's headers' seq 2 and 3 after three saves */
    uint32_t ch = PS_KIND[UISAVE_ARP_SEQ].first;
    PsHdr a, b;
    st_read(ps_sector(ch, 0), &a, sizeof a);
    st_read(ps_sector(ch, 1), &b, sizeof b);
    CHECK(a.magic == PS_MAGIC && b.magic == PS_MAGIC && ((a.seq == 3 && b.seq == 2) || (a.seq == 2 && b.seq == 3)),
          "the arp chunk's copies after 3 saves: seq %u / %u", a.seq, b.seq);

    /* a save cut off: after the erase, after part of the payload, before the header; each leaves the save before */
    for (int at = 0; at < 3; at++) {
        cut = 0;
        progs_left = at == 0 ? 0 : at == 1 ? 3 : (int)((PS_USED_BYTES + PS_PER(PS_KIND[UISAVE_SEQ_SETTINGS].size) *
                                                         PS_KIND[UISAVE_SEQ_SETTINGS].size + 255u) / 256u);
        CHECK(!save(UISAVE_SEQ_SETTINGS, 13, 0x77), "a save cut off (case %d) reported SAVED", at);
        progs_left = -1;
        cut = 0;
        wipe_ram();
        ps_load();
        CHECK(syn_slot_used(UISAVE_SEQ_SETTINGS, 13) && holds(UISAVE_SEQ_SETTINGS, 13, 0x10 + 16 + 1),
              "a save cut off (case %d) lost the save before it", at);
        CHECK(used_count() == 12, "a save cut off (case %d): %d slots used", at, used_count());
    }
    /* .. and the next save goes through */
    CHECK(save(UISAVE_SEQ_SETTINGS, 13, 0x78), "the save after the cut");
    wipe_ram();
    ps_load();
    CHECK(holds(UISAVE_SEQ_SETTINGS, 13, 0x78), "the save after the cut did not land");

    /* a performance save cut off before its header: the performance before it, and its chunk's others, stay */
    CHECK(save(PERF, 12, 0x41), "a second performance in a chunk");
    cut = 0;
    progs_left = (int)((PS_USED_BYTES + PS_PER(WHOLE_PERF_STORED_SIZE) * WHOLE_PERF_STORED_SIZE + 255u) / 256u);
    CHECK(!save(PERF, 13, 0x42), "a performance save cut off reported SAVED");
    progs_left = -1;
    cut = 0;
    wipe_ram();
    ps_load();
    CHECK(holds(PERF, 13, 0x10 + PERF * 16 + 1) && holds(PERF, 12, 0x41), "a performance save cut off lost the ones before");
    /* .. a save keeps the chunk's others (it writes over the copy before), many times */
    for (int i = 0; i < 10; i++)
        CHECK(save(PERF, 14, (uint8_t)(0x50 + i)), "performance save %d", i);
    wipe_ram();
    ps_load();
    CHECK(holds(PERF, 12, 0x41) && holds(PERF, 13, 0x10 + PERF * 16 + 1) && holds(PERF, 14, 0x59) &&
          !syn_slot_used(PERF, 15) && used_count() == 14, "a performance save lost its chunk's others (%d used)", used_count());

    /* many saves of one chunk: always the last one back */
    for (int i = 0; i < 40; i++)
        CHECK(save(UISAVE_PATCH_PRESET, 5, (uint8_t)(0x30 + i)), "save %d", i);
    wipe_ram();
    ps_load();
    CHECK(holds(UISAVE_PATCH_PRESET, 5, 0x30 + 39) && holds(UISAVE_PATCH_PRESET, 0, 0x10 + 2 * 16),
          "after 40 saves: the last one, and its chunk's other slot, back");

    /* another layout version: the chunk is left out (its slots empty, not misread) */
    uint32_t pch = PS_KIND[PERF].first;
    for (uint32_t c = 0; c < 2; c++) {
        PsHdr h;
        st_read(ps_sector(pch, c), &h, sizeof h);
        if (h.magic != PS_MAGIC) continue;
        memset(nor + ps_sector(pch, c), 0xFF, 32);  /* (a header rewritten as an old firmware would have it) */
        h.ver = 5u;                                  /* (v6 .. v8 migrate: below) */
        h.hcrc = st_crc32(&h, sizeof h - 4u);
        memcpy(nor + ps_sector(pch, c), &h, sizeof h);
    }
    wipe_ram();
    ps_load();
    {
        WholePerformance w;
        CHECK(!syn_slot_used(PERF, 0) && ps_perf_read(0, &w) == -1 && syn_slot_used(PERF, 13) &&
              syn_slot_used(PERF, BANK_SLOTS - 1u), "a chunk of another layout loaded (or the next chunk lost)");
    }
    /* .. and a save into it starts it again: the slot saved, the others in it empty */
    CHECK(save(PERF, 1, 0x61), "a performance save into a chunk of another layout");
    wipe_ram();
    ps_load();
    CHECK(holds(PERF, 1, 0x61) && !syn_slot_used(PERF, 0), "a performance save into a chunk of another layout");

    /* the 128-slot layout's sequence chunks (FREC_VER_SEQ 2, 9 slots a chunk, where today's sit) are left out */
    {
        static uint8_t pay[4096];
        uint32_t len = PS_USED_BYTES + 9u * sizeof syn_seq_bank[0];
        memset(pay, 0x23, len);
        put_chunk(PS_KIND[UISAVE_SEQ_SETTINGS].first, UISAVE_SEQ_SETTINGS, 0u, 2u, pay, len);
        wipe_ram();
        ps_load();
        CHECK(!syn_slot_used(UISAVE_SEQ_SETTINGS, 0) && !syn_slot_used(UISAVE_SEQ_SETTINGS, 5) &&
              syn_slot_used(UISAVE_SEQ_SETTINGS, 13), "an old sequence chunk loaded (or the next chunk lost)");
    }

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
     * bytes, stored as in RAM), v7 (packed, no BEND MOD), v8 (packed, no DRIVE) and v9 (no VEL > LEVEL or DEPTH MOD, the
     * envelope flags as bytes), as the firmware before each wrote them: their slots load through engine_from_v5 / _v6 /
     * _v7 / _v8 / _v9_stored, and the next save writes the chunk as the current version */
    for (uint32_t ver = 5u; ver <= 9u; ver++) {
        uint32_t c = ver - 4u, chunk = PS_KIND[UISAVE_PATCH_PRESET].first + c, first, n = ps_slots(&PS_KIND[UISAVE_PATCH_PRESET], c, &first);
        uint32_t size = ver == 5u ? ENGINE_V5_SIZE : (ver == 6u ? ENGINE_V6_SIZE : ENGINE_STORED_SIZE), len = PS_USED_BYTES + n * size, i, j;
        static uint8_t pay[4096];
        memset(pay, 0, sizeof pay);
        pay[0] = 0x01u;                                  /* the chunk's first and last slots used */
        pay[(n - 1u) >> 3] |= (uint8_t)(1u << ((n - 1u) & 7u));
        for (i = 0; i < n; i++)
            for (j = 0; j < size; j++)
                pay[PS_USED_BYTES + i * size + j] = (uint8_t)(j * 7u + i);
        put_chunk(chunk, UISAVE_PATCH_PRESET, (uint8_t)c, (uint8_t)ver, pay, len);
        wipe_ram();
        ps_load();
        uint32_t last = first + n - 1u;
        CHECK(syn_slot_used(UISAVE_PATCH_PRESET, (uint8_t)first) && syn_slot_used(UISAVE_PATCH_PRESET, (uint8_t)last) &&
              !syn_slot_used(UISAVE_PATCH_PRESET, (uint8_t)(first + 1u)), "v%u chunk: the used slots", (unsigned)ver);
        /* where each old byte must land: in v8's layout, the v5 bytes after the attack's place move up 3 + 8, the v6
         * ones after atk_filter's place up 8, the attack's new bytes their defaults; then to v9 (engine_from_v8_ram).
         * v7, v8: as engine_from_v7 / _v8 unpack them (their byte maps are checked in test_flash_record) */
        static const uint8_t def8[8] = ATK_V8_DEFAULTS;
        int ok = 1;
        for (i = 0; i < 2; i++) {
            uint32_t s = i ? last : first, k = i ? n - 1u : 0u;
            const uint8_t *e = slot_ptr(UISAVE_PATCH_PRESET, s);
            EngineState want;
            if (ver >= 7u) {
                if (ver == 7u)
                    engine_from_v7(pay + PS_USED_BYTES + k * size, &want);
                else if (ver == 8u)
                    engine_from_v8(pay + PS_USED_BYTES + k * size, &want);
                else
                    engine_from_v9_stored(pay + PS_USED_BYTES + k * size, &want);
                ok &= memcmp(e, &want, sizeof want) == 0;
                continue;
            }
            uint8_t v8[ENGINE_V8_RAM];
            uint32_t ins = ver == 5u ? ENGINE_V5_INSERT : ENGINE_V6_INSERT, add = ver == 5u ? 11u : 8u;
            for (j = 0; j < ins; j++)
                v8[j] = (uint8_t)(j * 7u + k);
            if (ver == 5u) {
                v8[ENGINE_V5_INSERT] = 0u;
                v8[ENGINE_V5_INSERT + 1u] = ATK_LEVEL_DEFAULT;
                v8[ENGINE_V5_INSERT + 2u] = 64u;
            }
            memcpy(v8 + ENGINE_V6_INSERT, def8, 8);
            for (j = ins; j < size; j++)
                v8[j + add] = (uint8_t)(j * 7u + k);
            engine_from_v8_ram(v8, &want);
            ok &= memcmp(e, &want, sizeof want) == 0;
        }
        CHECK(ok, "v%u chunk: a patch's bytes not where the migration puts them", (unsigned)ver);
        const EngineState *e = (const EngineState *)(const void *)slot_ptr(UISAVE_PATCH_PRESET, first);
        CHECK(ver >= 7u || (e->name[NAME_LEN - 1] == (uint8_t)((size - 1u) * 7u) && e->oscmix.atk_track == ATKTRK_AUTO),
              "v%u chunk: the name / the attack's last byte", (unsigned)ver);
        /* a save in that chunk writes it as the current version: the migrated slots come back as their packed form */
        EngineState keep;
        uint8_t rec[ENGINE_STORED_SIZE];
        engine_store_pack((const EngineState *)(const void *)slot_ptr(UISAVE_PATCH_PRESET, last), rec);
        engine_store_unpack(rec, &keep);
        CHECK(save(UISAVE_PATCH_PRESET, first + 1u, 0x66), "v%u chunk: save into it", (unsigned)ver);
        wipe_ram();
        ps_load();
        CHECK(holds(UISAVE_PATCH_PRESET, first + 1u, 0x66) &&
              memcmp(&keep, slot_ptr(UISAVE_PATCH_PRESET, last), sizeof keep) == 0, "v%u chunk: not the same after a save",
              (unsigned)ver);
        PsHdr h2;
        CHECK(ps_current(chunk, UISAVE_PATCH_PRESET, (uint8_t)c, &h2) >= 0 && h2.ver == FREC_VER_ENGINE,
              "v%u chunk: the save did not write the current version", (unsigned)ver);
    }

    /* an arp chunk of FREC_VER_ARP 3 and a performance chunk of FREC_VER_PERF 6 (before 2026-10-17's RATE: each step's
     * byte its gate, the pattern's TIME DIVISION where GATE is now) load through arp_from_v3 and arp_from_v4;
     * performance chunks of FREC_VER_PERF 6 (that arp, v8 patches), 7 (arp v4, v8 patches) and 8 (proto/sh2-filter's:
     * arp v4, v9 patches) load through whole_perf_from_old, and a performance saved into one keeps the others,
     * converted, and writes the chunk as today's */
    {
        static uint8_t pay[4096];
        ArpState v3, want, v4f;
        WholePerformance w, got, exp;
        uint32_t first, n, len, i, ok;
        arp_defaults(&v3);                           /* (the v3 bytes, in today's field names) */
        v3.layer_timing = ARPLAYER_L1;               /* v3: target_layer */
        for (i = 0; i < ARP_NSTEP; i++)
            v3.steps[i].style = 1u;                  /* v3: UP */
        v3.steps[1].chord_root = 60;
        v3.steps[1].rate = 77;                       /* v3: step 2's gate, the first chord step's */
        v3.steps[4].chord_root = 65;
        v3.steps[4].rate = 12;
        v3.gate_length = ARPDIV_1_8;                 /* v3: TIME DIVISION */
        want = v3;
        arp_from_v3(&want);
        CHECK(want.gate_length == 77 && want.steps[0].rate == ARPDIV_1_8 && want.steps[4].rate == ARPDIV_1_8 &&
              want.steps[1].chord_root == 60 && want.current_genre_bank == v3.current_genre_bank, "arp_from_v3");
        v4f = want;                                  /* (the arp as v7 and v8 performances hold it) */
        arp_from_v4(&want);
        CHECK(ARP_TIMING(&want) == ARPDIV_1_8 && ARP_LAYER(&want) == ARPLAYER_L1 && want.steps[0].rate == ARPRATE_1_32 &&
              want.steps[4].rate == ARPRATE_1_32 && want.steps[1].style == ARPSTYLE_UP && want.gate_length == 77,
              "arp_from_v4 after it: TIMING the old 1/8 steps, RATE 1/32 (a 4-cell rhythm across an eighth)");
        n = ps_slots(&PS_KIND[UISAVE_ARP_SEQ], 0u, &first);
        len = PS_USED_BYTES + n * sizeof(ArpState);
        memset(pay, 0, len);
        pay[0] = 1u << 3;
        memcpy(pay + PS_USED_BYTES + 3u * sizeof v3, &v3, sizeof v3);
        put_chunk(PS_KIND[UISAVE_ARP_SEQ].first, UISAVE_ARP_SEQ, 0u, 3u, pay, len);
        wipe_ram();
        ps_load();
        CHECK(syn_slot_used(UISAVE_ARP_SEQ, 3) && !syn_slot_used(UISAVE_ARP_SEQ, 0) &&
              memcmp(slot_ptr(UISAVE_ARP_SEQ, 3), &want, sizeof want) == 0, "a v3 arp chunk: loaded, migrated");

        for (uint32_t ver = 6u; ver <= 9u; ver++) {
            static uint8_t mig[WHOLE_PERF_STORED_SIZE];
            n = ps_slots(&PS_KIND[PERF], 0u, &first);
            len = PS_USED_BYTES + n * WHOLE_PERF_STORED_SIZE;
            memset(pay, 0, len);
            pay[0] = 1u << 0 | 1u << 2;
            for (i = 0; i < 3; i += 2) {
                memset(&w, (int)(0x40 + i), sizeof w);
                w.arp = ver == 6u ? v3 : v4f;
                for (uint32_t l = 0; l < NLAYER; l++) {  /* (a v9 patch's KEY TRACK in 4-bit sources' nibbles) */
                    w.engine[l].filter[0].mod_src = w.engine[l].filter[1].mod_src = MSRC_LFO1;
                    w.engine[l].filter[0].morph_mod_src = w.engine[l].filter[1].morph_mod_src = MSRC_ENV2;
                    for (uint32_t k = 0; k < NLFO; k++)  /* (v9: SPD MOD SRC 4 bits, the envelope flags 0 / 1) */
                        w.engine[l].lfo[k].speed_mod_src = MSRC_VELOCITY;
                    for (uint32_t k = 0; k < NENV; k++) {
                        w.engine[l].env[k].retrig_sync = 1u;
                        w.engine[l].env[k].legato = (uint8_t)(k & 1u);
                        w.engine[l].env[k].invert = k ? 1u : 0u;
                    }
                }
                if (ver >= 8u) {                         /* v9 patches: today's filters as they are */
                    w.engine[0].filter[0].flavour = FLT_MS20;   /* (its drive 0x40 + i) */
                    w.engine[1].filter[1].flavour = FLT_MULTI;
                    w.engine[1].filter[0].drive = 0u;
                } else {
                    w.engine[0].filter[0].flavour = 2u;  /* v8's MS-20 LOW, its morph 0x40 + i the drive */
                    w.engine[1].filter[1].flavour = 0u;  /* v8's SVF */
                }
                whole_perf_pack(&w, pay + PS_USED_BYTES + i * WHOLE_PERF_STORED_SIZE);
                /* the patches as v9 stored them; for v6 / v7 (v8 patches) the DRIVE bytes dropped as v8's store did,
                 * the 2 spare 0s at the end */
                for (uint32_t l = 0; l < NLAYER; l++) {
                    uint8_t *pt = pay + PS_USED_BYTES + i * WHOLE_PERF_STORED_SIZE + sizeof(MasterPerformancePreset) +
                                  l * ENGINE_STORED_SIZE;
                    engine_store_pack_v9(&w.engine[l], pt);
                    if (ver >= 8u)
                        continue;
                    memmove(pt + ENG_ST_DRIVE0, pt + ENG_ST_DRIVE0 + 1u, ENG_ST_DRIVE1 - ENG_ST_DRIVE0 - 1u);
                    memmove(pt + ENG_ST_DRIVE1 - 1u, pt + ENG_ST_DRIVE1 + 1u, ENGINE_STORED_SIZE - ENG_ST_DRIVE1 - 1u);
                    pt[ENGINE_STORED_SIZE - 2u] = pt[ENGINE_STORED_SIZE - 1u] = 0u;
                }
            }
            put_chunk(PS_KIND[PERF].first, PERF, 0u, (uint8_t)ver, pay, len);
            wipe_ram();
            ps_load();
            memcpy(mig, pay + PS_USED_BYTES + 2u * WHOLE_PERF_STORED_SIZE, sizeof mig);
            whole_perf_from_old(mig, (uint8_t)ver);
            whole_perf_unpack(mig, &exp);
            CHECK(memcmp(&exp.arp, ver == 9u ? &v4f : &want, sizeof want) == 0 && exp.engine[0].filter[0].flavour == FLT_MS20 &&
                  exp.engine[0].filter[0].drive == 0x42 && exp.engine[1].filter[1].flavour == FLT_MULTI &&
                  exp.engine[1].filter[0].drive == 0u, "whole_perf_from_old (v%u): the arp, the patches' filters", (unsigned)ver);
            CHECK(exp.engine[0].env_vel_level[0] == ENV1_VEL_LEVEL_V9 && exp.engine[1].env_vel_level[2] == 0u &&
                  exp.engine[1].lfo_depth_amt[1] == 0u && exp.engine[0].filter[0].mod_amt == (0x42 - 64) * 2 &&
                  exp.engine[0].env[1].legato == 1u && exp.engine[1].env[2].invert == 1u && exp.engine[1].name[3] == (0x42 & 63),
                  "whole_perf_from_old (v%u): the patches to v10", (unsigned)ver);
            CHECK(syn_slot_used(PERF, 0) && syn_slot_used(PERF, 2) && !syn_slot_used(PERF, 1) && syn_perf_name[2][0] == 0x42 &&
                  ps_perf_read(2, &got) == 0 && memcmp(&got, &exp, sizeof got) == 0,
                  "a v%u performance chunk: loaded, migrated", (unsigned)ver);
            CHECK(save(PERF, 1, 0x61), "a performance save into a v%u chunk", (unsigned)ver);
            wipe_ram();
            ps_load();
            ok = ps_perf_read(2, &got) == 0 && memcmp(&got, &exp, sizeof got) == 0;
            memcpy(mig, pay + PS_USED_BYTES, sizeof mig);
            whole_perf_from_old(mig, (uint8_t)ver);
            whole_perf_unpack(mig, &exp);
            ok = ok && ps_perf_read(0, &got) == 0 && memcmp(&got, &exp, sizeof got) == 0;
            CHECK(ok && holds(PERF, 1, 0x61), "the v%u chunk's others kept, migrated, after a save into it", (unsigned)ver);
            PsHdr h3;
            CHECK(ps_current(PS_KIND[PERF].first, PERF, 0u, &h3) >= 0 && h3.ver == FREC_VER_PERF,
                  "the save into the v%u chunk wrote v%u", (unsigned)ver, (unsigned)FREC_VER_PERF);
        }
    }

    /* an arp chunk of FREC_VER_ARP 4 (2026-10-17 .. 21: RATE each step's length, HOLD the last style, its strum 1..12 up
     * / 13..23 down 96ths apart) loads through arp_from_v4: TIMING the first chord step's length, every RATE the one
     * filling its old length, HOLD first, its strum up / down at the nearest RATE */
    {
        static uint8_t pay[4096];
        ArpState v4, want;
        uint32_t first, n, len;
        arp_defaults(&v4);                           /* (the v4 bytes, in today's field names) */
        v4.layer_timing = ARPLAYER_L2;               /* v4: target_layer */
        v4.steps[2].chord_root = 60;
        v4.steps[2].rate = ARPDIV_1_2;               /* v4: ARPDIV_*, the step's length */
        v4.steps[2].style = 8u;                      /* v4: HOLD */
        v4.steps[2].variation = 16u;                 /* v4: DN 4 */
        v4.steps[3].chord_root = 62;
        v4.steps[3].rate = ARPDIV_1_4;
        v4.steps[3].style = 0u;                      /* v4: BLOCK */
        v4.steps[4].chord_root = 64;
        v4.steps[4].rate = ARPDIV_1_8;
        v4.steps[4].style = 8u;
        v4.steps[4].variation = 0u;                  /* v4: HOLD, no strum */
        want = v4;
        arp_from_v4(&want);
        CHECK(ARP_TIMING(&want) == ARPDIV_1_2 && ARP_LAYER(&want) == ARPLAYER_L2 &&
              want.steps[2].style == ARPSTYLE_HOLD && want.steps[2].variation == 2u && want.steps[2].rate == ARPRATE_1_64T &&
              want.steps[3].style == ARPSTYLE_BLOCK && want.steps[3].rate == ARPRATE_1_16 &&
              want.steps[4].style == ARPSTYLE_HOLD && want.steps[4].variation == 0u && want.steps[4].rate == ARPRATE_1_32,
              "arp_from_v4: TIMING %u, HOLD first, DN 4 -> DOWN at 1/64T", ARP_TIMING(&want));
        n = ps_slots(&PS_KIND[UISAVE_ARP_SEQ], 0u, &first);
        len = PS_USED_BYTES + n * sizeof(ArpState);
        memset(pay, 0, len);
        pay[0] = 1u << 5;
        memcpy(pay + PS_USED_BYTES + 5u * sizeof v4, &v4, sizeof v4);
        put_chunk(PS_KIND[UISAVE_ARP_SEQ].first, UISAVE_ARP_SEQ, 0u, 4u, pay, len);
        wipe_ram();
        ps_load();
        CHECK(syn_slot_used(UISAVE_ARP_SEQ, 5) && memcmp(slot_ptr(UISAVE_ARP_SEQ, 5), &want, sizeof want) == 0,
              "a v4 arp chunk: loaded, migrated");
    }

    /* no flash: a SAVE reports failure (the slot stays in RAM), and no performance reads */
    flash_ok = 0;
    CHECK(!save(UISAVE_ARP_SEQ, 3, 0x55), "no flash reported SAVED");
    {
        WholePerformance w;
        CHECK(ps_perf_read(1, &w) == -1, "a performance read without flash");
    }
    flash_ok = 1;

    test_names();
    test_factory();

    printf(failures ? "%d preset store checks failed\n" : "preset store tests: all passed\n", failures);
    return failures;
}
