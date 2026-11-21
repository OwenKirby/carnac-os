/* SPDX-License-Identifier: GPL-3.0-only */
/* PERF TEST (perf_test.h). The scenarios follow docs/fm1-measure-checklist.md and src/host/bench_voice_arch.c:
 *   default   both envelopes sustain 100, spread 60, cross-mod 20, LFO1 on FILTER1's cutoff (100), resonance 90 on
 *             both filters of the row's flavour (bench_voice_arch.c's patch)
 *   heavy     the default plus ring mod, noise, three chained mod slots on the cutoff, delay and reverb sends, the
 *             mod wheel at 90, FORMANT filters (test_render_hash.c's patch with noise and slots)
 * Held notes: the patch's voice cap (varch_voice_cap), 48 + 3n. The sequencer scenario: two heavy SINGLE layers
 * (DUAL, 4 + 4 voices), bench_maq.c's worst-case MAQ slot (3 lanes of 4-note chords at 1/32 with a 4x ratchet) on both
 * layers at 167 BPM, and the ARP on both layers at 1/32; then the same on the AMP, SEQ and MOD 1 pages, whose screens
 * redraw the most while it plays. Last, SINGLE LADDER (MULTI since 2026-10-15, at LP24: the same circuit, the row names
 * kept for the history) on the FILTER 1 page: its moving plot divides 64-bit values
 * (fm1_divdi.c). */
#include "perf_test.h"
#include "PresetStructures.h"
#include "dsp/preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/voice_allocator.h"
#include "dsp/pcm_osc.h"
#include "UIStateMachine.h"
#include "dsp/wavetables.h"
#include "dsp/mfx.h"
#include "dsp/fx.h"                  /* the MIX page's live mix (F_MIX), kept and restored */
#include "hardware/mem_section.h"    /* the test's state in .pool (2026-11-04: ~8.6 KB of the 96 KiB RAM) */
#include "hardware/mem_section.h"

/* PT_WT_RAM (2026-10-15): the banks the scenarios play (OSC1: bank 0, the default; OSC2 of the heavy patches: bank 3)
 * copied to RAM for the run and wavetable_frames pointed at the copies, then back. The same firmware, the same patches:
 * the difference against a normal run is what reading the wavetables through XIP flash costs. */
static const uint8_t PT_WT_BANKS[2] = {0u, 3u};
static int8_t pt_wt[2][NUM_MORPH_STEPS * WT_FRAME_SAMPLES] SYNTH_POOL;   /* (8-bit frames since 2026-11-18) */
static const int8_t *pt_wt_saved[2][NUM_MORPH_STEPS];
static uint8_t pt_wt_on, pt_run_mode;      /* the copies are in use; the last run's mode */

static void wt_to_ram(void)
{
    uint32_t b, f, i;
    for (b = 0; b < 2u; b++)
        for (f = 0; f < NUM_MORPH_STEPS; f++) {
            const int8_t **slot = &wavetable_frames[PT_WT_BANKS[b] * NUM_MORPH_STEPS + f];
            int8_t *dst = &pt_wt[b][f * WT_FRAME_SAMPLES];
            pt_wt_saved[b][f] = *slot;
            for (i = 0; i < WT_FRAME_SAMPLES; i++)
                dst[i] = (*slot)[i];
            *slot = dst;                                                       /* one aligned store: never torn */
        }
    pt_wt_on = 1;
}

static void wt_to_flash(void)
{
    uint32_t b, f;
    if (!pt_wt_on)
        return;
    for (b = 0; b < 2u; b++)
        for (f = 0; f < NUM_MORPH_STEPS; f++)
            wavetable_frames[PT_WT_BANKS[b] * NUM_MORPH_STEPS + f] = pt_wt_saved[b][f];
    pt_wt_on = 0;
}

enum { K_IDLE, K_HELD, K_SEQ, K_RS, K_REC, K_ALL, K_LOOP, K_LOOPCH, K_FACT };   /* K_LOOP (2026-11-11): a played loop, not a torture: see loop_slot; K_LOOPCH: K_LOOP with a four-note chord on the keys (both layers) changed every PT_CHORD_MS, the tails piling up to the caps; K_RS: K_HELD's chord struck again every PT_RESTRIKE_MS; K_REC: K_SEQ with
                                                       * the REC page's loop playing too; K_ALL: K_REC with the chord held (every
                                                       * note source at once, 2026-11-06) */
/* K_FACT (2026-11-14): a factory performance as it is played. The live drop-outs came from NEWGRANGE DAWN (perf 0)
 * with its SEQ and ARP running and chords on the keys, a case none of the synthetic rows covers. The row loads factory
 * performance `arch` whole (pt_factory_load, the glue's: the power-on load's path), starts both transports and strikes
 * K_LOOPCH's chords over them, each left to ring out in the patch's own release. */
#define PTF_NO_TRANSPORT 1u          /* K_FACT, in .flavour: SEQ and ARP stopped */
#define PTF_NO_CHORDS 2u             /* K_FACT, in .flavour: no chords on the keys */
enum { P_SETUP, P_SETTLE, P_MEASURE };
#define F_HEAVY 1u
#define F_ONE 2u                     /* one note, not the cap */
#define F_FX_MOD 4u                  /* the MOD reverb (the tank, moving) and the PING PONG delay */
#define F_FX_CEL 8u                  /* the CELESTIAL reverb (the tank + the octave shifter) and PING PONG */
#define F_FX_TUM 16u                 /* the TUMULUS reverb (the still tank, long) and PING PONG */
#define F_FX_SPR 32u                 /* the SPRING reverb (two dispersive springs) and PING PONG */
/* (64u: F_DLY_BBD, the BBD delay, removed 2026-11-14) */
/* (128u: F_ATK_FILT, STRIKE's, retired 2026-11-04) */
#define F_HP 256u                    /* both filters' morph at 110 (SH-2: past BP, all five output taps) */
#define F_PCM 512u                   /* PCM waves (needs the store uploaded): OSC1 CHOIR at SCAN 64 (both whole
                                      * notes: both attacks, then both loops), OSC2 LORE; with K_RS / K_SEQ OSC1 plays
                                      * BELL (both attacks at every note, then the decaying tails). The wave order
                                      * (2026-11-06, the rebuilt bank, bank_fw.py): CHOIR 0, LORE 7, BELL 8 */
#define F_DRIVE 1024u                /* AMP DRIVE 80 through the model in bits 11..13 (F_DRV) */
#define F_DRV(m) (F_DRIVE | (uint16_t)((m) << 11))
#define F_MFX 0x4000u                /* STONE PHASER on layer 1 (RATE 0.5 Hz, FEEDBACK 100, STEREO 64, AUTO) */
#define F_MFX2 0x8000u               /* ... on both layers, DUAL (K_IDLE: nothing playing, the effect alone) */
#define F_MFXT(t) ((uint32_t)(t) << 16)   /* with F_MFX / F_MFX2: the type instead (MFX_*, 2026-11-02), at its
                                      * defaults (UIStateMachine.c mfx_type_set) */
#define F_MIX 0x1000000u             /* the MIX page all engaged (2026-11-02, dsp/fx.h fx_live): BALANCE / DRY-WET off its
                                      * middle (the faders' ramps), LOOP TONE dark (the delay's feedback low-pass), both
                                      * cross-feeds high (the loop) */
#define F_AIR (1u << 26)            /* with F_HEAVY: noise COLOUR at AIR (the whistle, 2026-11-05) instead of 20 */
#define F_MFX2T(t) ((uint32_t)(t) << 28)   /* with F_MFX2: layer 2's own type (MFX_*) instead of a copy of layer 1's */
#define F_LONGREL (1u << 27)        /* both envelopes' RELEASE 110: every note rings on as a tail (a pad's) */
#define F_NV(n) ((uint32_t)(n) << 20)   /* K_HELD (2026-11-18, the price rows): exactly n voices (1..8) whatever the patch costs
                                      * (voice_allocator.h pool_force); bits 20..23, so never with F_MFX (its type reads 16..23) */
#define F_NV_OF(f) (((f) >> 20) & 15u)
/* the cost rows (2026-11-20, -D PT_COST_ONLY: what the voice price did not count): mod routes, noise, the MIX page's
 * cross-feeds and LOOP TONE's wear. F_RT8 / F_RT16: 8 / 16 routes of a pad's kind (LFOs, envelopes, the wheel, keyboard
 * and velocity into SCAN, CUTOFF, MIX, PAN, PITCH) in place of the one LFO1 > CUTOFF. Bits 28..30 only without F_MFX2
 * (its layer 2 type reads them) */
