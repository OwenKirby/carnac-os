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
#include "hardware/mem_section.h"

/* PT_WT_RAM (2026-10-15): the banks the scenarios play (OSC1: bank 0, the default; OSC2 of the heavy patches: bank 3)
 * copied to RAM for the run and wavetable_frames pointed at the copies, then back. The same firmware, the same patches:
 * the difference against a normal run is what reading the wavetables through XIP flash costs. */
static const uint8_t PT_WT_BANKS[2] = {0u, 3u};
static int16_t pt_wt[2][NUM_MORPH_STEPS * WT_FRAME_SAMPLES] SYNTH_POOL;
static const int16_t *pt_wt_saved[2][NUM_MORPH_STEPS];
static uint8_t pt_wt_on, pt_run_mode;      /* the copies are in use; the last run's mode */

static void wt_to_ram(void)
{
    uint32_t b, f, i;
    for (b = 0; b < 2u; b++)
        for (f = 0; f < NUM_MORPH_STEPS; f++) {
            const int16_t **slot = &wavetable_frames[PT_WT_BANKS[b] * NUM_MORPH_STEPS + f];
            int16_t *dst = &pt_wt[b][f * WT_FRAME_SAMPLES];
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

enum { K_IDLE, K_HELD, K_SEQ, K_RS };   /* K_RS: K_HELD's chord struck again every PT_RESTRIKE_MS */
enum { P_SETUP, P_SETTLE, P_MEASURE };
#define F_HEAVY 1u
#define F_ONE 2u                     /* one note, not the cap */
#define F_FX_MOD 4u                  /* the MOD reverb (the tank, moving) and the PING PONG delay */
#define F_FX_CEL 8u                  /* the CELESTIAL reverb (the tank + the octave shifter) and PING PONG */
#define F_FX_TUM 16u                 /* the TUMULUS reverb (the still tank, long) and PING PONG */
#define F_FX_SPR 32u                 /* the SPRING reverb (two dispersive springs) and PING PONG */
#define F_DLY_BBD 64u                /* the delay as a BBD (DELAY TYPE), with any of the above */
#define F_ATK_FILT 128u              /* STRIKE: half the attack through FILTER1 (ATTACK TO FILTER 64), its dearer path */
#define F_HP 256u                    /* both filters' morph at 110 (SH-2: past BP, all five output taps) */
#define F_PCM 512u                   /* PCM waves (needs the store uploaded): OSC1 CHOIR at SCAN 64 (both loops read),
                                      * OSC2 WOOD CHIMES; with K_RS / K_SEQ OSC1 plays WOOD CHIMES too (the attack
                                      * at every note). gen_pcm_waves.py's wave order: WOOD CHIMES 0, CHOIR 4 */
#define F_DRIVE 1024u                /* AMP DRIVE 80 through the model in bits 11..13 (F_DRV) */
#define F_DRV(m) (F_DRIVE | (uint16_t)((m) << 11))
#define F_MFX 0x4000u                /* STONE PHASER on layer 1 (RATE 0.5 Hz, FEEDBACK 100, STEREO 64, AUTO) */
#define F_MFX2 0x8000u               /* ... on both layers, DUAL (K_IDLE: nothing playing, the effect alone) */
#define F_MFXT(t) ((uint32_t)(t) << 16)   /* with F_MFX / F_MFX2: the type instead (MFX_*, 2026-11-02), at its
                                      * defaults (UIStateMachine.c mfx_type_set) */
#define F_MIX 0x1000000u             /* the MIX page all engaged (2026-11-02, dsp/fx.h fx_live): BALANCE / DRY-WET off its
                                      * middle (the faders' ramps), LOOP TONE dark (the delay's feedback low-pass), both
                                      * cross-feeds high (the loop) */
#define PT_RESTRIKE_MS 250u          /* K_RS: shorter than the longer attacks, so voices are nearly always attacking */
#define F_FX_ANY (F_FX_MOD | F_FX_CEL | F_FX_TUM | F_FX_SPR)

typedef struct {
    const char *name;
    uint8_t kind, arch, flavour;
    uint32_t flags;
    uint8_t page;
} PtScen;

static const PtScen SCEN[] = {
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
    {"SINGLE MS20", K_HELD, VARCH_SINGLE, FLT_MS20, 0, UIPAGE_HOME1},
    {"SINGLE SH-2", K_HELD, VARCH_SINGLE, FLT_SH2, 0, UIPAGE_HOME1},
    {"SINGLE FORMANT", K_HELD, VARCH_SINGLE, FLT_FORMANT, 0, UIPAGE_HOME1},
    {"DUAL FLT LADDER", K_HELD, VARCH_DUAL_FILTER, FLT_MULTI, 0, UIPAGE_HOME1},
    {"SEQ+ARP HOME", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"SEQ+ARP AMP", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_AMP},
    {"SEQ+ARP SEQ", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_SEQ},
    {"SEQ+ARP MOD1", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY, UIPAGE_MOD1},
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
    {"HEAVY CEL+BBD PP", K_HELD, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_CEL | F_DLY_BBD, UIPAGE_HOME1},
    {"SEQ+ARP SPR+BBD", K_SEQ, VARCH_SINGLE, FLT_FORMANT, F_HEAVY | F_FX_SPR | F_DLY_BBD, UIPAGE_HOME1},
    /* 2026-10-16: STRIKE (SINGLE + an attack sample). Held notes play the attack once at the start; SEQ+ARP restarts it
     * on every step, its worst case */
    {"STRIKE", K_HELD, VARCH_STRIKE, FLT_MULTI, 0, UIPAGE_HOME1},
    {"STRIKE HEAVY", K_HELD, VARCH_STRIKE, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"SEQ+ARP STRIKE", K_SEQ, VARCH_STRIKE, FLT_FORMANT, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"SEQ+ARP STRK FILT", K_SEQ, VARCH_STRIKE, FLT_FORMANT, F_HEAVY | F_FX_CEL | F_ATK_FILT, UIPAGE_HOME1},
    /* 2026-10-21: SH-2 (the dearest filter, ~1.9x MULTI on the host) at every architecture's cap: the caps must hold */
    {"SINGLE SH2 H", K_HELD, VARCH_SINGLE, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"DUALOSC SH2 H", K_HELD, VARCH_DUAL_OSC, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"SPREAD SH2 H", K_HELD, VARCH_SPREAD, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"DUALFLT SH2 H", K_HELD, VARCH_DUAL_FILTER, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"STACK SH2 H", K_HELD, VARCH_STACK, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"BINAUR SH2 H", K_HELD, VARCH_BINAURAL, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"STRIKE SH2 H", K_HELD, VARCH_STRIKE, FLT_SH2, F_HEAVY | F_ATK_FILT, UIPAGE_HOME1},
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
    {"STRIKE HEAVY TUBE", K_HELD, VARCH_STRIKE, FLT_FORMANT, F_HEAVY | F_DRV(DRIVE_TUBE), UIPAGE_HOME1},
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
    {"DSPRD", K_HELD, VARCH_DUAL_SPREAD, FLT_MULTI, 0, UIPAGE_HOME1},
    {"DSPRD HEAVY", K_HELD, VARCH_DUAL_SPREAD, FLT_FORMANT, F_HEAVY, UIPAGE_HOME1},
    {"DSPRD MULTI H", K_HELD, VARCH_DUAL_SPREAD, FLT_MULTI, F_HEAVY, UIPAGE_HOME1},
    {"DSPRD SH2 H", K_HELD, VARCH_DUAL_SPREAD, FLT_SH2, F_HEAVY, UIPAGE_HOME1},
    {"DSPRD H CEL+PP", K_HELD, VARCH_DUAL_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"DSPRD MULTI H PCM", K_HELD, VARCH_DUAL_SPREAD, FLT_MULTI, F_HEAVY | F_PCM, UIPAGE_HOME1},
    {"DSPRD SH2 CEL+PP", K_HELD, VARCH_DUAL_SPREAD, FLT_SH2, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
    {"SEQ+ARP DSPRD CEL", K_SEQ, VARCH_DUAL_SPREAD, FLT_MULTI, F_HEAVY | F_FX_CEL, UIPAGE_HOME1},
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
};
#define NSCEN ((uint8_t)(sizeof SCEN / sizeof SCEN[0]))

static struct {
    uint8_t state, scen, phase;
    uint32_t t0, t_rs;                  /* t_rs: K_RS's last strike */
    PtResult res[NSCEN];
    /* the live state the test overwrites, put back after */
    EngineState eng[NLAYER];
    MasterPerformancePreset perf;
    SequencerLane lanes[SEQ_NLANE];
    ArpState arp;
    MfxParams mfx[2];
    uint8_t live[FX_L_COUNT];        /* the MIX page's (dsp/fx.h fx_live) */
} pt;

/* the audio side's tally (the ISR adds, pt_tick reads and clears under the lock) */
static volatile struct {
    uint32_t load_sum, halves, peak, late, ui_frames, ui_max_us;
    uint8_t over_run, over;
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
    if (load_pm > ld.peak)
        ld.peak = load_pm;
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
    e->filter[0].mod_src = MSRC_LFO1;
    e->filter[0].mod_amt = 72;
    e->filter[0].flavour = e->filter[1].flavour = s->flavour;
    e->filter[0].resonance = e->filter[1].resonance = 90;
    if (s->flags & F_HP)
        e->filter[0].morph = e->filter[1].morph = 110;
    if (s->flags & F_HEAVY) {
        e->oscmix.xmod_type = XMOD_RING;
        e->oscmix.noise_level = 100;
        e->oscmix.noise_colour = 40;
        e->fltmix.stack_width = 127;
        e->osc[1].wavebank = 3;
        e->filter[0].cutoff = 70;
        e->filter[1].cutoff = 90;
        e->lfo[0].rate = 70;
        e->lfo[1].rate = 90;
        e->lfo[2].rate = 40;
        e->amp.delay_send = 50;
        e->amp.reverb_send = 60;
        e->mod[0] = (ModSlotParams){MSRC_LFO2, MFN_SAMPLE_B_ON_A, MSRC_LFO3, 120};
        e->mod[1] = (ModSlotParams){MSRC_MODSLOT1, MFN_LAG, MSRC_ENV2, 100};
        e->mod[2] = (ModSlotParams){MSRC_MODSLOT2, MFN_MUL, MSRC_LFO1, 110};
        e->filter[0].mod_src = MSRC_MODSLOT3;
        e->filter[0].mod_amt = 112;
    }
    if (s->flags & F_DRIVE) {
        e->amp.drive = 80;
        e->amp.drive_model = (uint8_t)((s->flags >> 11) & 7u);
    }
    if (s->flags & F_PCM) {
        e->osc[0].wavebank = (uint8_t)(WAVE_PCM_FIRST + (s->kind == K_HELD ? 4u : 0u));
        e->osc[1].wavebank = WAVE_PCM_FIRST;
        e->osc[0].scan = e->osc[1].scan = 64;
    }
}

static void seq_slot(void)
{
    uint8_t l, i;
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    perf_state.int_tempo = 127;                     /* 167 BPM: the most steps a second */
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
    for (i = 0; i < ARP_NSTEP; i++) {               /* every step a chord, strummed across 4 octaves */
        ArpChordStep *st = &arp_state.steps[i];
        st->chord_root = (uint8_t)(55 + (i * 5) % 12);
        st->rate = ARPRATE_1_128;                    /* (TIMING 1/32: below) */
        st->octave_range = ARP_NOCT - 1u;
        st->style = (uint8_t)(ARPSTYLE_UP + i % 7u);
        st->variation = (uint8_t)(i * 3u);
    }
    arp_state.sequence_length = ARP_NSTEP;
    arp_state.gate_length = 90;
    arp_set_layer(&arp_state, ARPLAYER_BOTH);
    arp_set_timing(&arp_state, ARPDIV_1_32);
}

static void setup(const PtScen *s)
{
    uint8_t n, i;
    live_defaults();
    if (s->kind != K_IDLE)
        patch(&engine_state[0], s);
    if (s->kind == K_SEQ) {
        patch(&engine_state[1], s);
        seq_slot();
    }
    if (s->flags & F_ATK_FILT) {
        engine_state[0].oscmix.atk_filter = 64;
        engine_state[1].oscmix.atk_filter = 64;
    }
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
        };
        uint32_t t = (s->flags >> 16) & 0xFFu;
        MFX_PARAMS(0) = MP[t > MFX_STONE_PHASER && t < MFX_COUNT ? t : MFX_STONE_PHASER];
        if (s->flags & F_MFX2) {
            MFX_PARAMS(1) = MFX_PARAMS(0);
            perf_state.perf_mode = PERF_DUAL_SYNTH;
        }
    }
    if (s->flags & F_MIX) {
        static const uint8_t LIVE[FX_L_COUNT] = {40u, 24u, 110u, 110u};
        for (int i = 0; i < FX_L_COUNT; i++) fx_live[i] = LIVE[i];
    } else {
        static const uint8_t LIVE[FX_L_COUNT] = {64u, 64u, 0u, 0u};
        for (int i = 0; i < FX_L_COUNT; i++) fx_live[i] = LIVE[i];
    }
    if (s->flags & F_FX_ANY) {
        perf_state.reverb_type = (s->flags & F_FX_CEL) ? REVTYPE_CELESTIAL : (s->flags & F_FX_TUM) ? REVTYPE_TUMULUS
                                 : (s->flags & F_FX_SPR) ? REVTYPE_SPRING : REVTYPE_MOD;
        perf_state.delay_type = (s->flags & F_DLY_BBD) ? DELAYTYPE_BBD : DELAYTYPE_TAPE;
        perf_state.reverb_decay = 110;
        perf_state.delay_pingpong = 1;
        perf_state.delay_degrade = 90;
    }
    synth_restart();
    ld.over = 0;
    tally_clear();
    if (s->flags & F_HEAVY)
        synth_midi_3(0xB0, 1, 90);
    if (s->kind == K_HELD || s->kind == K_RS) {
        n = (s->flags & F_ONE) ? 1u : varch_voice_cap(&engine_state[0]);
        for (i = 0; i < n; i++)
            synth_midi_3(0x90, (uint8_t)(48 + i * 3), (uint8_t)((s->flags & F_HEAVY) ? 70 + i * 7 : 100));
    } else if (s->kind == K_SEQ) {
        maq_set_playing(synth_seq(), 1);
        carp_set_playing(synth_arp(), 1);           /* (the ARP has its own transport since 2026-10-17) */
    }
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
