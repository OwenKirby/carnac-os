/* Listening showcase of every attack and PCM wave, played by the real voice (2026-10-26: the user is choosing which to
 * keep). Each sound is OSC1's wave in SPREAD at SPREAD 0 (SINGLE's path), FILTER1 open, no resonance, no effects, so
 * what you hear is the sample itself, as the FM1 reads it (mu-law, loops, the wave's own attack).
 *   attacks: each at C4, C3 and G4 (the key tracking), one file each and a reel
 *   waves:   a held chord at SCAN 0, the SCAN swept 0 -> 127 under it, then a melody at SCAN 64
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/host/pcm_images_host.c src/host/render_pcm_showcase.c -lm -o render_pcm_showcase
 *   render_pcm_showcase <out dir>      (the dir must exist: writes attacks/ and waves/ files beside the reels)
 * Test scaffolding only. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/pcm_store.h"
#include "dsp/pcm_osc.h"
#include "dsp/attacks.h"

extern const uint8_t pcm_image_host0[], pcm_image_host1[];
extern const uint32_t pcm_image_host_len0, pcm_image_host_len1;

#define RATE 44100u
static int32_t buf[2 * 128];
static FILE *one, *reel;

static void put_u32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 255, f); }
static FILE *wav_open(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return NULL; }
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
    put_u32(f, 16); put_u32(f, 1 | 2u << 16); put_u32(f, RATE); put_u32(f, RATE * 4); put_u32(f, 4 | 16u << 16);
    fwrite("data\0\0\0\0", 1, 8, f);
    return f;
}
static void wav_close(FILE *f)
{
    long n = ftell(f);
    fseek(f, 4, SEEK_SET); put_u32(f, (uint32_t)(n - 8));
    fseek(f, 40, SEEK_SET); put_u32(f, (uint32_t)(n - 44));
    fclose(f);
}

/* frames of the engine into the sound's file and the reel; scan_to >= 0 moves OSC1's SCAN there linearly */
static void render(double sec, int scan_to)
{
    uint32_t frames = (uint32_t)(sec * RATE), done = 0;
    int scan_from = engine_state[0].osc[0].scan;
    while (done < frames) {
        uint32_t n = frames - done < 128u ? frames - done : 128u;
        if (scan_to >= 0)
            engine_state[0].osc[0].scan = (uint8_t)(scan_from + (scan_to - scan_from) * (int)done / (int)frames);
        synth_render_q15(buf, n);
        for (uint32_t i = 0; i < 2 * n; i++) {
            int32_t s = buf[i] > 32767 ? 32767 : buf[i] < -32768 ? -32768 : buf[i];
            fputc(s & 255, one); fputc((s >> 8) & 255, one);
            fputc(s & 255, reel); fputc((s >> 8) & 255, reel);
        }
        done += n;
    }
}
static void on(uint8_t note) { synth_midi_3(0x90, note, 100); }
static void off(uint8_t note) { synth_midi_3(0x80, note, 0); }

static void patch(uint8_t code, uint8_t scan)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->arch = VARCH_SPREAD;
    e->oscmix.spread = 0;
    e->oscmix.spread_mod_amt = 0;
    e->oscmix.sub_level = 0;
    e->oscmix.noise_level = 0;
    e->osc[0].wavebank = code;
    e->osc[0].scan = scan;
    e->osc[0].scan_mod_amt = 0;
    e->osc[0].pitch_mod_amt = 0;
    e->filter[0].cutoff = 127;
    e->filter[0].resonance = 0;
    e->filter[0].mod_amt = 0;
    e->env[0].attack = 0; e->env[0].decay = 0; e->env[0].sustain = 127; e->env[0].release = 40;
    e->amp.reverb_send = 0;
    e->amp.delay_send = 0;
    e->amp.drive = 0;
    synth_restart();
}

static void file_name(char *dst, size_t n, const char *dir, const char *sub, int i, const char *name)
{
    char clean[16];
    size_t k;
    for (k = 0; name[k] && k < sizeof clean - 1; k++)
        clean[k] = (name[k] == ' ' || name[k] == '+' || name[k] == '/') ? '_' : name[k];
    clean[k] = 0;
    snprintf(dst, n, "%s/%s/%02d %s.wav", dir, sub, i, clean);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[512], nm[13];
    if (pcm_store_attach(0, pcm_image_host0, pcm_image_host_len0) <= 0 ||
        pcm_store_attach(1, pcm_image_host1, pcm_image_host_len1) <= 0) {
        printf("the PCM images do not attach\n");
        return 1;
    }
    synth_init();

    /* the attacks: wave codes WAVE_ATK_FIRST.., each struck at C4, C3 and G4 */
    snprintf(path, sizeof path, "%s/_reel attacks.wav", dir);
    if (!(reel = wav_open(path))) return 1;
    for (int i = 0; i < (int)ATK_COUNT; i++) {
        uint8_t code = (uint8_t)(WAVE_ATK_FIRST + i);
        static const uint8_t notes[3] = {60, 48, 67};
        const char *name = wave_name(code, nm);   /* (an attack's name is its own string, not copied to nm) */
        file_name(path, sizeof path, dir, "attacks", i + 1, name);
        if (!(one = wav_open(path))) return 1;
        patch(code, 0);
        for (int k = 0; k < 3; k++) {
            on(notes[k]);
            render(0.9, -1);
            off(notes[k]);
            render(0.25, -1);
        }
        render(0.6, -1);
        wav_close(one);
        printf("attack %2d  %s\n", i + 1, name);
    }
    wav_close(reel);

    /* the PCM waves: every store wave the wave list holds (codes WAVE_PCM_FIRST..) */
    snprintf(path, sizeof path, "%s/_reel waves.wav", dir);
    if (!(reel = wav_open(path))) return 1;
    int w = 0;
    for (uint32_t p = 0; p < wave_list_count(); p++) {
        uint8_t code = wave_list_code(p);
        static const uint8_t chord[4] = {48, 55, 60, 64};
        static const uint8_t tune[9] = {60, 62, 64, 67, 69, 67, 64, 62, 60};
        if (code < WAVE_PCM_FIRST)
            continue;
        const char *name = wave_name(code, nm);
        file_name(path, sizeof path, dir, "waves", ++w, name);
        if (!(one = wav_open(path))) return 1;
        patch(code, 0);
        for (int k = 0; k < 4; k++) on(chord[k]);
        render(2.5, -1);                          /* SCAN 0: loop A */
        render(3.5, 127);                         /* swept to loop B */
        render(1.0, -1);
        for (int k = 0; k < 4; k++) off(chord[k]);
        render(1.0, -1);
        engine_state[0].osc[0].scan = 64;         /* between the two */
        for (int k = 0; k < 9; k++) {
            on(tune[k]);
            render(k == 8 ? 1.2 : 0.33, -1);
            off(tune[k]);
            render(0.05, -1);
        }
        render(1.0, -1);
        wav_close(one);
        printf("wave   %2d  %s\n", w, name);
    }
    wav_close(reel);
    return 0;
}
