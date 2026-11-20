/* Host test: the CC map's step 1 (2026-11-18): SUSTAIN (CC 64) in every voice mode, ALL NOTES OFF (123), ALL SOUND
 * OFF (120), RESET ALL CONTROLLERS (121), BANK SELECT (CC 0) with Program Change, and a CUSTOM CC keeping its number.
 * Exit code 0 = pass.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/test_midi_cc.c -lm -o test_midi_cc */
#include <stdio.h>
#include <stdint.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/voice_allocator.h"
#include "dsp/midi_io.h"

extern MidiInterfaceState master_midi;
static int32_t buf[64 * 2];
static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void render(uint32_t frames)
{
    for (uint32_t n = 0; n < frames; n += 64)
        synth_render_q15(buf, 64);
}

static int open_gates(int note)                    /* voices with their gate open on note (-1: any) */
{
    int n = 0;
    for (int v = 0; v < POLYPHONY_VOICES; v++)
        n += poly_bank[v].is_gate_open && (note < 0 || poly_bank[v].active_note == note);
    return n;
}

static int sounding(void)
{
    int n = 0;
    for (int v = 0; v < POLYPHONY_VOICES; v++)
        n += !voice_is_free(&poly_bank[v]);
    return n;
}

static void fresh(uint8_t mode)
{
    synth_init();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    perf_state.midi_ch[0] = 0;
    perf_state.midi_ch[1] = 1;
    perf_state.cc1_num = perf_state.cc2_num = 0;
    perf_state.voice_mode[0] = perf_state.voice_mode[1] = mode;
    synth_restart();
    render(256);
}

static void cc(uint8_t ch, uint8_t num, uint8_t val) { synth_midi_3((uint8_t)(0xB0u | ch), num, val); render(64); }
static void on(uint8_t note) { synth_midi_3(0x90, note, 100); render(64); }
static void off(uint8_t note) { synth_midi_3(0x80, note, 0); render(64); }

