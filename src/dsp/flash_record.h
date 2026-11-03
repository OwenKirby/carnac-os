#pragma once
/* Flash record framing: every record written to non-volatile storage is one layout version byte followed by the
 * struct's bytes, unchanged. mvave_fm1_master_specification.txt 6.4.
 *
 *   [ version ][ payload: the struct exactly as PresetStructures.h / global.h lays it out ]
 *
 * The parameter structs stay pure parameter blocks (sizes held by their static asserts); only the stored copy
 * carries the version, so the RAM patch bank and the live patches never hold one.
 *
 * Bump a kind's FREC_VER_* whenever its struct's layout changes (a field added, removed, moved or re-ranged). A
 * reader that meets any other version gets FREC_OLD_LAYOUT and its destination is left untouched, so it can fall
 * back to defaults or, later, run a migration from that version. 0x00 and 0xFF are never versions: they are what
 * zeroed and erased flash read as, so an unwritten slot reads FREC_EMPTY. */
#include <stddef.h>
#include <stdint.h>
#include "hardware/synth_libc.h"
#include "PresetStructures.h"
#include "global.h"
#include "mfx.h"                    /* MFX_COUNT: a patch's MFX type, checked as it loads */

/* the current layout of each kind: 1..0xFE */
#define FREC_VER_ENGINE 11u          /* EngineState, one patch (PATCH_PRESET); 2: the noise colour morph, morph / L/R mod bytes; 3: the name; 4: filter flavours reordered (MS-20 HIGH 3, FORMANT 4); 5: MOD 4 removed; 6: STRIKE's attack bytes (oscmix.atk_*); 7: five more (atk_filter .. atk_track), stored packed (engine_store_pack); 8: BEND MOD source / amount, the LFO flags packed; 9: the filters' DRIVE, SH-2 for the SVF, one MS-20 (2026-10-21); 10: VEL > LEVEL, LFO DEPTH MOD, the cutoff MOD AMOUNT 0..127, the envelope flags packed (2026-10-26); 11: the MFX and 4 reserved bytes (2026-11-03, the format bump; nothing older migrates: before V1 patches may go) */
#define FREC_VER_PERF   11u          /* MasterPerformancePreset (WHOLE_PERF); 4: delay_type; 5: reverb types renumbered, PLATE and HALL removed; 6: the whole performance (WholePerformance, whole_perf_pack: + both patches packed, the sequence, the arp). It holds a patch, a sequence and an arp too, so it moves on when FREC_VER_ENGINE / _SEQ / _ARP do; 7: the arp's v4; 8: the patches' v9 (proto/sh2-filter builds); 9: the arp's v5 (2026-10-21); 10: the patches' v10 (2026-10-26); 11: the patches\' v11, REV PREDELAY removed, the MIX page\'s bytes and 16 reserved (2026-11-03; nothing older migrates) */
#define FREC_VER_SEQ    3u           /* SequencerLane[SEQ_NLANE], one sequencer slot (SEQ_SETTINGS); 2: scale, scale_root; 3: the 32-slot bank (2026-10-17, the lane unchanged: the 128-slot bank's chunks are left out, so the factory sequences show) */
#define FREC_VER_ARP    5u           /* ArpState, one arp slot (ARP_SEQ); 2: chord_root (a MIDI note) and the 24-way variation; 3: TIE, accents; 4: each step's RATE where its gate was, the pattern's GATE where TIME DIVISION was ; 5: RATE the notes' rate, TIMING the steps' length (in LAYER's byte), HOLD the first style, its strum none / up / down (older arps do not migrate) */
#define FREC_VER_GLOBAL 1u           /* GlobalSystemConfig */

#define FREC_HEADER 1u               /* the version byte */
#define FREC_VER_EMPTY_ZERO 0x00u
#define FREC_VER_EMPTY_ERASED 0xFFu

