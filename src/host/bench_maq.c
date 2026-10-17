/* Host cost of the MAQ sequencer engine per audio frame, against the synth itself. Test scaffolding only.
 * Times (QueryPerformanceCounter) 60 s of frames of:
 *   idle     maq_frame with the sequencer stopped (the cost every frame pays)
 *   heavy    maq_frame playing a worst-case slot: 3 NOTE lanes of 4-note chords at 1/32 with a RATCHET lane on 4x,
 *            all 16 steps, into a no-op sink (the engine alone: the notes it starts are ordinary voices)
 *   voice    the synth rendering 8 held notes of the default patch, divided by 8 (one voice), for scale
 *
 *   gcc -Os -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/bench_maq.c -o bench_maq */
#include <stdio.h>
#include <windows.h>
#include "preset.h"
#include "dsp/midi_clock.h"
#include "dsp/maq_seq.h"
#include "dsp/synth_runtime.h"

#define FRAMES (44100u * 60u)

static volatile uint32_t sink_count;
static void sink(uint8_t st, uint8_t d1, uint8_t d2) { (void)st; (void)d1; (void)d2; sink_count++; }

static double now_ns(void)
{
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart * 1e9 / (double)f.QuadPart;
}

static double run_seq(uint8_t play, SequencerLane *lanes)
{
    static MaqSeq q;
    double t0, t1;
    uint32_t f;
    midi_clock_init();
    maq_init(&q, lanes, &perf_state, sink);
    maq_set_playing(&q, play);
    t0 = now_ns();
    for (f = 0; f < FRAMES; f++) {
        midi_clock_frame(f);
        maq_frame(&q, f);
    }
    t1 = now_ns();
    return (t1 - t0) / FRAMES;
}

int main(void)
{
    static SequencerLane lanes[SEQ_NLANE];
    static int32_t buf[SYNTH_RENDER_BLOCK * 2];
    double idle, heavy, clock_only, voice, t0;
    uint32_t f, i, l;

    preset_defaults();
    perf_state.int_tempo = 127;                      /* 167 BPM: the most steps per second */
    seq_defaults(lanes);
    for (l = 0; l < 3; l++) {                        /* 3 chord lanes at 1/32, the 4th a 4x ratchet on all three */
        lanes[l].event_type = SEQEV_NOTE;
        lanes[l].target_mask = SEQ_TGT_L1;
        lanes[l].prescale = 3;
        for (i = 0; i < SEQ_NSTEP; i++) {
            lanes[l].steps[i].note_buffer[0] = (uint8_t)(48 + l * 7);
            lanes[l].steps[i].note_buffer[1] = (uint8_t)(52 + l * 7);
            lanes[l].steps[i].note_buffer[2] = (uint8_t)(55 + l * 7);
            lanes[l].steps[i].note_buffer[3] = (uint8_t)(59 + l * 7);
        }
    }
    lanes[3].event_type = SEQEV_RATCHET;
    lanes[3].target_mask = SEQ_TGT_LANE1 | SEQ_TGT_LANE2 | SEQ_TGT_LANE3;
    lanes[3].prescale = 3;
    for (i = 0; i < SEQ_NSTEP; i++) lanes[3].steps[i].value = 127;

    /* the clock alone, to take out of both */
    midi_clock_init();
    t0 = now_ns();
    for (f = 0; f < FRAMES; f++) midi_clock_frame(f);
    clock_only = (now_ns() - t0) / FRAMES;

    idle = run_seq(0, lanes) - clock_only;
    sink_count = 0;
    heavy = run_seq(1, lanes) - clock_only;
    printf("events sent in 60 s (heavy): %u (%.0f per second)\n", sink_count, sink_count / 60.0);

    synth_init();
    for (i = 0; i < 8; i++) synth_midi_3(0x90, (uint8_t)(48 + i * 3), 100);
    t0 = now_ns();
    for (f = 0; f < FRAMES / 8u; f += SYNTH_RENDER_BLOCK) synth_render_q15(buf, SYNTH_RENDER_BLOCK);
    voice = (now_ns() - t0) / (FRAMES / 8u) / 8.0;

    printf("maq_frame, stopped:      %6.2f ns/frame\n", idle);
    printf("maq_frame, heavy pattern: %6.2f ns/frame\n", heavy);
    printf("one voice (default patch, 8 held): %6.2f ns/frame\n", voice);
    printf("heavy sequencer = %.1f%% of one voice, %.2f%% of 8 voices\n", 100.0 * heavy / voice, 100.0 * heavy / (8.0 * voice));
    return 0;
}
