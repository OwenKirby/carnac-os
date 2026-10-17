/* The preset store on the FM1 (2026-10-16): a unity-build fragment, like wt_store_fm1.c. synth_main_fm1.c includes it
 * after its banks (syn_arp_bank, syn_seq_bank, syn_perf_bank, preset.c's patch_bank), syn_used / syn_slot_used, sm,
 * syn_lock / syn_unlock and flash_ok; X0X's storage.c gives st_read / st_erase / st_prog / st_crc32 / st_buf.
 * src/host/test_preset_store.c runs it on a simulated NOR (save, load, both copies, a save cut off, other layouts). */
/* ------------------------------------------------------- preset store --- */
/* The SAVEs in flash (2026-10-16): the four banks above (patches, performances, sequences, arps; 128 slots each) and
 * which slots were written, kept across power cycles. In the store flash the wavetable image was uploaded to before it
 * moved into the firmware (WT_STORE_BASE, 0xA0000, 240 KiB): each bank in chunks of the slots that fit a 4 KiB
 * sector, each chunk two sectors written in turn, as X0X's storage.c does (the payload from offset 256, the CRC'd
 * 32-byte header at 0 last, read back after): a save cut off by a power cut leaves the chunk's copy before it in
 * charge. A header names the bank, the chunk and the bank's layout version (FREC_VER_*: a chunk written under another
 * layout is left out, its slots empty, until a migration exists), so the old wavetable image there reads as nothing.
 * The payload: the chunk's used bits (PS_USED_BYTES), then its slots. 2 + 15 + 6 + 2 = 25 chunks, 200 KiB.
 * A SAVE copies into the bank at once (syn_commit, inside syn_lock) and marks its chunk; the main loop writes it
 * (ps_tick, outside the lock: the erase stops the flash, and the audio with it, for tens of ms), then reports the save
 * done, so SAVED means it is in flash. The boot loads every chunk (ps_load). Patches are stored packed (engine_store_pack,
 * ENGINE_STORED_SIZE a slot); a patch chunk still in the v5 or v6 layout loads through engine_from_v5 / _v6 and is
 * written as v7 the next time one of its slots is saved. */
#define PS_BASE WT_STORE_BASE
#define PS_SECTOR 4096u
#define PS_PAYLOAD_OFF 256u
#define PS_PAYLOAD_MAX (PS_SECTOR - PS_PAYLOAD_OFF)
#define PS_USED_BYTES 16u
#define PS_MAGIC 0x504E5243u                        /* "CRNP" */
#define PS_PER(size) ((PS_PAYLOAD_MAX - PS_USED_BYTES) / (size))
#define PS_CHUNKS(size) ((SYN_SLOTS + PS_PER(size) - 1u) / PS_PER(size))
typedef struct {
    uint32_t magic;
    uint8_t kind, chunk, ver, rsv0;                 /* UISAVE_*, the chunk in its bank, the bank's FREC_VER_* */
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} PsHdr;
PS_STATIC_ASSERT(sizeof(PsHdr) == 32u, "the store's header");
typedef struct {
    uint8_t *bank;
    uint16_t size;                                  /* one slot in the bank */
    uint16_t stored;                                /* one slot in flash: size, but a patch is packed (engine_store_pack) */
    uint8_t first, ver;                             /* its first chunk; its layout version */
} PsKind;
#define PS_N_ARP PS_CHUNKS(sizeof(ArpState))
#define PS_N_SEQ PS_CHUNKS(sizeof syn_seq_bank[0])
#define PS_N_PATCH PS_CHUNKS(ENGINE_STORED_SIZE)
#define PS_N_PERF PS_CHUNKS(sizeof(MasterPerformancePreset))
#define PS_NCHUNK (PS_N_ARP + PS_N_SEQ + PS_N_PATCH + PS_N_PERF)
PS_STATIC_ASSERT(UISAVE_ARP_SEQ == 0 && UISAVE_SEQ_SETTINGS == 1 && UISAVE_PATCH_PRESET == 2 && UISAVE_WHOLE_PERF == 3,
                 "PS_KIND is in UISAVE_* order");
PS_STATIC_ASSERT(PS_NCHUNK <= 32u && PS_NCHUNK * 2u * PS_SECTOR <= WT_STORE_SIZE, "the chunks fit the store and ps_dirty");
PS_STATIC_ASSERT(SYN_SLOTS <= PS_USED_BYTES * 8u && PATCH_SLOTS == SYN_SLOTS, "the used bits");
/* v5 and v6 patches (2026-10-16, before STRIKE's attack bytes) load through engine_from_v5 / _v6: the same slots to a
 * chunk as v7's packed ones, so their chunks sit where v7's do and the chunks after them never moved */