typedef enum { FREC_ENGINE, FREC_PERF, FREC_SEQ, FREC_ARP, FREC_GLOBAL, FREC_KIND_COUNT } FrecKind;
typedef enum {
    FREC_OK,                         /* current layout: the payload was copied out */
    FREC_EMPTY,                      /* never written (version byte 0x00 or 0xFF) */
    FREC_OLD_LAYOUT,                 /* written with another layout version: not copied */
    FREC_BAD_KIND
} FrecStatus;

/* stored size of each kind: the version byte plus the payload */
#define FREC_SIZE_ENGINE (FREC_HEADER + ENGINE_STORED_SIZE)                     /* 185: packed, engine_store_pack */
#define FREC_SIZE_PERF   (FREC_HEADER + sizeof(MasterPerformancePreset))        /* 71 */
#define FREC_SIZE_SEQ    (FREC_HEADER + sizeof(SequencerLane) * SEQ_NLANE)      /* 417 */
#define FREC_SIZE_ARP    (FREC_HEADER + sizeof(ArpState))                       /* 55 */
#define FREC_SIZE_GLOBAL (FREC_HEADER + sizeof(GlobalSystemConfig))             /* 5 */
PS_STATIC_ASSERT(FREC_SIZE_ENGINE == 185u, "EngineState flash record");
PS_STATIC_ASSERT(FREC_SIZE_PERF == 71u, "MasterPerformancePreset flash record");
PS_STATIC_ASSERT(FREC_SIZE_SEQ == 417u, "sequencer slot flash record");
PS_STATIC_ASSERT(FREC_SIZE_ARP == 55u, "ArpState flash record");
PS_STATIC_ASSERT(FREC_SIZE_GLOBAL == 5u, "GlobalSystemConfig flash record");
PS_STATIC_ASSERT(FREC_VER_ENGINE != 0u && FREC_VER_ENGINE != 0xFFu && FREC_VER_PERF != 0u && FREC_VER_PERF != 0xFFu &&
                 FREC_VER_SEQ != 0u && FREC_VER_SEQ != 0xFFu && FREC_VER_ARP != 0u && FREC_VER_ARP != 0xFFu &&
                 FREC_VER_GLOBAL != 0u && FREC_VER_GLOBAL != 0xFFu, "0x00 and 0xFF mark an empty record");

static inline uint8_t frec_version(FrecKind k)
{
    static const uint8_t ver[FREC_KIND_COUNT] = {
        FREC_VER_ENGINE, FREC_VER_PERF, FREC_VER_SEQ, FREC_VER_ARP, FREC_VER_GLOBAL
    };
    return (unsigned)k < FREC_KIND_COUNT ? ver[k] : (uint8_t)0u;
}

/* the whole stored record, version byte included; 0 for an unknown kind */
static inline uint16_t frec_size(FrecKind k)
{
    static const uint16_t size[FREC_KIND_COUNT] = {
        FREC_SIZE_ENGINE, FREC_SIZE_PERF, FREC_SIZE_SEQ, FREC_SIZE_ARP, FREC_SIZE_GLOBAL
    };
    return (unsigned)k < FREC_KIND_COUNT ? size[k] : (uint16_t)0u;
}

static inline void engine_store_pack(const EngineState *e, uint8_t *out);     /* below: a patch's stored form */
static inline void engine_store_unpack(const uint8_t *in, EngineState *e);

/* payload -> record (frec_size(k) bytes): the current version, then the struct's bytes (a patch: packed) */
static inline void frec_pack(FrecKind k, const void *payload, uint8_t *rec)
{
    uint16_t n = frec_size(k);
    if (n == 0u) return;
    rec[0] = frec_version(k);
    if (k == FREC_ENGINE)
        engine_store_pack((const EngineState *)payload, rec + FREC_HEADER);
    else
        memcpy(rec + FREC_HEADER, payload, (size_t)(n - FREC_HEADER));
}

/* the version byte of a stored record, for a migration that needs to know which layout it holds */
static inline uint8_t frec_stored_version(const uint8_t *rec) { return rec[0]; }

