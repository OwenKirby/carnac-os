/* The preset store on the FM1 (2026-10-16): a unity-build fragment, like wt_store_fm1.c. synth_main_fm1.c includes it
 * after its banks (syn_arp_bank, syn_seq_bank, preset.c's patch_bank), syn_perf_name, syn_used / syn_slot_used, sm,
 * syn_lock / syn_unlock and flash_ok; X0X's storage.c gives st_read / st_erase / st_prog / st_crc32.
 * src/host/test_preset_store.c runs it on a simulated NOR (save, load, both copies, a save cut off, other layouts). */
/* ------------------------------------------------------- preset store --- */
/* The SAVEs in flash (2026-10-16): the four banks (128 patches; since 2026-10-17 32 sequences, 32 arps and 32 whole
 * performances) and which slots were written, kept across power cycles. In the store flash the wavetable image was
 * uploaded to before it moved into the firmware (WT_STORE_BASE, 0xA0000, 240 KiB): each bank in chunks of the slots
 * that fit a 4 KiB sector, each chunk two sectors written in turn, as X0X's storage.c does (the CRC'd 32-byte header
 * at 0 written last, read back after): a save cut off by a power cut leaves the chunk's copy before it in charge.
 * Framing (2026-11-03, the format bump): the payload starts right after the header (PS_PAYLOAD_OFF 32), the first page
 * (the header and the payload's first 224 bytes) programmed last, in one go; the header's rsv0 says so (PS_FRAME_TIGHT).
 * A chunk written before (rsv0 0xFF) has its payload at 256, as X0X's: it still loads (the sequences, the arps, the
 * names), and is written the new way the next time it is saved. The 224 bytes buy 11 a patch (22 to a chunk at 184).
 * A header names the bank, the chunk and the bank's layout version (FREC_VER_*: a chunk written under another layout
 * is left out, its slots empty; nothing migrates before V1), so the old wavetable image there, and a chunk of the
 * 128-slot layout before 2026-10-17 where another bank's chunk sits now, read as nothing.
 * The payload: the chunk's used bits (PS_USED_BYTES), then its slots. Chunks: arp 0, sequences 2..5, performances
 * 6..13, patches 17..22 (where they always were: the patches outlive the change); 14..16 are free (1: the sequence
 * and arp names, below). Since
 * 2026-10-21 the store ends after chunk 22 (PS_NCHUNK 23): its old chunks 23 and 24 and the flash after them are PCM
 * wave area 1 (pcm_store_fm1.c).
 * A SAVE copies into the bank at once (syn_commit, inside syn_lock) and marks its chunk; the main loop writes it
 * (ps_tick, outside the lock: the erase stops the flash, and the audio with it, for tens of ms), then reports the save
 * done, so SAVED means it is in flash. The boot loads every chunk (ps_load). Patches are stored packed (engine_store_pack,
 * ENGINE_STORED_SIZE a slot). Older layouts (patches v10, performances v10, arps v4 and before) read as empty: since
 * the format bump of 2026-11-03 nothing migrates (before V1 the patches may go).
 * A performance (WholePerformance: 954 bytes in RAM, WHOLE_PERF_STORED_SIZE packed) lives in flash only: the boot reads
 * its name (syn_perf_name) and that it is there, a load reads the one slot (ps_perf_read), and a SAVE stages the one
 * it writes (ps_perf_save), which ps_tick puts into its chunk over the chunk's copy before. A performance chunk's used
 * bits are the slots saved in flash; a factory performance (factory_presets.c) is made when it loads.
 * Names of the sequences and arp patterns (2026-10-25): chunk 1, one table for both banks (syn_seq_name, syn_arp_name),
 * so the sequence and arp chunks keep their layout (a 9-slot sequence chunk has no room for 9 names). Its used bits
 * (PS_NAMES_USED: bit s a sequence slot, BANK_SLOTS + s an arp slot) mark the names a SAVE wrote; the others keep the
 * factory's. A SAVE writes the table only when the slot's name changed (one erase, not two, for a plain re-save).
 * Since 2026-10-29 (PS_NAMES_VER 2) the chunk also keeps GLOBAL's USER tunings after the names (dsp/tuning.h), written
 * when an MTS message changed one (tuning_unsaved); a version 1 chunk still loads its names. */
