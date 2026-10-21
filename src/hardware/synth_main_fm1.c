/* SPDX-License-Identifier: GPL-3.0-only
 * Boot, safe mode, crash screen, audio ISR and main loop adapted from X0X's firmware/src/app/main_fm1.c
 * (github.com/charlesvestal/fm1-x0x), itself adapted from Felucca's main.c / audio.c, Copyright (C) 2026
 * Leo Kuroshita (@kurogedelic), Hugelton Instruments. The editor helpers are X0X's app/editor.c, likewise. */
/* The synth on the FM1, over X0X's platform: the glue that replaces X0X's app layer (its 909 / 808 / 303 groovebox).
 *
 * A unity-build fragment: X0X's firmware/src/x0x.c up to (not including) its app, then every synth source (src/dsp,
 * UIStateMachine.c, src/hardware/fm1_x0x_canvas.h, the screens), then this file. src/host/x0x_unity_check.sh builds
 * exactly that.
 *
 * What it keeps from X0X unchanged, because they are what makes a bad build recoverable:
 *   - the boot-loop guard: a crash or hang in the first 30 s counts as a failed boot; after two, SAFE MODE (no sound,
 *     USB on, the web installer reaches it; PLAY tries again); after four, the chip's own update mode;
 *   - the M-UPGRADE update entry (ota_service) in the main loop, and in safe mode;
 *   - OCT- + OCT+ held 5 s: the chip's update mode (UBOOT), from the main loop and from safe mode;
 *   - the crash screen and the watchdog.
 * Two cores (2026-10-15): X0X's fm1_cpu1 / plat_cpu2_* start the second core at power-on, and the audio interrupt
 * hands it half the voices each run (synth_set_core2, docs/dual-core-plan.md). Not in safe mode, not after a failed
 * boot, not once barred (a crash beside it, or bad reads): then one core renders everything, the same sound.
 *
 * Who touches the synth: the audio ISR (ALNK0) renders and drains USB / TRS MIDI into synth_midi_3. The main loop
 * reads the panel and calls the UI state machine, which writes the patch bytes and drives the MAQ / ARP; every such
 * call runs inside syn_lock() (interrupts off, a few microseconds), so the render never sees half an event. The screen
 * drawing only reads, outside the lock.
 *
 * The panel (spec 2, 3, 5, 7, 8), as the browser emulator (src/host/web_entry.c, web/emu) has it:
 *   FX SEL ENV LFO EDIT GLO HOME ARP SAVE SEQ   section buttons (UIEV_BUTTON)
 *   PLAY / REC                                  UIEV_TRANSPORT
 *   SELECT / ALGORITHM encoders                 UIEV_SELECT_ENC / UIEV_ALGO_ENC
 *   PRESETS encoder                             UIEV_PRESET_ENC (loads); does nothing while HOME is held
 *   SAVE held + OCT- / OCT+                     COPY / PASTE (ui_sm_save_oct; SAVE then acts on its release, as HOME)
 *   HOME held + OCT- / OCT+                     layer focus L1 / L2 (spec 5.2 asks for a press of the PRESETS
 *                                               encoder, which the FM1's HAL does not report: no push switch is wired
 *                                               to the matrix). HOME acts on release, and not at all after a chord, so
 *                                               the page being edited stays up. Held HOME_HINT_MS: the focused
 *                                               layer's OCT LED lit, the other flashing (SINGL: OCT- alone)
 *   KNOB 1..4                                   the page's knob map (ui_sm_knob), relative; HOME 1..3 velocity, bend
 *                                               and mod wheel, sent as MIDI
 *   keys F3..G5                                 notes from F3 (MIDI 53) + 12 x the octave; SEQ / ARP pages first offer
 *                                               each key to ui_sm_seq_key / ui_sm_arp_key
 *   OCT- / OCT+                                 tap: octave down / up (-3..+3); on SEQ, held with a step key: loop start
 *                                               / end (the octave moves only on a tap with no key)
 *
 * Saving (2026-10-16): a SAVE (patch, performance, sequence, arp) goes to flash (preset_store_fm1.c, in the store flash
 * the wavetables left when they moved into the firmware) and comes back at power-on; the THEME too (settings_tick). */
#include "dsp/synth_runtime.h"
#include "dsp/preset.h"
#include "dsp/wavetables.h"
#include "dsp/flash_record.h"       /* FREC_VER_*: the preset store keeps each bank's layout version */
#include "UIStateMachine.h"
#include "hardware/mem_section.h"     /* SYNTH_POOL */
#include "carnac_splash.c"               /* the start-up screen: after ui_seq_draw.c's runes, which the unity puts first */

/* X0X's platform pieces that are not its app: the user-slot header layout (plat_slot), the panel map and its
 * calibration (OCT- + OCT+ held at power-on), plat.h over the HAL */
#include "app/slots.c"
#include "app/panel.c"
#include "app/plat_fm1.c"

#ifndef SYNTH_VERSION
#define SYNTH_VERSION "DEV"
#endif

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[], _c1_start[], _c1_end[], _c1_load[];
/* the end of .ram_text alone. X0X's app.ld has none: there _rt_end (the PC guard's RAM window, = _c1_end) is also
 * where .ram_text ends, as .c1_text follows it with nothing in it. build_fm1.py --dsp-ram moves .c1_text (the DSP) to
 * the top of RAM and defines this: copying .ram_text to _rt_end would then overwrite all of .data, .bss and .pool
 * with flash bytes. Undefined (weak), it reads 0 and the copy runs to _rt_end as before. */
extern uint32_t _rt_text_end[] __attribute__((weak));

static void syn_lock(void) { (void)irq_save(); }
static void syn_unlock(void) { irq_restore(0); }

/* the PERF TEST (src/hardware/perf_test.h): started and read over SysEx (ED_PERFT below, tools: fm1_perf.py) */
#include "perf_test.c"
#if defined(SYNTH_PROF)
uint32_t synth_prof_now(void) { return plat_cycles(); }   /* the render's stage profile (dsp/synth_prof.h) */

/* What one operation costs on this core, for choosing between integer and FPU code: MB_N of each, in a dependent
 * chain (each result feeds the next, so they cannot overlap or be folded away), interrupts off, in plat_cycles ticks;
 * kind 0 is the loop alone, to take off the others. Single precision only: a double would be the soft routines. */
#define MB_N 1000u
enum { MB_LOOP, MB_IMUL, MB_IDIV, MB_UDIV, MB_MUL64, MB_FADD, MB_FMUL, MB_FDIV, MB_I2F2I, MB_LOAD, MB_COUNT };
static volatile int32_t mb_a = 1234567, mb_b = 37;
static volatile float mb_fa = 1.2345f, mb_fb = 1.0001f;
static volatile int32_t mb_sink;
static volatile float mb_fsink;
static int16_t mb_tab[256];
static uint32_t mb_time(uint32_t kind)
{
    uint32_t i, t0, t1, f = irq_save();
    int32_t a = mb_a, b = mb_b;
    uint32_t ua = (uint32_t)mb_a, ub = (uint32_t)mb_b;
    int64_t w = 0;
    float x = mb_fa, y = mb_fb;
    t0 = plat_cycles();
    switch (kind) {
    case MB_LOOP: for (i = 0; i < MB_N; i++) a = a + (int32_t)i; break;
    case MB_IMUL: for (i = 0; i < MB_N; i++) a = a * b + (int32_t)i; break;
    case MB_IDIV: for (i = 0; i < MB_N; i++) a = (a + 1000000) / b + (int32_t)i * 4096; break;
    case MB_UDIV: for (i = 0; i < MB_N; i++) ua = (ua + 1000000u) / ub + i * 4096u; break;
    case MB_MUL64: for (i = 0; i < MB_N; i++) { w += (int64_t)a * b; a = (int32_t)(w >> 7) + (int32_t)i; } break;
    case MB_FADD: for (i = 0; i < MB_N; i++) x = x + y; break;
    case MB_FMUL: for (i = 0; i < MB_N; i++) x = x * y; break;
    case MB_FDIV: for (i = 0; i < MB_N; i++) x = x / y; break;
    case MB_I2F2I: for (i = 0; i < MB_N; i++) a = (int32_t)((float)a * 0.5f) + (int32_t)i; break;
    default: for (i = 0; i < MB_N; i++) a = mb_tab[(uint32_t)a & 255u] + (int32_t)i; break;   /* a RAM load */
    }
    t1 = plat_cycles();
    irq_restore(f);
    mb_sink = a + (int32_t)ua + (int32_t)w;
    mb_fsink = x;
    return t1 - t0;
}
#endif

