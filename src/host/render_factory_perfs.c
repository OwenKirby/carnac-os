/* Listening render of the factory performances (2026-10-17): each one loaded whole as the FM1 loads it (factory_perf:
 * its patches, effects, tempo, sequence and arp pattern), PLAY pressed, 8 bars on the internal clock, then stopped and
 * 3 s of tail, to a 16-bit stereo WAV per performance. Prints each one's peak and RMS level. Test scaffolding only.
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/pcm_images_host.c src/host/foundations_image_host.c src/host/render_factory_perfs.c -lm \
 *       -o render_factory_perfs
 *   render_factory_perfs OUTDIR       writes OUTDIR/NN_NAME.wav */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "PresetStructures.h"
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/midi_clock.h"
#include "dsp/maq_seq.h"
#include "dsp/pcm_store.h"
#include "dsp/wavetables.h"

#define RATE 44100u
#define BLOCK 32u

/* the glue's banks factory_presets.c fills (patch_bank: preset.c's) */
static SequencerLane syn_seq_bank[BANK_SLOTS][SEQ_NLANE];
static ArpState syn_arp_bank[BANK_SLOTS];
static uint8_t syn_perf_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_seq_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_arp_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_used[UISAVE_COUNT][PATCH_SLOTS / 8u];
#include "factory_presets.c"

static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void wav_header(FILE *f, uint32_t frames)
{
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + frames * 4u); fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16); put_u16(f, 1); put_u16(f, 2); put_u32(f, RATE); put_u32(f, RATE * 4u); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, frames * 4u);
}

int main(int argc, char **argv)
{
    static int32_t buf[BLOCK * 2u];
    WholePerformance w;
    char path[512], name[NAME_LEN + 1];
    uint32_t s, i, silent = 0;
    if (argc < 2) { fprintf(stderr, "usage: render_factory_perfs OUTDIR\n"); return 2; }
    synth_init();
    {   /* the PCM waves, as uploaded to a unit (the bank plays them) */
        extern const uint8_t pcm_image_host0[], pcm_image_host1[];
        extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;
        pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0);
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1);
    }
    {   /* wavetable bank 0, Foundations, as uploaded to a unit (without it bank 0 plays the fallback sine) */
        extern const uint8_t foundations_image_host[];
        extern const uint32_t foundations_image_host_len;
        wavetable_bank_attach(foundations_image_host, foundations_image_host_len);
    }
    factory_install();
    for (s = 0; s < FP_NPERF; s++) {
        uint32_t bpm, play, total, frame = 0, k;
        double sum = 0.0, peak = 0.0;
        FILE *f;
        factory_perf(s, &w);
        perf_state = w.perf;
        engine_state[0] = w.engine[0];
        engine_state[1] = w.engine[1];
        memcpy(seq_lanes, w.seq, sizeof seq_lanes);
        arp_state = w.arp;
        synth_restart();
        bpm = MIDI_CLOCK_BPM_BASE + perf_state.int_tempo;
        play = 8u * 4u * RATE * 60u / bpm;
        if (play > 40u * RATE) play = 40u * RATE;
        total = play + 3u * RATE;
        for (i = 0; i < NAME_LEN && FP_TAB[s].name[i]; i++)
            name[i] = FP_TAB[s].name[i] == ' ' ? '_' : FP_TAB[s].name[i];
        name[i] = 0;
        snprintf(path, sizeof path, "%s/%02u_%s.wav", argv[1], (unsigned)s, name);
        f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        wav_header(f, total);
        maq_set_playing(synth_seq(), 1);
        carp_set_playing(synth_arp(), 1);
        while (frame < total) {
            uint32_t n = total - frame < BLOCK ? total - frame : BLOCK;
            if (frame < play && frame + n >= play) {
                maq_set_playing(synth_seq(), 0);
                carp_set_playing(synth_arp(), 0);
            }
            synth_render_q15(buf, n);
            for (k = 0; k < n * 2u; k++) {
                int32_t v = buf[k];
                double a = fabs((double)v) / 32768.0;
                v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
                put_u16(f, (uint16_t)(int16_t)v);
                sum += a * a;
                if (a > peak) peak = a;
            }
            frame += n;
        }
        fclose(f);
        {
            double rms = sqrt(sum / (2.0 * total));
            printf("%2u %-16s %3u BPM  %s  peak %6.1f dBFS  rms %6.1f dBFS%s\n", (unsigned)s, FP_TAB[s].name, (unsigned)bpm,
                   FA_TAB[s].maq ? "ARP" : "SEQ", 20.0 * log10(peak + 1e-9), 20.0 * log10(rms + 1e-9),
                   rms < 0.003 ? "  (near silent!)" : "");
            silent += rms < 0.003;
        }
    }
    return silent ? 1 : 0;
}
