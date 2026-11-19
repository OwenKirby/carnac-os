/* The wavetables' editor command on the FM1 (spec 6.5): a unity-build fragment, like src/ui_seq_draw.c.
 *
 * Since 2026-10-16 the wavetable image is in the firmware (src/dsp/wavetables_image.c, read in place through XIP,
 * attached from the start: wavetable_attach.c), and the store flash it was uploaded to holds the saved presets. The
 * upload commands (96 WT_BEGIN, 97 WT_WRITE, 98 WT_END) and X0X's sample-slot commands (11..16) went 2026-11-18 (the
 * CC map's flash): they only refused. What is left (SysEx framing F0 7D 46 4C cmd args F7):
 *
 *   99 WT_INFO   -> attached, banks, set id (5 x 7 bit), store KiB (0: no store), then one name string per bank
 *
 * Glue: synth_main_fm1.c includes this file after its editor helpers (ed_b, ed_str) and routes the command here.
 */
#include "wavetables.h"

enum { ED_WT_INFO = 99 };

/* the editor's command for the wavetables: handled (reply built) -> 1 */
static int ed_wt_handle(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i;
    (void)a;
    (void)na;
    if (cmd != ED_WT_INFO)
        return 0;
    ed_b((uint32_t)wavetable_attached());
    ed_b(NUM_BANKS);
    for (i = 0; i < 5u; i++)
        ed_b(wavetable_set_id >> (7u * i));
    ed_b(0);                                               /* no store: the image is in the firmware */
    for (i = 0; i < NUM_BANKS; i++)
        ed_str(wavetable_bank_names[i], 20);
    return 1;
}
