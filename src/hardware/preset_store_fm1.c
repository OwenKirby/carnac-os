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
 * 6..13, patches 17..22 (where they always were: the patches outlive the change); 14..16 are free (1: the sequence
 * and arp names, below). Since
 * 2026-10-21 the store ends after chunk 22 (PS_NCHUNK 23): its old chunks 23 and 24 and the flash after them are PCM
 * wave area 1 (pcm_store_fm1.c).
 * A SAVE copies into the bank at once (syn_commit, inside syn_lock) and marks its chunk; the main loop writes it
 * (ps_tick, outside the lock: the erase stops the flash, and the audio with it, for tens of ms), then reports the save
 * done, so SAVED means it is in flash. The boot loads every chunk (ps_load). Patches are stored packed (engine_store_pack,
 * ENGINE_STORED_SIZE a slot); a patch chunk still in the v5 .. v8 layout loads through engine_from_v5 / _v6 / _v7 / _v8
 * and is written as the current version the next time one of its slots is saved.
 * An arp chunk of FREC_VER_ARP 3 / 4 and a performance chunk of FREC_VER_PERF 6 / 7 / 8 (2026-10-17: knob 1 became
 * each step's RATE, GATE the pattern's; 2026-10-21: the patches' v9 (8, proto/sh2-filter), then RATE the notes' rate
 * and TIMING the steps' length (arp 5, perf 9)) are the same size as now: they load through arp_from_v3 / _v4 and
 * whole_perf_from_old, and a
 * performance chunk is converted whole in st_buf before a SAVE writes one of its slots, so its other slots stay.
 * A performance (WholePerformance: 883 bytes in RAM, WHOLE_PERF_STORED_SIZE packed) lives in flash only: the boot reads
 * its name (syn_perf_name) and that it is there, a load reads the one slot (ps_perf_read), and a SAVE stages the one
 * it writes (ps_perf_save), which ps_tick puts into its chunk over the chunk's copy before. A performance chunk's used
 * bits are the slots saved in flash; a factory performance (factory_presets.c) is made when it loads.
 * Names of the sequences and arp patterns (2026-10-25): chunk 1, one table for both banks (syn_seq_name, syn_arp_name),
 * so the sequence and arp chunks keep their layout (a 9-slot sequence chunk has no room for 9 names). Its used bits
 * (PS_NAMES_USED: bit s a sequence slot, BANK_SLOTS + s an arp slot) mark the names a SAVE wrote; the others keep the
 * factory's. A SAVE writes the table only when the slot's name changed (one erase, not two, for a plain re-save). */
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
#define PS_NCHUNK 23u                               /* the chunks the store spans (14..16 free); PCM waves after */
#define PS_NAMES_CHUNK 1u                           /* the sequence and arp names */
#define PS_NAMES_KIND UISAVE_COUNT                  /* its header's kind (no UISAVE_* bank) */
#define PS_NAMES_VER 1u
#define PS_NAMES_LEN (PS_USED_BYTES + 2u * BANK_SLOTS * NAME_LEN)
PS_STATIC_ASSERT(UISAVE_ARP_SEQ == 0 && UISAVE_SEQ_SETTINGS == 1 && UISAVE_PATCH_PRESET == 2 && UISAVE_WHOLE_PERF == 3,
                 "PS_KIND is in UISAVE_* order");
PS_STATIC_ASSERT(PS_N_ARP == 1u && PS_N_SEQ == 4u && PS_N_PERF == 8u && PS_N_PATCH == 6u, "the chunk counts above");
PS_STATIC_ASSERT(PS_FIRST_ARP + PS_N_ARP <= PS_FIRST_SEQ && PS_FIRST_SEQ + PS_N_SEQ <= PS_FIRST_PERF &&
                 PS_FIRST_PERF + PS_N_PERF <= PS_FIRST_PATCH && PS_FIRST_PATCH + PS_N_PATCH <= PS_NCHUNK,
                 "the banks' chunks do not overlap");
PS_STATIC_ASSERT(PS_NCHUNK <= 32u && PS_NCHUNK * 2u * PS_SECTOR <= WT_STORE_SIZE, "the chunks fit the store and ps_dirty");
PS_STATIC_ASSERT(PATCH_SLOTS <= PS_USED_BYTES * 8u && BANK_SLOTS <= PATCH_SLOTS, "the used bits");
PS_STATIC_ASSERT(PS_FIRST_ARP + PS_N_ARP <= PS_NAMES_CHUNK && PS_NAMES_CHUNK < PS_FIRST_SEQ &&
                 2u * BANK_SLOTS <= PS_USED_BYTES * 8u && PS_NAMES_LEN <= PS_PAYLOAD_MAX, "the names chunk");
PS_STATIC_ASSERT(offsetof(MasterPerformancePreset, name) == 0u, "ps_load reads a stored performance's name first");
/* v5 .. v8 patches (2026-10-16/17/21, before STRIKE's attack bytes, BEND MOD or DRIVE) load through engine_from_v5 /
 * _v6 / _v7 / _v8: the same slots to a chunk as v9's packed ones, so their chunks sit where v8's do and the chunks after them never
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
static uint8_t ps_names_used[PS_USED_BYTES];        /* the names chunk's used bits: the names a SAVE wrote */

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
    if (ps_current(pk->first + c, UISAVE_WHOLE_PERF, (uint8_t)c, &h) < 0 || (h.ver != pk->ver && !WHOLE_PERF_OLD(h.ver)) ||
        h.len != PS_USED_BYTES + n * pk->stored)
        return -1;
    i = slot - first;
    if (!((st_buf[i >> 3] >> (i & 7u)) & 1u))
        return -1;
    if (WHOLE_PERF_OLD(h.ver))
        whole_perf_from_old(st_buf + PS_USED_BYTES + i * pk->stored, h.ver);
    whole_perf_unpack(st_buf + PS_USED_BYTES + i * pk->stored, w);
    return 0;
}