PS_STATIC_ASSERT(PS_PER(ENGINE_V5_SIZE) == PS_PER(ENGINE_STORED_SIZE) && PS_PER(ENGINE_V6_SIZE) == PS_PER(ENGINE_STORED_SIZE),
                 "v5, v6 and v7 patch chunks hold the same slots");
static const PsKind PS_KIND[UISAVE_COUNT] = {
    {(uint8_t *)syn_arp_bank, sizeof(ArpState), sizeof(ArpState), 0u, FREC_VER_ARP},
    {(uint8_t *)syn_seq_bank, sizeof syn_seq_bank[0], sizeof syn_seq_bank[0], PS_N_ARP, FREC_VER_SEQ},
    {(uint8_t *)patch_bank, sizeof(EngineState), ENGINE_STORED_SIZE, PS_N_ARP + PS_N_SEQ, FREC_VER_ENGINE},
    {(uint8_t *)syn_perf_bank, sizeof(MasterPerformancePreset), sizeof(MasterPerformancePreset), PS_N_ARP + PS_N_SEQ + PS_N_PATCH,
     FREC_VER_PERF},
};
static uint32_t ps_dirty;                           /* bit per chunk (PS_KIND[].first + chunk): to write */
static uint8_t ps_commit_wait;                      /* a SAVE waits for its chunk: ui_sm_commit_done after */

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
    return f >= SYN_SLOTS ? 0u : (SYN_SLOTS - f < per ? SYN_SLOTS - f : per);
}

/* boot: every chunk into its bank (only the slots marked used; the others keep the defaults) */
static void ps_load(void)
{
    uint32_t k, c, i, first, n, nch;
    uint32_t old;
    PsHdr h;
    if (!flash_ok)
        return;
    for (k = 0; k < UISAVE_COUNT; k++) {
        const PsKind *pk = &PS_KIND[k];
        nch = PS_CHUNKS(pk->stored);
        for (c = 0; c < nch; c++) {
            n = ps_slots(pk, c, &first);
            if (ps_current(pk->first + c, (uint8_t)k, (uint8_t)c, &h) < 0)
                continue;                           /* never written, or damaged: empty */
            old = 0u;                               /* a patch chunk of an older layout that migrates: its slot size */
            if (k == UISAVE_PATCH_PRESET && h.ver == 5u)
                old = ENGINE_V5_SIZE;
            if (k == UISAVE_PATCH_PRESET && h.ver == 6u)
                old = ENGINE_V6_SIZE;
            if (old ? h.len != PS_USED_BYTES + n * old : (h.ver != pk->ver || h.len != PS_USED_BYTES + n * pk->stored))
                continue;                           /* another layout: empty */
            for (i = 0; i < n; i++)
                if ((st_buf[i >> 3] >> (i & 7u)) & 1u) {
                    uint8_t *dst = pk->bank + (first + i) * pk->size;
                    const uint8_t *src = st_buf + PS_USED_BYTES + i * (old ? old : pk->stored);
                    if (old == ENGINE_V5_SIZE)
                        engine_from_v5(src, (EngineState *)(void *)dst);
                    else if (old == ENGINE_V6_SIZE)
                        engine_from_v6(src, (EngineState *)(void *)dst);
                    else if (k == UISAVE_PATCH_PRESET)
                        engine_store_unpack(src, (EngineState *)(void *)dst);
                    else
                        memcpy(dst, src, pk->size);
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
    memset(st_buf, 0, PS_USED_BYTES);               /* the payload, built in st_buf (a RAM source for the driver) */
    for (i = 0; i < n; i++)
        if (syn_slot_used((uint8_t)k, (uint8_t)(first + i)))
            st_buf[i >> 3] |= (uint8_t)(1u << (i & 7u));
    if (k == UISAVE_PATCH_PRESET)
        for (i = 0; i < n; i++)
            engine_store_pack((const EngineState *)(const void *)(pk->bank + (first + i) * pk->size),
                              st_buf + PS_USED_BYTES + i * pk->stored);
    else
        memcpy(st_buf + PS_USED_BYTES, pk->bank + first * pk->size, n * pk->size);
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
        for (c = 0; c < PS_CHUNKS(PS_KIND[k].stored); c++)
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
