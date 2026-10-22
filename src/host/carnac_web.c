/* SPDX-License-Identifier: GPL-3.0-only */
/* Carnac in the browser: the FM1's app (src/hardware/synth_panel.c: the panel, the settings, the SAVE banks and their
 * preset store; the UI state machine; the screens; the synth) on a simulated FM1, compiled to WebAssembly and run in
 * an AudioWorklet. Modelled on X0X's web/emu/x0x_web.c + host/x0x_host.c (github.com/charlesvestal/fm1-x0x): the
 * worklet asks for audio, and producing it advances the device's clock one millisecond at a time, which renders the
 * synth every 256 frames and runs the main loop's work as synth_main_fm1.c fm1_main does (the panel each pass, the
 * screen and the LEDs every 16 ms). X0X's own gfx.c draws the screen into a 240 x 240 framebuffer, so the page shows
 * the FM1's pixels; X0X's storage.c and the preset store write a simulated NOR, which the page keeps in the browser.
 *
 * Not simulated: the second core (one core renders every voice, the same sound), USB (MIDI in comes from the page,
 * Web MIDI), the editor SysEx, the PERF TEST, the update modes, the battery (the moon shows USB power).
 *
 *   web/emu/build.sh   ->  web/emu/carnac.wasm (+ the page, web/emu/index.html and worklet.js) */
#include <stdint.h>
#include <string.h>
#define X0X_HOST 1                                  /* plat.h: no .pool section */

#define EXPORT __attribute__((used, visibility("default")))

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];                      /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static uint32_t blits;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    blits++;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#define __attribute__(x)                            /* X0X's sources: their .pool sections are the device's */
#include "gfx.c"
#undef __attribute__

static uint32_t fm1_ms;                             /* the device's milliseconds since power-on */
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"

/* ---------------------------------------------------------- platform --- */
static uint32_t held_btn, held_keys, lit_btn, lit_keys, dim_keys, play_red;
static int32_t enc_acc[NE];
static uint32_t master_q12 = 2304;                  /* the MASTER pot (plat_fm1.c's): set by web_master */
static uint8_t fm1_led_dim_div;                     /* dim keys at 1/n: X0X's quarter, the ARP page's half (web_dim_div) */

uint32_t plat_ms(void) { return fm1_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master_q12; }
int plat_battery(void) { return 4; }                /* powered by a USB host */
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
    fm1_led_dim_div = dim_keys ? 4u : 0u;           /* plat_fm1.c's GLOW_DIV; panel_leds may halve it after */
}
void plat_glow(int on) { (void)on; }
void plat_play_red(int red) { play_red = (uint32_t)red; }
void plat_keys_dim(uint32_t k) { dim_keys = k; }

/* MIDI in from the page (Web MIDI), as USB-MIDI event packets: cable 0, CIN = the status' high nibble */
#define MQ 256u
static uint32_t min_q[MQ], mi_w, mi_r;
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = min_q[mi_r++ % MQ];
    return 1;
}
void plat_midi_out(uint32_t pkt) { (void)pkt; }

/* the flash: a 1 MiB NOR (erase 0xFF, a write only clears bits), as src/host/test_preset_store.c has it. The page keeps
 * NOR_KEEP_OFF .. +NOR_KEEP_LEN (the preset store at 0xA0000 up to X0X's settings object at 0xFC000) in the browser
 * and puts it back before web_boot */
#define NOR_SIZE 0x100000u
#define NOR_KEEP_OFF 0xA0000u
#define NOR_KEEP_LEN (0xFE000u - NOR_KEEP_OFF)
static uint8_t nor[NOR_SIZE];
static uint32_t nor_writes;
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > NOR_SIZE || n > NOR_SIZE - off)
        return -8;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if ((off & 0xFFFu) || off >= NOR_SIZE)
        return -8;
    memset(nor + off, 0xFF, 0x1000u);
    nor_writes++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off > NOR_SIZE || n > NOR_SIZE - off)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    nor_writes++;
    return 0;
}
#define __attribute__(x)
#include "storage.c"
#undef __attribute__