/* --------------------------------------------------------------- audio --- */
/* X0X's 512-frame halves (11.6 ms): the render averages a heavy moment over more time, for one more half of latency.
 * synth_render_q15 cuts each REND_FRAMES block into its own 128-frame passes. */
#define FS 44100u                                   /* nominal (the I2S runs at 44,117.6 Hz): the load's budget */
#define HALF_FRAMES 512u
#define HALF_WORDS (HALF_FRAMES * 2u)
#define REND_FRAMES 256u
#if FELUCCA_UAC
_Static_assert(REND_FRAMES == UA_HALF, "usb.c sizes the USB audio ring band for this block");
#endif
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

#define DBG_MAGIC 0x53594E31u                       /* "SYN1": read with `fm1t memr` */
struct syn_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots, stage;
    uint32_t crash_seen;                            /* the crash count already reported at a boot */
} syn_dbg __attribute__((section(".noinit")));
static uint8_t safe_mode;

/* USB and TRS MIDI in (plat_midi_in: USB-MIDI event packets, byte 0 = cable / CIN) into the synth. ISR only. */
static void midi_drain(void)
{
    uint32_t pkt, guard = 64;
    while (guard-- && plat_midi_in(&pkt)) {
        uint32_t cin = pkt & 0x0Fu;
        uint8_t st = (uint8_t)(pkt >> 8);
        if (cin >= 0x8u && cin <= 0xEu && st >= 0x80u && st < 0xF0u) {   /* a channel message */
            uint8_t d2 = (uint8_t)((pkt >> 24) & 0x7Fu);
            if ((st & 0xF0u) == 0x90u)
                d2 = instrument_velocity(d2);       /* GLOBAL > VEL CURVE (0 stays a note-off) */
            synth_midi_3(st, (uint8_t)((pkt >> 16) & 0x7Fu), d2);
        }
        else if (cin == 0xFu && st >= 0xF8u)                               /* realtime: clock, start, stop */
            synth_midi_3(st, 0, 0);
    }
}

/* The synth's Q15 to the codec, through the MASTER pot (master_q12, 0..4096, plat_fm1.c), which glides over the
 * block. Full MASTER puts Q15 full scale at 2^22, X0X's NORMAL level (Felucca's). */
static int32_t out_vol = -1;
static void out_level(int32_t *lr, uint32_t n)
{
    int32_t tgt = (int32_t)master_q12, v = out_vol < 0 ? tgt : out_vol, step = (tgt - v) / (int32_t)REND_FRAMES;
    uint32_t i;
    for (i = 0; i < n; i++) {
        v += step;
        lr[2u * i] = (lr[2u * i] * v) >> 5;          /* |Q15| x 4096 < 2^31 */
        lr[2u * i + 1u] = (lr[2u * i + 1u] * v) >> 5;
    }
    out_vol = tgt;
}

/* GLOBAL > CLOCK OUT (2026-10-17): while the clock is internal, every pulse of it (24 a quarter) goes out on USB as
 * MIDI clock, after the block that made it (X0X's own clock out has the same block grain), and the sequencer's
 * transport as START / STOP. ISR only. */
static uint32_t clk_sent;
static uint8_t clk_play;
static void clock_out(void)
{
    uint32_t n = midi_clock_pulse_count(), guard = 8;
    uint8_t play;
    if (instrument.clock_out != CLKOUT_USB || midi_clock_is_external()) {
        clk_sent = n;
        clk_play = 0;
        return;
    }
    play = (uint8_t)(maq_playing(synth_seq()) || carp_playing(synth_arp()));   /* (either transport) */
    if (play && !clk_play)
        plat_midi_out(0x0Fu | 0xFAu << 8);
    else if (!play && clk_play)
        plat_midi_out(0x0Fu | 0xFCu << 8);
    clk_play = play;
    while (clk_sent != n && guard--) {
        plat_midi_out(0x0Fu | 0xF8u << 8);
        clk_sent++;
    }
    clk_sent = n;                                   /* (a stall's backlog is dropped, never burst) */
}

static uint32_t render_block(int32_t *o)            /* ALNK0: one half, measured; its load per mille */
{
    uint32_t t0 = fm1_ticks(), us, budget = HALF_FRAMES * 1000000u / FS, k;
    uint8_t ok = pt_audio_ok();                     /* 0: the PERF TEST's guard cut the scenario: silence */
    syn_dbg.in_audio = 1;
#if FELUCCA_UAC
    uac_render_start();
#endif
    for (k = 0; k < HALF_FRAMES; k += REND_FRAMES) {
        midi_drain();
        if (ok) {
            synth_render_q15(o + 2u * k, REND_FRAMES);
        } else {
            uint32_t i;
            for (i = 0; i < 2u * REND_FRAMES; i++)
                o[2u * k + i] = 0;
        }
        out_level(o + 2u * k, REND_FRAMES);
        clock_out();
#if FELUCCA_UAC
        uac_tap(o + 2u * k, REND_FRAMES);           /* the USB audio input: the same output */
#endif
    }
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    syn_dbg.cpu_q8 = (syn_dbg.cpu_q8 * 15u + (us * 256u) / budget) / 16u;
    audio_cpu_pct = (syn_dbg.cpu_q8 * 100u) >> 8;
    if (us * 100u / budget > audio_peak_pct)
        audio_peak_pct = us * 100u / budget;
    syn_dbg.halves++;
    syn_dbg.last_us = us;
    if (us > syn_dbg.max_us)
        syn_dbg.max_us = us;
    syn_dbg.in_audio = 0;
    return us * 1000u / budget;
}

/* Overload shedding (Felucca sheds too, audio.c SHED_TICKS): a half that took SHED_ON_PM of its time, or ran late, takes
 * one voice from each layer's limit (voice_shed: the voice left out fades in a few ms, not a cut), at most one every
 * SHED_GAP halves while the fade still costs; a calm second (every half under SHED_OFF_PM) gives one back. So a patch
 * and a playing style the CPU cannot carry lose voices instead of buzzing. Off during the PERF TEST: it measures. */