#define F_RT8 64u
#define F_RT16 128u
#define F_XFONLY (1u << 28)          /* the cross-feeds high (110, 110), LOOP TONE at its middle */
#define F_WEARONLY (1u << 29)        /* LOOP TONE dark (24: the wear on), no cross-feed */
#define F_NOISE (1u << 30)           /* NOISE LEVEL 60, white (without F_HEAVY) */
/* (PT_COST_ONLY, the effects' price, 2026-11-20): the reverb type t (REVTYPE_*) in bits 16..19 as t + 1, PING PONG in bit
 * 31; only without F_MFX / F_MFX2 (whose types read those bits) and without F_FX_* */
#define F_RVT(t) ((uint32_t)((t) + 1u) << 16)
#define F_PPON (1u << 31)
/* (PT_COST_ONLY, the transport's price, 2026-11-20): with K_LOOP / K_LOOPCH, the REC page's loop playing too (bit 26: F_AIR,
 * read only with F_HEAVY) */
#define F_RECL (1u << 26)
#define F_MFXEQ (1u << 25)           /* BRONZE AGE with its EQ off noon (DIST 120, the scooped render's: every band run) */
#define PT_RESTRIKE_MS 250u          /* K_RS: shorter than the longer attacks, so voices are nearly always attacking */
#define PT_CHORD_MS 1000u            /* K_LOOPCH: a half bar at 120 BPM; with RELEASE 110 three chords ring at once */
#define F_FX_ANY (F_FX_MOD | F_FX_CEL | F_FX_TUM | F_FX_SPR)

typedef struct {
    const char *name;
    uint8_t kind, arch, flavour;
    uint32_t flags;
    uint8_t page;
} PtScen;

