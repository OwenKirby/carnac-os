/* Host test: every note source at once (keys, SEQ, ARP and the REC page's loop) while the overload shedder moves the
 * voice limit at random, in each voice mode. When everything is stopped and every key let go, no voice may still
 * sound (a hung note keeps the load up and keeps the shedder from giving voices back). Exit code 0 = pass.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/test_shed_sources.c -lm -o test_shed_sources */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/voice_allocator.h"
#include "dsp/maq_seq.h"
#include "dsp/arp_seq.h"
#include "dsp/krec.h"

static int32_t buf[128 * 2];
static uint32_t rng;
static int fails;
static uint32_t seen_src;                          /* bit per VSRC_* seen on a sounding voice */
static int peak_sounding, shed_hits;

static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static void run(uint32_t frames)
{
    while (frames) {
        uint32_t n = frames < 128u ? frames : 128u;
        synth_render_q15(buf, n);
        frames -= n;
    }
}
static int sounding(void)
{
    int n = 0;
    for (int v = 0; v < POLYPHONY_VOICES; v++)
        n += !voice_is_free(&poly_bank[v]);
    return n;
}

static void scenario(uint8_t mode, uint32_t seed)
{
    rng = seed;
    synth_init();
    perf_state.voice_mode[0] = perf_state.voice_mode[1] = mode;
    perf_state.int_tempo = 140;
    seq_lane_set_event(&seq_lanes[0], SEQEV_NOTE);
    for (int s = 0; s < (int)SEQ_NSTEP; s++)
        seq_lanes[0].steps[s].note_buffer[0] = (uint8_t)(48 + (s * 5) % 17);
    for (int s = 0; s < 3; s++) {
        ArpChordStep *st = &arp_state.steps[s];
        st->chord_root = (uint8_t)(55 + s * 2); st->rate = (uint8_t)(ARPRATE_1_8 + s); st->octave_range = 1;
        st->style = (uint8_t)(ARPSTYLE_UP + s);
    }
    arp_state.sequence_length = 3;
    Krec *k = synth_rec();

    /* record a short loop from the keys */
    krec_arm(k);
    krec_tap(k, synth_frame());
    for (int i = 0; i < 4; i++) {
        uint8_t n = (uint8_t)(52 + (rnd() % 12));
        synth_midi_3(0x90, n, 100); run(6000);
        synth_midi_3(0x80, n, 0); run(3000);
    }
    krec_tap(k, synth_frame());                      /* closed: it plays */
    maq_set_playing(synth_seq(), 1);
    carp_set_playing(synth_arp(), 1);

    /* chaos: keys at random, the shed limit at random */
    uint8_t down[128] = {0};
    for (int t = 0; t < 700; t++) {                  /* 700 x ~46 ms */
        uint32_t r = rnd() % 8u;
        uint8_t n = (uint8_t)(36 + rnd() % 48);
        if (r < 3u && !down[n]) { synth_midi_3(0x90, n, (uint8_t)(40 + rnd() % 80)); down[n] = 1; }
        else if (r < 6u && down[n]) { synth_midi_3(0x80, n, 0); down[n] = 0; }
        if (!(rnd() & 3u)) voice_shed = (uint8_t)(rnd() % 8u);
        if (t == 300) { krec_set_playing(k, 0, synth_frame()); }
        if (t == 330) { krec_set_playing(k, 1, synth_frame()); }
        if (t == 450) { maq_set_playing(synth_seq(), 0); carp_set_playing(synth_arp(), 0); }
        if (t == 500) { maq_set_playing(synth_seq(), 1); carp_set_playing(synth_arp(), 1); }
        for (int q = 0; q < 16; q++) {               /* (look between the blocks) */
            run(128);
            for (int v = 0; v < POLYPHONY_VOICES; v++)
                if (!voice_is_free(&poly_bank[v])) seen_src |= 1u << poly_bank[v].src;
            if (sounding() > peak_sounding) peak_sounding = sounding();
        }
        shed_hits += voice_shed != 0;
    }

    /* everything off */
    voice_shed = 0;
    for (int n = 0; n < 128; n++)
        if (down[n]) synth_midi_3(0x80, (uint8_t)n, 0);
    maq_set_playing(synth_seq(), 0);
    carp_set_playing(synth_arp(), 0);
    krec_set_playing(k, 0, synth_frame());
    run(44100 * 12);
    int left = sounding();
    if (left) {
        printf("FAIL: voice mode %u seed %u: %d voice(s) still sounding 12 s after everything stopped\n", mode, seed, left);
        for (int v = 0; v < POLYPHONY_VOICES; v++)
            if (!voice_is_free(&poly_bank[v]))
                printf("      voice %d note %u src %u gate %u silent %u\n", v, poly_bank[v].active_note, poly_bank[v].src,
                       poly_bank[v].is_gate_open, poly_bank[v].silent);
        fails++;
    }
}

int main(void)
{
    static const uint8_t modes[] = {VMODE_MONO_RETRIG, VMODE_MONO_LEGATO, VMODE_POLY_REUSE, VMODE_POLY_RESET,
                                    VMODE_UNISON_STACK};
    for (unsigned m = 0; m < sizeof modes; m++)
        for (uint32_t seed = 1; seed <= 3; seed++)
            scenario(modes[m], seed * 7919u);
    printf("sources heard on voices: ARP %u SEQ %u REC %u KEYS %u; most voices at once %d; shed limit non-zero in %d of %d steps\n",
           (seen_src >> VSRC_ARP) & 1u, (seen_src >> VSRC_SEQ) & 1u, (seen_src >> VSRC_REC) & 1u,
           (seen_src >> VSRC_KEYS) & 1u, peak_sounding, shed_hits, 15 * 700);
    if (!fails)
        printf("keys + SEQ + ARP + REC loop with the shed limit moving: no voice left sounding in any mode\n");
    return fails != 0;
}