#define SHED_ON_PM 880u
#define SHED_OFF_PM 650u
#define SHED_GAP 4u                                 /* halves between two sheds (~46 ms) */
#define SHED_CALM 86u                               /* halves of calm before a voice comes back (~1 s) */
static uint32_t shed_wait, shed_calm, shed_count;   /* shed_count: voices shed since boot (the debug read) */
static void shed_update(uint32_t pm, uint8_t late)
{
    uint8_t n = voice_shed;
    if (pt_state() == PT_RUN) {
        voice_shed = 0;
        return;
    }
    if (shed_wait)
        shed_wait--;
    if ((pm >= SHED_ON_PM || late) && n + 1u < VOICE_POOL) {
        shed_calm = 0;
        if (!shed_wait) {
            voice_shed = (uint8_t)(n + 1u);
            shed_wait = SHED_GAP;
            shed_count++;
        }
    } else if (pm < SHED_OFF_PM && n) {
        if (++shed_calm >= SHED_CALM) {
            voice_shed = (uint8_t)(n - 1u);
            shed_calm = 0;
        }
    } else {
        shed_calm = 0;
    }
}

void fm1_alnk0_irq(void)                            /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), pm = render_block(&abuf[half * HALF_WORDS]);
        uint8_t late = 0;
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half) {
            syn_dbg.late++;                         /* the DMA moved on while we rendered */
            audio_xruns++;
            late = 1;
        }
        pt_audio_half(pm, late);
        shed_update(pm, late);
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

static void audio_silence(void)                     /* IRQs off (flash erase): the DMA would loop stale audio */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}

