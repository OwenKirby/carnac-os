/* Compatibility shim. New code calls synth_render_q15() directly. */
#include "../../hardware/audio_hal.h"
#include "synth_runtime.h"

int32_t audio_hardware_buffer[HALF_WORDS];

void process_next_synth_block(void)
{
    synth_render_q15(audio_hardware_buffer, BLOCK_SIZE);
}
