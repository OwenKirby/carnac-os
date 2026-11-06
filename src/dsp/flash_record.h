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
#include "attacks.h"                /* ATK_COUNT: the ATTACK sample's width */

/* the current layout of each kind: 1..0xFE */
#define FREC_VER_ENGINE 13u          /* EngineState, one patch (PATCH_PRESET); 2: the noise colour morph, morph / L/R mod bytes; 3: the name; 4: filter flavours reordered (MS-20 HIGH 3, FORMANT 4); 5: MOD 4 removed; 6: STRIKE's attack bytes (oscmix.atk_*); 7: five more (atk_filter .. atk_track), stored packed (engine_store_pack); 8: BEND MOD source / amount, the LFO flags packed; 9: the filters' DRIVE, SH-2 for the SVF, one MS-20 (2026-10-21); 10: VEL > LEVEL, LFO DEPTH MOD, the cutoff MOD AMOUNT 0..127, the envelope flags packed (2026-10-26); 11: the MFX and 4 reserved bytes (2026-11-03, the format bump; nothing older migrates: before V1 patches may go); 12: every byte at its own bit width (ENG_W, 2026-11-03), room after for fields added at the end; 13: MOD4, the sources renumbered (RANDOM, POLY AT), a send's destination 6 bits, VEL > ATTACK (2026-11-03) */
#define FREC_VER_PERF   13u          /* MasterPerformancePreset (WHOLE_PERF); 4: delay_type; 5: reverb types renumbered, PLATE and HALL removed; 6: the whole performance (WholePerformance, whole_perf_pack: + both patches packed, the sequence, the arp). It holds a patch, a sequence and an arp too, so it moves on when FREC_VER_ENGINE / _SEQ / _ARP do; 7: the arp's v4; 8: the patches' v9 (proto/sh2-filter builds); 9: the arp's v5 (2026-10-21); 10: the patches' v10 (2026-10-26); 11: the patches\' v11, REV PREDELAY removed, the MIX page\'s bytes and 16 reserved (2026-11-03; nothing older migrates); 12: the patches' v12; 13: the patches' v13 */
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

/* ---- a patch in flash (v12, 2026-11-03): ENGINE_STORED_SIZE bytes. Every EngineState byte in order at its own width
 * (ENG_W: 7 bits for a 0..127 value, 5 for a mod source, 1 for a switch, 6 for a name character, a list's own width
 * with a spare value or more), least significant bit first, the bytes after 0. A field added at the END of EngineState,
 * its width at the end of ENG_W, reads 0 from a patch saved before it, so it needs no version bump if 0 is its off /
 * default. A field added inside a struct breaks that struct's assert below, so its width gets chosen; a value wider
 * than its field's width would lose its high bits (test_flash_record.c packs every factory patch to catch one). Older
 * patches are not migrated (before V1 they may go).
 *
 * THE ZERO RULE (2026-11-03, binding from V1 on): after the V1 freeze a field is only ever added at the end (here, the
 * performance's reserved[], the settings record), and every patch, performance and settings record saved before it
 * reads it as 0. So 0 must mean "off / as before": a switch off, a source NONE, an amount that does nothing, a list's
 * old behaviour as entry 0. A bipolar amount (64 = none) is stored XOR 64 (BIPOLAR_CENTER) so its stored 0 is its
 * centre; a level whose old behaviour was full is stored inverted (127 - v). A reserved byte taken by a feature obeys
 * the same rule. A field that cannot have a do-nothing 0 needs a version bump and a migration instead. */
#define EW_V 7u                      /* a 0..127 value (VAL_MAX) */
#define EW_SRC 5u                    /* a mod source, MSRC_* (MSRC_COUNT 19 of 32) */
#define EW_SW 1u                     /* a 0 / 1 switch */
#define EW_OSC EW_V, EW_V, EW_V, EW_V, 8u /* WAVE: a wave code, PCM waves from 64 */, EW_SRC, EW_V, EW_SRC, EW_V, EW_SW
#define EW_OSCMIX EW_V, EW_V, EW_V, EW_V, EW_SRC, EW_V, EW_SRC, 3u /* XMOD_* */, EW_V, EW_SRC, EW_V,                   \
                  2u /* SUBOCT_* */, 3u /* SUBWAVE_* */, EW_V, EW_V, EW_SRC, EW_V, EW_V, EW_V, EW_V, EW_SRC, EW_V,          \
                  6u /* ATK_COUNT 19 */, EW_V, EW_V, EW_V, EW_V, EW_V, EW_V, 2u /* ATKTRK_* */, EW_V, EW_SRC, EW_V