/* ------------------------------------------------------------ timer --- */
static uint8_t usb_due;
void fm1_timer5_irq(void)                           /* 10 kHz: the panel scan, USB at 2 kHz, the ms count */
{
    static uint32_t sub;
    fm1_timer5_ack();
    syn_dbg.timer_irqs++;
    if (syn_dbg.in_audio)
        syn_dbg.nested++;
    fm1_input_tick();
    if (sub % 5u == 0u) {
        usb_due = 1;
#if FELUCCA_UAC
        if (syn_dbg.in_audio)
            uac_service();                          /* one packet per 1 ms frame: cannot wait for the render */
#endif
    }
    if (usb_due && !syn_dbg.in_audio) {             /* usb_poll shares the MIDI queues with the audio ISR */
        usb_due = 0;
        usb_poll();
#if X0X_TRS
        uart_midi_poll();
#endif
    }
    if (++sub == 10u)
        sub = 0;
    {
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

/* ---------------------------------------------------------- the store --- */
/* The instrument's settings across a power cycle: the THEME (GLOBAL > THEME, UIStateMachine.theme; 2026-10-16) and
 * GLOBAL's MASTER TUNE, VEL CURVE and CLOCK OUT (dsp/preset.c instrument; 2026-10-17). In
 * X0X's settings object (storage.c OBJ_SET: two 4 KiB sectors at 0xFC000, written in turn, the CRC'd header last, so a
 * save torn by a power cut leaves the one before in charge). The payload starts "CRNC": X0X's own settings loader
 * wants its PROJ_FORMAT word followed by three zero bytes and at least 20 bytes, so after a reinstall of X0X it takes
 * its defaults and never reads ours. Saved from the main loop SET_SAVE_MS after the last change (scrolling the list
 * writes once); a save mutes the audio for the sector erase (tens of ms, audio_silence), and core 1 is idle in RAM
 * then (fm1_cpu1.h). Room for more instrument settings: rsv. */
static UIStateMachine sm;                           /* (the panel's state machine, defined below) */
#define SET_VER 2u                                  /* 2 (2026-10-17): + GLOBAL's MASTER TUNE, VEL CURVE, CLOCK OUT */
#define SET_SAVE_MS 2000u
typedef struct {
    uint8_t magic[4];                               /* "CRNC" */
    uint8_t ver;                                    /* SET_VER (1: the THEME alone) */
    uint8_t theme;                                  /* UITHEME_* */
    uint8_t master_tune, vel_curve, clock_out;      /* InstrumentSettings (ver 2) */
    uint8_t rsv[7];
} SynSettings;
static uint8_t set_saved[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};   /* what flash holds: theme, tune, curve, clock out */
static uint32_t set_changed_ms;
static uint8_t set_pending;

static void settings_now(uint8_t s[4])
{
    s[0] = sm.theme;
    s[1] = instrument.master_tune;
    s[2] = instrument.vel_curve;
    s[3] = instrument.clock_out;
}

static void settings_load(void)
{
    SynSettings r;
    if (st_load(OBJ_SET, &r, sizeof r) == (int)sizeof r && r.magic[0] == 'C' && r.magic[1] == 'R' &&
        r.magic[2] == 'N' && r.magic[3] == 'C' && (r.ver == 1u || r.ver == SET_VER) && r.theme < UITHEME_COUNT) {
        sm.theme = r.theme;
        if (r.ver == SET_VER && r.master_tune >= MASTER_TUNE_MIN && r.master_tune <= MASTER_TUNE_MAX &&
            r.vel_curve < VELCURVE_COUNT && r.clock_out < CLKOUT_COUNT) {
            instrument.master_tune = r.master_tune;
            instrument.vel_curve = r.vel_curve;
            instrument.clock_out = r.clock_out;
        }
        settings_now(set_saved);
    }
}

/* the main loop's: settings that changed and then held still SET_SAVE_MS go to flash */
static void settings_tick(void)
{
    SynSettings r;
    uint8_t now[4];
    settings_now(now);
    if (memcmp(now, set_saved, sizeof now) == 0) {
        set_pending = 0;
        return;
    }
    if (!set_pending) {
        set_pending = 1;
        set_changed_ms = fm1_ms;
        return;
    }
    if (fm1_ms - set_changed_ms < SET_SAVE_MS || pt_state() == PT_RUN)
        return;
    memset(&r, 0, sizeof r);
    r.magic[0] = 'C'; r.magic[1] = 'R'; r.magic[2] = 'N'; r.magic[3] = 'C';
    r.ver = SET_VER;
    r.theme = now[0];
    r.master_tune = now[1];
    r.vel_curve = now[2];
    r.clock_out = now[3];
    if (st_save(OBJ_SET, &r, sizeof r) == 0)
        memcpy(set_saved, now, sizeof now);         /* (a failed save retries SET_SAVE_MS later) */
    else
        set_changed_ms = fm1_ms;
}

/* The banks a SAVE writes and a load reads: patches in preset.c's patch_bank (128), sequences and arps below (32 each
 * since 2026-10-17), all in .pool; the 32 performances are whole snapshots (WholePerformance) kept in flash only, their
 * names here. syn_used marks the slots written (or the factory's), so a never-written slot reads EMPTY. Kept in flash
 * by the preset store below (2026-10-16; before, RAM only, as the browser emulator's store still is: web_entry.c). */
static SequencerLane syn_seq_bank[BANK_SLOTS][SEQ_NLANE] SYNTH_POOL;
static ArpState syn_arp_bank[BANK_SLOTS] SYNTH_POOL;
static uint8_t syn_perf_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_used[UISAVE_COUNT][PATCH_SLOTS / 8u];
static WholePerformance syn_perf_load SYNTH_POOL;   /* a performance being loaded: read outside syn_lock, taken inside */
static uint8_t syn_perf_req;                        /* 1: a performance load waits for the main loop (syn_perf_tick) */

static UIStateMachine sm;

static uint8_t syn_slot_used(uint8_t target, uint8_t slot)
{
    return (uint8_t)((syn_used[target][slot >> 3] >> (slot & 7u)) & 1u);
}

#include "preset_store_fm1.c"                      /* the SAVEs in flash */
#include "factory_presets.c"                       /* the factory bank, STONE & FIRE */

static uint8_t syn_slot_query(void *ctx, uint8_t target, uint8_t slot, uint8_t name[NAME_LEN])
{
    (void)ctx;
    if (target >= UISAVE_COUNT || slot >= ui_sm_bank_slots(target) || !syn_slot_used(target, slot))
        return UISLOT_EMPTY;
    if (target == UISAVE_PATCH_PRESET) memcpy(name, patch_bank[slot].name, NAME_LEN);
    else if (target == UISAVE_WHOLE_PERF) memcpy(name, syn_perf_name[slot], NAME_LEN);
    return UISLOT_USED;
}

/* the machine's commit_pending: save_target into save_slot, its chunk marked for the preset store (inside syn_lock);
 * ps_tick reports it done once it is in flash. A performance: the live one and everything it plays, staged whole */
static void syn_commit(void)
{
    uint8_t t = sm.save_target, s = sm.save_slot;
    if (ps_commit_wait)
        return;                                     /* (the one before is still being written) */
    if (t >= UISAVE_COUNT || s >= ui_sm_bank_slots(t)) {
        ui_sm_commit_done(&sm, 0u);
        return;
    }
    switch (t) {
    case UISAVE_PATCH_PRESET: patch_store(sm.s.active_synth_layer, s); break;
    case UISAVE_SEQ_SETTINGS: memcpy(syn_seq_bank[s], seq_lanes, sizeof syn_seq_bank[s]); break;
    case UISAVE_ARP_SEQ: syn_arp_bank[s] = arp_state; break;
    default:
        ps_perf_save.perf = perf_state;
        ps_perf_save.engine[0] = engine_state[0];
        ps_perf_save.engine[1] = engine_state[1];
        memcpy(ps_perf_save.seq, seq_lanes, sizeof ps_perf_save.seq);
        ps_perf_save.arp = arp_state;
        ps_perf_save_slot = s;
        memcpy(syn_perf_name[s], perf_state.name, NAME_LEN);
        break;
    }
    syn_used[t][s >> 3] |= (uint8_t)(1u << (s & 7u));
    ps_dirty |= 1u << (PS_KIND[t].first + s / PS_PER(PS_KIND[t].stored));
    ps_commit_wait = 1;
}

/* the machine's load_pending (the Preset Encoder): load_target from load_slot into the live state (inside syn_lock). A
 * performance waits for the main loop (syn_perf_tick): it is read from flash */
static void syn_load(void)
{
    uint8_t t = sm.load_target, s = sm.load_slot, ok;
    ok = (uint8_t)(t < UISAVE_COUNT && s < ui_sm_bank_slots(t) && syn_slot_used(t, s));
    if (ok && t == UISAVE_WHOLE_PERF) {
        syn_perf_req = 1;
        return;
    }
    if (ok) {
        switch (t) {
        case UISAVE_PATCH_PRESET: patch_load(sm.load_layer, s); break;
        case UISAVE_SEQ_SETTINGS: memcpy(seq_lanes, syn_seq_bank[s], sizeof seq_lanes); break;
        default: arp_state = syn_arp_bank[s]; break;
        }
    }
    ui_sm_load_done(&sm, ok);
}

/* the main loop's: the performance load syn_load left. Its slot from flash, outside syn_lock (a factory slot never
 * saved over: made, factory_perf), then the live state takes it whole inside the lock: the performance's settings, both
 * layers' patches, the sequence and the arp pattern. (The Preset Encoder waits: load_pending holds load_slot.) */
static void syn_perf_tick(void)
{
    uint8_t ok;
    if (!syn_perf_req)
        return;
    ok = (uint8_t)(ps_perf_read(sm.load_slot, &syn_perf_load) == 0 || factory_perf(sm.load_slot, &syn_perf_load) == 0);
    syn_lock();
    if (ok) {
        perf_state = syn_perf_load.perf;
        engine_state[0] = syn_perf_load.engine[0];
        engine_state[1] = syn_perf_load.engine[1];
        memcpy(seq_lanes, syn_perf_load.seq, sizeof seq_lanes);
        arp_state = syn_perf_load.arp;
    }
    ui_sm_load_done(&sm, ok);
    syn_unlock();
    syn_perf_req = 0;
}

/* ------------------------------------------------------------ editor --- */
/* The editor protocol's frame (F0 7D 46 4C cmd args F7, felucca-firmware/web/EDITOR_PROTOCOL.md), X0X's helpers: INFO,
 * PING, and the wavetable store's commands (src/hardware/wt_store_fm1.c: WT_BEGIN .. WT_INFO, and the sample-slot
 * commands, refused: the slots hold the wavetables). */
#define ED_HDR0 0x7D
#define ED_HDR1 0x46
#define ED_HDR2 0x4C
enum { ED_INFO = 1, ED_SMP_BEGIN = 11, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO, ED_SMP_READ, ED_PING = 25 };
enum { ED_PERFT = 44 };                             /* X0X's number for its own PERF TEST (app/editor.c) */
/* the audio's health while it plays (2026-10-16, fm1_perf.py stats): halves rendered, late halves, the slowest half
 * (us), the load now and its peak (%), voices shed and the voices held off now; [1] then zeroes the counts */
enum { ED_STATS = 46 };
/* debugging (2026-10-16, the reverb buzz): [1, slot] load that performance (as the Preset Encoder does); [2, 0 / 1]
 * the reverb's back on core 1 / core 0; [3] the effects reset (fx_init) -> rc 0 */
enum { ED_DEBUG = 47 };

static uint8_t ed_out[288];
static uint32_t ed_n;

static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0;
    ed_out[1] = ED_HDR0;
    ed_out[2] = ED_HDR1;
    ed_out[3] = ED_HDR2;
    ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
static void ed_str(const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
    ota_wire_send(ed_out, ed_n);
}
static void keys_release(void);

#include "wt_store_fm1.c"                          /* (the upload commands, refused: the image is in the firmware) */

static void ed_handle(const uint8_t *f, uint32_t n)
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    ed_begin(cmd);
    if (ed_wt_handle(cmd, a, na)) {
        ed_send();
        return;
    }
    switch (cmd) {
    case ED_INFO:
        ed_str("CARNAC OS " SYNTH_VERSION, 24);
        ed_b(0);                                    /* no engines / parameters over this protocol */
        ed_b(0);
        ed_b(0);
        ed_b(SEQ_NSTEP);
        ed_b(0);
        ed_b(0);
        ed_b(plat_cpu2_on() ? 2u : 1u);             /* the cores rendering (last two bytes, fm1_perf.py) */
        ed_b((uint32_t)plat_cpu2_gave_up());        /* why the second is off: 1 bad reads, 2 a job unfinished, 3 barred */
        break;
    case ED_PING:
        ed_b(0);
        break;
    case ED_DEBUG:
        syn_lock();
        if (na >= 2u && a[0] == 1u) {
            sm.load_target = UISAVE_WHOLE_PERF;
            sm.load_slot = a[1];
            sm.load_layer = 0;
            syn_load();
        } else if (na >= 2u && a[0] == 2u) {
            synth_fx_one_core(a[1]);
        } else if (na >= 1u && a[0] == 3u) {
            fx_init();
        }
        syn_unlock();
        ed_b(0);
        break;
    case ED_STATS: {                                /* + the levels since the last read and the live settings */
        uint32_t v[29], i, k, nv = 0, c1 = 0;
        int32_t lv[4];
        synth_levels(lv);
        for (i = 0; i < POLYPHONY_VOICES; i++)
            nv += !voice_is_free(&poly_bank[i]);
        v[0] = syn_dbg.halves; v[1] = syn_dbg.late; v[2] = syn_dbg.max_us; v[3] = audio_cpu_pct;
        v[4] = audio_peak_pct; v[5] = shed_count; v[6] = voice_shed;
        v[7] = (uint32_t)lv[0]; v[8] = (uint32_t)lv[1]; v[9] = (uint32_t)lv[2]; v[10] = nv;
        v[11] = perf_state.reverb_type; v[12] = perf_state.reverb_decay; v[13] = perf_state.reverb_damp;
        v[14] = perf_state.reverb_predelay; v[15] = perf_state.delay_type; v[16] = perf_state.delay_time;
        v[17] = perf_state.delay_feedback; v[18] = perf_state.delay_degrade; v[19] = perf_state.delay_pingpong;
        v[20] = engine_state[0].amp.reverb_send; v[21] = engine_state[0].amp.delay_send;
        v[22] = engine_state[1].amp.reverb_send; v[23] = engine_state[1].amp.delay_send; v[24] = perf_state.perf_mode;
        v[25] = engine_state[0].arch; v[26] = engine_state[1].arch;
        while (c1 < FM1_C1_STACK_WORDS && _c1_ustack[c1] == FM1_C1_MARK)
            c1++;                                   /* core 1's stack: the words never touched, from the bottom */
        v[27] = (uint32_t)lv[3]; v[28] = (FM1_C1_STACK_WORDS - c1) * 4u;
        for (i = 0; i < 29u; i++)
            for (k = 0; k < 5u; k++)
                ed_b(v[i] >> (7u * k));               /* 35 bits each, 7 at a time, low first */
        if (na && a[0] == 1u) {
            syn_dbg.late = 0; syn_dbg.max_us = 0; audio_peak_pct = 0; shed_count = 0;
        }
        break;
    }
    case ED_PERFT:                                  /* the PERF TEST: [] start; [3] start, wavetables in RAM;
                                                     * [1] where it is; [2, i] scenario i */
        if (na == 0u || a[0] == 3u) {
            syn_lock();
            keys_release();                         /* the test restarts every voice: no key may hang on */
            pt_start(fm1_ms, na && a[0] == 3u ? PT_WT_RAM : 0u);
            syn_unlock();
            ed_b(1);
        } else if (a[0] == 1u) {
            ed_b(pt_state());
            ed_b(pt_scenario());
            ed_b(pt_count());
            ed_b(pt_mode());
        } else if (a[0] == 2u && na >= 2u && a[1] < pt_count()) {
            const PtResult *r = pt_result(a[1]);    /* 14-bit values as two 7-bit bytes, low first */
            ed_b(a[1]);
            ed_b(r->avg_pm);
            ed_b(r->avg_pm >> 7);
            ed_b(r->peak_pm);
            ed_b(r->peak_pm >> 7);
            ed_b(r->late);
            ed_b(r->late >> 7);
            ed_b(r->ui_fps10);
            ed_b(r->ui_fps10 >> 7);
            ed_b(r->ui_max_ms > 127u ? 127u : r->ui_max_ms);
            ed_b(r->voices);
            ed_b(r->over);
            ed_str(pt_name(a[1]), 20);
#if defined(SYNTH_PROF)
        } else if (a[0] == 5u) {                    /* the operation costs: ticks for MB_N of each, 21-bit */
            ed_b(plat_cycles_cpu() ? 1u : 0u);
            ed_b(MB_N / 100u);
            ed_b(MB_COUNT);
            for (i = 0; i < MB_COUNT; i++) {
                uint32_t t = mb_time(i);
                ed_b(t);
                ed_b(t >> 7);
                ed_b(t >> 14);
            }
#endif
        } else if (a[0] == 4u && na >= 2u && a[1] < pt_count()) {
            const PtResult *r = pt_result(a[1]);    /* the profile: each stage's ticks a sample, 14-bit */
#if defined(SYNTH_PROF)
            ed_b(a[1]);
            ed_b(plat_cycles_cpu() ? 1u : 0u);      /* 1: core cycles; 0: TIMER4 ticks (24 MHz) */
            ed_b(pt_lap_cost() > 127u ? 127u : pt_lap_cost());
            ed_b(SPROF_N);
            for (i = 0; i < SPROF_N; i++) {
                uint32_t c = r->cyc[i] > 16383u ? 16383u : r->cyc[i];
                ed_b(c);
                ed_b(c >> 7);
            }
#else
            (void)r;
            ed_b(a[1]);
            ed_b(0);
            ed_b(0);
            ed_b(0);                                /* no stages: not a profile build */
#endif
        } else {
            return;
        }
        break;
    case ED_SMP_INFO:                               /* the three slots, empty: they hold the wavetables */
        ed_b(SMP_USER_SLOTS);
        ed_b(SMP_USER_SIZE / 1024u);
        for (i = 0; i < SMP_USER_SLOTS; i++) {
            ed_b(0);
            ed_str("", 8);
            ed_b(0);
        }
        break;
    default:
        return;                                     /* not ours: no reply */
    }
    ed_send();
}

static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;                                     /* not an editor frame: ota_service() reads it */
    if ((p[3] >= ED_SMP_BEGIN && p[3] <= ED_SMP_READ) || (p[3] >= ED_WT_BEGIN && p[3] <= ED_WT_INFO)) {
        ed_handle(p, n);                            /* long frames (data): handled in place */
        ota_frame_done();
        return;
    }
    {
        static uint8_t f[64];
        uint32_t k = n > sizeof f ? sizeof f : n, i;
        for (i = 0; i < k; i++)
            f[i] = p[i];
        ota_frame_done();
        ed_handle(f, k);
    }
}

