/* The preset store on the FM1 (2026-10-16): a unity-build fragment, like wt_store_fm1.c. synth_main_fm1.c includes it
 * after its banks (syn_arp_bank, syn_seq_bank, preset.c's patch_bank), syn_perf_name, syn_used / syn_slot_used, sm,
 * syn_lock / syn_unlock and flash_ok; X0X's storage.c gives st_read / st_erase / st_prog / st_crc32 / st_buf.
 * src/host/test_preset_store.c runs it on a simulated NOR (save, load, both copies, a save cut off, other layouts). */
/* ------------------------------------------------------- preset store --- */
/* The SAVEs in flash (2026-10-16): the four banks (128 patches; since 2026-10-17 32 sequences, 32 arps and 32 whole
 * performances) and which slots were written, kept across power cycles. In the store flash the wavetable image was
 * uploaded to before it moved into the firmware (WT_STORE_BASE, 0xA0000, 240 KiB): each bank in chunks of the slots
 * that fit a 4 KiB sector, each chunk two sectors written in turn, as X0X's storage.c does (the payload from offset
 * 256, the CRC'd 32-byte header at 0 last, read back after): a save cut off by a power cut leaves the chunk's copy
 * before it in charge. A header names the bank, the chunk and the bank's layout version (FREC_VER_*: a chunk written
 * under another layout is left out, its slots empty, until a migration exists), so the old wavetable image there, and
 * a chunk of the 128-slot layout before 2026-10-17 where another bank's chunk sits now, read as nothing.
 * The payload: the chunk's used bits (PS_USED_BYTES), then its slots. Chunks: arp 0, sequences 2..5, performances
 * 6..13, patches 17..22 (where they always were: the patches outlive the change); 1, 14..16, 23 and 24 are free, all in
 * the 200 KiB the 128-slot banks took.
 * A SAVE copies into the bank at once (syn_commit, inside syn_lock) and marks its chunk; the main loop writes it
 * (ps_tick, outside the lock: the erase stops the flash, and the audio with it, for tens of ms), then reports the save
 * done, so SAVED means it is in flash. The boot loads every chunk (ps_load). Patches are stored packed (engine_store_pack,
 * ENGINE_STORED_SIZE a slot); a patch chunk still in the v5, v6 or v7 layout loads through engine_from_v5 / _v6 / _v7
 * and is written as the current version the next time one of its slots is saved.
 * An arp chunk of FREC_VER_ARP 3 / 4 and a performance chunk of FREC_VER_PERF 6 / 7 (2026-10-17: knob 1 became each
 * step's RATE, GATE the pattern's; 2026-10-21: RATE the notes' rate, TIMING the steps' length) are the same size as
 * now: they load through arp_from_v3 / _v4 and whole_perf_from_v6 / _v7, and a
 * performance chunk is converted whole in st_buf before a SAVE writes one of its slots, so its other slots stay.
 * A performance (WholePerformance: 883 bytes in RAM, WHOLE_PERF_STORED_SIZE packed) lives in flash only: the boot reads
 * its name (syn_perf_name) and that it is there, a load reads the one slot (ps_perf_read), and a SAVE stages the one
 * it writes (ps_perf_save), which ps_tick puts into its chunk over the chunk's copy before. A performance chunk's used
 * bits are the slots saved in flash; a factory performance (factory_presets.c) is made when it loads. */
#define PS_BASE WT_STORE_BASE
#define PS_SECTOR 4096u
#define PS_PAYLOAD_OFF 256u
#define PS_PAYLOAD_MAX (PS_SECTOR - PS_PAYLOAD_OFF)
#define PS_USED_BYTES 16u
#define PS_MAGIC 0x504E5243u                        /* "CRNP" */
#define PS_PER(size) ((PS_PAYLOAD_MAX - PS_USED_BYTES) / (size))
#define PS_CHUNKS(size, slots) (((slots) + PS_PER(size) - 1u) / PS_PER(size))
typedef struct {
    uint32_t magic;
    uint8_t kind, chunk, ver, rsv0;                 /* UISAVE_*, the chunk in its bank, the bank's FREC_VER_* */
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} PsHdr;
PS_STATIC_ASSERT(sizeof(PsHdr) == 32u, "the store's header");
typedef struct {
    uint8_t *bank;                                  /* NULL: the performances, in flash only */
    uint16_t size;                                  /* one slot in RAM */
    uint16_t stored;                                /* one slot in flash: size, but a patch / performance is packed */
    uint8_t first, ver;                             /* its first chunk; its layout version */
    uint8_t slots;                                  /* the bank's slots */
} PsKind;
#define PS_N_ARP PS_CHUNKS(sizeof(ArpState), BANK_SLOTS)
#define PS_N_SEQ PS_CHUNKS(sizeof syn_seq_bank[0], BANK_SLOTS)
#define PS_N_PATCH PS_CHUNKS(ENGINE_STORED_SIZE, PATCH_SLOTS)
#define PS_N_PERF PS_CHUNKS(WHOLE_PERF_STORED_SIZE, BANK_SLOTS)
#define PS_FIRST_ARP 0u
#define PS_FIRST_SEQ 2u
#define PS_FIRST_PERF 6u
#define PS_FIRST_PATCH 17u                          /* the 128-slot layout's: the saved patches stay */
#define PS_NCHUNK 25u                               /* the chunks the store spans (some free) */
PS_STATIC_ASSERT(UISAVE_ARP_SEQ == 0 && UISAVE_SEQ_SETTINGS == 1 && UISAVE_PATCH_PRESET == 2 && UISAVE_WHOLE_PERF == 3,
                 "PS_KIND is in UISAVE_* order");