/* ---- a patch in flash (v11): ENGINE_STORED_SIZE bytes. Every EngineState byte in order, but for the name, the two
 * bytes nothing reads (OSC1's SYNC: only OSC2 syncs; ENV1's INVERT: ENV1 is never inverted), the three LFOs' CLOCK SYNC
 * and KEY RESET, the envelopes' RETRIG SYNC and LEGATO and ENV2 / ENV3's INVERT (0 / 1 each) and the LFOs' DEPTH MOD
 * SRC; then the LFO flags as bits of one byte, the envelope flags as bits of the next, then the name at 6 bits a
 * character (UI_CHAR_MAP has 41), ANALOG in the last byte. Each filter's KEY TRACK rides in the high nibbles of its MOD
 * SRC (bits 0..3) and MORPH MOD SRC (bits 4..7) bytes, each LFO's DEPTH MOD SRC in the high nibble of its SPD MOD SRC
 * byte (a source fits 4 bits). v11 (2026-11-03) adds the MFX and the reserved bytes after the v10 ones; older patches
 * are not migrated (they read as empty slots). Unpacked, the two unread bytes are 0. */
#define ENG_OFS_NAME ((uint32_t)offsetof(EngineState, name))
#define ENG_OFS_UNREAD1 ((uint32_t)offsetof(EngineState, osc[0].sync))
#define ENG_OFS_UNREAD2 ((uint32_t)offsetof(EngineState, env[0].invert))
#define ENG_OFS_LFO ((uint32_t)offsetof(EngineState, lfo))
#define ENG_OFS_ENV ((uint32_t)offsetof(EngineState, env))
#define ENG_OFS_ANALOG ((uint32_t)offsetof(EngineState, analog))
#define ENG_OFS_FKT ((uint32_t)offsetof(EngineState, filter_key_track))
#define ENG_OFS_V10 ((uint32_t)offsetof(EngineState, env_vel_level))
#define ENG_OFS_LDSRC ((uint32_t)offsetof(EngineState, lfo_depth_src))
#define ENG_OFS_FMSRC(i) ((uint32_t)offsetof(EngineState, filter[i].mod_src))
#define ENG_OFS_FMMSRC(i) ((uint32_t)offsetof(EngineState, filter[i].morph_mod_src))
#define ENG_OFS_LSPD(i) ((uint32_t)offsetof(EngineState, lfo[i].speed_mod_src))
#define ENG_NAME_PACKED ((NAME_LEN * 6u + 7u) / 8u)
#define ENG_NFLAG (2u * NLFO)
#define ENG_NEFLAG (2u * NENV + 2u)
PS_STATIC_ASSERT(CHAR_MAP_LEN <= 64u, "a name character fits 6 bits");
PS_STATIC_ASSERT(ENG_NFLAG <= 8u && ENG_NEFLAG <= 8u, "the LFO flags and the envelope flags fit a byte each");
PS_STATIC_ASSERT(MSRC_COUNT <= 16u, "a mod source fits 4 bits: KEY TRACK and DEPTH MOD SRC have the high nibbles");
PS_STATIC_ASSERT(NFILTER == 2u && NENV == 3u && ENG_OFS_FKT + NFILTER == ENG_OFS_V10, "KEY TRACK, then the v10 bytes");
PS_STATIC_ASSERT(sizeof(EngineState) - 3u - NFILTER - ENG_NFLAG - ENG_NEFLAG - NLFO - NAME_LEN + 2u + ENG_NAME_PACKED + 1u ==
                 ENGINE_STORED_SIZE, "the packed patch fills ENGINE_STORED_SIZE exactly, ANALOG its last byte");