/* ------------------------------------------------------------- panel --- */
#define KEY_F3 53u                                  /* the lowest key at octave 0 */
#define OCT_MIN (-3)
#define OCT_MAX 3
#define NOTE_NONE 0xFFu
static int32_t octave;
static uint32_t keys_prev, btn_prev;
static uint8_t key_note[NKEYS];                     /* the note key k played (its note-off goes there), NOTE_NONE */
static uint8_t oct_held[2], oct_used[2];            /* OCT- / OCT+ down, and used as a modifier since */
static uint8_t home_held, home_chord;               /* HOME down; an OCT pressed since (a layer chord, not a page) */
static uint32_t home_ms;                            /* when HOME went down */
static uint8_t save_held, save_chord;               /* the same for SAVE: an OCT since = COPY / PASTE, not a save */
static uint32_t save_ms;
#define HOME_HINT_MS 200u                           /* HOME held this long: the OCT LEDs show the layer chord */
static uint32_t knob_last_ms[4];
static uint32_t bend_last_ms;                       /* the last PITCH BEND detent (HOME knob 2): its spring waits for it */
#define BEND_SPRING_WAIT_MS 150u                    /* no detent this long: the bend glides back to centre */

static uint8_t base_note(void) { return (uint8_t)((int32_t)KEY_F3 + 12 * octave); }
static uint8_t keys_page(void) { return sm.page == UIPAGE_SEQ || sm.page == UIPAGE_ARP; }
static uint8_t key_vel(void)
{
    uint8_t v = sm.perf_knob[UIPERF_VELOCITY];
    return v ? v : 1u;                              /* a note-on with velocity 0 is a note-off */
}

/* every note the keys are sounding off (before the octave moves: no note may hang) */
static void keys_release(void)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (key_note[k] != NOTE_NONE) {
            synth_midi_3(0x80, key_note[k], 0);
            key_note[k] = NOTE_NONE;
        }
}

static void octave_shift(int32_t d)
{
    int32_t o = octave + d;
    if (o < OCT_MIN || o > OCT_MAX)
        return;
    keys_release();
    octave = o;
}

/* one key edge, as the browser's worklet takes it: on SEQ / ARP the machine first (step selectors, loop shortcuts,
 * chord and step keys), else (or when it does not take it) played */