int main(void)
{
    static const uint8_t modes[] = {VMODE_MONO_RETRIG, VMODE_MONO_LEGATO, VMODE_POLY_REUSE, VMODE_POLY_RESET,
                                    VMODE_UNISON_STACK, VMODE_POLY_UNISON};
    for (unsigned m = 0; m < sizeof modes; m++) {
        int mono = modes[m] == VMODE_MONO_RETRIG || modes[m] == VMODE_MONO_LEGATO || modes[m] == VMODE_UNISON_STACK;
        fresh(modes[m]);
        on(60);
        cc(0, 64, 127);
        off(60);
        CHECK(open_gates(60) > 0, "mode %u: the pedal did not hold a released key", modes[m]);
        cc(0, 64, 0);
        CHECK(open_gates(-1) == 0, "mode %u: the pedal's release left %d gate(s) open", modes[m], open_gates(-1));

        /* a key down again when the pedal comes up keeps its note, until it is let go */
        cc(0, 64, 127);
        on(62);
        off(62);
        on(62);
        cc(0, 64, 0);
        CHECK(open_gates(62) > 0, "mode %u: a key held again was let go by the pedal", modes[m]);
        off(62);
        CHECK(open_gates(-1) == 0, "mode %u: the re-held key's release left gates open", modes[m]);

        if (mono) {                                 /* the last note played is held, not a lower one */
            cc(0, 64, 127);
            on(60);
            on(64);
            off(64);
            off(60);
            CHECK(open_gates(64) > 0, "mode %u: mono under the pedal lost its note", modes[m]);
            cc(0, 64, 0);
            CHECK(open_gates(-1) == 0, "mode %u: mono: the pedal's release left gates open", modes[m]);
        } else {                                    /* a chord let go under the pedal sounds on whole */
            cc(0, 64, 127);
            on(60); on(64); on(67);
            off(60); off(64); off(67);
            CHECK(open_gates(60) > 0 && open_gates(64) > 0 && open_gates(67) > 0 &&
                  open_gates(-1) == (modes[m] == VMODE_POLY_UNISON ? 8 : 3),   /* (POLY UNISON: 3 + 3 + 2 voices) */
                  "mode %u: the pedal held %d gates of a 3-note chord", modes[m], open_gates(-1));
            cc(0, 64, 0);
            CHECK(open_gates(-1) == 0, "mode %u: the chord outlived the pedal", modes[m]);
        }

        /* the pedal on a channel no layer plays holds nothing */
        cc(2, 64, 127);
        on(60);
        off(60);
        CHECK(open_gates(-1) == 0, "mode %u: a pedal on an unplayed channel held a key", modes[m]);
        cc(2, 64, 0);

        /* ALL NOTES OFF lets every key go; the pedal still holds them; RESET ALL CONTROLLERS lifts the pedal */
        on(60); on(64);
        cc(0, 123, 0);
        CHECK(open_gates(-1) == 0, "mode %u: ALL NOTES OFF left %d gate(s) open", modes[m], open_gates(-1));
        cc(0, 64, 127);
        on(60);
        cc(0, 123, 0);
        CHECK(open_gates(-1) > 0, "mode %u: ALL NOTES OFF went past the pedal", modes[m]);
        master_midi.mod_wheel_val = 99;
        cc(0, 121, 0);
        CHECK(open_gates(-1) == 0, "mode %u: RESET ALL CONTROLLERS left the pedal down", modes[m]);
        CHECK(master_midi.mod_wheel_val == 0, "mode %u: RESET ALL CONTROLLERS kept the wheel", modes[m]);
        off(60);

        /* ALL SOUND OFF: silent in a few ms, releases and all */
        on(60);
        render(4096);
        off(60);
        cc(0, 120, 0);
        render(2048);
        CHECK(sounding() == 0, "mode %u: %d voice(s) sounding after ALL SOUND OFF", modes[m], sounding());
    }

    /* BANK SELECT: 0 loads a patch at once (no request), 1..3 ask the panel, on Layer 1's channel only */
    uint8_t b, s;
    fresh(VMODE_POLY_REUSE);
    synth_midi_3(0xC0, 5, 0);
    CHECK(!synth_pc_take(&b, &s), "a patch Program Change asked the panel");
    cc(0, 0, SYNTH_PCBANK_PERF);
    cc(0, 32, 0);
    synth_midi_3(0xC0, 7, 0);
    CHECK(synth_pc_take(&b, &s) && b == SYNTH_PCBANK_PERF && s == 7, "a performance Program Change was not asked for");
    CHECK(!synth_pc_take(&b, &s), "the request was taken twice");
    cc(0, 0, SYNTH_PCBANK_SEQ);
    synth_midi_3(0xC0, 3, 0);
    CHECK(synth_pc_take(&b, &s) && b == SYNTH_PCBANK_SEQ && s == 3, "a sequence Program Change was not asked for");
    cc(0, 0, 9);                                    /* (no such bank: the last one stays) */
    synth_midi_3(0xC0, 4, 0);
    CHECK(synth_pc_take(&b, &s) && b == SYNTH_PCBANK_SEQ && s == 4, "an unknown bank moved the selection");
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    cc(1, 0, SYNTH_PCBANK_PERF);
    synth_midi_3(0xC1, 2, 0);
    CHECK(!synth_pc_take(&b, &s), "a performance Program Change on Layer 2's channel was taken");

    /* a CUSTOM CC set to 64 keeps the number: the pedal is not held */
    fresh(VMODE_POLY_REUSE);
    perf_state.cc1_num = 64;
    cc(0, 64, 127);
    CHECK(master_midi.custom_cc1_val == 127, "CUSTOM CC1 on 64 did not take the value");
    on(60);
    off(60);
    CHECK(open_gates(-1) == 0, "CC 64 held a key while CUSTOM CC1 owns it");

    if (!fails)
        printf("SUSTAIN, ALL NOTES / SOUND OFF, RESET CONTROLLERS, BANK SELECT: as the MIDI spec has them\n");
    return fails != 0;
}