/* boot: every chunk into its bank (only the slots marked used; the others keep the defaults); a performance: its name */
static void ps_load(void)
{
    uint32_t k, c, i, first, n, nch;
    uint32_t old;
    uint8_t arp3, perf_old;
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
            if (k == UISAVE_PATCH_PRESET && h.ver >= 5u && h.ver <= 8u)
                old = h.ver == 5u ? ENGINE_V5_SIZE : (h.ver == 6u ? ENGINE_V6_SIZE : ENGINE_STORED_SIZE);
            arp3 = (uint8_t)(k == UISAVE_ARP_SEQ && (h.ver == 3u || h.ver == 4u));   /* same-size layouts that */
            perf_old = (uint8_t)(k == UISAVE_WHOLE_PERF && WHOLE_PERF_OLD(h.ver));   /* migrate in place */
            if (old ? h.len != PS_USED_BYTES + n * old :
                      ((h.ver != pk->ver && !arp3 && !perf_old) || h.len != PS_USED_BYTES + n * pk->stored))
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
                    else if (old && h.ver == 7u)
                        engine_from_v7(src, (EngineState *)(void *)dst);
                    else if (old)
                        engine_from_v8(src, (EngineState *)(void *)dst);
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
    if (ps_current(PS_NAMES_CHUNK, PS_NAMES_KIND, 0u, &h) < 0 || h.ver != PS_NAMES_VER || h.len != PS_NAMES_LEN)
        return;                                     /* (none yet: every name the factory's) */
    memcpy(ps_names_used, st_buf, PS_USED_BYTES);
    for (i = 0; i < 2u * BANK_SLOTS; i++)
        if ((ps_names_used[i >> 3] >> (i & 7u)) & 1u)
            memcpy(i < BANK_SLOTS ? syn_seq_name[i] : syn_arp_name[i - BANK_SLOTS],
                   st_buf + PS_USED_BYTES + i * NAME_LEN, NAME_LEN);
}

/* a SAVE of sequence / arp slot s (UISAVE_SEQ_SETTINGS / _ARP_SEQ) named name: into the bank's names; the names
 * chunk marked when it changed (inside syn_lock, as syn_commit) */