static void key_edge(uint32_t k, uint32_t down)
{
    uint8_t note = (uint8_t)(base_note() + k), taken = 0, mods;
    if (keys_page()) {
        mods = (uint8_t)((oct_held[0] ? UISEQ_MOD_OCT_DOWN : 0u) | (oct_held[1] ? UISEQ_MOD_OCT_UP : 0u));
        if (oct_held[0]) oct_used[0] = 1;
        if (oct_held[1]) oct_used[1] = 1;
        taken = (uint8_t)(ui_sm_seq_key(&sm, (uint8_t)k, note, (uint8_t)down, mods) ||
                          ui_sm_arp_key(&sm, (uint8_t)k, note, (uint8_t)down, key_vel()));
    }
    if (down && !taken && key_note[k] == NOTE_NONE) {
        synth_midi_3(0x90, note, key_vel());
        key_note[k] = note;
    } else if (!down && key_note[k] != NOTE_NONE) {  /* released the way it was pressed */
        synth_midi_3(0x80, key_note[k], 0);
        key_note[k] = NOTE_NONE;
    }
}

/* a knob turned d detents: the page's knob map for the focused layer (ui_sm_knob), as web_knob_set has it with a
 * relative step. Fast turns (detents < 25 ms apart) move 4 a detent. */
static void knob_turn(uint32_t k, int32_t d)
{
    UiKnobInfo info;
    uint8_t *p = ui_sm_knob(&sm, sm.page, sm.s.active_synth_layer, (uint8_t)k, &info);
    int32_t v, hi;
    if (fm1_ms - knob_last_ms[k] < 25u)
        d *= 4;
    knob_last_ms[k] = fm1_ms;
    if (info.kind == UIKNOB_SEQ) {
        ui_sm_seq_knob_turn(&sm, (uint8_t)k, (int8_t)(d > 127 ? 127 : d < -127 ? -127 : d));
        return;
    }
    if (!p)
        return;
    hi = info.n_enum ? (int32_t)info.n_enum - 1 : (int32_t)VAL_MAX;
    if (info.n_enum)                                /* a list moves one entry a detent */
        d = d > 0 ? 1 : -1;
    v = (int32_t)*p + d;
    v = v < 0 ? 0 : v > hi ? hi : v;
    *p = (uint8_t)v;
    if (info.kind == UIKNOB_PERF) {                 /* LAYER: the play controllers; velocity stays here (key_vel) */
        uint32_t pk = (uint32_t)(p - sm.perf_knob);
        if (pk == UIPERF_BEND) {
            synth_midi_3(0xE0, 0, (uint8_t)v);
            bend_last_ms = fm1_ms;
        } else if (pk == UIPERF_MODWHEEL) {
            synth_midi_3(0xB0, 1, (uint8_t)v);
        } else if (pk == UIPERF_CC1 || pk == UIPERF_CC2) {
            synth_custom_cc((uint8_t)(pk - UIPERF_CC1), (uint8_t)v);   /* the CUSTOMCC1 / 2 mod sources */
        }
    }
    ui_sm_knob_written(&sm, sm.page, (uint8_t)k);
}

/* PITCH BEND is spring-loaded, as a wheel is: BEND_SPRING_WAIT_MS after its last detent it glides back to centre, a
 * third of the way each UI frame (~0.2 s from full throw), sent as MIDI as the knob sends it. Once per UI frame,
 * inside the lock. */
static void bend_spring(void)
{
    int32_t v = sm.perf_knob[UIPERF_BEND], d = (int32_t)BIPOLAR_CENTER - v;
    if (d == 0 || fm1_ms - bend_last_ms < BEND_SPRING_WAIT_MS)
        return;
    v += d > 0 ? (d + 2) / 3 : (d - 2) / 3;
    sm.perf_knob[UIPERF_BEND] = (uint8_t)v;
    synth_midi_3(0xE0, 0, (uint8_t)v);
}

static int8_t detents8(int32_t d) { return (int8_t)(d > 127 ? 127 : d < -127 ? -127 : d); }

/* the panel since the last call: buttons, encoders, knobs, keys. Main loop; every machine / synth call inside the
 * lock. Returns 1 when anything happened. */
static uint32_t panel_input(void)
{
    static const uint8_t SECTION[NB] = {
        BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO, BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ,
        0xFF, 0xFF, 0xFF, 0xFF,                     /* PLAY, REC, OCT-, OCT+: below */
    };
    uint32_t btn = plat_buttons(), down = btn & ~btn_prev, up = ~btn & btn_prev;
    uint32_t keys = plat_keys(), kch = keys ^ keys_prev, i, any = (down | up | kch) != 0u;
    int32_t e[NE];
    for (i = 0; i < NE; i++) {
        e[i] = plat_enc((int)i);
        any |= e[i] != 0;
    }
    btn_prev = btn;
    keys_prev = keys;
    if (!any)
        return 0;
    syn_lock();
    for (i = 0; i < NB; i++)
        if ((down >> i) & 1u) {
            if (i == B_HOME) {                      /* acts on release (below), unless an OCT makes it a chord */
                home_held = 1;
                home_chord = 0;
                home_ms = fm1_ms;
            } else if (i == B_SAVE) {               /* acts on release too, unless an OCT makes it COPY / PASTE */
                save_held = 1;
                save_chord = 0;
                save_ms = fm1_ms;
            } else if (home_held && (i == B_OCTDN || i == B_OCTUP)) {
                home_chord = 1;                     /* the layer, not the octave (refused for L2 under SINGL) */
                ui_sm_set_layer_focus(&sm, (uint8_t)(i == B_OCTUP));
            } else if (save_held && (i == B_OCTDN || i == B_OCTUP)) {
                save_chord = 1;                     /* OCT-: COPY, OCT+: PASTE (asks first) */
                ui_sm_save_oct(&sm, (uint8_t)(i == B_OCTUP));
            } else if (SECTION[i] != 0xFFu)
                ui_sm_handle(&sm, UIEV_BUTTON, (int8_t)SECTION[i]);
            else if (i == B_PLAY)
                ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
            else if (i == B_REC)
                ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
            else {                                  /* OCT- / OCT+: off the SEQ page they act at once */
                uint32_t w = i == B_OCTUP;
                oct_held[w] = 1;
                oct_used[w] = 0;
                if (sm.page != UIPAGE_SEQ) {
                    octave_shift(w ? 1 : -1);
                    oct_used[w] = 1;
                }
            }
            if (sm.commit_pending)
                syn_commit();
        }
    if (((up >> B_SAVE) & 1u) && save_held) {
        save_held = 0;
        ui_sm_set_hold(&sm, BTN_COUNT);
        if (!save_chord) {
            ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);
            if (sm.commit_pending)
                syn_commit();
        }
    }
    if (((up >> B_HOME) & 1u) && home_held) {
        home_held = 0;
        ui_sm_set_hold(&sm, BTN_COUNT);
        if (!home_chord) {
            ui_sm_handle(&sm, UIEV_BUTTON, BTN_HOME);
            if (sm.commit_pending)
                syn_commit();
        }
    }
    for (i = B_OCTDN; i <= B_OCTUP; i++)
        if ((up >> i) & 1u) {                       /* SEQ: a tap that set no loop boundary moves the octave */
            uint32_t w = i == B_OCTUP;
            if (oct_held[w] && !oct_used[w])
                octave_shift(w ? 1 : -1);
            oct_held[w] = 0;
        }
    if (e[EN_SELECT])
        ui_sm_handle(&sm, UIEV_SELECT_ENC, detents8(e[EN_SELECT]));
    if (e[EN_ALGO])
        ui_sm_handle(&sm, UIEV_ALGO_ENC, detents8(e[EN_ALGO]));
    if (e[EN_PRESET] && !home_held) {               /* (HOME held: a slip of the hand, not a load) */
        ui_sm_handle(&sm, UIEV_PRESET_ENC, detents8(e[EN_PRESET]));
        if (sm.load_pending)
            syn_load();
    }
    for (i = 0; i < 4u; i++)
        if (e[EN_K1 + i])
            knob_turn(i, e[EN_K1 + i]);
    for (i = 0; i < NKEYS; i++)
        if ((kch >> i) & 1u)
            key_edge(i, (keys >> i) & 1u);
    syn_unlock();
    return 1;
}

