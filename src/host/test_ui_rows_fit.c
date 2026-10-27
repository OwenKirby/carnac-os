/* Every menu row fits the page's list at 2x (src/ui_page_draw.c pu_draw_menu, since 2026-10-26): its label, a letter's
 * gap and its longest value in at most PU_ROW_CHARS letters, so no label is ever drawn smaller. SEQ, ARP, SAVE and
 * the MOD pages draw their rows their own way and are left out.
 *
 *   gcc -Isrc -Isrc/dsp -Isrc/hardware $(DSP) src/host/test_ui_rows_fit.c -o test_ui_rows_fit
 * Exit code 0 = every row fits; otherwise each one too long is listed. */
#include <stdio.h>
#include <string.h>
#include "../UIStateMachine.c"

#define PU_ROW_CHARS 18          /* 6 .. 230 at 12 px a letter: label + gap + value, 12 (nl + nv) + 8 <= 224 */

static int value_chars(const UiRow *r)
{
    int v, n, w = 3;                                     /* a plain 0..127 */
    if (r->names != NULL) {
        for (w = 0, v = r->min; v <= r->max; v++)
            if ((n = (int)strlen(r->names[v])) > w)
                w = n;
        return w;
    }
    if (r->kind == RK_WAVE) return 12;                   /* a wave's name: buf13 */
    if (r->kind == RK_FINE) return 5;                    /* "+100C" */
    if (r->flags & RF_BPM) return 7;                     /* "167 BPM" */
    if (r->flags & RF_NOTE) return 4;                    /* "C#-1" */
    return w;
}

int main(void)
{
    unsigned p, i, bad = 0, rows = 0;
    for (p = 0; p < sizeof UI_PAGES / sizeof UI_PAGES[0]; p++) {
        const UiPage *g = &UI_PAGES[p];
        if (g->rows == ARP_ROWS || g->rows == SEQ_ROWS || g->rows == SAVE_COMMIT_ROWS || g->rows == MOD_ROWS)
            continue;
        for (i = 0; i < g->nrows; i++) {
            const UiRow *r = &g->rows[i];
            int nl = (int)strlen(r->label), nv = value_chars(r);
            rows++;
            if (nl + nv > PU_ROW_CHARS) {
                printf("too long: page %u \"%s\" (%d) + value (%d) = %d > %d\n", p, r->label, nl, nv, nl + nv,
                       PU_ROW_CHARS);
                bad++;
            }
        }
    }
    if (bad)
        printf("%u of %u rows too long for 2x\n", bad, rows);
    else
        printf("all %u menu rows fit at 2x\n", rows);
    return bad != 0;
}
