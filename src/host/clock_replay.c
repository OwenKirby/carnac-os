/* Replays a clock probe capture through the host build of midi_clock.c (src/hardware/fm1_clock.py replay).
 * stdin: one line an event, in order: "p <frame>" a 0xF8 handed to the clock on that frame, "e <frame> <status>" a
 * transport message (0xFA / 0xFB / 0xFC, decimal), "s <frame> <n>" the FM1 lost n frames just before that frame
 * (midi_clock_slip). stdout: the frame of every pulse the clock played, one a line.
 * Frames are run one by one as the render does: the clock's own frame first, then that frame's MIDI.
 * Test scaffolding only.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/clock_replay.c -lm -o build/clock_replay.exe */
#include <stdio.h>
#include "preset.h"
#include "dsp/midi_clock.h"

static uint32_t now, seen;

static void record(uint32_t frame)
{
    while (seen != midi_clock_pulse_count()) {
        seen++;
        printf("%u\n", frame);
    }
}

static void run_to(uint32_t f)          /* every frame before f, then f's own clock frame */
{
    while (now < f) {
        midi_clock_frame(now);
        record(now);
        now++;
    }
    if (now == f) {
        midi_clock_frame(now);
        record(now);
        now++;
    }
}

int main(void)
{
    char kind;
    unsigned f, st;
    int first = 1;
    preset_defaults();
    perf_state.clock_source = CLKSRC_MIDI;
    midi_clock_init();
    seen = midi_clock_pulse_count();
    while (scanf(" %c", &kind) == 1) {
        if (scanf("%u", &f) != 1) break;
        if (first) { now = f > 1000u ? f - 1000u : 0u; first = 0; }
        if (kind == 's') { /* below */ }
        else if (f + 1u <= now) f = now - 1u;     /* (same frame as the last: no clock frame again) */
        else run_to(f);
        if (kind == 's') {
            if (scanf("%u", &st) != 1) break;
            if (f > now) run_to(f - 1u);
            midi_clock_slip(st);
            continue;
        }
        if (kind == 'e') {
            if (scanf("%u", &st) != 1) break;
            midi_clock_realtime((uint8_t)st);
        } else {
            midi_clock_realtime(0xF8);
        }
        record(now - 1u);
    }
    run_to(now + 44100u);                     /* a second on: the lock's last pulses, then it lets go */
    return 0;
}