#define EW_FILTER EW_V, EW_V, EW_V, EW_V, 3u /* FLT_* */, EW_SRC, EW_SRC, EW_V, EW_V
#define EW_FLTMIX EW_V, EW_V, EW_V, EW_V, EW_SRC, EW_SRC, EW_V, EW_V, EW_V, EW_V, EW_V, EW_V, EW_SRC, EW_SRC, EW_V
#define EW_ENV EW_V, EW_V, EW_V, EW_V, EW_V, EW_V, EW_V, EW_SW, EW_SW, EW_V, EW_SW
#define EW_LFO EW_V, EW_V, EW_V, EW_V, EW_SRC, EW_V, EW_SW, EW_V, EW_SW
#define EW_MOD EW_SRC, 4u /* MFN_* */, EW_SRC, EW_V
#define EW_AMP EW_V, EW_V, EW_V, EW_V, EW_SRC, EW_SRC, EW_V, 3u /* DRIVE_* */
#define EW_SEND 8u, EW_V          /* a MOD SEND: MDST_* | slot << 6 (8 bits), its amount */
#define EW_MFX 5u /* MFX_* */, EW_V, EW_V, EW_V, EW_V, EW_V, EW_V, EW_V
#define EW_X3(a) a, a, a
#define EW_X4(a) a, a, a, a
#define EW_NAME EW_X4(EW_X4(6u))
#define EW_BYTES(...) ((uint32_t)sizeof((const uint8_t[]){__VA_ARGS__}))
PS_STATIC_ASSERT(EW_BYTES(EW_OSC) == sizeof(OscParams) && EW_BYTES(EW_OSCMIX) == sizeof(OscMixerParams) &&
                 EW_BYTES(EW_FILTER) == sizeof(FilterParams) && EW_BYTES(EW_FLTMIX) == sizeof(FilterMixerParams) &&
                 EW_BYTES(EW_ENV) == sizeof(EnvParams) && EW_BYTES(EW_LFO) == sizeof(LfoParams) &&
                 EW_BYTES(EW_MOD) == sizeof(ModSlotParams) && EW_BYTES(EW_AMP) == sizeof(AmpParams) &&
                 EW_BYTES(EW_MFX) == sizeof(MfxParams) && EW_BYTES(EW_SEND) == sizeof(ModSendParams) && EW_BYTES(EW_NAME) == NAME_LEN, "a width per stored byte");
PS_STATIC_ASSERT(MSRC_COUNT <= 32u && XMOD_COUNT <= 8u && SUBOCT_COUNT <= 4u && SUBWAVE_COUNT <= 8u && ATK_COUNT <= 64u &&
                 ATKTRK_COUNT <= 4u && FLT_COUNT <= 8u && MFN_COUNT <= 16u && DRIVE_COUNT <= 8u && MFX_COUNT <= 32u &&
                 VARCH_COUNT <= 16u && CHAR_MAP_LEN <= 64u && NOSC == 2u && NFILTER == 2u && NENV == 3u && NLFO == 3u &&
                 NMODSLOT == 4u && ENGINE_RESERVED == 1u && NMODSEND == 12u && MDST_COUNT <= 64u, "every list fits its width; the repeats below");
static const uint8_t ENG_W[] = {
    EW_OSC, EW_OSC, EW_OSCMIX, EW_FILTER, EW_FILTER, EW_FLTMIX, EW_X3(EW_ENV), EW_X3(EW_LFO), EW_X4(EW_MOD), EW_AMP,
    4u /* arch, VARCH_* */, EW_NAME, EW_V /* ANALOG */, EW_V, EW_V /* KEY TRACK */, EW_X3(EW_V) /* VEL > LEVEL */,
    EW_X3(EW_SRC) /* DEPTH MOD SRC */, EW_X3(EW_V) /* DEPTH MOD AMT */, EW_MFX, EW_X3(8u) /* noise HARMONIC, its mod source and amount (2026-11-06, were reserved: same widths) */,
    8u /* reserved */,
    EW_X4(EW_X3(EW_SEND)) /* the MOD SENDS (2026-11-03, added at the end: no bump) */
};
PS_STATIC_ASSERT(sizeof ENG_W == sizeof(EngineState), "ENG_W: one width per EngineState byte, in order");

static inline void engine_store_pack(const EngineState *e, uint8_t *out)
{
    const uint8_t *d = (const uint8_t *)e;
    uint32_t i, j = 0, acc = 0, bits = 0;
    memset(out, 0, ENGINE_STORED_SIZE);
    for (i = 0; i < sizeof(EngineState) && j < ENGINE_STORED_SIZE; i++) {
        acc |= ((uint32_t)d[i] & ((1u << ENG_W[i]) - 1u)) << bits;
        for (bits += ENG_W[i]; bits >= 8u && j < ENGINE_STORED_SIZE; bits -= 8u, acc >>= 8)
            out[j++] = (uint8_t)acc;
    }
    if (bits && j < ENGINE_STORED_SIZE)
        out[j] = (uint8_t)acc;
}
static inline void engine_store_unpack(const uint8_t *in, EngineState *e)
{
    uint8_t *d = (uint8_t *)e;
    uint32_t i, j = 0, acc = 0, bits = 0;
    for (i = 0; i < sizeof(EngineState); i++) {
        for (; bits < ENG_W[i]; bits += 8u)
            acc |= (uint32_t)(j < ENGINE_STORED_SIZE ? in[j++] : 0u) << bits;
        d[i] = (uint8_t)(acc & ((1u << ENG_W[i]) - 1u));
        acc >>= ENG_W[i];
        bits -= ENG_W[i];
    }
    if (e->analog > VAL_MAX) e->analog = 0u;
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