static const PtScen SCEN[] = {
#if defined(PT_PRICE_ONLY)          /* (-D PT_PRICE_ONLY, 2026-11-18: each architecture at 2/4/6/8 voices, the price
                                      * table's measurement: slope = cost a pair of voices, one on each core) */
    {"IDLE", K_IDLE, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_HOME1},
#define PT_PR4(nm, a, flt, fl)     {nm " 2", K_HELD, a, flt, (fl) | F_NV(2), UIPAGE_HOME1}, {nm " 4", K_HELD, a, flt, (fl) | F_NV(4), UIPAGE_HOME1},     {nm " 6", K_HELD, a, flt, (fl) | F_NV(6), UIPAGE_HOME1}, {nm " 8", K_HELD, a, flt, (fl) | F_NV(8), UIPAGE_HOME1}
#define PT_PRICE(nm, a)     PT_PR4("P " nm, a, FLT_MULTI, 0), PT_PR4("P " nm " H", a, FLT_MULTI, F_HEAVY),     PT_PR4("P " nm " SH2", a, FLT_SH2, F_HEAVY), PT_PR4("P " nm " PCM", a, FLT_MULTI, F_PCM)
    PT_PRICE("SPRD0", VARCH_SINGLE),
    PT_PRICE("SPRD", VARCH_SPREAD),
    PT_PRICE("DOSC", VARCH_DUAL_OSC),
    PT_PRICE("DFLT", VARCH_DUAL_FILTER),
    PT_PRICE("STACK", VARCH_STACK),
    PT_PRICE("BIN", VARCH_BINAURAL),
    PT_PRICE("FULL", VARCH_FULL),
#elif defined(PT_COST_ONLY)          /* (-D PT_COST_ONLY, 2026-11-20: the costs the price did not count; slopes from
                                      * 2/4/6/8 voices, the MIX page's against the same row without it) */
    {"IDLE", K_IDLE, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_HOME1},
#define PT_PR4(nm, a, flt, fl)     {nm " 2", K_HELD, a, flt, (fl) | F_NV(2), UIPAGE_HOME1}, {nm " 4", K_HELD, a, flt, (fl) | F_NV(4), UIPAGE_HOME1},     {nm " 6", K_HELD, a, flt, (fl) | F_NV(6), UIPAGE_HOME1}, {nm " 8", K_HELD, a, flt, (fl) | F_NV(8), UIPAGE_HOME1}
    PT_PR4("C DOSC R1", VARCH_DUAL_OSC, FLT_MULTI, 0),
    PT_PR4("C DOSC R8", VARCH_DUAL_OSC, FLT_MULTI, F_RT8),
    PT_PR4("C DOSC R16", VARCH_DUAL_OSC, FLT_MULTI, F_RT16),
    PT_PR4("C SPRD0 R1", VARCH_SINGLE, FLT_MULTI, 0),
    PT_PR4("C SPRD0 R16", VARCH_SINGLE, FLT_MULTI, F_RT16),
    PT_PR4("C SPRD0 NOISE", VARCH_SINGLE, FLT_MULTI, F_NOISE),
#define PT_FXR(nm, t, pp, nv) {"C FX " nm, K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_RVT(t) | (pp) | F_NV(nv), UIPAGE_HOME1}
    PT_FXR("TOMB 4", REVTYPE_TOMB, 0, 4), PT_FXR("TOMB PP 4", REVTYPE_TOMB, F_PPON, 4),
    PT_FXR("SPRING 4", REVTYPE_SPRING, 0, 4), PT_FXR("MOD 4", REVTYPE_MOD, 0, 4), PT_FXR("CEL 4", REVTYPE_CELESTIAL, 0, 4),
    PT_FXR("DOLMEN 4", REVTYPE_DOLMEN, 0, 4), PT_FXR("TUM 4", REVTYPE_TUMULUS, 0, 4),
    PT_FXR("CEL PP 4", REVTYPE_CELESTIAL, F_PPON, 4),
    PT_FXR("TOMB 8", REVTYPE_TOMB, 0, 8), PT_FXR("CEL PP 8", REVTYPE_CELESTIAL, F_PPON, 8),
    /* the transport, as a player leaves it running (loop_slot: a 16th bass line, the ARP's 16ths over pad chords, 120
     * BPM), and the REC page's loop with it; the floor column against IDLE's is its cost (2026-11-20, the user: "don't
     * ever assume that those will be off") */
    {"C LOOP SPRD0", K_LOOP, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_HOME1},
    {"C LOOP SPRD0 REC", K_LOOP, VARCH_SINGLE, FLT_MULTI, F_RECL, UIPAGE_HOME1},
    {"C LOOP DOSC", K_LOOP, VARCH_DUAL_OSC, FLT_MULTI, 0, UIPAGE_HOME1},
    {"C LOOP DOSC REC", K_LOOP, VARCH_DUAL_OSC, FLT_MULTI, F_RECL, UIPAGE_HOME1},
    {"C LOOPCH DOSC", K_LOOPCH, VARCH_DUAL_OSC, FLT_MULTI, 0, UIPAGE_HOME1},
    {"C LOOPCH DOSC REC", K_LOOPCH, VARCH_DUAL_OSC, FLT_MULTI, F_RECL, UIPAGE_HOME1},
    {"C LOOP DOSC STOP", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_NV(4), UIPAGE_HOME1},
    {"C DOSC TUM 4", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_FX_TUM | F_NV(4), UIPAGE_HOME1},
    {"C DOSC TUM 4 XF", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_FX_TUM | F_XFONLY | F_NV(4), UIPAGE_HOME1},
    {"C DOSC TUM 4 WEAR", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_FX_TUM | F_WEARONLY | F_NV(4), UIPAGE_HOME1},
    {"C DOSC TUM 4 MIX", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_FX_TUM | F_MIX | F_NV(4), UIPAGE_HOME1},
    {"C DOSC TUM 8", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_FX_TUM | F_NV(8), UIPAGE_HOME1},
    {"C DOSC TUM 8 XF", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_FX_TUM | F_XFONLY | F_NV(8), UIPAGE_HOME1},
    {"C DOSC TUM 8 WEAR", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_FX_TUM | F_WEARONLY | F_NV(8), UIPAGE_HOME1},
    {"C DOSC TUM 8 MIX", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_FX_TUM | F_MIX | F_NV(8), UIPAGE_HOME1},
#elif !defined(PT_FACT_ONLY)          /* (-D PT_FACT_ONLY: the K_FACT rows alone, a two-minute run) */
    {"IDLE", K_IDLE, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_HOME1},
    {"SINGLE 1V", K_HELD, VARCH_SINGLE, FLT_MULTI, F_ONE, UIPAGE_HOME1},
    {"SINGLE", K_HELD, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_HOME1},
    {"SINGLE HEAVY", K_HELD, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"DUAL OSC", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, 0, UIPAGE_HOME1},
    {"SPREAD", K_HELD, VARCH_SPREAD, FLT_MULTI, 0, UIPAGE_HOME1},
    {"DUAL FILTER", K_HELD, VARCH_DUAL_FILTER, FLT_MULTI, 0, UIPAGE_HOME1},
    {"STACK", K_HELD, VARCH_STACK, FLT_MULTI, 0, UIPAGE_HOME1},
    {"BINAURAL", K_HELD, VARCH_BINAURAL, FLT_MULTI, 0, UIPAGE_HOME1},
    {"DUAL OSC HEAVY", K_HELD, VARCH_DUAL_OSC, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"SPREAD HEAVY", K_HELD, VARCH_SPREAD, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"DUAL FLT HEAVY", K_HELD, VARCH_DUAL_FILTER, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"STACK HEAVY", K_HELD, VARCH_STACK, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"BINAURAL HEAVY", K_HELD, VARCH_BINAURAL, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"SINGLE LADDER", K_HELD, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_HOME1},
    {"SINGLE MS10", K_HELD, VARCH_SINGLE, FLT_MS10, 0, UIPAGE_HOME1},
    {"SINGLE SH-2", K_HELD, VARCH_SINGLE, FLT_SH2, 0, UIPAGE_HOME1},
    {"SINGLE FORMANT", K_HELD, VARCH_SINGLE, FLT_FORMANT, 0, UIPAGE_HOME1},
    {"DUAL FLT LADDER", K_HELD, VARCH_DUAL_FILTER, FLT_MULTI, 0, UIPAGE_HOME1},
    {"SEQ+ARP HOME", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"SEQ+ARP AMP", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_AMP},
    {"SEQ+ARP SEQ", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_SEQ},
    {"SEQ+ARP MOD1", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_MOD},
    {"LADDER FILTER PG", K_HELD, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_FILTER1},   /* the plot's 64-bit divides */
    /* the new effects (2026-10-15), on the heaviest rows: the reverb runs on the second core beside the delay */
    {"HEAVY MOD+PP", K_HELD, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_MOD, UIPAGE_HOME1},
    {"HEAVY CELEST+PP", K_HELD, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"SEQ+ARP CELEST+PP", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    /* MULTI at the same caps as the other filters (2026-10-15): the heavy patch on each architecture */
    {"SINGLE MULTI H", K_HELD, VARCH_SINGLE, FLT_MULTI, F_HEAVY, UIPAGE_HOME1},
    {"DUALOSC MULTI H", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_HEAVY, UIPAGE_HOME1},
    {"SPREAD MULTI H", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY, UIPAGE_HOME1},
    {"DUALFLT MULTI H", K_HELD, VARCH_DUAL_FILTER, FLT_MULTI, F_HEAVY, UIPAGE_HOME1},
    {"STACK MULTI H", K_HELD, VARCH_STACK, FLT_MULTI, F_HEAVY, UIPAGE_HOME1},
    {"BINAUR MULTI H", K_HELD, VARCH_BINAURAL, FLT_MULTI, F_HEAVY, UIPAGE_HOME1},
    /* the dearest architectures at their caps under the tank reverbs (2026-10-15: buzz reported with reverb) */
    {"SPREAD H CEL+PP", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"DUALOSC H CEL+PP", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"SPREAD H TUM+PP", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_TUM, UIPAGE_HOME1},
    {"SEQ+ARP SPRD CEL", K_SEQ, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    /* 2026-10-16: SPRING against HEAVY CELEST+PP (the budget it was designed to), the BBD against the tape echo */
    {"HEAVY SPRING+PP", K_HELD, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_SPR, UIPAGE_HOME1},
    /* 2026-10-21: SH-2 (the dearest filter, ~1.9x MULTI on the host) at every architecture's cap: the caps must hold */
    {"SINGLE SH2 H", K_HELD, VARCH_SINGLE, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"DUALOSC SH2 H", K_HELD, VARCH_DUAL_OSC, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"SPREAD SH2 H", K_HELD, VARCH_SPREAD, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"DUALFLT SH2 H", K_HELD, VARCH_DUAL_FILTER, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"STACK SH2 H", K_HELD, VARCH_STACK, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"BINAUR SH2 H", K_HELD, VARCH_BINAURAL, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"SPREAD SH2 CEL+PP", K_HELD, VARCH_SPREAD, FLT_SH2, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"DUALFLT SH2 CEL", K_HELD, VARCH_DUAL_FILTER, FLT_SH2, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"SEQ+ARP SH2 CEL", K_SEQ, VARCH_SINGLE, FLT_SH2, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"DUALFLT SH2 HP", K_HELD, VARCH_DUAL_FILTER, FLT_SH2, F_HEAVY | F_HP, UIPAGE_HOME1},
    {"BINAUR SH2 HP", K_HELD, VARCH_BINAURAL, FLT_SH2, F_HEAVY | F_HP, UIPAGE_HOME1},
    /* 2026-10-21: PCM waves on the oscillators (pcm_osc.c), each beside the row above it without them */
    {"SINGLE PCM", K_HELD, VARCH_SINGLE, FLT_MULTI, F_PCM, UIPAGE_HOME1},
    {"DUAL OSC PCM", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_PCM, UIPAGE_HOME1},
    {"SPREAD PCM", K_HELD, VARCH_SPREAD, FLT_MULTI, F_PCM, UIPAGE_HOME1},
    {"DUAL FILTER PCM", K_HELD, VARCH_DUAL_FILTER, FLT_MULTI, F_PCM, UIPAGE_HOME1},
    {"STACK PCM", K_HELD, VARCH_STACK, FLT_MULTI, F_PCM, UIPAGE_HOME1},
    {"BINAURAL PCM", K_HELD, VARCH_BINAURAL, FLT_MULTI, F_PCM, UIPAGE_HOME1},
    {"DUALOSC MULTI H PCM", K_HELD, VARCH_DUAL_OSC, FLT_MULTI, F_HEAVY | F_PCM, UIPAGE_HOME1},
    {"DUALFLT MULTI H PCM", K_HELD, VARCH_DUAL_FILTER, FLT_MULTI, F_HEAVY | F_PCM, UIPAGE_HOME1},
    {"BINAUR MULTI H PCM", K_HELD, VARCH_BINAURAL, FLT_MULTI, F_HEAVY | F_PCM, UIPAGE_HOME1},
    /* 2026-11-12: SPREAD's four playheads with SCAN moving every tick (HEAVY's LFO3 -> OSC1 SCAN) */
    {"SPREAD MULTI H PCM", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_PCM, UIPAGE_HOME1},
    {"SINGLE RS", K_RS, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_HOME1},
    {"SINGLE RS PCM", K_RS, VARCH_SINGLE, FLT_MULTI, F_PCM, UIPAGE_HOME1},
    {"DUALOSC RS", K_RS, VARCH_DUAL_OSC, FLT_MULTI, 0, UIPAGE_HOME1},
    {"DUALOSC RS PCM", K_RS, VARCH_DUAL_OSC, FLT_MULTI, F_PCM, UIPAGE_HOME1},
    {"SEQ+ARP HOME PCM", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_PCM, UIPAGE_HOME1},
    /* AMP DRIVE (2026-10-25): every model on SINGLE at its cap, the dearest on the two-path, STRIKE and SEQ rows */
    {"SINGLE DRV SOFT", K_HELD, VARCH_SINGLE, FLT_MULTI, F_DRV(DRIVE_SOFT), UIPAGE_HOME1},
    {"SINGLE DRV HARD", K_HELD, VARCH_SINGLE, FLT_MULTI, F_DRV(DRIVE_HARD), UIPAGE_HOME1},
    {"SINGLE DRV FOLD", K_HELD, VARCH_SINGLE, FLT_MULTI, F_DRV(DRIVE_FOLD), UIPAGE_HOME1},
    {"SINGLE DRV TAPE", K_HELD, VARCH_SINGLE, FLT_MULTI, F_DRV(DRIVE_TAPE), UIPAGE_HOME1},
    {"SINGLE DRV TUBE", K_HELD, VARCH_SINGLE, FLT_MULTI, F_DRV(DRIVE_TUBE), UIPAGE_HOME1},
    {"SINGLE DRV OD", K_HELD, VARCH_SINGLE, FLT_MULTI, F_DRV(DRIVE_OVERDRIVE), UIPAGE_HOME1},
    {"STACK HEAVY TUBE", K_HELD, VARCH_STACK, FLT_FORMANT, F_HEAVY | F_DRV(DRIVE_TUBE), UIPAGE_HOME1},
    {"BINAUR HEAVY OD", K_HELD, VARCH_BINAURAL, FLT_FORMANT, F_HEAVY | F_DRV(DRIVE_OVERDRIVE), UIPAGE_HOME1},
    {"SEQ+ARP HOME TUBE", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_DRV(DRIVE_TUBE), UIPAGE_HOME1},
    /* FULL (2026-10-26): DUAL OSC into DUAL FILTER at its cap (4: 2 voices a core; 6 measured 71..82 %) */
    {"FULL", K_HELD, VARCH_FULL, FLT_MULTI, 0, UIPAGE_HOME1},
    {"FULL HEAVY", K_HELD, VARCH_FULL, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"FULL MULTI H", K_HELD, VARCH_FULL, FLT_MULTI, F_HEAVY, UIPAGE_HOME1},
    {"FULL SH2 H", K_HELD, VARCH_FULL, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"FULL H CEL+PP", K_HELD, VARCH_FULL, FLT_MULTI, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"FULL MULTI H PCM", K_HELD, VARCH_FULL, FLT_MULTI, F_HEAVY | F_PCM, UIPAGE_HOME1},
    {"FULL SH2 H PCM", K_HELD, VARCH_FULL, FLT_SH2, F_HEAVY | F_PCM, UIPAGE_HOME1},
    {"FULL SH2 CEL+PP", K_HELD, VARCH_FULL, FLT_SH2, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    /* DUAL SPREAD (2026-10-26): SPREAD on both oscillators -> FILTER1, at its cap (4: 2 a core; at 6 SEQ+ARP CEL was 95.6 %) */
    /* MFX STONE PHASER (2026-11-01): its cost beside the same row without it, in one build (no layout noise) */
    {"IDLE PHASER", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX, UIPAGE_HOME1},
    {"IDLE PHASER x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2, UIPAGE_HOME1},
    {"SPREAD PHASER", K_HELD, VARCH_SPREAD, FLT_MULTI, F_MFX, UIPAGE_HOME1},
    {"SPREAD H CEL PHSR", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX, UIPAGE_HOME1},
    {"SEQ+ARP HOME PHSR2", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_MFX2, UIPAGE_HOME1},
    /* the brigade types at half rate (2026-11-02): beside IDLE PHASER (x2), SPREAD H CEL PHSR, SEQ+ARP HOME PHSR2 */
    {"IDLE ENSEMBLE", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_ENSEMBLE), UIPAGE_HOME1},
    {"IDLE ENSEMBLE x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2 | F_MFXT(MFX_ENSEMBLE), UIPAGE_HOME1},
    {"IDLE ENS+PHSR", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_ENS_PHASER), UIPAGE_HOME1},
    {"IDLE ENS+PHSR x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2 | F_MFXT(MFX_ENS_PHASER), UIPAGE_HOME1},
    {"IDLE CHORUS", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_CHORUS), UIPAGE_HOME1},
    {"IDLE CHORUS x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2 | F_MFXT(MFX_CHORUS), UIPAGE_HOME1},
    {"SPREAD H CEL ENPH", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_ENS_PHASER), UIPAGE_HOME1},
    {"SEQ+ARP HOME ENPH2", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_MFX2 | F_MFXT(MFX_ENS_PHASER), UIPAGE_HOME1},
    /* the MIX page (2026-11-02), beside the same rows without it: SPREAD H CEL+PP, SEQ+ARP CELEST+PP, HEAVY SPRING+PP */
    {"SPREAD H CEL MIX", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MIX, UIPAGE_HOME1},
    {"SEQ+ARP CEL MIX", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_CEL | F_MIX, UIPAGE_HOME1},
    {"HEAVY SPRING MIX", K_HELD, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_SPR | F_MIX, UIPAGE_HOME1},
    /* the stereo BBD in the loop (2026-11-03: its two lines, REV > DLY stereo, LOOP TONE's wear), beside HEAVY
     * CEL+BBD PP */
    /* TAPE FLANGER (2026-11-02): the full rate's two read heads a side */
    {"IDLE TAPE FLANGER", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_TAPE_FLANGER), UIPAGE_HOME1},
    {"IDLE TAPE FLNG x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2 | F_MFXT(MFX_TAPE_FLANGER), UIPAGE_HOME1},
    {"SPREAD H CEL TFLG", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_TAPE_FLANGER), UIPAGE_HOME1},
    {"SEQ+ARP HOME TFLG2", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_MFX2 | F_MFXT(MFX_TAPE_FLANGER), UIPAGE_HOME1},
    /* BBD FLANGER, PITCH SHIFT (2026-11-02; PITCH SHIFT +7 with FEEDBACK: its splices' searches) */
    {"IDLE BBD FLANGER", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_BBD_FLANGER), UIPAGE_HOME1},
    {"IDLE BBD FLNG x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2 | F_MFXT(MFX_BBD_FLANGER), UIPAGE_HOME1},
    {"SPREAD H CEL BBDF", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_BBD_FLANGER), UIPAGE_HOME1},
    {"IDLE PITCH SHIFT", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_PITCH_SHIFT), UIPAGE_HOME1},
    {"PITCH SHIFT HELD", K_HELD, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_PITCH_SHIFT), UIPAGE_HOME1},
    {"IDLE PITCH SHFT x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2 | F_MFXT(MFX_PITCH_SHIFT), UIPAGE_HOME1},
    {"SPREAD H CEL PSHF", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_PITCH_SHIFT), UIPAGE_HOME1},
    /* BBD ECHO (2026-11-02): run whole in the plan (one brigade a layer) */
    {"IDLE BBD ECHO", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_BBD_ECHO), UIPAGE_HOME1},
    {"IDLE BBD ECHO x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2 | F_MFXT(MFX_BBD_ECHO), UIPAGE_HOME1},
    {"SPREAD H CEL ECHO", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_BBD_ECHO), UIPAGE_HOME1},
    /* BRONZE AGE (2026-11-03): the clipping at twice the rate, a side a core */
    {"IDLE BRONZE AGE", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_METAL), UIPAGE_HOME1},
    {"IDLE BRONZE AGE x2", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX2 | F_MFXT(MFX_METAL), UIPAGE_HOME1},
    {"SPREAD H CEL BRNZ", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_METAL), UIPAGE_HOME1},
    {"SPREAD H CEL BREQ", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_METAL) | F_MFXEQ,
     UIPAGE_HOME1},
    /* the resonators (2026-11-04) */
    {"IDLE WIND HARP", K_IDLE, VARCH_SINGLE, FLT_MULTI, F_MFX | F_MFXT(MFX_WIND_HARP), UIPAGE_HOME1},
    {"SPREAD H CEL HARP", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_WIND_HARP),
     UIPAGE_HOME1},
    {"SPREAD H CEL STON", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_MFX | F_MFXT(MFX_RINGING_STONES),
     UIPAGE_HOME1},
    /* the noise whistle (2026-11-05): COLOUR at AIR beside SINGLE HEAVY, BINAURAL HEAVY, SPREAD H CEL+PP */
    {"SINGLE HEAVY AIR", K_HELD, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_AIR, UIPAGE_HOME1},
    {"BINAURAL HEAVY AIR", K_HELD, VARCH_BINAURAL, FLT_FORMANT, F_HEAVY | F_AIR, UIPAGE_HOME1},
    {"SPREAD H CEL+PP AIR", K_HELD, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL | F_AIR, UIPAGE_HOME1},
    /* every note source at once (2026-11-06): the REC page's loop beside the sequencer and the arp, then with the chord held */
    {"SEQ+ARP+REC HOME", K_REC, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"SEQ+ARP+REC CELEST+PP", K_REC, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"ALL HOME", K_ALL, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"ALL SPRD CEL", K_ALL, VARCH_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"ALL DUALOSC CEL", K_ALL, VARCH_DUAL_OSC, FLT_MULTI, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    /* LOOP (2026-11-11): DUAL, layer 1 a one-note-a-step sequence, layer 2 a pad ARP, 120 BPM, long releases, TUMULUS +
     * PING PONG; the same patch on both layers (what the user's STACK + DUAL complaint plays) */
    {"LOOP SPREAD", K_LOOP, VARCH_SPREAD, FLT_MULTI, F_LONGREL | F_FX_TUM, UIPAGE_HOME1},
    {"LOOP DUALOSC", K_LOOP, VARCH_DUAL_OSC, FLT_MULTI, F_LONGREL | F_FX_TUM, UIPAGE_HOME1},
    {"LOOP STACK", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM, UIPAGE_HOME1},
    {"LOOP STACK H", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_HEAVY, UIPAGE_HOME1},
    {"LOOP STACK MFX1", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX, UIPAGE_HOME1},
    {"LOOP STACK MFX2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2, UIPAGE_HOME1},
    {"LOOP STACK SH2", K_LOOP, VARCH_STACK, FLT_SH2, F_LONGREL | F_FX_TUM, UIPAGE_HOME1},
    /* the loop with chords played over it (2026-11-11): the caps reached, every new chord stealing tails */
    {"LOOPCH DUALOSC", K_LOOPCH, VARCH_DUAL_OSC, FLT_MULTI, F_LONGREL | F_FX_TUM, UIPAGE_HOME1},
    {"LOOPCH STACK", K_LOOPCH, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM, UIPAGE_HOME1},
    {"LOOPCH STACK H", K_LOOPCH, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_HEAVY, UIPAGE_HOME1},
    {"LOOPCH STACK MFX2", K_LOOPCH, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2, UIPAGE_HOME1},
    {"LOOPCH STACK SH2", K_LOOPCH, VARCH_STACK, FLT_SH2, F_LONGREL | F_FX_TUM, UIPAGE_HOME1},
    /* the MFX in the loop (2026-11-11): one layer or both, the same type or two (the second type is layer 2's) */
    {"LOOP BRNZ1", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX | F_MFXT(MFX_METAL), UIPAGE_HOME1},
    {"LOOP BRNZ2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_METAL), UIPAGE_HOME1},
    {"LOOP BRNZ EQ2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_METAL) | F_MFXEQ, UIPAGE_HOME1},
    {"LOOP PHSR+BRNZ", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_STONE_PHASER) | F_MFX2T(MFX_METAL), UIPAGE_HOME1},
    {"LOOP BRNZ+PHSR", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_METAL) | F_MFX2T(MFX_STONE_PHASER), UIPAGE_HOME1},
    {"LOOP ENPH1", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX | F_MFXT(MFX_ENS_PHASER), UIPAGE_HOME1},
    {"LOOP ENPH2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_ENS_PHASER), UIPAGE_HOME1},
    {"LOOP ENS+PHSR", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_ENSEMBLE) | F_MFX2T(MFX_STONE_PHASER), UIPAGE_HOME1},
    {"LOOP ENPH+BRNZ", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_ENS_PHASER) | F_MFX2T(MFX_METAL), UIPAGE_HOME1},
    /* every MFX type's full cost (both layers: each whole on its own core, so the wall cost is one layer's: the voice pool's
     * mfx price, PresetStructures.h) and, for three, the one-layer cost that checks the model (a fixed part + half) */
    {"LOOP ENSM1", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX | F_MFXT(MFX_ENSEMBLE), UIPAGE_HOME1},
    {"LOOP ENSM2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_ENSEMBLE), UIPAGE_HOME1},
    {"LOOP CHOR2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_CHORUS), UIPAGE_HOME1},
    {"LOOP TFLG2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_TAPE_FLANGER), UIPAGE_HOME1},
    {"LOOP BBDF2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_BBD_FLANGER), UIPAGE_HOME1},
    {"LOOP PSHF2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_PITCH_SHIFT), UIPAGE_HOME1},
    {"LOOP ECHO1", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX | F_MFXT(MFX_BBD_ECHO), UIPAGE_HOME1},
    {"LOOP ECHO2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_BBD_ECHO), UIPAGE_HOME1},
    {"LOOP HARP1", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX | F_MFXT(MFX_WIND_HARP), UIPAGE_HOME1},
    {"LOOP HARP2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_WIND_HARP), UIPAGE_HOME1},
    {"LOOP STON2", K_LOOP, VARCH_STACK, FLT_MULTI, F_LONGREL | F_FX_TUM | F_MFX2 | F_MFXT(MFX_RINGING_STONES), UIPAGE_HOME1},
#else
    {"IDLE", K_IDLE, VARCH_SINGLE, FLT_MULTI, 0, UIPAGE_HOME1},   /* (the reference) */
#endif
    /* the factory performances, played (K_FACT, 2026-11-14); KEYS / SEQ+ARP / IDLE take one part away */
    {"FACT 0 PLAY", K_FACT, 0, 0, 0, UIPAGE_HOME1},
    {"FACT 1 PLAY", K_FACT, 1, 0, 0, UIPAGE_HOME1},
    {"FACT 2 PLAY", K_FACT, 2, 0, 0, UIPAGE_HOME1},
    {"FACT 3 PLAY", K_FACT, 3, 0, 0, UIPAGE_HOME1},
    {"FACT 4 PLAY", K_FACT, 4, 0, 0, UIPAGE_HOME1},
    {"FACT 5 PLAY", K_FACT, 5, 0, 0, UIPAGE_HOME1},
    {"FACT 6 PLAY", K_FACT, 6, 0, 0, UIPAGE_HOME1},
    {"FACT 7 PLAY", K_FACT, 7, 0, 0, UIPAGE_HOME1},
    /* (2026-11-19: every performance of THE OLD KNOWLEDGE, 20) */
    {"FACT 8 PLAY", K_FACT, 8, 0, 0, UIPAGE_HOME1},
    {"FACT 9 PLAY", K_FACT, 9, 0, 0, UIPAGE_HOME1},
    {"FACT 10 PLAY", K_FACT, 10, 0, 0, UIPAGE_HOME1},
    {"FACT 11 PLAY", K_FACT, 11, 0, 0, UIPAGE_HOME1},
    {"FACT 12 PLAY", K_FACT, 12, 0, 0, UIPAGE_HOME1},
    {"FACT 13 PLAY", K_FACT, 13, 0, 0, UIPAGE_HOME1},
    {"FACT 14 PLAY", K_FACT, 14, 0, 0, UIPAGE_HOME1},
    {"FACT 15 PLAY", K_FACT, 15, 0, 0, UIPAGE_HOME1},
    {"FACT 16 PLAY", K_FACT, 16, 0, 0, UIPAGE_HOME1},
    {"FACT 17 PLAY", K_FACT, 17, 0, 0, UIPAGE_HOME1},
    {"FACT 18 PLAY", K_FACT, 18, 0, 0, UIPAGE_HOME1},
    {"FACT 19 PLAY", K_FACT, 19, 0, 0, UIPAGE_HOME1},
    {"FACT 0 KEYS", K_FACT, 0, PTF_NO_TRANSPORT, 0, UIPAGE_HOME1},
    {"FACT 0 SEQ+ARP", K_FACT, 0, PTF_NO_CHORDS, 0, UIPAGE_HOME1},
    {"FACT 0 IDLE", K_FACT, 0, PTF_NO_TRANSPORT | PTF_NO_CHORDS, 0, UIPAGE_HOME1},
    {"FACT 1 KEYS", K_FACT, 1, PTF_NO_TRANSPORT, 0, UIPAGE_HOME1},
    {"FACT 1 SEQ+ARP", K_FACT, 1, PTF_NO_CHORDS, 0, UIPAGE_HOME1},
    {"FACT 1 IDLE", K_FACT, 1, PTF_NO_TRANSPORT | PTF_NO_CHORDS, 0, UIPAGE_HOME1},
    {"FACT 6 KEYS", K_FACT, 6, PTF_NO_TRANSPORT, 0, UIPAGE_HOME1},
    {"FACT 6 SEQ+ARP", K_FACT, 6, PTF_NO_CHORDS, 0, UIPAGE_HOME1},
    {"FACT 6 IDLE", K_FACT, 6, PTF_NO_TRANSPORT | PTF_NO_CHORDS, 0, UIPAGE_HOME1},
};
#define NSCEN ((uint8_t)(sizeof SCEN / sizeof SCEN[0]))

static struct {
    uint8_t state, scen, phase;
    uint32_t t0, t_rs;                  /* t_rs: K_RS's last strike (K_LOOPCH: its last chord change) */
    uint8_t chord;                      /* K_LOOPCH: the chord sounding (0..3) */
    PtResult res[NSCEN];
    /* the live state the test overwrites, put back after */
    EngineState eng[NLAYER];
    MasterPerformancePreset perf;
    SequencerLane lanes[SEQ_NLANE];
    ArpState arp;
    MfxParams mfx[2];
    uint8_t live[FX_L_COUNT];        /* the MIX page's (dsp/fx.h fx_live) */
} pt SYNTH_POOL;                     /* (zeroed at start-up, as .bss) */

/* the audio side's tally (the ISR adds, pt_tick reads and clears under the lock) */
static volatile struct {
    uint32_t load_sum, halves, peak, late, ui_frames, ui_max_us;
    uint8_t over_run, over;
    uint32_t pk[4];                     /* the slowest half's synth_half_last */
    uint32_t guarded;                   /* halves the deadline guard acted in */
} ld;

uint8_t pt_count(void) { return NSCEN; }
const char *pt_name(uint8_t i) { return i < NSCEN ? SCEN[i].name : ""; }
uint8_t pt_state(void) { return pt.state; }
uint8_t pt_scenario(void) { return pt.scen; }
uint8_t pt_mode(void) { return pt_run_mode; }

/* the profile's own cost: ticks one counter read takes (each stage pays one per lap), measured at the start */
static uint32_t pt_lap;
uint32_t pt_lap_cost(void) { return pt_lap; }
static void lap_calibrate(void)
{
#if defined(SYNTH_PROF)
    uint32_t t0 = synth_prof_now(), i;
    for (i = 0; i < 64u; i++)
        (void)synth_prof_now();
    pt_lap = (synth_prof_now() - t0) / 65u;
#endif
}
const PtResult *pt_result(uint8_t i) { return i < NSCEN ? &pt.res[i] : 0; }

static void tally_clear(void)
{
    ld.load_sum = ld.halves = ld.peak = ld.late = ld.ui_frames = ld.ui_max_us = 0;
    ld.pk[0] = ld.pk[1] = ld.pk[2] = ld.pk[3] = 0;
    ld.guarded = 0;
#if defined(SYNTH_PROF)
    {
        uint32_t i;
        for (i = 0; i < SPROF_N; i++)
            synth_prof_acc[i] = 0;
    }
#endif
    ld.over_run = 0;
}

uint8_t pt_audio_ok(void) { return !(pt.state == PT_RUN && ld.over); }

uint8_t pt_audio_half(uint32_t load_pm, uint8_t late)
{
    if (pt.state != PT_RUN)
        return 1;
    if (ld.over)                                    /* cut: the silent halves after it are not the scenario's */
        return 0;
    ld.load_sum += load_pm;
    ld.halves++;
    ld.guarded += (synth_half_last()[2] >> 24) != 0u;
    if (load_pm > ld.peak) {
        const uint32_t *h = synth_half_last();     /* (the glue takes the half's spike watch before this call) */
        ld.peak = load_pm;
        for (unsigned i = 0; i < 4u; i++) ld.pk[i] = h[i];
    }
    if (late)
        ld.late++;
    if (load_pm > 1000u) {
        if (++ld.over_run >= PT_OVER_HALVES)
            ld.over = 1;
    } else {
        ld.over_run = 0;
    }
    return pt_audio_ok();
}

void pt_ui_frame(uint32_t draw_us)
{
    if (pt.state != PT_RUN)
        return;
    ld.ui_frames++;
    if (draw_us > ld.ui_max_us)
        ld.ui_max_us = draw_us;
}

static void patch(EngineState *e, const PtScen *s)
{
    e->arch = s->arch;
    e->env[0].sustain = 100;
    e->env[1].sustain = 100;
    e->oscmix.spread = 60;
    e->oscmix.xmod_amt = 20;
    e->route[3] = (ModRoute){MSRC_LFO1, MSRC_NONE, MDST_F1_CUTOFF, BIPOLAR_CENTER + 36, VAL_MAX};   /* (v13: its own
                                                                     * source LFO1 at 72) */
    e->filter[0].flavour = e->filter[1].flavour = s->flavour;
    e->filter[0].resonance = e->filter[1].resonance = 90;
    if (s->flags & F_LONGREL)
        e->env[0].release = e->env[1].release = 110;
    if (s->flags & F_HP)
        e->filter[0].morph = e->filter[1].morph = 110;
    if (s->flags & (F_RT8 | F_RT16)) {               /* (PT_COST_ONLY: a pad's routes) */
        static const ModRoute RT[16] = {
            {MSRC_LFO1, MSRC_NONE, MDST_OSC1_SCAN, 64 + 24, 127}, {MSRC_MODWHEEL, MSRC_NONE, MDST_OSC1_SCAN, 64 + 30, 127},
            {MSRC_MODWHEEL, MSRC_NONE, MDST_F1_CUTOFF, 64 + 30, 127}, {MSRC_ENV3, MSRC_NONE, MDST_F1_CUTOFF, 64 + 30, 127},
            {MSRC_ENV2, MSRC_NONE, MDST_OSC_MIX, 64 + 22, 127}, {MSRC_LFO3, MSRC_NONE, MDST_PAN, 64 + 24, 127},
            {MSRC_LFO3, MSRC_NONE, MDST_OSC2_SCAN, 64 + 14, 127}, {MSRC_LFO2, MSRC_MODWHEEL, MDST_PITCH, 64 + 3, 127},
            {MSRC_VELOCITY, MSRC_NONE, MDST_F1_CUTOFF, 64 + 16, 127}, {MSRC_RANDOM, MSRC_NONE, MDST_OSC2_PITCH, 64 + 2, 127},
            {MSRC_KEYBOARD, MSRC_NONE, MDST_PAN, 64 + 10, 127}, {MSRC_ENV3, MSRC_NONE, MDST_LFO1_DEPTH, 64 + 40, 127},
            {MSRC_LFO2, MSRC_NONE, MDST_F1_RES, 64 + 10, 127}, {MSRC_ENV2, MSRC_NONE, MDST_XMOD, 64 + 10, 127},
            {MSRC_LFO1, MSRC_NONE, MDST_OSC2_SCAN, 64 + 12, 127}, {MSRC_KEYBOARD, MSRC_NONE, MDST_OSC_MIX, 64 + 8, 127}};
        int nr = (s->flags & F_RT16) ? 16 : 8;
        for (int i = 0; i < NROUTE; i++)
            e->route[i] = i < nr ? RT[i] : (ModRoute)ROUTE_FREE;
        e->lfo[1].rate = 82; e->lfo[2].shape = 106; e->lfo[2].rate = 40;
        e->env[2].attack = 90; e->env[2].sustain = 80;
    }
    if ((s->flags & F_NOISE) && !(s->flags & F_MFX2)) {
        e->oscmix.noise_level = 60;
        e->oscmix.noise_colour = NOISE_WHITE;
    }
    if (s->flags & F_HEAVY) {
        e->oscmix.xmod_type = XMOD_RING;
        e->oscmix.noise_level = 100;
        e->oscmix.noise_colour = s->flags & F_AIR ? NOISE_AIR : 10;   /* 10: white -> pink, as 40 was before the
                                                                      * resonance (2026-11-06), bit for bit */
        e->osc[1].wavebank = 3;
        e->filter[0].cutoff = 70;
        e->filter[1].cutoff = 90;
        e->lfo[0].rate = 70;
        e->lfo[1].rate = 90;
        e->lfo[2].rate = 40;
        e->amp.delay_send = 50;
        e->amp.reverb_send = 60;
        /* (2026-11-11, the mod matrix: the slots' chain became three routes, two of them with a VIA) */
        e->route[0] = (ModRoute){MSRC_LFO2, MSRC_LFO3, MDST_F1_CUTOFF, BIPOLAR_CENTER + 40};
        e->route[1] = (ModRoute){MSRC_ENV2, MSRC_LFO1, MDST_F1_CUTOFF, BIPOLAR_CENTER + 30};
        e->route[2] = (ModRoute){MSRC_LFO3, MSRC_NONE, MDST_OSC1_SCAN, BIPOLAR_CENTER + 30};
        e->filter[0].mod_amt = 112;
    }
    if (s->flags & F_DRIVE) {
        e->amp.drive = 80;
        e->amp.drive_model = (uint8_t)((s->flags >> 11) & 7u);
    }
    if (s->flags & F_PCM) {
        e->osc[0].wavebank = (uint8_t)(WAVE_PCM_FIRST + (s->kind == K_HELD ? 0u : 8u));
        e->osc[1].wavebank = WAVE_PCM_FIRST + 7u;
        e->osc[0].scan = e->osc[1].scan = 64;
    }
}

static void seq_slot(void)
{
    uint8_t l, i;
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.int_tempo = 215;                     /* 255 BPM: the most steps a second */
    for (l = 0; l < 3u; l++) {                      /* bench_maq.c's worst case, on both layers */
        seq_lanes[l].event_type = SEQEV_NOTE;
        seq_lanes[l].target_mask = SEQ_TGT_BOTH;
        seq_lanes[l].prescale = 3;
        for (i = 0; i < SEQ_NSTEP; i++) {
            seq_lanes[l].steps[i].note_buffer[0] = (uint8_t)(48 + l * 7);
            seq_lanes[l].steps[i].note_buffer[1] = (uint8_t)(52 + l * 7);
            seq_lanes[l].steps[i].note_buffer[2] = (uint8_t)(55 + l * 7);
            seq_lanes[l].steps[i].note_buffer[3] = (uint8_t)(59 + l * 7);
        }
    }
    seq_lanes[3].event_type = SEQEV_RATCHET;
    seq_lanes[3].target_mask = SEQ_TGT_LANE1 | SEQ_TGT_LANE2 | SEQ_TGT_LANE3;
    seq_lanes[3].prescale = 3;
    for (i = 0; i < SEQ_NSTEP; i++)
        seq_lanes[3].steps[i].value = 127;
    for (i = 0; i < 8u; i++) {                       /* (8 steps, as before ARP_NSTEP 16) every step a chord, strummed across 4 octaves */
        ArpChordStep *st = &arp_state.steps[i];
        st->chord_root = (uint8_t)(55 + (i * 5) % 12);
        st->rate = ARPRATE_1_128;                    /* (TIMING 1/32: below) */
        st->octave_range = ARP_NOCT - 1u;
        st->style = (uint8_t)(ARPSTYLE_UP + i % 7u);
        st->variation = (uint8_t)(i * 3u);
    }
    arp_state.sequence_length = 8u;
    arp_state.gate_length = 90;
    arp_set_layer(&arp_state, ARPLAYER_BOTH);
    arp_set_timing(&arp_state, ARPDIV_1_32);
}

/* K_LOOP: what a player leaves running. Layer 1: one NOTE lane, one note a 1/16 step (a bass line, 16 steps, a rest in
 * every fourth). Layer 2: the ARP over four pad chords, each step a bar's beat (TIMING 1/4, 16th notes, up two octaves).
 * 120 BPM (int_tempo 80: 40 + the value). No ratchet, no second lane. */
static void loop_slot(void)
{
    static const uint8_t LINE[SEQ_NSTEP] = {36, 36, 43, 0, 41, 41, 48, 0, 39, 39, 46, 0, 38, 43, 41, 0};
    static const uint8_t ROOT[4] = {48, 53, 55, 50};
    uint8_t i;
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.int_tempo = 80;
    seq_lanes[0].event_type = SEQEV_NOTE;
    seq_lanes[0].target_mask = SEQ_TGT_L1;
    seq_lanes[0].prescale = SEQ_PRESCALE_1_16;
    for (i = 0; i < SEQ_NSTEP; i++) {
        seq_lanes[0].steps[i].note_buffer[0] = LINE[i] ? LINE[i] : 36u;
        seq_lanes[0].steps[i].note_buffer[1] = seq_lanes[0].steps[i].note_buffer[2] = seq_lanes[0].steps[i].note_buffer[3] = SEQ_NOTE_NULL;
        seq_lanes[0].steps[i].flags = LINE[i] ? 0u : SEQ_FLAG_MUTE;               /* (a muted step: the rest) */
    }
    for (i = 0; i < ARP_NSTEP; i++) {
        ArpChordStep *st = &arp_state.steps[i];
        st->chord_root = (uint8_t)ROOT[i & 3u];
        st->rate = ARPRATE_1_16;
        st->octave_range = 1;
        st->style = ARPSTYLE_UP;
        st->variation = 0;
    }
    arp_state.sequence_length = 4;
    arp_state.gate_length = 96;
    arp_set_layer(&arp_state, ARPLAYER_L2);
    arp_set_timing(&arp_state, ARPDIV_1_4);
}

/* K_LOOPCH's chords: a minor seventh an octave over each of loop_slot's pad roots, pt.chord's struck (0x90) or let go
 * (0x80) */
static void loop_chord(uint8_t status)
{
    static const uint8_t ROOT[4] = {60, 65, 67, 62}, IV[4] = {0, 3, 7, 10};
    uint8_t i;
    for (i = 0; i < 4u; i++)
        synth_midi_3(status, (uint8_t)(ROOT[pt.chord & 3u] + IV[i]), status == 0x90 ? (uint8_t)(84 + i * 6) : 0u);
}

/* a two-bar loop in the REC page's recorder, playing: eighth notes up a scale and a held triad on each bar's top (22
 * notes), as a played-in loop would be */
static void rec_loop(void)
{
    Krec *k = synth_rec();
    uint16_t nn = 0;
    krec_clear(k);
    for (uint16_t b = 0; b < 2u; b++) {
        for (uint16_t i = 0; i < 3u; i++) {                     /* the triad */
            k->n[nn].t = (uint16_t)(b * KREC_BAR);
            k->n[nn].len = (uint16_t)(KREC_TPB * 2u);
            k->n[nn].note = (uint8_t)(48 + 4 * i);
            k->n[nn].vel = 90;
            k->n[nn].chp = 0;
            k->n[nn++].fresh = 0;
        }
        for (uint16_t i = 0; i < 8u; i++) {                     /* the eighths */
            k->n[nn].t = (uint16_t)(b * KREC_BAR + i * (KREC_TPB / 2u));
            k->n[nn].len = (uint16_t)(KREC_TPB / 2u - 8u);
            k->n[nn].note = (uint8_t)(60 + (i * 2u) % 12u);
            k->n[nn].vel = (uint8_t)(70 + i * 5u);
            k->n[nn].chp = 0;
            k->n[nn++].fresh = 0;
        }
    }
    k->nn = nn;
    k->len = (uint16_t)(2u * KREC_BAR);
    k->state = KREC_PLAY;
    k->pass_mask = 1u;
    krec_set_playing(k, 1, synth_frame());
}

static void setup(const PtScen *s)
{
    uint8_t n, i;
    live_defaults();
    if (s->kind != K_IDLE && s->kind != K_FACT)
        patch(&engine_state[0], s);
    if (s->kind == K_SEQ || s->kind == K_REC || s->kind == K_ALL) {
        patch(&engine_state[1], s);
        seq_slot();
    }
    if (s->kind == K_LOOP || s->kind == K_LOOPCH) {
        patch(&engine_state[1], s);
        loop_slot();
    }
    if (s->kind != K_FACT)
        MFX_PARAMS(0).type = MFX_PARAMS(1).type = MFX_OFF;
    if (s->flags & (F_MFX | F_MFX2)) {
        static const MfxParams MP[MFX_COUNT] = {
            [MFX_STONE_PHASER] = {MFX_STONE_PHASER, {64, 41, 100, 127}, 64, {0, 0}},
            [MFX_ENSEMBLE] = {MFX_ENSEMBLE, {40, 74, 45, 64}, 100, {0, 0}},
            [MFX_ENS_PHASER] = {MFX_ENS_PHASER, {74, 64, 26, 120}, 100, {30, 76}},
            [MFX_CHORUS] = {MFX_CHORUS, {45, 94, 46, 64}, 127, {20, 0}},
            [MFX_TAPE_FLANGER] = {MFX_TAPE_FLANGER, {40, 40, 64, 40}, 0, {0, 0}},
            [MFX_BBD_FLANGER] = {MFX_BBD_FLANGER, {50, 30, 80, 70}, 0, {1, 0}},
            [MFX_PITCH_SHIFT] = {MFX_PITCH_SHIFT, {19, 64, 60, 64}, 127, {30, 0}},
            [MFX_BBD_ECHO] = {MFX_BBD_ECHO, {99, 50, 115, 60}, 40, {0, 30}},
            [MFX_METAL] = {MFX_METAL, {100, 64, 64, 64}, 0, {64, 64}},
            [MFX_WIND_HARP] = {MFX_WIND_HARP, {90, 70, 40, 50}, 100, {9, 1}},
            [MFX_RINGING_STONES] = {MFX_RINGING_STONES, {85, 60, 30, 50}, 100, {0, 0}},
        };
        uint32_t t = (s->flags >> 16) & 0xFFu;
        MFX_PARAMS(0) = MP[t > MFX_STONE_PHASER && t < MFX_COUNT ? t : MFX_STONE_PHASER];
        if (s->flags & F_MFXEQ) {
            static const MfxParams EQ = {MFX_METAL, {120, 76, 20, 50}, 0, {64, 92}};
            MFX_PARAMS(0) = EQ;
        }
        if (s->flags & F_MFX2) {
            uint32_t t2 = (s->flags >> 28) & 0xFu;
            MFX_PARAMS(1) = t2 ? MP[t2 < MFX_COUNT ? t2 : MFX_STONE_PHASER] : MFX_PARAMS(0);
            perf_state.perf_mode = PERF_DUAL_SYNTH;
        }
    }
    if (s->kind == K_FACT) {
        (void)pt_factory_load(s->arch);             /* (its own effects, MFX and MIX page) */
        for (int i = 0; i < FX_L_COUNT; i++) fx_live[i] = perf_state.mix[i];
    } else if (s->flags & F_MIX) {
        static const uint8_t LIVE[FX_L_COUNT] = {40u, 24u, 110u, 110u};
        for (int i = 0; i < FX_L_COUNT; i++) fx_live[i] = LIVE[i];
    } else if ((s->flags & (F_XFONLY | F_WEARONLY)) && !(s->flags & F_MFX2)) {
        const uint8_t xf = (s->flags & F_XFONLY) ? 110u : 0u;
        const uint8_t LIVE[FX_L_COUNT] = {64u, (s->flags & F_WEARONLY) ? 24u : 64u, xf, xf};
        for (int i = 0; i < FX_L_COUNT; i++) fx_live[i] = LIVE[i];
    } else {
        static const uint8_t LIVE[FX_L_COUNT] = {64u, 64u, 0u, 0u};
        for (int i = 0; i < FX_L_COUNT; i++) fx_live[i] = LIVE[i];
    }
    if (s->flags & F_FX_ANY) {
        perf_state.reverb_type = (s->flags & F_FX_CEL) ? REVTYPE_CELESTIAL : (s->flags & F_FX_TUM) ? REVTYPE_TUMULUS
                                 : (s->flags & F_FX_SPR) ? REVTYPE_SPRING : REVTYPE_MOD;
        perf_state.reverb_decay = 110;
        perf_state.delay_pingpong = 1;
        perf_state.delay_degrade = 90;
    }
#if defined(PT_COST_ONLY)
    if ((s->flags & (0xFu << 16)) && !(s->flags & (F_MFX | F_MFX2 | F_FX_ANY))) {   /* (the effects' price rows) */
        perf_state.reverb_type = (uint8_t)(((s->flags >> 16) & 0xFu) - 1u);
        perf_state.reverb_decay = 110;
        perf_state.delay_pingpong = (uint8_t)((s->flags & F_PPON) ? 1u : 0u);
        perf_state.delay_degrade = 36;
        engine_state[0].amp.delay_send = 50;
        engine_state[0].amp.reverb_send = 60;
    }
#endif
    pool_force = (uint8_t)(s->kind == K_HELD ? F_NV_OF(s->flags) : 0u);
    synth_restart();
    ld.over = 0;
    tally_clear();
    if (s->flags & F_HEAVY)
        synth_midi_3(0xB0, 1, 90);
    if (s->kind == K_HELD || s->kind == K_RS || s->kind == K_ALL) {
        n = (s->flags & F_ONE) ? 1u : pool_force ? pool_force : varch_voice_cap(&engine_state[0]);
        for (i = 0; i < n; i++)
            synth_midi_3(0x90, (uint8_t)(48 + i * 3), (uint8_t)((s->flags & F_HEAVY) ? 70 + i * 7 : 100));
    }
    if (s->kind == K_LOOPCH || (s->kind == K_FACT && !(s->flavour & PTF_NO_CHORDS))) {
        pt.chord = 0;
        loop_chord(0x90);
    }
    if (s->kind == K_SEQ || s->kind == K_REC || s->kind == K_ALL || s->kind == K_LOOP || s->kind == K_LOOPCH ||
        (s->kind == K_FACT && !(s->flavour & PTF_NO_TRANSPORT))) {
        maq_set_playing(synth_seq(), 1);
        carp_set_playing(synth_arp(), 1);           /* (the ARP has its own transport since 2026-10-17) */
    }
    if (s->kind == K_REC || s->kind == K_ALL ||
        ((s->kind == K_LOOP || s->kind == K_LOOPCH) && (s->flags & F_RECL) && !(s->flags & F_HEAVY)))
        rec_loop();
}

static void restore(void)
{
    uint8_t l;
    for (l = 0; l < NLAYER; l++) {
        engine_state[l] = pt.eng[l];
        engine_load_count[l]++;
    }
    perf_state = pt.perf;
    for (l = 0; l < SEQ_NLANE; l++)
        seq_lanes[l] = pt.lanes[l];
    arp_state = pt.arp;
    MFX_PARAMS(0) = pt.mfx[0];
    MFX_PARAMS(1) = pt.mfx[1];
    for (int i = 0; i < FX_L_COUNT; i++) fx_live[i] = pt.live[i];
    pool_force = 0;
    synth_restart();
    wt_to_flash();
    ld.over = 0;
}

void pt_start(uint32_t now_ms, uint8_t mode)
{
    uint8_t l;
    if (pt.state == PT_RUN)
        return;
    pt_run_mode = mode & PT_WT_RAM;
    lap_calibrate();
    if (pt_run_mode)
        wt_to_ram();
    for (l = 0; l < NLAYER; l++)
        pt.eng[l] = engine_state[l];
    pt.perf = perf_state;
    for (l = 0; l < SEQ_NLANE; l++)
        pt.lanes[l] = seq_lanes[l];
    pt.arp = arp_state;
    pt.mfx[0] = MFX_PARAMS(0);
    pt.mfx[1] = MFX_PARAMS(1);
    for (int i = 0; i < FX_L_COUNT; i++) pt.live[i] = fx_live[i];
    for (l = 0; l < NSCEN; l++)
        pt.res[l] = (PtResult){0};
    pt.scen = 0;
    pt.phase = P_SETUP;
    pt.t0 = now_ms;
    pt.state = PT_RUN;
}

void pt_stop(void)
{
    if (pt.state != PT_RUN)
        return;
    restore();
    pt.state = PT_STOPPED;
}

static uint8_t voices_sounding(void)
{
    uint8_t v, n = 0;
    for (v = 0; v < POLYPHONY_VOICES; v++)
        n = (uint8_t)(n + !voice_is_free(&poly_bank[v]));
    return n;
}

static uint16_t sat16(uint32_t v) { return (uint16_t)(v > 0xFFFFu ? 0xFFFFu : v); }

uint8_t pt_tick(uint32_t now_ms)
{
    const PtScen *s;
    PtResult *r;
    uint32_t dt;
    if (pt.state != PT_RUN)
        return PT_PAGE_KEEP;
    s = &SCEN[pt.scen];
    r = &pt.res[pt.scen];
    dt = now_ms - pt.t0;
    if (s->kind == K_RS && pt.phase != P_SETUP && now_ms - pt.t_rs >= PT_RESTRIKE_MS) {
        uint8_t i, n = varch_voice_cap(&engine_state[0]);
        for (i = 0; i < n; i++) {                    /* the same chord as setup: off, then on again (a new attack) */
            uint8_t note = (uint8_t)(48 + i * 3);
            synth_midi_3(0x80, note, 0);
            synth_midi_3(0x90, note, (uint8_t)((s->flags & F_HEAVY) ? 70 + i * 7 : 100));
        }
        pt.t_rs = now_ms;
    }
    if ((s->kind == K_LOOPCH || (s->kind == K_FACT && !(s->flavour & PTF_NO_CHORDS))) && pt.phase != P_SETUP && now_ms - pt.t_rs >= PT_CHORD_MS) {
        loop_chord(0x80);                            /* let the chord go (its tails ring on), then the next */
        pt.chord++;
        loop_chord(0x90);
        pt.t_rs = now_ms;
    }
    switch (pt.phase) {
    case P_SETUP:
        setup(s);
        pt.phase = P_SETTLE;
        pt.t0 = pt.t_rs = now_ms;
        break;
    case P_SETTLE:
        if (ld.over || dt >= PT_SETTLE_MS) {        /* cut while settling: measure the cut itself */
            if (!ld.over)
                tally_clear();
            pt.phase = P_MEASURE;
            pt.t0 = now_ms;
        }
        break;
    default: {
        uint8_t v = voices_sounding();
        if (v > r->voices)
            r->voices = v;
        if (!ld.over && dt < PT_MEASURE_MS)
            break;
        r->avg_pm = sat16(ld.halves ? ld.load_sum / ld.halves : 0u);
        r->peak_pm = sat16(ld.peak);
        r->late = sat16(ld.late);
        r->over = ld.over;
        r->floor_pm = sat16(synth_floor_pm());
        r->pk_out_pm = sat16(ld.pk[0]);
        r->pk_vox_pm = sat16(ld.pk[1]);
        r->pk_notes = (uint8_t)(ld.pk[2] & 0xFFu);
        r->pk_steals = (uint8_t)(ld.pk[2] >> 8 & 0xFFu);
        r->pk_c0 = (uint8_t)(ld.pk[3] & 0xFFu);
        r->pk_c1 = (uint8_t)(ld.pk[3] >> 8 & 0xFFu);
        r->guarded = sat16(ld.guarded);
        r->ui_fps10 = sat16(dt ? ld.ui_frames * 10000u / dt : 0u);
        r->ui_max_ms = sat16((ld.ui_max_us + 999u) / 1000u);
#if defined(SYNTH_PROF)
        {
            uint32_t i, frames = ld.halves * PT_HALF_FRAMES;
            for (i = 0; i < SPROF_N; i++)
                r->cyc[i] = sat16(frames ? synth_prof_acc[i] / frames : 0u);
        }
#endif
        if (++pt.scen < NSCEN) {
            pt.phase = P_SETUP;
        } else {
            restore();
            pt.state = PT_DONE;
            return PT_PAGE_KEEP;
        }
        break;
    }
    }
    return SCEN[pt.scen].page;
}