PS_STATIC_ASSERT(PS_N_ARP == 1u && PS_N_SEQ == 4u && PS_N_PERF == 8u && PS_N_PATCH == 6u, "the chunk counts above");
PS_STATIC_ASSERT(PS_FIRST_ARP + PS_N_ARP <= PS_FIRST_SEQ && PS_FIRST_SEQ + PS_N_SEQ <= PS_FIRST_PERF &&
                 PS_FIRST_PERF + PS_N_PERF <= PS_FIRST_PATCH && PS_FIRST_PATCH + PS_N_PATCH <= PS_NCHUNK,
                 "the banks' chunks do not overlap");
PS_STATIC_ASSERT(PS_NCHUNK <= 32u && PS_NCHUNK * 2u * PS_SECTOR <= WT_STORE_SIZE, "the chunks fit the store and ps_dirty");
PS_STATIC_ASSERT(PATCH_SLOTS <= PS_USED_BYTES * 8u && BANK_SLOTS <= PATCH_SLOTS, "the used bits");
PS_STATIC_ASSERT(offsetof(MasterPerformancePreset, name) == 0u, "ps_load reads a stored performance's name first");
/* v5, v6 and v7 patches (2026-10-16/17, before STRIKE's attack bytes or BEND MOD) load through engine_from_v5 / _v6 /
 * _v7: the same slots to a chunk as v8's packed ones, so their chunks sit where v8's do and the chunks after them never
 * moved */
PS_STATIC_ASSERT(PS_PER(ENGINE_V5_SIZE) == PS_PER(ENGINE_STORED_SIZE) && PS_PER(ENGINE_V6_SIZE) == PS_PER(ENGINE_STORED_SIZE),
                 "v5, v6 and v7 patch chunks hold the same slots");
static const PsKind PS_KIND[UISAVE_COUNT] = {
    {(uint8_t *)syn_arp_bank, sizeof(ArpState), sizeof(ArpState), PS_FIRST_ARP, FREC_VER_ARP, BANK_SLOTS},
    {(uint8_t *)syn_seq_bank, sizeof syn_seq_bank[0], sizeof syn_seq_bank[0], PS_FIRST_SEQ, FREC_VER_SEQ, BANK_SLOTS},
    {(uint8_t *)patch_bank, sizeof(EngineState), ENGINE_STORED_SIZE, PS_FIRST_PATCH, FREC_VER_ENGINE, PATCH_SLOTS},
    {NULL, sizeof(WholePerformance), WHOLE_PERF_STORED_SIZE, PS_FIRST_PERF, FREC_VER_PERF, BANK_SLOTS},
};
static uint32_t ps_dirty;                           /* bit per chunk (PS_KIND[].first + chunk): to write */
static uint8_t ps_commit_wait;                      /* a SAVE waits for its chunk: ui_sm_commit_done after */
static WholePerformance ps_perf_save;               /* the performance a SAVE took (syn_commit), for ps_tick to write */
static uint8_t ps_perf_save_slot;

static uint32_t ps_sector(uint32_t chunk, uint32_t copy) { return PS_BASE + (chunk * 2u + copy) * PS_SECTOR; }

