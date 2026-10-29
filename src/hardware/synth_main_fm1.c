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
 * Who touches the synth: the audio ISR (ALNK0) renders and drains USB / TRS MIDI into synth_midi_at (each event timed
 * to its frame). The main loop reads the panel and calls the UI state machine, which writes the patch bytes and drives
 * the MAQ / ARP; every such call runs inside syn_lock() (interrupts off, a few microseconds), so the render never sees
 * half an event. The screen drawing only reads, outside the lock.
 *
 * The panel (spec 2, 3, 5, 7, 8; src/hardware/synth_panel.c, which the browser emulator, web/emu, runs too):
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

/* The battery for the header's moon (ui_seq_draw.c sq_draw_moon): 0 .. 32 a 32nd of the charge each, -2 on a USB
 * host's power (plat_battery 4: there is no charger line), -1 not read yet. The stock thresholds (plat_battery: 531 /
 * 561 / 591 for 1/3, 2/3 and FULL, a third every 30) put empty at 501, so 90 counts make the 32 steps. A step changes
 * only once the main loop's average is a third of a step past its edge: a reading on an edge holds still. */
static int32_t carnac_battery_moon(void)
{
    static int32_t shown;
    int32_t q = (batt_raw - 501) * 32;              /* 90 a step */
    if (plat_battery() == 4)
        return -2;
    if (!batt_raw)
        return -1;
    while (shown > 0 && q < shown * 90 - 30)
        shown--;
    while (shown < 32 && q >= (shown + 1) * 90 + 30)
        shown++;
    return shown;
}

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

/* Tight MIDI in (2026-10-24). Every packet in X0X's MIDI-in queue (usb.c midi_in_q, fed by USB and the TRS jack in
 * the TIMER5 ISR) is stamped with the time it was read (midi_stamp), and the MIDI is read during the render too
 * (midi_in_render), so no stamp waits for a render to end. Each half then plays the packets read during the half
 * before it, each at its own frame (synth_midi_at): a constant 2 halves (23.2 ms) from arrival to sound, within the
 * 0.5 ms of the read, where it used to be 11.6..23.2 ms, every packet on a half's first frame. */
#define HALF_TICKS (HALF_FRAMES * 1000000u / 44118u * FM1_TICKS_PER_US)   /* a half at the I2S's 44,117.6 Hz */
static uint32_t midi_stamp_q[MQ], midi_stamp_w;   /* fm1_ticks() at each queued packet's read, by queue index */
static uint32_t half_t;                             /* fm1_ticks() at the start of the last half */

static void midi_stamp(void)                        /* TIMER5, after each read: the packets it queued */
{
    uint32_t now = fm1_ticks();
    while (midi_stamp_w != mi_w) {
        midi_stamp_q[midi_stamp_w % MQ] = now;
        midi_stamp_w++;
    }
}

/* TIMER5 during the render, which usb_poll must not run in: only the MIDI-in endpoint (its own RxPktRdy, not the
 * interrupt flags usb_poll reads) and the TRS ring. Nothing in the audio ISR touches the USB controller. */
static void midi_in_render(void)
{
    if (usb.up && usb.config)
        ep1_rx();
#if X0X_TRS
    uart_midi_poll();
#endif
}

/* The packets read before this half began (t0) into the synth, timed into it (USB-MIDI event packets, byte 0 = cable
 * / CIN). ISR only. */
static void midi_drain(uint32_t t0)
{
    uint32_t pkt, guard = 64, prev = half_t, span = t0 - half_t;
    half_t = t0;
    if (span < HALF_TICKS / 2u || span > 2u * HALF_TICKS) {   /* the first half, or one missed: a nominal one */
        span = HALF_TICKS;
        prev = t0 - HALF_TICKS;
    }
    while (guard-- && mi_r != mi_w) {
        uint32_t s = midi_stamp_q[mi_r % MQ], off = s - prev, delay, cin;
        uint8_t st;
        if ((int32_t)(s - t0) >= 0)
            break;                                  /* read in this half: the next one plays it */
        if ((int32_t)off < 0)
            off = 0;                                /* (left over from a missed half: at once) */
        delay = off >= span ? HALF_FRAMES - 1u : off * HALF_FRAMES / span;
        if (!plat_midi_in(&pkt))
            break;
        cin = pkt & 0x0Fu;
        st = (uint8_t)(pkt >> 8);
        if (cin >= 0x8u && cin <= 0xEu && st >= 0x80u && st < 0xF0u) {   /* a channel message */
            uint8_t d2 = (uint8_t)((pkt >> 24) & 0x7Fu);
            if ((st & 0xF0u) == 0x90u)
                d2 = instrument_velocity(d2);       /* GLOBAL > VEL CURVE (0 stays a note-off) */
            synth_midi_at(delay, st, (uint8_t)((pkt >> 16) & 0x7Fu), d2);
        }
        else if (cin == 0xFu && st >= 0xF8u)                               /* realtime: clock, start, stop */
            synth_midi_at(delay, st, 0, 0);
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
    midi_drain(t0);
    for (k = 0; k < HALF_FRAMES; k += REND_FRAMES) {
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
        if (syn_dbg.in_audio) {
            midi_in_render();                       /* MIDI in keeps its time through the render */
            midi_stamp();
        }
    }
    if (usb_due && !syn_dbg.in_audio) {             /* usb_poll shares the MIDI queues with the audio ISR */
        usb_due = 0;
        usb_poll();
#if X0X_TRS
        uart_midi_poll();
#endif
        midi_stamp();
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

/* the settings, the SAVE banks and the panel: shared with the browser emulator (src/host/carnac_web.c) */
#include "synth_panel.c"

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
#include "pcm_store_fm1.c"                         /* PCM waves in the free store flash: upload, attach */

static void ed_handle(const uint8_t *f, uint32_t n)
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    ed_begin(cmd);
    if (ed_wt_handle(cmd, a, na) || ed_pcm_handle(cmd, a, na)) {
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
    if ((p[3] >= ED_SMP_BEGIN && p[3] <= ED_SMP_READ) || (p[3] >= ED_WT_BEGIN && p[3] <= ED_PCM_INFO)) {
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
    carnac_splash(SYNTH_VERSION, settings_theme()); /* CARNAC OS (src/carnac_splash.c) in the last session's THEME; never
                                                     * in safe mode, which runs as little as it can */
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
    pcm_boot();                                     /* the PCM waves uploaded to the free store flash */
    if (pcm_waves_state != PCM_WAVES_OK) {          /* the firmware came without its waves (the .fwsc alone): the
                                                     * splash again, its field empty of stones, and why, a moment */
        sp_notice[0] = pcm_waves_state == PCM_WAVES_MISSING ? "WAVES MISSING" : "WAVES OUT OF DATE";
        sp_notice[1] = "RUN THE INSTALLER";
        carnac_splash(SYNTH_VERSION, settings_theme());
        sp_notice[0] = 0;
        fm1_wdt_feed();
        fm1_delay_ms(3000);
        fm1_wdt_feed();
    }
    syn_boot_perf();                                /* performance 0 (NEWGRANGE DAWN): what the first key plays */
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
        ed_service();                               /* the editor SysEx (PCM wave upload, STATS, PERF TEST) */
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
