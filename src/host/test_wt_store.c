/* Host test of the FM1 wavetable image and its editor commands (src/hardware/wt_store_fm1.c) since 2026-10-16, when
 * the image moved into the firmware: the banks start attached to the linked image (whose header and CRC must hold),
 * and the old upload, replayed from wt_store_upload.py's .syx file, is refused frame by frame without touching the
 * flash, which holds the saved presets now.
 *   python src/hardware/wt_store_upload.py syx upload.syx
 *   gcc -O1 -Wall -Wextra -Isrc -Isrc/dsp -Isrc/hardware src/host/test_wt_store.c src/dsp/wavetable_attach.c \
 *       src/dsp/wavetables.c src/dsp/wavetables_image.c -o test_wt_store && ./test_wt_store upload.syx
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wavetables.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* the stand-ins the fragment needs (synth_main_fm1.c's editor helpers) */
enum { ED_SMP_BEGIN = 11, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO };
static uint8_t reply[600];
static uint32_t reply_n;
static void ed_b(uint32_t v) { if (reply_n < sizeof reply) reply[reply_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_str(const char *s, uint32_t max)
{
    for (uint32_t i = 0; s && s[i] && i < max; i++) ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}

#include "wt_store_fm1.c"

extern const uint8_t wavetable_image_host[WT_STORE_IMAGE_LEN];

/* one request: cmd + args -> the reply's rc (WT_WRITE / SMP_WRITE: after the offset), -1 when not handled */
static int req(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    reply_n = 0;
    if (!ed_wt_handle(cmd, a, na)) return -1;
    return reply[cmd == ED_WT_WRITE ? 3 : cmd == ED_SMP_WRITE ? 4 : cmd == ED_SMP_BEGIN || cmd == ED_SMP_END ? 1 : 0];
}

static int frames_on_image(void)
{
    const int16_t *data = (const int16_t *)(const void *)(wavetable_image_host + WT_STORE_DATA_OFF);
    for (int i = 0; i < NUM_BANKS * NUM_MORPH_STEPS; i++)
        if (wavetable_frames[i] != data + i * WT_FRAME_SAMPLES) return 0;
    return 1;
}

/* the old tool's upload, frame by frame: every frame refused (rc 1) */
static void replay(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { CHECK(0, "cannot open %s", path); return; }
    static uint8_t syx[1 << 20];
    size_t n = fread(syx, 1, sizeof syx, f), i = 0;
    fclose(f);
    int count = 0, refused = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && syx[j] != 0xF7) j++;
        if (j >= n || syx[i] != 0xF0 || syx[i + 1] != 0x7D || syx[i + 2] != 0x46 || syx[i + 3] != 0x4C) {
            CHECK(0, "malformed frame at byte %zu", i);
            return;
        }
        refused += req(syx[i + 4], syx + i + 5, (uint32_t)(j - i - 5)) == 1;
        count++;
        i = j + 1;
    }
    CHECK(count == 2 + (int)(WT_STORE_DATA_LEN / 256u), "the upload has %d frames", count);
    CHECK(refused == count, "%d of the old upload's %d frames not refused", count - refused, count);
}

int main(int argc, char **argv)
{
    /* the start: attached to the linked image, which is whole */
    CHECK(wavetable_attached() && frames_on_image(), "the banks do not start on the linked image");
    CHECK(wavetable_attach(wavetable_image_host) == 0 && frames_on_image(), "the linked image's header or CRC is bad");

    /* the old upload: refused, and nothing changes (this build has no flash hooks at all: the fragment cannot write) */
    if (argc > 1) replay(argv[1]);
    CHECK(req(ED_WT_BEGIN, NULL, 0) == 1 && req(ED_WT_END, NULL, 0) == 1, "WT_BEGIN / WT_END not refused");
    { uint8_t a[4] = {0, 0x20, 0, 0};
      CHECK(req(ED_WT_WRITE, a, sizeof a) == 1 && reply[0] == 0 && reply[1] == 0x20, "WT_WRITE not refused (or offset)"); }
    CHECK(wavetable_attached() && frames_on_image(), "a refused upload detached the banks");

    /* WT_INFO: attached, NUM_BANKS, the set id, no store, the names */
    CHECK(req(ED_WT_INFO, NULL, 0) == 1 && reply[1] == NUM_BANKS && reply[7] == 0, "WT_INFO header");
    uint32_t id = 0;
    for (int i = 0; i < 5; i++) id |= (uint32_t)reply[2 + i] << (7 * i);
    CHECK(id == wavetable_set_id, "WT_INFO set id 0x%08X", id);
    CHECK(!strcmp((const char *)reply + 8, wavetable_bank_names[0]), "WT_INFO first name");

    /* the sample slots stay refused; SMP_INFO is the glue's */
    uint8_t slot = 1, w[4 + 10] = {1, 0, 8, 0};
    CHECK(req(ED_SMP_BEGIN, &slot, 1) == 1 && req(ED_SMP_ERASE, &slot, 1) == 1 &&
          req(ED_SMP_WRITE, w, sizeof w) == 1 && req(ED_SMP_END, &slot, 1) == 1, "a sample-slot command got through");
    CHECK(req(ED_SMP_INFO, NULL, 0) == -1, "SMP_INFO should stay the glue's");

    printf(failures ? "%d wavetable checks failed\n" : "wavetable image tests: all passed\n", failures);
    return failures;
}
