#pragma once
/* Host-only (render_demo). Its HALF_WORDS would redefine Felucca's own (firmware/src/audio.c) in the FM1 unity build. */
#if defined(FELUCCA_ID)
#error "audio_hal.h is host-only: the FM1 build calls synth_init / synth_midi_3 / synth_render_q15 via synth_runtime.h"
#endif
#include <stdint.h>

/* Platform-neutral synth output: signed Q15, interleaved stereo. */
#define BLOCK_SIZE 32u
#define HALF_WORDS (BLOCK_SIZE * 2u)

void synth_init(void);
void synth_midi_3(uint8_t status, uint8_t data1, uint8_t data2);
void synth_render_q15(int32_t *interleaved_lr, uint32_t frames);
void synth_web_render(uint32_t frames);
int32_t *synth_web_buffer(void);
