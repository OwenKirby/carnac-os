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
#define FREC_VER_ENGINE 8u           /* EngineState, one patch (PATCH_PRESET); 2: the noise colour morph, morph / L/R mod bytes; 3: the name; 4: filter flavours reordered (MS-20 HIGH 3, FORMANT 4); 5: MOD 4 removed; 6: STRIKE's attack bytes (oscmix.atk_*); 7: five more (atk_filter .. atk_track), stored packed (engine_store_pack); 8: BEND MOD source / amount, the LFO flags packed; v5, v6 and v7 migrate (engine_from_v5 / _v6 / _v7) */
#define FREC_VER_PERF   7u           /* MasterPerformancePreset (WHOLE_PERF); 4: delay_type; 5: reverb types renumbered, PLATE and HALL removed; 6: the whole performance (WholePerformance, whole_perf_pack: + both patches packed, the sequence, the arp). It holds a patch, a sequence and an arp too, so it moves on when FREC_VER_ENGINE / _SEQ / _ARP do; 7: the arp's v4 (v6 migrates: whole_perf_from_v6) */
#define FREC_VER_SEQ    3u           /* SequencerLane[SEQ_NLANE], one sequencer slot (SEQ_SETTINGS); 2: scale, scale_root; 3: the 32-slot bank (2026-10-17, the lane unchanged: the 128-slot bank's chunks are left out, so the factory sequences show) */
#define FREC_VER_ARP    4u           /* ArpState, one arp slot (ARP_SEQ); 2: chord_root (a MIDI note) and the 24-way variation; 3: TIE, accents; 4: each step's RATE where its gate was, the pattern's GATE where TIME DIVISION was (v3 migrates: arp_from_v3) */
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

/* ---- a patch in flash (v8): ENGINE_STORED_SIZE bytes. Every EngineState byte in order, but for the name, the two
 * bytes nothing reads (OSC1's SYNC: only OSC2 syncs; ENV1's INVERT: ENV1 is never inverted) and the three LFOs' CLOCK
 * SYNC and KEY RESET (0 / 1 each), then those six as bits of one byte, then the name at 6 bits a character
 * (UI_CHAR_MAP has 41), then 0s. Unpacked, the two unread bytes are 0. */
#define ENG_OFS_NAME ((uint32_t)offsetof(EngineState, name))
#define ENG_OFS_UNREAD1 ((uint32_t)offsetof(EngineState, osc[0].sync))
#define ENG_OFS_UNREAD2 ((uint32_t)offsetof(EngineState, env[0].invert))
#define ENG_OFS_BEND ((uint32_t)offsetof(EngineState, oscmix) + (uint32_t)offsetof(OscMixerParams, atk_bend))
#define ENG_OFS_LFO ((uint32_t)offsetof(EngineState, lfo))
#define ENG_NAME_PACKED ((NAME_LEN * 6u + 7u) / 8u)
#define ENG_NFLAG (2u * NLFO)
PS_STATIC_ASSERT(CHAR_MAP_LEN <= 64u, "a name character fits 6 bits");
PS_STATIC_ASSERT(ENG_NFLAG <= 8u, "the LFO flags fit a byte");
PS_STATIC_ASSERT(sizeof(EngineState) - 2u - ENG_NFLAG - NAME_LEN + 1u + ENG_NAME_PACKED <= ENGINE_STORED_SIZE,
                 "the packed patch fits ENGINE_STORED_SIZE");
/* flag f (0..5): LFO f / 2's CLOCK SYNC (even) or KEY RESET (odd) */
static inline uint32_t engine_flag_ofs(uint32_t f)
{
    return ENG_OFS_LFO + (f >> 1) * (uint32_t)sizeof(LfoParams) +
           ((f & 1u) ? (uint32_t)offsetof(LfoParams, key_reset) : (uint32_t)offsetof(LfoParams, clock_sync));
}
static inline int engine_byte_stored(uint32_t i)
{
    uint32_t f;
    if (i == ENG_OFS_UNREAD1 || i == ENG_OFS_UNREAD2 || (i >= ENG_OFS_NAME && i < ENG_OFS_NAME + NAME_LEN))
        return 0;
    for (f = 0; f < ENG_NFLAG; f++)
        if (i == engine_flag_ofs(f))
            return 0;
    return 1;
}
static inline void engine_store_pack(const EngineState *e, uint8_t *out)
{
    const uint8_t *d = (const uint8_t *)e;
    uint32_t i, j = 0, acc = 0, bits = 0, fl = 0;
    for (i = 0; i < sizeof(EngineState); i++)
        if (engine_byte_stored(i))
            out[j++] = d[i];
    for (i = 0; i < ENG_NFLAG; i++)
        fl |= (d[engine_flag_ofs(i)] ? 1u : 0u) << i;
    out[j++] = (uint8_t)fl;
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
    while (j < ENGINE_STORED_SIZE)
        out[j++] = 0u;
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
    engine_name_unpack(in + j + 1u, e->name);
}