static void ps_name_save(uint8_t k, uint8_t s, const uint8_t name[NAME_LEN])
{
    uint8_t *dst = k == UISAVE_SEQ_SETTINGS ? syn_seq_name[s] : syn_arp_name[s];
    uint32_t b = (k == UISAVE_SEQ_SETTINGS ? 0u : BANK_SLOTS) + s;
    if (memcmp(dst, name, NAME_LEN) == 0)
        return;
    memcpy(dst, name, NAME_LEN);
    ps_names_used[b >> 3] |= (uint8_t)(1u << (b & 7u));
    ps_dirty |= 1u << PS_NAMES_CHUNK;
}

static int ps_write(uint32_t chunk, uint8_t kind, uint8_t c, uint8_t ver, uint32_t len, int cur, uint32_t seq);

/* the names chunk to flash: 0 ok */
static int ps_save_names(void)
{
    PsHdr h;
    int cur = ps_current(PS_NAMES_CHUNK, PS_NAMES_KIND, 0u, &h);
    uint32_t seq = cur < 0 ? 0u : h.seq;
    memcpy(st_buf, ps_names_used, PS_USED_BYTES);
    memcpy(st_buf + PS_USED_BYTES, syn_seq_name, BANK_SLOTS * NAME_LEN);
    memcpy(st_buf + PS_USED_BYTES + BANK_SLOTS * NAME_LEN, syn_arp_name, BANK_SLOTS * NAME_LEN);
    return ps_write(PS_NAMES_CHUNK, PS_NAMES_KIND, 0u, PS_NAMES_VER, PS_NAMES_LEN, cur, seq);
}

/* one chunk to flash, into the copy that is not current: 0 ok */
static int ps_save_chunk(uint32_t k, uint32_t c)
{
    const PsKind *pk = &PS_KIND[k];
    uint32_t chunk = pk->first + c, first, n = ps_slots(pk, c, &first), len = PS_USED_BYTES + n * pk->stored, i, seq;
    int cur;
    PsHdr h;
    cur = ps_current(chunk, (uint8_t)k, (uint8_t)c, &h);
    seq = cur < 0 ? 0u : h.seq;
    if (pk->bank == NULL) {                         /* a performance: the staged one over the chunk's copy before */
        if (cur < 0 || (h.ver != pk->ver && !WHOLE_PERF_OLD(h.ver)) || h.len != len)
            memset(st_buf, 0, len);                 /* (none, or another layout: the chunk's others stay unsaved) */
        else if (WHOLE_PERF_OLD(h.ver))             /* v6 .. v8: every slot to today's first (unused ones are never read) */
            for (i = 0; i < n; i++)
                whole_perf_from_old(st_buf + PS_USED_BYTES + i * pk->stored, h.ver);
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
    return ps_write(chunk, (uint8_t)k, (uint8_t)c, pk->ver, len, cur, seq);
}

/* the payload in st_buf (len bytes) to chunk's copy that is not current (cur), its header last, read back: 0 ok */
static int ps_write(uint32_t chunk, uint8_t kind, uint8_t c, uint8_t ver, uint32_t len, int cur, uint32_t seq)
{
    uint32_t base = ps_sector(chunk, cur == 0 ? 1u : 0u), off;
    PsHdr h;
    if (st_erase(base))
        return -1;
    for (off = 0; off < len; off += 256u)
        if (st_prog(base + PS_PAYLOAD_OFF + off, st_buf + off, len - off > 256u ? 256u : len - off))
            return -2;
    memset(&h, 0xFF, sizeof h);
    h.magic = PS_MAGIC;
    h.kind = kind;
    h.chunk = c;
    h.ver = ver;
    h.rsv0 = 0xFFu;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if (st_prog(base, &h, sizeof h))                /* the commit record, last */
        return -3;
    {   /* read back: a failing part must not report SAVED */
        PsHdr chk;
        if (ps_current(chunk, kind, c, &chk) != (cur == 0 ? 1 : 0) || chk.seq != h.seq)
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
    if ((ps_dirty >> PS_NAMES_CHUNK) & 1u) {
        if (!flash_ok || ps_save_names())
            rc = 1;
        ps_dirty &= ~(1u << PS_NAMES_CHUNK);
    }
    if (ps_commit_wait) {
        syn_lock();
        ui_sm_commit_done(&sm, (uint8_t)!rc);
        syn_unlock();
        ps_commit_wait = 0;
    }
}