#define PS_BASE WT_STORE_BASE
#define PS_SECTOR 4096u
#define PS_PAYLOAD_OFF 32u                          /* right after the header (PS_FRAME_TIGHT) */
#define PS_PAYLOAD_OFF_OLD 256u                     /* before 2026-11-03 (X0X's framing) */
#define PS_FRAME_TIGHT 0x01u                        /* the header's rsv0: the payload at PS_PAYLOAD_OFF */
#define PS_PAYLOAD_MAX (PS_SECTOR - PS_PAYLOAD_OFF)
#define PS_PAGE 256u                                /* the flash's program page */
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
PS_STATIC_ASSERT(sizeof(PsHdr) == 32u && PS_PAYLOAD_OFF == sizeof(PsHdr), "the store's header, the payload after it");
static uint8_t ps_buf[PS_PAYLOAD_MAX] __attribute__((aligned(4)));   /* a chunk's payload (X0X's st_buf is 224 short) */
static uint8_t ps_page[PS_PAGE] __attribute__((aligned(4)));         /* the first page: the header, the payload's head */
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
#define PS_NCHUNK 23u                               /* the chunks the store spans (14..16: bank 0, pcm_store_fm1.c) */
#define PS_NAMES_CHUNK 1u                           /* the sequence and arp names */
#define PS_NAMES_KIND UISAVE_COUNT                  /* its header's kind (no UISAVE_* bank) */
#define PS_NAMES_VER 2u                             /* 2: + the USER tunings */
#define PS_NAMES_V1_LEN (PS_USED_BYTES + 2u * BANK_SLOTS * NAME_LEN)
#define PS_NAMES_LEN (PS_NAMES_V1_LEN + sizeof tuning_user)
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
/* the framing's change moves no slot: an old-framed sequence or arp chunk held as many (3840 - 16 bytes then) */
#define PS_PER_OLD(size) ((PS_SECTOR - PS_PAYLOAD_OFF_OLD - PS_USED_BYTES) / (size))
PS_STATIC_ASSERT(PS_PER_OLD(sizeof syn_seq_bank[0]) == PS_PER(sizeof syn_seq_bank[0]) &&
                 PS_PER_OLD(sizeof(ArpState)) >= BANK_SLOTS && PS_PER(sizeof(ArpState)) >= BANK_SLOTS,
                 "the sequences' and arps' slots stay where they were");
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
    if (h->magic != PS_MAGIC || h->kind != kind || h->chunk != c ||
        h->len > (h->rsv0 == PS_FRAME_TIGHT ? PS_PAYLOAD_MAX : PS_SECTOR - PS_PAYLOAD_OFF_OLD) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

/* the chunk's current copy, its payload in ps_buf: 0 / 1, or -1 (none) */
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
        uint32_t off = h[k].rsv0 == PS_FRAME_TIGHT ? PS_PAYLOAD_OFF : PS_PAYLOAD_OFF_OLD;
        if (v[k] && !st_read(ps_sector(chunk, (uint32_t)k) + off, ps_buf, h[k].len) &&
            st_crc32(ps_buf, h[k].len) == h[k].crc) {
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

/* a performance from flash (outside syn_lock: it reads its chunk into ps_buf): 0, or -1 (never saved there, or
 * unreadable) */
static int ps_perf_read(uint32_t slot, WholePerformance *w)
{
    const PsKind *pk = &PS_KIND[UISAVE_WHOLE_PERF];
    uint32_t c = slot / PS_PER(pk->stored), first, n, i;
    PsHdr h;
    if (!flash_ok || slot >= pk->slots)
        return -1;
    n = ps_slots(pk, c, &first);
    if (ps_current(pk->first + c, UISAVE_WHOLE_PERF, (uint8_t)c, &h) < 0 || h.ver != pk->ver ||
        h.len != PS_USED_BYTES + n * pk->stored)
        return -1;
    i = slot - first;
    if (!((ps_buf[i >> 3] >> (i & 7u)) & 1u))
        return -1;
    whole_perf_unpack(ps_buf + PS_USED_BYTES + i * pk->stored, w);
    return 0;
}

/* boot: every chunk into its bank (only the slots marked used; the others keep the defaults); a performance: its name */
static void ps_load(void)
{
    uint32_t k, c, i, first, n, nch;
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
            if (h.ver != pk->ver || h.len != PS_USED_BYTES + n * pk->stored)
                continue;                           /* another layout: empty */
            for (i = 0; i < n; i++)
                if ((ps_buf[i >> 3] >> (i & 7u)) & 1u) {
                    const uint8_t *src = ps_buf + PS_USED_BYTES + i * pk->stored;
                    uint8_t *dst = pk->bank ? pk->bank + (first + i) * pk->size : NULL;
                    if (pk->bank == NULL)           /* a performance: its name (MasterPerformancePreset's first bytes) */
                        memcpy(syn_perf_name[first + i], src, NAME_LEN);
                    else if (k == UISAVE_PATCH_PRESET)
                        engine_store_unpack(src, (EngineState *)(void *)dst);
                    else
                        memcpy(dst, src, pk->size);
                    syn_used[k][(first + i) >> 3] |= (uint8_t)(1u << ((first + i) & 7u));
                }
        }
    }
    if (ps_current(PS_NAMES_CHUNK, PS_NAMES_KIND, 0u, &h) < 0 ||
        !((h.ver == PS_NAMES_VER && h.len == PS_NAMES_LEN) || (h.ver == 1u && h.len == PS_NAMES_V1_LEN)))
        return;                                     /* (none yet: every name the factory's, every tuning in tune) */
    if (h.ver == PS_NAMES_VER)
        memcpy(tuning_user, ps_buf + PS_NAMES_V1_LEN, sizeof tuning_user);
    memcpy(ps_names_used, ps_buf, PS_USED_BYTES);
    for (i = 0; i < 2u * BANK_SLOTS; i++)
        if ((ps_names_used[i >> 3] >> (i & 7u)) & 1u)
            memcpy(i < BANK_SLOTS ? syn_seq_name[i] : syn_arp_name[i - BANK_SLOTS],
                   ps_buf + PS_USED_BYTES + i * NAME_LEN, NAME_LEN);
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
    memcpy(ps_buf, ps_names_used, PS_USED_BYTES);
    memcpy(ps_buf + PS_USED_BYTES, syn_seq_name, BANK_SLOTS * NAME_LEN);
    memcpy(ps_buf + PS_USED_BYTES + BANK_SLOTS * NAME_LEN, syn_arp_name, BANK_SLOTS * NAME_LEN);
    memcpy(ps_buf + PS_NAMES_V1_LEN, tuning_user, sizeof tuning_user);
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
        if (cur < 0 || h.ver != pk->ver || h.len != len)
            memset(ps_buf, 0, len);                 /* (none, or another layout: the chunk's others stay unsaved) */
        if (ps_perf_save_slot >= first && ps_perf_save_slot < first + n) {
            i = ps_perf_save_slot - first;
            ps_buf[i >> 3] |= (uint8_t)(1u << (i & 7u));
            whole_perf_pack(&ps_perf_save, ps_buf + PS_USED_BYTES + i * pk->stored);
        }
    } else {
        memset(ps_buf, 0, PS_USED_BYTES);           /* the payload, built in ps_buf (a RAM source for the driver) */
        for (i = 0; i < n; i++)
            if (syn_slot_used((uint8_t)k, (uint8_t)(first + i)))
                ps_buf[i >> 3] |= (uint8_t)(1u << (i & 7u));
        if (k == UISAVE_PATCH_PRESET)
            for (i = 0; i < n; i++)
                engine_store_pack((const EngineState *)(const void *)(pk->bank + (first + i) * pk->size),
                                  ps_buf + PS_USED_BYTES + i * pk->stored);
        else
            memcpy(ps_buf + PS_USED_BYTES, pk->bank + first * pk->size, n * pk->size);
    }
    return ps_write(chunk, (uint8_t)k, (uint8_t)c, pk->ver, len, cur, seq);
}

