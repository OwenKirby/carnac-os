/* Host checks for the 512-sample wavetables, the store image they come from and the oscillator that reads them.
 * Build (from src/dsp):
 *   gcc -std=c11 -Wall -Wextra -I. test_wavetables.c wavetables.c wavetable_attach.c wavetables_image.c oscillators.c -lm
 * Run:                   ./a.out      (exit status is the number of failed checks)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "wavetables.h"
#include "oscillators.h"

static int failures = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            failures++;                                   \
            printf("FAIL: " __VA_ARGS__);                 \
            printf("\n");                                 \
        }                                                 \
    } while (0)

/* Every frame at every mip level: inside the headroom and no DC. Level 0 is also not near-silent: the top
 * levels keep only a few harmonics, so a bright frame can be quiet there by design. */
static void test_table(void)
{
    for (int level = 0; level < WT_LEVELS; level++) {
        int len = WT_LEVEL_SAMPLES[level];
        for (int b = 0; b < NUM_BANKS; b++) {
            for (int f = 0; f < NUM_MORPH_STEPS; f++) {
                const int16_t *frame = wavetable_frame(b, f, level);
                long sum = 0;
                double energy = 0.0;
                int peak = 0;
                for (int i = 0; i < len; i++) {
                    int v = frame[i];
                    sum += v;
                    energy += (double)v * (double)v;
                    if (abs(v) > peak) peak = abs(v);
                }
                double mean = (double)sum / len;
                double rms = sqrt(energy / len);
                CHECK(peak <= 31000, "level %d bank %d frame %d peak %d above headroom", level, b, f, peak);
                CHECK(fabs(mean) < 64.0, "level %d bank %d frame %d DC offset %.1f", level, b, f, mean);
                if (level == 0)
                    CHECK(rms > 300.0, "bank %d frame %d nearly silent (rms %.1f)", b, f, rms);
            }
        }
    }
}

/* The mip level is the richest one whose top harmonic stays below Nyquist: level L up to 44100 / (512 >> L) Hz. */
static void test_levels(void)
{
    static const struct { double hz; int level; } cases[] = {
        {20.0, 0}, {86.0, 0}, {86.2, 1}, {172.0, 1}, {173.0, 2}, {440.0, 3}, {1000.0, 4},
        {2756.0, 5}, {2757.0, 6}, {10000.0, 6},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t q16 = (uint32_t)(cases[i].hz * 65536.0);
        int got = wt_level_for_freq(q16);
        CHECK(got == cases[i].level, "%.1f Hz picks level %d, expected %d", cases[i].hz, got, cases[i].level);
    }
}

static void test_names(void)
{
    CHECK(strcmp(wavetable_bank_names[0], "Menhir") == 0, "bank 0 name is '%s'", wavetable_bank_names[0]);
    CHECK(strcmp(wavetable_bank_names[NUM_BANKS - 1], "Hearth") == 0,
          "last bank name is '%s'", wavetable_bank_names[NUM_BANKS - 1]);
}

/* The store image: the host build starts attached; a good image attaches, and an erased, damaged or foreign one
 * is refused with every bank on the sine (wavetable_attach.c). */
extern const uint8_t wavetable_image_host[WT_STORE_IMAGE_LEN];
static uint8_t image_copy[WT_STORE_IMAGE_LEN] __attribute__((aligned(4)));

static int all_sine(void)
{
    for (int b = 0; b < NUM_BANKS; b++)
        for (int f = 0; f < NUM_MORPH_STEPS; f++)
            if (wavetable_frame(b, f, 0) != wavetable_sine) return 0;
    return 1;
}