/* flag f (0..5): LFO f / 2's CLOCK SYNC (even) or KEY RESET (odd) */
static inline uint32_t engine_flag_ofs(uint32_t f)
{
    return ENG_OFS_LFO + (f >> 1) * (uint32_t)sizeof(LfoParams) +
           ((f & 1u) ? (uint32_t)offsetof(LfoParams, key_reset) : (uint32_t)offsetof(LfoParams, clock_sync));
}
/* envelope flag f (0..7): ENV f / 2's RETRIG SYNC (even) or LEGATO (odd); 6, 7: ENV2's, ENV3's INVERT */
static inline uint32_t engine_eflag_ofs(uint32_t f)
{
    if (f >= 2u * NENV)
        return ENG_OFS_ENV + (f - 5u) * (uint32_t)sizeof(EnvParams) + (uint32_t)offsetof(EnvParams, invert);
    return ENG_OFS_ENV + (f >> 1) * (uint32_t)sizeof(EnvParams) +
           ((f & 1u) ? (uint32_t)offsetof(EnvParams, legato) : (uint32_t)offsetof(EnvParams, retrig_sync));
}
static inline int engine_byte_stored(uint32_t i)
{
    uint32_t f;
    if (i == ENG_OFS_UNREAD1 || i == ENG_OFS_UNREAD2 || i == ENG_OFS_ANALOG || (i >= ENG_OFS_FKT && i < ENG_OFS_V10) ||
        (i >= ENG_OFS_NAME && i < ENG_OFS_NAME + NAME_LEN) || (i >= ENG_OFS_LDSRC && i < ENG_OFS_LDSRC + NLFO))
        return 0;
    for (f = 0; f < ENG_NFLAG; f++)
        if (i == engine_flag_ofs(f))
            return 0;
    for (f = 0; f < ENG_NEFLAG; f++)
        if (i == engine_eflag_ofs(f))
            return 0;
    return 1;
}
static inline void engine_store_pack(const EngineState *e, uint8_t *out)
{
    const uint8_t *d = (const uint8_t *)e;
    uint32_t i, k, j = 0, acc = 0, bits = 0, fl = 0, efl = 0;
    for (i = 0; i < sizeof(EngineState); i++)
        if (engine_byte_stored(i)) {
            uint8_t b = d[i];
            if (i == ENG_OFS_FMSRC(0) || i == ENG_OFS_FMSRC(1))
                b = (uint8_t)((b & 15u) | (e->filter_key_track[i == ENG_OFS_FMSRC(1)] & 15u) << 4);
            else if (i == ENG_OFS_FMMSRC(0) || i == ENG_OFS_FMMSRC(1))
                b = (uint8_t)((b & 15u) | (e->filter_key_track[i == ENG_OFS_FMMSRC(1)] >> 4) << 4);
            for (k = 0; k < NLFO; k++)
                if (i == ENG_OFS_LSPD(k))
                    b = (uint8_t)((b & 15u) | (e->lfo_depth_src[k] & 15u) << 4);
            out[j++] = b;
        }
    for (i = 0; i < ENG_NFLAG; i++)
        fl |= (d[engine_flag_ofs(i)] ? 1u : 0u) << i;
    out[j++] = (uint8_t)fl;
    for (i = 0; i < ENG_NEFLAG; i++)
        efl |= (d[engine_eflag_ofs(i)] ? 1u : 0u) << i;
    out[j++] = (uint8_t)efl;
    for (i = 0; i < NAME_LEN; i++) {
        acc |= (uint32_t)(e->name[i] & 63u) << bits;
        bits += 6u;
        while (bits >= 8u) {
            out[j++] = (uint8_t)acc;
            acc >>= 8;
            bits -= 8u;
        }
    }
    if (bits)
        out[j++] = (uint8_t)acc;
    out[j] = e->analog;
}
static inline void engine_name_unpack(const uint8_t *in, uint8_t *name)
{
    uint32_t i, j = 0, acc = 0, bits = 0;
    for (i = 0; i < NAME_LEN; i++) {
        while (bits < 6u) {
            acc |= (uint32_t)in[j++] << bits;
            bits += 8u;
        }
        name[i] = (uint8_t)(acc & 63u);
        acc >>= 6;
        bits -= 6u;
    }
}
static inline void engine_store_unpack(const uint8_t *in, EngineState *e)
{
    uint8_t *d = (uint8_t *)e;
    uint32_t i, j = 0;
    for (i = 0; i < sizeof(EngineState); i++)
        d[i] = engine_byte_stored(i) ? in[j++] : 0u;
    for (i = 0; i < ENG_NFLAG; i++)
        d[engine_flag_ofs(i)] = (uint8_t)((in[j] >> i) & 1u);
    j++;
    for (i = 0; i < ENG_NEFLAG; i++)
        d[engine_eflag_ofs(i)] = (uint8_t)((in[j] >> i) & 1u);
    j++;
    for (i = 0; i < NLFO; i++) {
        LfoParams *l = &e->lfo[i];
        e->lfo_depth_src[i] = (uint8_t)(l->speed_mod_src >> 4);
        l->speed_mod_src &= 15u;
    }
    engine_name_unpack(in + j, e->name);
    e->analog = in[ENGINE_STORED_SIZE - 1u] <= VAL_MAX ? in[ENGINE_STORED_SIZE - 1u] : (uint8_t)0u;
    for (i = 0; i < NFILTER; i++) {
        FilterParams *f = &e->filter[i];
        uint8_t kt = (uint8_t)(f->mod_src >> 4 | (f->morph_mod_src >> 4) << 4);
        e->filter_key_track[i] = kt <= VAL_MAX ? kt : (uint8_t)0u;
        f->mod_src &= 15u;
        f->morph_mod_src &= 15u;
    }
    if (e->mfx.type >= MFX_COUNT)                   /* (a type this firmware lacks: OFF) */
        e->mfx.type = MFX_OFF;
    varch_single_to_spread(e);
}