/* the LEDs: the open section's button alone (no glow on the others), PLAY (green; red while recording), the keys the
 * SEQ / ARP pages give a job (ui_sm_key_leds: its "mid" level shows lit, X0X's LEDs have two levels), and the keys
 * held. SAVE flashes while a press of it would act: on the save page with an ACTION armed (SAVE by default), and
 * under the confirm popup */
static uint32_t page_button(uint8_t page)
{
    if (page <= UIPAGE_OSCMIX) return 1u << B_FX;
    if (page <= UIPAGE_FLTMIX) return 1u << B_SEL;
    if (page <= UIPAGE_ENV3) return 1u << B_ENV;
    if (page <= UIPAGE_LFO3) return 1u << B_LFO;
    if (page <= UIPAGE_MOD3) return 1u << B_EDIT;
    if (page <= UIPAGE_EFFECTS) return 1u << B_GLO;
    if (page <= UIPAGE_GLOBAL) return 1u << B_HOME;                   /* HOME's views, then GLOBAL */
    if (page == UIPAGE_ARP) return 1u << B_ARP;
    if (page == UIPAGE_SEQ) return 1u << B_SEQ;
    return 1u << B_SAVE;
}

static void panel_leds(void)
{
    UiKeyLeds kl;
    uint32_t b = page_button(sm.page), rec = maq_recording(synth_seq()) || sm.arp_rec;
    uint8_t mods = (uint8_t)((oct_held[0] ? UISEQ_MOD_OCT_DOWN : 0u) | (oct_held[1] ? UISEQ_MOD_OCT_UP : 0u));
    if (ui_sm_play_lit(&sm) || rec)
        b |= 1u << B_PLAY;
    if (rec)
        b |= 1u << B_REC;
    if (octave < 0) b |= 1u << B_OCTDN;
    if (octave > 0) b |= 1u << B_OCTUP;
    if (home_held && fm1_ms - home_ms >= UI_HOLD_MS)   /* the hold strip names what OCT- / OCT+ do */
        ui_sm_set_hold(&sm, BTN_HOME);
    else if (save_held && fm1_ms - save_ms >= UI_HOLD_MS)
        ui_sm_set_hold(&sm, BTN_SAVE);
    if (home_held && fm1_ms - home_ms >= HOME_HINT_MS) {   /* the layer chord: focused lit, the other flashing */
        uint32_t on = sm.s.active_synth_layer ? 1u << B_OCTUP : 1u << B_OCTDN, other = (1u << B_OCTDN | 1u << B_OCTUP) & ~on;
        b &= ~(1u << B_OCTDN | 1u << B_OCTUP);
        b |= on;
        if (sm.perf->perf_mode != PERF_SINGL_SYNTH && ((fm1_ms / 125u) & 1u))   /* 4 Hz; SINGL has no L2 */
            b |= other;
    }
    if (ui_sm_save_armed(&sm)) {
        b &= ~(1u << B_SAVE);
        if ((fm1_ms / 250u) & 1u)                   /* 2 Hz */
            b |= 1u << B_SAVE;
    }
    if (!ui_sm_key_leds(&sm, base_note(), mods, &kl))
        kl.lit = kl.mid = kl.dim = 0;
    plat_glow(0);
    plat_play_red((int)rec);
    plat_keys_dim(kl.dim);
    plat_leds(b, kl.lit | kl.mid | keys_prev);
    if (sm.page == UIPAGE_ARP && kl.dim)            /* the ARP's chord steps at half brightness, not X0X's quarter */
        fm1_led_dim_div = 2u;                       /* (2026-10-21: too faint to read; the playhead stays full) */
}

/* ------------------------------------------------------------- boot --- */
#define BOOTGUARD_MAGIC 0x42475244u
struct bootguard_s bootguard __attribute__((section(".noinit")));   /* declared in x0x.c (ota_commit) */

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    if (fm1_c1_on)
        fm1_cpu1_bar();
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "CARNAC CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, C_WHITE, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

/* SAFE MODE: the synth crashed (or hung) twice while starting. Nothing that makes sound runs: no audio, no synth, no
 * UI; USB is on, so the web installer (this firmware, X0X, Felucca or the stock one) and M-UPGRADE reach it. PLAY
 * tries again. The chip's own update mode stays the last resort (fm1_cstart). X0X's, word for word but the name. */
static void safe_main(void)
{
    char b[24];
    uint32_t t0, play, both, t_oct;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_B, "CARNAC SAFE MODE", C_HI, 1);
    draw_text_box(0, 52, 240, &FONT_S, "IT CRASHED TWICE", C_WHITE, 1);
    draw_text_box(0, 72, 240, &FONT_S, "WHILE STARTING.", C_WHITE, 1);
    draw_text_box(0, 102, 240, &FONT_S, "NO SOUND. USB IS ON:", C_WHITE, 1);
    draw_text_box(0, 122, 240, &FONT_S, "REINSTALL FROM THE", C_WHITE, 1);
    draw_text_box(0, 142, 240, &FONT_S, "WEB INSTALLER.", C_WHITE, 1);
    draw_text_box(0, 172, 240, &FONT_S, "PLAY: TRY AGAIN", C_HI, 1);
    draw_text_box(0, 190, 240, &FONT_S, "HOLD OCT- + OCT+: UPDATE", C_GRAY, 1);
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 208, 240, &FONT_S, b, C_GRAY, 1);
    }
    fm1_input_init();
    panel_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 1);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    play = 1u << panel.btn[B_PLAY];
    both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    t0 = t_oct = fm1_ms;
    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (bootguard.pending && fm1_ms - t0 > 10000u)
            bootguard.pending = 0;                  /* safe mode itself is up: not another failed boot */
        if ((fm1_in.buttons & play) && fm1_ms - t0 > 500u) {
            bootguard.failed = 0;                   /* try again, from scratch */
            bootguard.pending = 0;
            draw_text_box(0, 172, 240, &FONT_S, "STARTING...", C_HI, 1);
            usb_detach();
            fm1_delay_ms(30);
            fm1_reboot();
        }
        if ((fm1_in.buttons & both) != both)
            t_oct = fm1_ms;
        else if (fm1_ms - t_oct > 5000u)
            enter_uboot("UBOOT");
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                      /* an install commits and resets; else back here */
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
    }
}

/* ------------------------------------------------------- PERF TEST --- */
/* during the test the panel only stops it: any button, key or encoder (the knobs are encoders too) */
static void pt_panel(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), i, any = ((btn & ~btn_prev) | (keys & ~keys_prev)) != 0u;
    for (i = 0; i < NE; i++)
        any |= plat_enc((int)i) != 0;
    btn_prev = btn;
    keys_prev = keys;
    if (any) {
        syn_lock();
        pt_stop();
        syn_unlock();
    }
}

