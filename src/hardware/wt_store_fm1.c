/* The wavetable store's editor commands on the FM1 (spec 6.5): a unity-build fragment, like src/ui_seq_draw.c.
 *
 * Since 2026-10-16 the wavetable image is in the firmware (src/dsp/wavetables_image.c, read in place through XIP,
 * attached from the start: wavetable_attach.c), and the store flash it was uploaded to (WT_STORE_BASE..+WT_STORE_SIZE,
 * 0xA0000..0xDBFFF) holds the saved presets (synth_main_fm1.c, the preset store). So the upload commands are refused
 * and nothing here writes the flash; the editor's SysEx framing (F0 7D 46 4C cmd args F7) is kept so a tool sees why:
 *
 *   96 WT_BEGIN  -> rc 1                     98 WT_END -> rc 1
 *   97 WT_WRITE  -> offset (3 x 7 bit, as sent), rc 1
 *   99 WT_INFO   -> attached, banks, set id (5 x 7 bit), store KiB (0: no store), then one name string per bank
 *
 * The sample-slot commands SMP_BEGIN / WRITE / END / ERASE are refused (rc 1) as before; SMP_INFO answers in
 * synth_main_fm1.c with the three slots empty.
 *
 * Glue: synth_main_fm1.c includes this file after its editor helpers (ed_b, ed_str) and routes the WT and SMP commands
 * here from ed_handle.
 */
#include "wavetables.h"

enum { ED_WT_BEGIN = 96, ED_WT_WRITE, ED_WT_END, ED_WT_INFO };

/* the editor's commands for the store: handled (reply built) -> 1 */
static int ed_wt_handle(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i, off;
    switch (cmd) {
    case ED_WT_BEGIN:
    case ED_WT_END:
        ed_b(1);                                           /* the image is in the firmware: nothing to upload */
        return 1;
    case ED_WT_WRITE:
        off = na >= 4u ? (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 : 0u;
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_b(1);
        return 1;
    case ED_WT_INFO:
        ed_b((uint32_t)wavetable_attached());
        ed_b(NUM_BANKS);
        for (i = 0; i < 5u; i++)
            ed_b(wavetable_set_id >> (7u * i));
        ed_b(0);                                           /* no store: the image is in the firmware */
        for (i = 0; i < NUM_BANKS; i++)
            ed_str(wavetable_bank_names[i], 20);
        return 1;
    case ED_SMP_BEGIN: case ED_SMP_ERASE: case ED_SMP_END:  /* the sample slots are the preset store's */
        ed_b(na ? a[0] : 0u);
        ed_b(1);
        return 1;
    case ED_SMP_WRITE:
        ed_b(na ? a[0] : 0u);
        for (i = 1; i < 4u; i++)
            ed_b(na > i ? a[i] : 0u);
        ed_b(1);
        return 1;
    default:
        return 0;
    }
}