/* A v7 patch (ENGINE_STORED_SIZE bytes): its RAM layout (ENGINE_V7_RAM) was today's without the two BEND MOD bytes
 * after atk_bend; it stored every byte of it in order but the two unread ones, atk_bend and the name, then the name
 * 6 bits a character, then atk_bend minus 64 in the last byte. BEND MOD gets its defaults (VELOCITY, no amount). */
#define ENG_V7_NAME (ENG_OFS_NAME - 2u)
PS_STATIC_ASSERT(sizeof(EngineState) == ENGINE_V7_RAM + 2u && ENG_OFS_BEND + 3u ==
                 (uint32_t)offsetof(EngineState, oscmix) + (uint32_t)sizeof(OscMixerParams) &&
                 ENG_OFS_UNREAD1 < ENG_OFS_BEND && ENG_OFS_UNREAD2 > ENG_OFS_BEND,
                 "engine_from_v7: BEND MOD inserted after atk_bend, the last of OscMixerParams");
static inline void engine_from_v7(const uint8_t *in, EngineState *e)
{
    static const uint8_t def[8] = ATK_V8_DEFAULTS;
    uint8_t v7[ENGINE_V7_RAM];
    uint8_t *d = (uint8_t *)e;
    uint32_t i, j = 0;
    for (i = 0; i < ENGINE_V7_RAM; i++)
        v7[i] = (i == ENG_OFS_UNREAD1 || i == ENG_OFS_UNREAD2 - 2u || i == ENG_OFS_BEND ||
                 (i >= ENG_V7_NAME && i < ENG_V7_NAME + NAME_LEN)) ? 0u : in[j++];
    engine_name_unpack(in + j, v7 + ENG_V7_NAME);
    v7[ENG_OFS_BEND] = (uint8_t)(in[ENGINE_STORED_SIZE - 1u] + 64u);
    memcpy(d, v7, ENG_OFS_BEND + 1u);
    d[ENG_OFS_BEND + 1u] = def[6];
    d[ENG_OFS_BEND + 2u] = def[7];
    memcpy(d + ENG_OFS_BEND + 3u, v7 + ENG_OFS_BEND + 1u, ENGINE_V7_RAM - ENG_OFS_BEND - 1u);
}

/* A v6 patch (ENGINE_V6_SIZE bytes, stored as it sat in RAM, no atk_filter .. atk_bend_mod_amt) -> the current layout:
 * the bytes before go where they were, the eight get ATK_V8_DEFAULTS (as v6 played: unfiltered, its whole length,
 * from the start, full velocity, AUTO tracking, no bend), the rest moves up eight. */
static inline void engine_from_v6(const uint8_t *v6, EngineState *e)
{
    static const uint8_t def[8] = ATK_V8_DEFAULTS;
    uint8_t *d = (uint8_t *)e;
    memcpy(d, v6, ENGINE_V6_INSERT);
    memcpy(d + ENGINE_V6_INSERT, def, sizeof def);
    memcpy(d + ENGINE_V6_INSERT + sizeof def, v6 + ENGINE_V6_INSERT, ENGINE_V6_SIZE - ENGINE_V6_INSERT);
}
PS_STATIC_ASSERT(sizeof(EngineState) == ENGINE_V6_SIZE + 8u, "engine_from_v6 inserts eight bytes");

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

/* ---- a whole performance in flash (FREC_VER_PERF 6, 2026-10-17): WHOLE_PERF_STORED_SIZE bytes, the performance's
 * own settings, layer 1's and layer 2's patches packed (engine_store_pack), the four sequencer lanes, the arp pattern */
#define WHOLE_PERF_STORED_SIZE ((uint32_t)sizeof(MasterPerformancePreset) + NLAYER * ENGINE_STORED_SIZE +                                 (uint32_t)sizeof(SequencerLane) * SEQ_NLANE + (uint32_t)sizeof(ArpState))
PS_STATIC_ASSERT(WHOLE_PERF_STORED_SIZE == 867u, "a stored whole performance");
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

/* ---- the arp's v3 -> v4 (2026-10-17): knob 1 became each step's RATE (the byte that was the step's GATE) and GATE the
 * pattern's (the byte that was TIME DIVISION). In place, the same size: every step takes the old TIME DIVISION, the
 * pattern the gate of its first chord step (the default without one). */
static inline void arp_from_v3(ArpState *a)
{
    uint8_t div = a->gate_length, gate = ARP_GATE_DEFAULT, i, found = 0u;   /* (v3: time_division) */
    for (i = 0u; i < ARP_NSTEP; i++) {
        if (!found && ARP_ROOT_IS_CHORD(a->steps[i].chord_root)) {
            gate = a->steps[i].rate;                /* (v3: gate_length) */
            found = 1u;
        }
        a->steps[i].rate = div < ARPDIV_COUNT ? div : (uint8_t)ARP_RATE_DEFAULT;
    }
    a->gate_length = gate <= VAL_MAX ? gate : (uint8_t)VAL_MAX;
}
/* a stored v6 whole performance (WHOLE_PERF_STORED_SIZE bytes, its arp last) to v7, in place */
static inline void whole_perf_from_v6(uint8_t *stored)
{
    arp_from_v3((ArpState *)(void *)(stored + WHOLE_PERF_STORED_SIZE - sizeof(ArpState)));
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
