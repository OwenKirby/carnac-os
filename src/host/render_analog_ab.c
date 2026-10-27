/* ANALOG A/B: the same short scenes at ANALOG 0, 50, 90 and 127, as WAVs to compare by ear, and the host cost of the
 * control tick's ANALOG work (8 held voices, 0 against 127; the FM1's own figure comes from PERF TEST).
 *
 *   render_analog_ab <out dir>      writes <dir>/<scene>_analog<NNN>.wav
 *
 * Scenes: 1 a held DUAL OSC chord (the oscillators beat and drift), 2 one bass note repeated (each note's scatter, the
 * voices' different cutoffs), 3 poly chord stabs (the voices' character), 4 a long SINGLE pad note held (pitch drift). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "PresetStructures.h"
#include "preset.h"
#include "dsp/synth_runtime.h"

#define RATE 44100u
#define BLOCK 32u

static int32_t buf[BLOCK * 2u];

static void put_u32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void put_u16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

typedef struct { uint32_t frame; uint8_t status, note, vel; } Ev;

static void patch(int scene, uint8_t analog)
{
    EngineState *e = &engine_state[0];
    live_defaults();
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    e->analog = analog;
    e->amp.reverb_send = 30;
    switch (scene) {
    case 1:                                          /* DUAL OSC pad: both oscillators at the same pitch */
        e->arch = VARCH_DUAL_OSC;
        e->filter[0].cutoff = 72; e->filter[0].resonance = 30; e->filter[0].mod_amt = 0;
        e->env[0].attack = 50; e->env[0].decay = 60; e->env[0].sustain = 120; e->env[0].release = 70;
        break;
    case 2:                                          /* SINGLE bass, ENV2 into the cutoff */
        e->arch = VARCH_SINGLE;
        e->filter[0].cutoff = 38; e->filter[0].resonance = 70; e->filter[0].mod_amt = 92;
        e->env[1].attack = 0; e->env[1].decay = 38; e->env[1].sustain = 0; e->env[1].release = 30;
        e->env[0].attack = 0; e->env[0].decay = 50; e->env[0].sustain = 80; e->env[0].release = 20;
        e->osc[0].octave = 46;                       /* -1 octave */
        break;
    case 3:                                          /* SINGLE stabs */
        e->arch = VARCH_SINGLE;
        e->filter[0].cutoff = 55; e->filter[0].resonance = 50; e->filter[0].mod_amt = 72;
        e->env[1].attack = 0; e->env[1].decay = 45; e->env[1].sustain = 10; e->env[1].release = 40;
        e->env[0].attack = 0; e->env[0].decay = 55; e->env[0].sustain = 40; e->env[0].release = 45;
        break;
    default:                                         /* SINGLE pad, one long note */
        e->arch = VARCH_SINGLE;
        e->filter[0].cutoff = 80; e->filter[0].resonance = 20; e->filter[0].mod_amt = 0;
        e->env[0].attack = 30; e->env[0].sustain = 127; e->env[0].release = 60;
        break;
    }
    synth_restart();
}

static int script(int scene, Ev *ev, uint32_t *len)
{
    int n = 0;
    uint32_t beat = RATE / 2u;                       /* 120 BPM */
    if (scene == 1) {
        static const uint8_t ch[4] = {48, 52, 55, 59};
        for (int i = 0; i < 4; i++) ev[n++] = (Ev){0, 0x90, ch[i], 100};
        for (int i = 0; i < 4; i++) ev[n++] = (Ev){RATE * 10u, 0x80, ch[i], 0};
        *len = RATE * 13u;
    } else if (scene == 2) {
        for (uint32_t s = 0; s < 32; s++) {
            ev[n++] = (Ev){s * beat / 2u, 0x90, 36, 100};
            ev[n++] = (Ev){s * beat / 2u + beat / 4u, 0x80, 36, 0};
        }
        *len = 17u * beat;
    } else if (scene == 3) {
        static const uint8_t ch[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
        for (uint32_t b = 0; b < 16; b++)
            for (int i = 0; i < 4; i++) {
                ev[n++] = (Ev){b * beat, 0x90, ch[(b / 2) % 4][i], 100};
                ev[n++] = (Ev){b * beat + beat / 3u, 0x80, ch[(b / 2) % 4][i], 0};
            }
        *len = 18u * beat;
    } else {
        ev[n++] = (Ev){0, 0x90, 57, 100};
        ev[n++] = (Ev){RATE * 15u, 0x80, 57, 0};
        *len = RATE * 17u;
    }
    return n;
}

static int ev_cmp(const void *a, const void *b)
{
    const Ev *x = a, *y = b;
    return x->frame != y->frame ? (x->frame < y->frame ? -1 : 1) : (int)x->status - (int)y->status;   /* offs first */
}

static void render(const char *path, int scene, uint8_t analog)
{
    static Ev ev[256];
    uint32_t len, frame = 0;
    int n = script(scene, ev, &len), k = 0;
    qsort(ev, (size_t)n, sizeof ev[0], ev_cmp);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    patch(scene, analog);
    fwrite("RIFF", 1, 4, f); put_u32(f, 36u + len * 4u); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put_u32(f, 16u); put_u16(f, 1); put_u16(f, 2);
    put_u32(f, RATE); put_u32(f, RATE * 4u); put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, len * 4u);
    while (frame < len) {
        while (k < n && ev[k].frame <= frame) {
            synth_midi_3(ev[k].status, ev[k].note, ev[k].vel);
            k++;
        }
        synth_render_q15(buf, BLOCK);
        for (uint32_t i = 0; i < BLOCK * 2u; i++) put_u16(f, (uint16_t)(int16_t)buf[i]);
        frame += BLOCK;
    }
    fclose(f);
}

/* host ns a sample with 8 voices held (SINGLE), ANALOG 0 or 127, best of 5 */
static double cost(uint8_t analog)
{
    double best = 1e30;
    for (int r = 0; r < 5; r++) {
        patch(4, analog);
        for (int i = 0; i < 8; i++) synth_midi_3(0x90, (uint8_t)(48 + i * 3), 100);
        clock_t t0 = clock();
        for (uint32_t fr = 0; fr < RATE * 4u; fr += BLOCK) synth_render_q15(buf, BLOCK);
        double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / (RATE * 4.0);
        if (ns < best) best = ns;
    }
    return best;
}

int main(int argc, char **argv)
{
    static const char *const name[5] = {"", "1_dualosc_chord", "2_bass_repeats", "3_chord_stabs", "4_held_note"};
    static const uint8_t amt[4] = {0, 50, 90, 127};
    char path[512];
    if (argc < 2) { fprintf(stderr, "usage: render_analog_ab <out dir>\n"); return 1; }
    synth_init();
    for (int s = 1; s <= 4; s++)
        for (int a = 0; a < 4; a++) {
            snprintf(path, sizeof path, "%s/%s_analog%03u.wav", argv[1], name[s], amt[a]);
            render(path, s, amt[a]);
        }
    double c0 = cost(0), c1 = cost(127);
    printf("wrote 16 WAVs to %s\nhost cost, 8 voices: ANALOG 0 %.0f ns/sample, 127 %.0f ns/sample (%+.1f %%)\n",
           argv[1], c0, c1, (c1 - c0) * 100.0 / c0);
    return 0;
}