static void test_store(void)
{
    CHECK(wavetable_attached(), "host build does not start attached");
    CHECK(wavetable_frame(1, 3, 0) == (const int16_t *)(wavetable_image_host + WT_STORE_DATA_OFF) + (1 * 8 + 3) * 1024,
          "frame (1, 3) is not where the image puts it");
    CHECK(wavetable_attach(wavetable_image_host) == 0, "the host image does not attach");

    memset(image_copy, 0xFF, sizeof image_copy);                          /* erased flash */
    CHECK(wavetable_attach(image_copy) == -2 && all_sine() && !wavetable_attached(), "erased store not refused");

    memcpy(image_copy, wavetable_image_host, sizeof image_copy);
    image_copy[WT_STORE_DATA_OFF + 12345] ^= 0x40;                        /* one bit off in the data */
    CHECK(wavetable_attach(image_copy) == -4 && all_sine(), "damaged data not refused");

    memcpy(image_copy, wavetable_image_host, sizeof image_copy);
    wt_store_hdr_t *h = (wt_store_hdr_t *)(void *)image_copy;             /* a valid header for another bank list */
    h->set_id ^= 1u;
    h->hcrc = wavetable_crc32(0, h, sizeof *h - 4u);
    CHECK(wavetable_attach(image_copy) == -3 && all_sine(), "image of another bank list not refused");

    memcpy(image_copy, wavetable_image_host, sizeof image_copy);          /* a copy elsewhere attaches there */
    CHECK(wavetable_attach(image_copy) == 0 && wavetable_frame(13, 7, 6) ==
              (const int16_t *)(void *)(image_copy + WT_STORE_DATA_OFF) + (13 * 8 + 7) * 1024 + 1008,
          "a good copy does not attach");
    CHECK(wavetable_attach(image_copy + 2) == -1, "misaligned image accepted");
    CHECK(wavetable_attach(wavetable_image_host) == 0, "the host image does not attach again");
}

/* Without an image the oscillator plays the sine at any bank and scan, in tune. */
static void test_detached(void)
{
    wavetable_detach();
    WavetableOsc osc;
    osc_init(&osc);
    osc.bank = NUM_BANKS - 1;
    osc.morph_q12 = 3 << 11;                                              /* between two frames */
    uint32_t freq_q16 = (uint32_t)(220.0 * 65536.0);
    int prev = 0, crossings = 0, peak = 0;
    for (int i = 0; i < 44100; i++) {
        int v = osc_process_skulpt_spread_fixed(&osc, freq_q16, 0);
        if (i > 0 && ((v >= 0) != (prev >= 0))) crossings++;
        if (abs(v) > peak) peak = abs(v);
        prev = v;
    }
    CHECK(abs(crossings - 440) <= 3, "detached: %d zero crossings at 220 Hz, expected 440", crossings);
    CHECK(peak > 4500 && peak < 5500, "detached: sine peak %d", peak);   /* wavetable_sine: ~4926, about -16 dB */
    CHECK(wavetable_attach(wavetable_image_host) == 0, "re-attach failed");
}

/* Bank 0, frame 0 is a pure sine. One second at f Hz must cross zero 2f times, within 0.5 %. */
static void test_pitch(void)
{
    static const double hz[] = {55.0, 440.0, 1760.0};
    for (int n = 0; n < 3; n++) {
        WavetableOsc osc;
        osc_init(&osc);
        osc.bank = 0;
        osc.morph_q12 = 0;
        uint32_t freq_q16 = (uint32_t)(hz[n] * 65536.0);
        int prev = 0;
        int crossings = 0;
        for (int i = 0; i < 44100; i++) {
            int v = osc_process_skulpt_spread_fixed(&osc, freq_q16, 0);
            if (i > 0 && ((v >= 0) != (prev >= 0))) crossings++;
            prev = v;
        }
        double expected = 2.0 * hz[n];
        CHECK(fabs((double)crossings - expected) <= 0.005 * expected + 1.0,
              "%.0f Hz: %d zero crossings in 1 s, expected %.0f", hz[n], crossings, expected);
    }
}

int main(void)
{
    test_table();
    test_levels();
    test_names();
    test_store();
    test_detached();
    test_pitch();
    if (failures == 0) printf("wavetable tests: all passed\n");
    else printf("wavetable tests: %d failed\n", failures);
    return failures;
}