/* copy (0 / 1) of chunk: its header valid -> 0 */
static int ps_head(uint32_t chunk, uint32_t copy, uint8_t kind, uint8_t c, PsHdr *h)
{
    if (st_read(ps_sector(chunk, copy), h, sizeof *h))
        return -1;
    if (h->magic != PS_MAGIC || h->kind != kind || h->chunk != c || h->len > PS_PAYLOAD_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

/* the chunk's current copy, its payload in st_buf: 0 / 1, or -1 (none) */
static int ps_current(uint32_t chunk, uint8_t kind, uint8_t c, PsHdr *out)
{
    PsHdr h[2];
    int v[2], i, order[2];
    v[0] = ps_head(chunk, 0u, kind, c, &h[0]) == 0;
    v[1] = ps_head(chunk, 1u, kind, c, &h[1]) == 0;
    order[0] = v[1] && (!v[0] || h[1].seq > h[0].seq) ? 1 : 0;
    order[1] = !order[0];
    for (i = 0; i < 2; i++) {
        int k = order[i];
        if (v[k] && !st_read(ps_sector(chunk, (uint32_t)k) + PS_PAYLOAD_OFF, st_buf, h[k].len) &&
            st_crc32(st_buf, h[k].len) == h[k].crc) {
            *out = h[k];
            return k;
        }
    }
    return -1;
}

static uint32_t ps_slots(const PsKind *pk, uint32_t c, uint32_t *first)
{
    uint32_t per = PS_PER(pk->stored), f = c * per;
    *first = f;
    return f >= pk->slots ? 0u : (pk->slots - f < per ? pk->slots - f : per);
}

/* a performance from flash (outside syn_lock: it reads its chunk into st_buf): 0, or -1 (never saved there, or
 * unreadable) */
static int ps_perf_read(uint32_t slot, WholePerformance *w)
{
    const PsKind *pk = &PS_KIND[UISAVE_WHOLE_PERF];
    uint32_t c = slot / PS_PER(pk->stored), first, n, i;
    PsHdr h;
    if (!flash_ok || slot >= pk->slots)
        return -1;
    n = ps_slots(pk, c, &first);
    if (ps_current(pk->first + c, UISAVE_WHOLE_PERF, (uint8_t)c, &h) < 0 ||
        (h.ver != pk->ver && h.ver != 6u && h.ver != 7u) ||
        h.len != PS_USED_BYTES + n * pk->stored)
        return -1;
    i = slot - first;
    if (!((st_buf[i >> 3] >> (i & 7u)) & 1u))
        return -1;
    if (h.ver == 6u)
        whole_perf_from_v6(st_buf + PS_USED_BYTES + i * pk->stored);
    if (h.ver <= 7u)
        whole_perf_from_v7(st_buf + PS_USED_BYTES + i * pk->stored);
    whole_perf_unpack(st_buf + PS_USED_BYTES + i * pk->stored, w);
    return 0;
}

/* boot: every chunk into its bank (only the slots marked used; the others keep the defaults); a performance: its name */
static void ps_load(void)
{
    uint32_t k, c, i, first, n, nch;
    uint32_t old;
    uint8_t arp3, perf6;
    PsHdr h;
    if (!flash_ok)
        return;
    for (k = 0; k < UISAVE_COUNT; k++) {
        const PsKind *pk = &PS_KIND[k];
        nch = PS_CHUNKS(pk->stored, pk->slots);
        for (c = 0; c < nch; c++) {
            n = ps_slots(pk, c, &first);
            if (ps_current(pk->first + c, (uint8_t)k, (uint8_t)c, &h) < 0)
                continue;                           /* never written, or damaged: empty */
            old = 0u;                               /* a patch chunk of an older layout that migrates: its slot size */
            if (k == UISAVE_PATCH_PRESET && h.ver >= 5u && h.ver <= 7u)
                old = h.ver == 5u ? ENGINE_V5_SIZE : (h.ver == 6u ? ENGINE_V6_SIZE : ENGINE_STORED_SIZE);
            arp3 = (uint8_t)(k == UISAVE_ARP_SEQ && (h.ver == 3u || h.ver == 4u));   /* same-size layouts that */
            perf6 = (uint8_t)(k == UISAVE_WHOLE_PERF && (h.ver == 6u || h.ver == 7u));   /* migrate in place */
            if (old ? h.len != PS_USED_BYTES + n * old :
                      ((h.ver != pk->ver && !arp3 && !perf6) || h.len != PS_USED_BYTES + n * pk->stored))
                continue;                           /* another layout: empty */
            for (i = 0; i < n; i++)
                if ((st_buf[i >> 3] >> (i & 7u)) & 1u) {
                    const uint8_t *src = st_buf + PS_USED_BYTES + i * (old ? old : pk->stored);
                    uint8_t *dst = pk->bank ? pk->bank + (first + i) * pk->size : NULL;
                    if (pk->bank == NULL)           /* a performance: its name (MasterPerformancePreset's first bytes) */
                        memcpy(syn_perf_name[first + i], src, NAME_LEN);
                    else if (old && h.ver == 5u)
                        engine_from_v5(src, (EngineState *)(void *)dst);
                    else if (old && h.ver == 6u)
                        engine_from_v6(src, (EngineState *)(void *)dst);
                    else if (old)
                        engine_from_v7(src, (EngineState *)(void *)dst);
                    else if (k == UISAVE_PATCH_PRESET)
                        engine_store_unpack(src, (EngineState *)(void *)dst);
                    else
                        memcpy(dst, src, pk->size);
                    if (arp3 && h.ver == 3u)
                        arp_from_v3((ArpState *)(void *)dst);
                    if (arp3)
                        arp_from_v4((ArpState *)(void *)dst);
                    syn_used[k][(first + i) >> 3] |= (uint8_t)(1u << ((first + i) & 7u));
                }
        }
    }
}

/* one chunk to flash, into the copy that is not current: 0 ok */
static int ps_save_chunk(uint32_t k, uint32_t c)
{
    const PsKind *pk = &PS_KIND[k];
    uint32_t chunk = pk->first + c, first, n = ps_slots(pk, c, &first), len = PS_USED_BYTES + n * pk->stored, i, base,
             off, seq;
    int cur;
    PsHdr h;
    cur = ps_current(chunk, (uint8_t)k, (uint8_t)c, &h);
    seq = cur < 0 ? 0u : h.seq;
    if (pk->bank == NULL) {                         /* a performance: the staged one over the chunk's copy before */
        if (cur < 0 || (h.ver != pk->ver && h.ver != 6u && h.ver != 7u) || h.len != len)
            memset(st_buf, 0, len);                 /* (none, or another layout: the chunk's others stay unsaved) */
        else if (h.ver != pk->ver)                  /* v6 / v7: every slot to v8 first (unused ones are never read) */
            for (i = 0; i < n; i++) {
                if (h.ver == 6u)
                    whole_perf_from_v6(st_buf + PS_USED_BYTES + i * pk->stored);
                whole_perf_from_v7(st_buf + PS_USED_BYTES + i * pk->stored);
            }
        if (ps_perf_save_slot >= first && ps_perf_save_slot < first + n) {
            i = ps_perf_save_slot - first;
            st_buf[i >> 3] |= (uint8_t)(1u << (i & 7u));
            whole_perf_pack(&ps_perf_save, st_buf + PS_USED_BYTES + i * pk->stored);
        }
    } else {
        memset(st_buf, 0, PS_USED_BYTES);           /* the payload, built in st_buf (a RAM source for the driver) */
        for (i = 0; i < n; i++)
            if (syn_slot_used((uint8_t)k, (uint8_t)(first + i)))
                st_buf[i >> 3] |= (uint8_t)(1u << (i & 7u));
        if (k == UISAVE_PATCH_PRESET)
            for (i = 0; i < n; i++)
                engine_store_pack((const EngineState *)(const void *)(pk->bank + (first + i) * pk->size),
                                  st_buf + PS_USED_BYTES + i * pk->stored);
        else
            memcpy(st_buf + PS_USED_BYTES, pk->bank + first * pk->size, n * pk->size);
    }
    base = ps_sector(chunk, cur == 0 ? 1u : 0u);
    if (st_erase(base))
        return -1;
    for (off = 0; off < len; off += 256u)
        if (st_prog(base + PS_PAYLOAD_OFF + off, st_buf + off, len - off > 256u ? 256u : len - off))
            return -2;
    memset(&h, 0xFF, sizeof h);
    h.magic = PS_MAGIC;
    h.kind = (uint8_t)k;
    h.chunk = (uint8_t)c;
    h.ver = pk->ver;
    h.rsv0 = 0xFFu;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if (st_prog(base, &h, sizeof h))                /* the commit record, last */
        return -3;
    {   /* read back: a failing part must not report SAVED */
        PsHdr chk;
        if (ps_current(chunk, (uint8_t)k, (uint8_t)c, &chk) != (cur == 0 ? 1 : 0) || chk.seq != h.seq)
            return -4;
    }
    return 0;
}

/* the main loop's: the marked chunks to flash, then the SAVE that waited is done (SAVED, or back to the menu) */
static void ps_tick(void)
{
    uint32_t k, c, rc = 0;
    if (!ps_dirty)
        return;
    for (k = 0; k < UISAVE_COUNT; k++)
        for (c = 0; c < PS_CHUNKS(PS_KIND[k].stored, PS_KIND[k].slots); c++)
            if ((ps_dirty >> (PS_KIND[k].first + c)) & 1u) {
                if (!flash_ok || ps_save_chunk(k, c))
                    rc = 1;
                ps_dirty &= ~(1u << (PS_KIND[k].first + c));
            }
    if (ps_commit_wait) {
        syn_lock();
        ui_sm_commit_done(&sm, (uint8_t)!rc);
        syn_unlock();
        ps_commit_wait = 0;
    }
}