/* ---- a whole performance in flash (FREC_VER_PERF 6, 2026-10-17; v11 2026-11-03): WHOLE_PERF_STORED_SIZE bytes, the performance's
 * own settings, layer 1's and layer 2's patches packed (engine_store_pack), the four sequencer lanes, the arp pattern */
#define WHOLE_PERF_STORED_SIZE ((uint32_t)sizeof(MasterPerformancePreset) + NLAYER * ENGINE_STORED_SIZE +                                 (uint32_t)sizeof(SequencerLane) * SEQ_NLANE + (uint32_t)sizeof(ArpState))
PS_STATIC_ASSERT(WHOLE_PERF_STORED_SIZE == 908u, "a stored whole performance");
static inline void whole_perf_pack(const WholePerformance *w, uint8_t *out)
{
    uint32_t l;
    memcpy(out, &w->perf, sizeof w->perf);
    out += sizeof w->perf;
    for (l = 0; l < NLAYER; l++, out += ENGINE_STORED_SIZE)
        engine_store_pack(&w->engine[l], out);
    memcpy(out, w->seq, sizeof w->seq);
    memcpy(out + sizeof w->seq, &w->arp, sizeof w->arp);
}
static inline void whole_perf_unpack(const uint8_t *in, WholePerformance *w)
{
    uint32_t l;
    memcpy(&w->perf, in, sizeof w->perf);
    in += sizeof w->perf;
    for (l = 0; l < NLAYER; l++, in += ENGINE_STORED_SIZE)
        engine_store_unpack(in, &w->engine[l]);
    memcpy(w->seq, in, sizeof w->seq);
    memcpy(&w->arp, in + sizeof w->seq, sizeof w->arp);
}

/* record -> payload. Copies only on FREC_OK; otherwise the payload is left as it was. */
static inline FrecStatus frec_unpack(FrecKind k, const uint8_t *rec, void *payload)
{
    uint16_t n = frec_size(k);
    if (n == 0u) return FREC_BAD_KIND;
    if (rec[0] == FREC_VER_EMPTY_ZERO || rec[0] == FREC_VER_EMPTY_ERASED) return FREC_EMPTY;
    if (rec[0] != frec_version(k)) return FREC_OLD_LAYOUT;
    if (k == FREC_ENGINE)
        engine_store_unpack(rec + FREC_HEADER, (EngineState *)payload);
    else
        memcpy(payload, rec + FREC_HEADER, (size_t)(n - FREC_HEADER));
    return FREC_OK;
}