/* ------------------------------------------------------------- synth --- */
#include "dsp/synth_runtime.h"
#include "dsp/preset.h"
#include "dsp/wavetables.h"
#include "dsp/flash_record.h"
#include "dsp/midi_clock.h"
#include "dsp/voice_allocator.h"
#include "dsp/synth_voice.h"
#include "dsp/oscillators.h"
#include "dsp/lfo.h"
#include "dsp/pcm_store.h"
#include "UIStateMachine.h"
#include "hardware/fm1_x0x_canvas.h"
#include "UIStateMachine.c"
#include "ui_seq_draw.c"
#include "ui_arp_draw.c"
#include "ui_page_draw.c"
#include "carnac_splash.c"

#ifndef SYNTH_VERSION
#define SYNTH_VERSION "WEB"
#endif

static int32_t carnac_battery_moon(void) { return -2; }   /* (on a USB host's power) */
static void syn_lock(void) {}                       /* one thread: the render and the main loop take turns */
static void syn_unlock(void) {}
#include "hardware/perf_test.h"
uint8_t pt_state(void) { return PT_IDLE; }          /* no PERF TEST here */

#include "hardware/synth_panel.c"

/* ------------------------------------------------------------- audio --- */
/* synth_main_fm1.c's render path: MIDI in, a 256-frame block, the MASTER level (Q15 full scale at 2^22 of the
 * codec's 2^23 at full MASTER, as on the FM1) */
#define REND_FRAMES 256u
static void midi_drain(void)
{
    uint32_t pkt, guard = 64;
    while (guard-- && plat_midi_in(&pkt)) {
        uint32_t cin = pkt & 0x0Fu;
        uint8_t st = (uint8_t)(pkt >> 8);
        if (cin >= 0x8u && cin <= 0xEu && st >= 0x80u && st < 0xF0u) {
            uint8_t d2 = (uint8_t)((pkt >> 24) & 0x7Fu);
            if ((st & 0xF0u) == 0x90u)
                d2 = instrument_velocity(d2);
            synth_midi_3(st, (uint8_t)((pkt >> 16) & 0x7Fu), d2);
        } else if (cin == 0xFu && st >= 0xF8u)
            synth_midi_3(st, 0, 0);
    }
}

static int32_t out_vol = -1;
static void out_level(int32_t *lr, uint32_t n)
{
    int32_t tgt = (int32_t)master_q12, v = out_vol < 0 ? tgt : out_vol, step = (tgt - v) / (int32_t)REND_FRAMES;
    uint32_t i;
    for (i = 0; i < n; i++) {
        v += step;
        lr[2u * i] = (lr[2u * i] * v) >> 5;
        lr[2u * i + 1u] = (lr[2u * i + 1u] * v) >> 5;
    }
    out_vol = tgt;
}

/* the output ring, stereo float, filled by run_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2u];
static uint32_t ring_w, ring_r;
static int32_t blk[2u * REND_FRAMES];
static void render_block(void)
{
    uint32_t k;
    midi_drain();
    synth_render_q15(blk, REND_FRAMES);
    out_level(blk, REND_FRAMES);
    for (k = 0; k < REND_FRAMES; k++) {
        uint32_t i = (ring_w++ % RING) * 2u;
        ring[i] = (float)blk[2u * k] / 8388608.0f;
        ring[i + 1u] = (float)blk[2u * k + 1u] / 8388608.0f;
    }
}

/* --------------------------------------------------------- the device --- */
#define SPLASH_MS 1500u
static uint32_t splash_t0, last_frame, force = 1, booted, running;
static uint32_t audio_due_x10;                      /* frames owed, x10 (44.1 a millisecond) */

