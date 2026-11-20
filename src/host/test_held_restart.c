/* Host test: keys still down when the synth restarts (the PERF TEST's scenarios press notes and restart without a
 * note-off; a patch load restarts too) must not outlive the restart. A stale held-key stack made the mono modes bring
 * the old note back on the next release (a hung note). Exit code 0 = pass.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/test_held_restart.c -lm -o test_held_restart */
#include <stdio.h>
#include <stdint.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/voice_allocator.h"

static int32_t buf[64 * 2];
static int fails;

static int sounding(void)
{
    int n = 0;
    for (int v = 0; v < POLYPHONY_VOICES; v++)
        n += !voice_is_free(&poly_bank[v]);
    return n;
}

static void render(uint32_t frames)
{
    for (uint32_t n = 0; n < frames; n += 64)
        synth_render_q15(buf, 64);
}

int main(void)
{
    static const uint8_t modes[] = {VMODE_MONO_RETRIG, VMODE_MONO_LEGATO, VMODE_POLY_REUSE, VMODE_POLY_RESET,
                                    VMODE_UNISON_STACK, VMODE_POLY_UNISON};
    for (unsigned m = 0; m < sizeof modes; m++) {
        synth_init();
        perf_state.voice_mode[0] = perf_state.voice_mode[1] = modes[m];
        for (int i = 0; i < 8; i++)                       /* a PERF TEST scenario's chord, never released */
            synth_midi_3(0x90, (uint8_t)(48 + i * 3), 100);
        render(2048);
        synth_restart();                                  /* the next scenario / the end of the test */
        synth_midi_3(0x90, 60, 100);                      /* the player's key */
        render(2048);
        synth_midi_3(0x80, 60, 0);
        render(44100 * 8);                                /* the release, long enough for any envelope here */
        int n = sounding();
        if (n) {
            printf("FAIL: voice mode %u: %d voice(s) still sounding after the only key was released\n", modes[m], n);
            fails++;
        }
    }
    if (!fails)
        printf("no key outlives a restart in any voice mode\n");
    return fails != 0;
}