/* the payload in ps_buf (len bytes) to chunk's copy that is not current (cur), read back: 0 ok. The pages after the
 * first, then the first (the header, the commit record, with the payload's head) last */
static int ps_write(uint32_t chunk, uint8_t kind, uint8_t c, uint8_t ver, uint32_t len, int cur, uint32_t seq)
{
    const uint32_t head = PS_PAGE - PS_PAYLOAD_OFF;  /* the payload's bytes in the first page */
    uint32_t base = ps_sector(chunk, cur == 0 ? 1u : 0u), off;
    PsHdr h;
    if (len > PS_PAYLOAD_MAX || st_erase(base))
        return -1;
    for (off = head; off < len; off += PS_PAGE)
        if (st_prog(base + PS_PAYLOAD_OFF + off, ps_buf + off, len - off > PS_PAGE ? PS_PAGE : len - off))
            return -2;
    memset(&h, 0xFF, sizeof h);
    h.magic = PS_MAGIC;
    h.kind = kind;
    h.chunk = c;
    h.ver = ver;
    h.rsv0 = PS_FRAME_TIGHT;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(ps_buf, len);
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(ps_page, &h, sizeof h);
    memcpy(ps_page + sizeof h, ps_buf, len < head ? len : head);
    if (st_prog(base, ps_page, (uint32_t)sizeof h + (len < head ? len : head)))   /* the commit record, last */
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
    if (tuning_unsaved) {                           /* an MTS dump changed a USER tuning */
        tuning_unsaved = 0u;
        ps_dirty |= 1u << PS_NAMES_CHUNK;
    }
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