static void boot(void)
{
    uint32_t k;
    palette_set(1);
    carnac_splash(SYNTH_VERSION, settings_theme());
    splash_t0 = fm1_ms;
    synth_init();
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], seq_lanes, &arp_state);
    ui_sm_attach_seq(&sm, synth_seq());
    ui_sm_attach_arp(&sm, synth_arp());
    ui_sm_attach_store(&sm, syn_slot_query, 0);
    ui_sm_attach_instrument(&sm, &instrument);
    seq_palette_set(SQPAL_ORANGE);
    settings_load();
    factory_install();
    ps_load();
    {   /* the PCM waves (src/dsp/gen_pcm_waves.py's images), as on a unit they were uploaded to */
        extern const uint8_t pcm_image_host0[], pcm_image_host1[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
    }
    for (k = 0; k < NKEYS; k++)
        key_note[k] = NOTE_NONE;
    booted = 1;
}

/* one millisecond of the device: the audio it owes, then the main loop's pass */
static void run_ms(void)
{
    fm1_ms++;
    audio_due_x10 += 441u;
    while (audio_due_x10 >= REND_FRAMES * 10u) {
        audio_due_x10 -= REND_FRAMES * 10u;
        render_block();
    }
    if (!running) {
        if (fm1_ms - splash_t0 < SPLASH_MS)
            return;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        btn_prev = plat_buttons();                  /* what is held at the start is no press */
        keys_prev = plat_keys();
        running = 1;
        force = 1;
        last_frame = fm1_ms - 16u;
    }
    settings_tick();
    ps_tick();
    syn_perf_tick();
    panel_input();
    if (fm1_ms - last_frame >= 16u) {
        uint32_t ms = fm1_ms - last_frame;
        last_frame = fm1_ms;
        ui_sm_tick(&sm, (uint16_t)(ms > 1000u ? 1000u : ms));
        bend_spring();
        ui_screen_draw(&sm, synth_seq(), synth_arp(), force);
        force = 0;
        panel_leds();
    }
}

/* ----------------------------------------------------------- exports --- */
static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }

/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    if (!booted) {
        memset(out_l, 0, n * sizeof out_l[0]);
        memset(out_r, 0, n * sizeof out_r[0]);
        return;
    }
    while (ring_w - ring_r < n)
        run_ms();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1u];
    }
}

/* the flash the page keeps: erased first (a new unit), then what the page gives back goes in before web_boot */
EXPORT uint8_t *web_nor(void) { return nor + NOR_KEEP_OFF; }
EXPORT uint32_t web_nor_len(void) { return NOR_KEEP_LEN; }
EXPORT uint32_t web_nor_writes(void) { return nor_writes; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }

EXPORT void web_boot(void)
{
    if (!booted)
        boot();
}
EXPORT void web_buttons(uint32_t m) { held_btn = m; }
EXPORT void web_keys(uint32_t m) { held_keys = m; }
EXPORT void web_enc(int32_t role, int32_t n)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += n;
}
/* the MASTER pot, 0 .. 1023 as the ADC reads it: squared, as fm1_main does */
EXPORT void web_master(uint32_t pot)
{
    if (pot > 1023u)
        pot = 1023u;
    master_q12 = (pot * pot) >> 8;
}
EXPORT void web_midi(uint32_t b0, uint32_t b1, uint32_t b2)
{
    uint32_t cin = b0 >= 0xF8u ? 0xFu : (b0 >> 4) & 0xFu;
    if (mi_w - mi_r >= MQ)
        return;
    min_q[mi_w++ % MQ] = cin | (b0 & 0xFFu) << 8 | (b1 & 0x7Fu) << 16 | (b2 & 0x7Fu) << 24;
}
EXPORT uint32_t web_lit_buttons(void) { return lit_btn; }
EXPORT uint32_t web_lit_keys(void) { return lit_keys; }
EXPORT uint32_t web_dim_keys(void) { return dim_keys & ~lit_keys; }
EXPORT uint32_t web_play_red(void) { return play_red; }
EXPORT uint32_t web_dim_div(void) { return fm1_led_dim_div; }
EXPORT uint16_t *web_fb(void) { return fb; }
EXPORT uint32_t web_blits(void) { return blits; }
EXPORT uint32_t web_now(void) { return fm1_ms; }
EXPORT uint32_t web_page(void) { return sm.page; }
