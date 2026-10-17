/* Host test: the MAQ / ARP idle fast paths (maq_seq.c maq_idle_frame, arp_seq.c carp_frame) are bit-exact. One
 * scripted session through the whole synth: idle with notes held, play / stop, REC stopped and playing, the ARP's
 * audition, a held (live) chord, TRANSPOSE MAQ, then the MIDI clock with Start / Stop / Continue and running pulses,
 * at block sizes 7 / 32 / 128 and with requests between blocks. It prints one hash a phase over the output and the
 * MAQ's and the ARP's whole state. run_tests.sh builds it twice, with and without -DSYNTH_SEQ_NO_IDLE, and the two
 * outputs must be the same. Test scaffolding only. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/maq_seq.h"
#include "dsp/arp_seq.h"

static int32_t buf[128 * 2];
static uint32_t h;
static uint32_t block = 32, clk_acc;
static int ext_clock;

static void fnv(const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
}

/* frames of output, hashing the samples and the sequencers' state after every block; the MIDI clock at 100 BPM */
static void run(uint32_t frames)
{
    while (frames) {
        uint32_t n = frames < block ? frames : block;
        if (ext_clock) {
            clk_acc += n * 2u;
            while (clk_acc >= 2205u) { clk_acc -= 2205u; synth_midi_3(0xF8, 0, 0); }
        }
        synth_render_q15(buf, n);
        fnv(buf, sizeof(int32_t) * 2u * n);
        MaqSeq q = *synth_seq();                     /* the state, less its pointers (addresses differ per build) */
        CarpSeq a = *synth_arp();
        q.lanes = NULL; q.perf = NULL; q.out = NULL;
        a.st = NULL; a.perf = NULL; a.out = NULL;
        fnv(&q, sizeof q);
        fnv(&a, sizeof a);
        frames -= n;
    }
}

static void phase(const char *name)
{
    printf("%-28s block %3u  %08x\n", name, block, h);
}

int main(void)
{
    static const uint32_t blocks[3] = {7, 32, 128};
    for (int bi = 0; bi < 3; bi++) {
        block = blocks[bi];
        h = 2166136261u;
        ext_clock = 0;
        clk_acc = 0;
        synth_init();
        perf_state.int_tempo = 131;
        seq_lane_set_event(&seq_lanes[0], SEQEV_NOTE);
        seq_lane_set_event(&seq_lanes[1], SEQEV_MODWHEEL);
        seq_lane_set_event(&seq_lanes[3], SEQEV_RATCHET);
        for (int s = 0; s < (int)SEQ_NSTEP; s++) {
            seq_lanes[0].steps[s].note_buffer[0] = (uint8_t)(48 + (s * 5) % 17);
            seq_lanes[1].steps[s].value = (uint8_t)(s * 8);
            seq_lanes[3].steps[s].value = (uint8_t)((s % 4 == 3) ? 40 : 0);
        }
        for (int s = 0; s < 3; s++) {
            ArpChordStep *st = &arp_state.steps[s];
            st->chord_root = (uint8_t)(55 + s * 2); st->gate_length = 90; st->octave_range = 1;
            st->style = (uint8_t)(ARPSTYLE_UP + s); st->variation = (uint8_t)(s * 3);
        }
        arp_state.sequence_length = 3;
        MaqSeq *q = synth_seq();
        CarpSeq *a = synth_arp();

        synth_midi_3(0x90, 60, 100); synth_midi_3(0x90, 64, 90);
        run(9000);                                   phase("idle, notes held");
        synth_midi_3(0x80, 60, 0); synth_midi_3(0x80, 64, 0);
        maq_set_playing(q, 1); run(30011);           phase("play");
        maq_set_playing(q, 0); run(5003);            phase("stop");
        maq_set_record(q, 1); run(3001);
        maq_set_rec_step(q, 5); run(2999);
        maq_set_record(q, 0); run(4000);             phase("rec while stopped");
        maq_set_playing(q, 1); run(6000);
        maq_set_record(q, 1); run(12000);
        maq_set_record(q, 0); run(6000);
        maq_set_playing(q, 0); run(4000);            phase("rec while playing");
        carp_audition(a, 57, 100); run(5000);
        carp_audition(a, 57, 0); run(3000);          phase("arp audition");
        carp_hold(a, 59, 90); run(20000);
        { uint8_t k[4] = {80, 2, ARPSTYLE_DOWN, 0}; carp_set_knobs(a, k); } run(7000);
        carp_hold(a, 59, 0); run(6000);              phase("arp live chord");
        arp_state.transpose_maq_option = 1;
        carp_set_transpose(a, 3); run(5000);
        maq_set_playing(q, 1); run(15000);
        maq_set_playing(q, 0); run(5000);
        carp_set_transpose(a, -2); run(3000);        phase("transpose maq");
        perf_state.clock_source = CLKSRC_MIDI;
        ext_clock = 1;
        run(6000);                                   phase("midi clock, stopped");
        synth_midi_3(0xFA, 0, 0); run(25000);
        synth_midi_3(0xFC, 0, 0); run(6000);         phase("midi start / stop");
        synth_midi_3(0xFB, 0, 0); run(8000);
        synth_midi_3(0xFC, 0, 0); run(3000);
        ext_clock = 0; run(4000);
        synth_midi_3(0xFA, 0, 0); run(6000);
        maq_set_playing(q, 0); run(3000);            phase("midi continue, start");
        perf_state.clock_source = CLKSRC_INTERNAL;
        run(6000);                                   phase("back to internal");
    }
    return 0;
}
