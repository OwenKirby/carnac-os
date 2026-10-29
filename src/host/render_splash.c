/* Host preview of the CARNAC OS start-up screen (src/carnac_splash.c) through the same stand-in canvas as the page
 * previews (felucca_canvas_shim.h). Test scaffolding only.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/render_splash.c -o render_splash
 *   render_splash OUTDIR [VERSION] [THEME] [nowaves]   (nowaves: the empty field of a firmware without its waves) writes OUTDIR/ui_splash.bin + .log (THEME: UITHEME_* number, default 0
 *                                            SAMHAIN), then:
 *   python src/host/seq_screen_png.py png OUTDIR/ui_splash.bin OUTDIR/ui_splash.log splash.png 2
 * Also prints how many band blits it took and checks every pixel was drawn (no black hole left by a band). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/midi_clock.h"
#include "felucca_canvas_shim.h"

static int blits;
static void shim_on_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const ShimText *t, int n)
{
    (void)x; (void)y; (void)w; (void)h; (void)t; (void)n;
    blits++;
}

#include "../ui_seq_draw.c"
#include "../carnac_splash.c"

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[512];
    FILE *f;
    int i, unset = 0;
    for (i = 0; i < 240 * 240; i++)
        shim_fb[i] = 0xF81Fu;                       /* magenta: any pixel left like this was never drawn */
    if (argc > 4 && !strcmp(argv[4], "nowaves")) {  /* the start-up screen of a firmware without its waves */
        sp_notice[0] = "WAVES MISSING";
        sp_notice[1] = "RUN THE INSTALLER";
    }
    carnac_splash(argc > 2 ? argv[2] : "DEV", argc > 3 ? (uint32_t)atoi(argv[3]) : UITHEME_SAMHAIN);
    for (i = 0; i < 240 * 240; i++)
        unset += shim_fb[i] == 0xF81Fu;
    printf("splash: %d blits, %d pixels never drawn\n", blits, unset);
    snprintf(p, sizeof p, "%s/ui_splash.log", dir);
    f = fopen(p, "w");
    if (f) fclose(f);
    snprintf(p, sizeof p, "%s/ui_splash.bin", dir);
    f = fopen(p, "wb");
    if (!f) return 1;
    fwrite(shim_fb, sizeof shim_fb, 1, f);
    fclose(f);
    return unset != 0;
}
