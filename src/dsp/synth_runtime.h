#pragma once
#include <stdint.h>
#include "maq_seq.h"
#include "arp_seq.h"

#define SYNTH_RENDER_BLOCK 32u
#define SYNTH_WEB_MAX_FRAMES 128u

void synth_init(void);
/* synth_init without the factory presets: every voice, the clock, the FX, the MAQ and the ARP start afresh on the
 * live patches as they are now. Not from inside a render (the audio ISR's caller holds it off). */
void synth_restart(void);
void synth_midi_3(uint8_t status, uint8_t data1, uint8_t data2);
/* the panel's CC1 / CC2 knobs (LAYER): the CUSTOMCC1 / 2 mod sources' value 0..127, as an incoming CUSTOM CC sets it */
void synth_custom_cc(uint8_t which, uint8_t value);
void synth_render_q15(int32_t *interleaved_lr, uint32_t frames);
/* A second core for the voices (the FM1: X0X's plat_cpu2_run / plat_cpu2_wait). run hands it fn(arg) and returns 1
 * if it took it; wait returns 0 when it is done, -1 when it failed (and is off: the caller does the job). Both are
 * called only inside synth_render_q15. Unset (the host, the browser, the profile build), one core renders both voice
 * sets; the output is the same either way. */
void synth_set_core2(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void));
#if !defined(FELUCCA_ID)                      /* browser emulator only */
int32_t *synth_web_buffer(void);
void synth_web_render(uint32_t frames);
#endif
/* the MAQ sequencer the render clocks: the UI's transport, record step and playhead come from here */
MaqSeq *synth_seq(void);
/* the peak |Q15| of the reverb's wet, the delay's wet, the dry left and the reverb's send since the last call (which
 * zeroes them) */
void synth_levels(int32_t out[4]);
/* debugging (the FM1's ED_DEBUG): 1 runs the reverb's back on core 0 too, beside its front */
void synth_fx_one_core(int on);
/* the ARP engine the render clocks: the ARP page's chord keys, transposition and playhead go through here */
CarpSeq *synth_arp(void);
