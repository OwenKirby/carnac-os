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

/* the current layout of each kind: 1..0xFE */
#define FREC_VER_ENGINE 7u           /* EngineState, one patch (PATCH_PRESET); 2: the noise colour morph, morph / L/R mod bytes; 3: the name; 4: filter flavours reordered (MS-20 HIGH 3, FORMANT 4); 5: MOD 4 removed; 6: STRIKE's attack bytes (oscmix.atk_*); 7: five more (atk_filter .. atk_track), stored packed (engine_store_pack); v5 and v6 migrate (engine_from_v5 / _v6) */
#define FREC_VER_PERF   5u           /* MasterPerformancePreset (WHOLE_PERF); 4: delay_type; 5: reverb types renumbered, PLATE and HALL removed */
#define FREC_VER_SEQ    2u           /* SequencerLane[SEQ_NLANE], one sequencer slot (SEQ_SETTINGS); 2: scale, scale_root */
#define FREC_VER_ARP    3u           /* ArpState, one arp slot (ARP_SEQ); 2: chord_root (a MIDI note) and the 24-way variation; 3: TIE, accents */
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
#define FREC_SIZE_ENGINE (FREC_HEADER + ENGINE_STORED_SIZE)                     /* 174: packed, engine_store_pack */
#define FREC_SIZE_PERF   (FREC_HEADER + sizeof(MasterPerformancePreset))        /* 52 */
#define FREC_SIZE_SEQ    (FREC_HEADER + sizeof(SequencerLane) * SEQ_NLANE)      /* 417 */
#define FREC_SIZE_ARP    (FREC_HEADER + sizeof(ArpState))                       /* 55 */
#define FREC_SIZE_GLOBAL (FREC_HEADER + sizeof(GlobalSystemConfig))             /* 5 */
PS_STATIC_ASSERT(FREC_SIZE_ENGINE == 174u, "EngineState flash record");
PS_STATIC_ASSERT(FREC_SIZE_PERF == 52u, "MasterPerformancePreset flash record");
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

/* ---- a patch in flash (v7): ENGINE_STORED_SIZE bytes. Every EngineState byte in order, but for the name, atk_bend and
 * the two bytes nothing reads (OSC1's SYNC: only OSC2 syncs; ENV1's INVERT: ENV1 is never inverted), then the name at
 * 6 bits a character (UI_CHAR_MAP has 41), then atk_bend minus 64 in the last byte (the spare byte v7 first wrote as 0:
 * no bend). Unpacked, the two unread bytes are 0. */
#define ENG_OFS_NAME ((uint32_t)offsetof(EngineState, name))
#define ENG_OFS_UNREAD1 ((uint32_t)offsetof(EngineState, osc[0].sync))
#define ENG_OFS_UNREAD2 ((uint32_t)offsetof(EngineState, env[0].invert))
#define ENG_OFS_BEND ((uint32_t)offsetof(EngineState, oscmix) + (uint32_t)offsetof(OscMixerParams, atk_bend))
#define ENG_NAME_PACKED ((NAME_LEN * 6u + 7u) / 8u)
PS_STATIC_ASSERT(CHAR_MAP_LEN <= 64u, "a name character fits 6 bits");
PS_STATIC_ASSERT(sizeof(EngineState) - 3u - NAME_LEN + ENG_NAME_PACKED + 1u == ENGINE_STORED_SIZE,
                 "the packed patch fills ENGINE_STORED_SIZE, atk_bend last");
static inline int engine_byte_stored(uint32_t i)
{
    return i != ENG_OFS_UNREAD1 && i != ENG_OFS_UNREAD2 && i != ENG_OFS_BEND &&
           (i < ENG_OFS_NAME || i >= ENG_OFS_NAME + NAME_LEN);
}
static inline void engine_store_pack(const EngineState *e, uint8_t *out)
{
    const uint8_t *d = (const uint8_t *)e;
    uint32_t i, j = 0, acc = 0, bits = 0;
    for (i = 0; i < sizeof(EngineState); i++)
        if (engine_byte_stored(i))
            out[j++] = d[i];
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
    while (j < ENGINE_STORED_SIZE - 1u)
        out[j++] = 0u;
    out[ENGINE_STORED_SIZE - 1u] = (uint8_t)(e->oscmix.atk_bend - 64u);
}
static inline void engine_store_unpack(const uint8_t *in, EngineState *e)
{
    uint8_t *d = (uint8_t *)e;
    uint32_t i, j = 0, acc = 0, bits = 0;
    for (i = 0; i < sizeof(EngineState); i++)
        d[i] = engine_byte_stored(i) ? in[j++] : 0u;
    for (i = 0; i < NAME_LEN; i++) {
        while (bits < 6u) {
            acc |= (uint32_t)in[j++] << bits;
            bits += 8u;
        }
        e->name[i] = (uint8_t)(acc & 63u);
        acc >>= 6;
        bits -= 6u;
    }
    e->oscmix.atk_bend = (uint8_t)(in[ENGINE_STORED_SIZE - 1u] + 64u);
}

/* A v6 patch (ENGINE_V6_SIZE bytes, stored as it sat in RAM, no atk_filter .. atk_bend) -> the current layout: the
 * bytes before go where they were, the six get ATK_V7_DEFAULTS (as v6 played: unfiltered, its whole length, from the
 * start, full velocity, AUTO tracking, no bend), the rest moves up six. */
static inline void engine_from_v6(const uint8_t *v6, EngineState *e)
{
    static const uint8_t def[6] = ATK_V7_DEFAULTS;
    uint8_t *d = (uint8_t *)e;
    memcpy(d, v6, ENGINE_V6_INSERT);
    memcpy(d + ENGINE_V6_INSERT, def, sizeof def);
    memcpy(d + ENGINE_V6_INSERT + sizeof def, v6 + ENGINE_V6_INSERT, ENGINE_V6_SIZE - ENGINE_V6_INSERT);
}
PS_STATIC_ASSERT(sizeof(EngineState) == ENGINE_V6_SIZE + 6u, "engine_from_v6 inserts six bytes");

/* A v5 patch (ENGINE_V5_SIZE bytes, no attack bytes): to v6 (the attack's first three: the first sound, level 100, no
 * transposition), then to the current layout. */
static inline void engine_from_v5(const uint8_t *v5, EngineState *e)
{
    uint8_t v6[ENGINE_V6_SIZE];
    memcpy(v6, v5, ENGINE_V5_INSERT);
    v6[ENGINE_V5_INSERT] = 0u;
    v6[ENGINE_V5_INSERT + 1u] = ATK_LEVEL_DEFAULT;
    v6[ENGINE_V5_INSERT + 2u] = 64u;
    memcpy(v6 + ENGINE_V5_INSERT + 3u, v5 + ENGINE_V5_INSERT, ENGINE_V5_SIZE - ENGINE_V5_INSERT);
    engine_from_v6(v6, e);
}
PS_STATIC_ASSERT(ENGINE_V6_SIZE == ENGINE_V5_SIZE + 3u, "engine_from_v5 inserts three bytes");

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