/* one step of the test; shows the page each scenario asks for, and the page from before once it is over */
static void pt_main(uint32_t *force)
{
    static uint8_t on, page0;
    uint8_t pg;
    if (!on) {
        if (pt_state() != PT_RUN)
            return;
        on = 1;
        page0 = sm.page;
    }
    syn_lock();
    pg = pt_tick(fm1_ms);
    if (pt_state() != PT_RUN)
        pg = page0;
    if (pg != PT_PAGE_KEEP && pg != sm.page) {
        ui_sm_goto(&sm, pg);
        *force = 1;
    }
    syn_unlock();
    if (pt_state() != PT_RUN) {
        on = 0;
        *force = 1;
        btn_prev = plat_buttons();                  /* what is held now is no press */
        keys_prev = plat_keys();
    }
}

#define SPLASH_MS 1500u                             /* the start-up screen shows at least this long */
static uint32_t splash_t0;

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t last_frame = 0, k, force = 1;
    {
        uint32_t f = irq_save();
        fm1_vdc14_raise(5);                         /* 1.50 V before anything loads it (fm1_sys.h) */
        plat_vdc14_level = (uint8_t)fm1_vdc14();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else RAM only */
        irq_restore(f);
        if (flash_ok) {
            fl_plain_window_init();                 /* the store reads as plaintext through XIP */
            ota_boot_cleanup();
        }
    }
    lcd_init();
    palette_set(1);
    if (syn_dbg.magic != DBG_MAGIC) {
        memset(&syn_dbg, 0, sizeof syn_dbg);
        syn_dbg.magic = DBG_MAGIC;
    }
    if (safe_mode)
        safe_main();                                /* never returns */
    carnac_splash(SYNTH_VERSION);                   /* CARNAC OS (src/carnac_splash.c); never in safe mode, which runs
                                                     * as little as it can */
    splash_t0 = fm1_ticks();
    if (fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != syn_dbg.crash_seen) {
        char b[16];                                 /* a crash since the last boot: say so, a moment */
        syn_dbg.crash_seen = fm1_crash.count;
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 176, 240, &FONT_S, "RESTARTED AFTER A CRASH", RGB(255, 80, 60), 1);
        draw_text_box(0, 196, 240, &FONT_S, b, C_GRAY, 1);
        fm1_wdt_feed();
        fm1_delay_ms(2500);
        fm1_wdt_feed();
    }
    syn_dbg.boots++;
    syn_dbg.max_us = 0;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    led_pos_init();
    plat_perf_init();

    syn_dbg.stage = 1;                              /* breadcrumbs: a WDT reset leaves the last one (`fm1t memr`) */
    synth_init();
    syn_dbg.stage = 2;
    syn_dbg.stage = 3;
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], seq_lanes, &arp_state);
    ui_sm_attach_seq(&sm, synth_seq());
    ui_sm_attach_arp(&sm, synth_arp());
    ui_sm_attach_store(&sm, syn_slot_query, 0);
    ui_sm_attach_instrument(&sm, &instrument);
    seq_palette_set(SQPAL_ORANGE);
    settings_load();                                /* the THEME of the last session */
    factory_install();                              /* the factory bank (slots 0..31), under whatever is saved */
    ps_load();                                      /* the saved patches, performances, sequences, arps */
    for (k = 0; k < NKEYS; k++)
        key_note[k] = NOTE_NONE;
    syn_dbg.stage = 4;

#if !defined(SYNTH_PROF)                            /* the profile build: one core (its stage counters are shared) */
    if (!plat_cpu2_barred() && !bootguard.failed && plat_cpu2_set(1) == 0)
        synth_set_core2(plat_cpu2_run, plat_cpu2_wait);   /* the second core renders half the voices (synth_runtime.c
                                                     * CORE1_VOICES). Started at power-on, before the audio and the
                                                     * timers, as X0X / Melodee start it (later, it does not reach
                                                     * its entry); barred after a crash beside it or a failed boot:
                                                     * one core, the same sound */
#endif
    audio_init();
    usb_start();
#if X0X_TRS
    uart_midi_init();                               /* before TIMER5: it read-modify-writes port H too */
#endif
    fm1_timer5_start(isr_timer5, 4);                /* above ALNK0 (3): the panel scan keeps its rhythm */
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u)
        panel_setup();                              /* OCT- + OCT+ held at power-on: HARDWARE CALIBRATION */
    fm1_delay_ms(300);
    while ((uint32_t)(fm1_ticks() - splash_t0) < SPLASH_MS * 1000u * FM1_TICKS_PER_US)
        fm1_wdt_feed();                             /* the splash stays up SPLASH_MS in all (calibration: after it) */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    btn_prev = plat_buttons();                      /* what is held at the start is no press */
    keys_prev = plat_keys();
    syn_dbg.stage = 5;

    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        settings_tick();
        ps_tick();
        syn_perf_tick();                            /* (after ps_tick: a SAVE before it is in flash) */
        if (fm1_ms > 30000u && bootguard.pending) { /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* the battery: a slow average (plat_battery) */
            if (b > 0)
                batt_raw = batt_raw ? batt_raw + (b - batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                master_q12 = (k10 * k10) >> 8;      /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s: update mode (Felucca's and X0X's, unchanged) */
            static uint32_t t0, shown;
            uint32_t both = (1u << B_OCTDN) | (1u << B_OCTUP);
            if ((plat_buttons() & both) != both) {
                if (shown)
                    force = 1;                      /* the countdown covered the screen */
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[24] = "UPDATE MODE IN 5..";
                    d[15] = (char)('0' + left);
                    draw_text_box(0, 110, 240, &FONT_S, d, C_WHITE, 1);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                enter_uboot("UBOOT");
            }
        }
        ed_service();                               /* the wavetable upload SysEx */
        ota_service();                              /* M-UPGRADE handshake */
        if (usb.ota_req) {
            usb.ota_req = 0;
            syn_lock();
            maq_set_playing(synth_seq(), 0u);
            carp_set_playing(synth_arp(), 0u);
            keys_release();
            syn_unlock();
            if (flash_ok)
                ota_session();                      /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            force = 1;
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
#if X0X_CDC
        cdc_task();
#endif
        if (pt_state() == PT_RUN)                   /* the PERF TEST owns the synth: a press only stops it */
            pt_panel();
        else
            panel_input();
        pt_main(&force);
        if (fm1_ms - last_frame >= 16u) {           /* ~60 frames/s at most; input in between */
            uint32_t ms = fm1_ms - last_frame;
            last_frame = fm1_ms;
            syn_dbg.ui_frames++;
            syn_lock();
            ui_sm_tick(&sm, (uint16_t)(ms > 1000u ? 1000u : ms));   /* the popups' timeouts */
            bend_spring();
            syn_unlock();
            {
                uint32_t t0 = fm1_ticks();
                ui_screen_draw(&sm, synth_seq(), synth_arp(), force);
                pt_ui_frame((fm1_ticks() - t0) / FM1_TICKS_PER_US);
            }
            force = 0;
            panel_leds();
        }
    }
}

/* X0X's, unchanged: halt a second core left running, the watchdog first, the boot-loop guard, RAM set up, the guards */
void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_cpu1_halt();
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 4u) {                   /* safe mode failed too: the chip's own update mode */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    p3 |= (bootguard.failed >= 2u) << 8;            /* two failed boots: safe mode (kept past the .bss clear) */
    fm1_irq_init();
    irq_stack_mark();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    {
        uint32_t *end = (uintptr_t)_rt_text_end ? _rt_text_end : _rt_end;
        for (s = _rt_load, d = _rt_start; d < end; s++, d++)
            *d = *s;
    }
    for (s = _c1_load, d = _c1_start; d < _c1_end; s++, d++)
        *d = *s;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    safe_mode = (uint8_t)(p3 >> 8);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
