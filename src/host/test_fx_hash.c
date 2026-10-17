/* Host test: the effects' output hashes (src/dsp/fx.c), every reverb type with the tape echo, PING PONG off and on,
 * TAPE DEGRADE, over 8 s of noise then silence, compared with src/host/fx_hash_golden.txt. "hot" rows send the noise
 * at about 4x full scale, as a loud chord's summed sends do: the sends' limiters (2026-10-16) and the headroom after.
 * "bbd" rows: the BBD delay (DELAY TYPE, 2026-10-16) with PING PONG and a worn DEGRADE. The render hashes play the
 * factory effects only (TOMB = the old ROOM network); this covers the tank (MOD, CELESTIAL, DOLMEN, TUMULUS) and the
 * stereo delay. A change meant to keep the sound must leave every line; one meant to alter it: listen, then --write.
 *   test_fx_hash [--write] [golden file]     (default src/host/fx_hash_golden.txt, run from the repo root) */
#include <stdio.h>
#include <string.h>
#include "PresetStructures.h"
#include "dsp/fx.h"

int main(int argc, char **argv)
{
    static const int types[] = {REVTYPE_TOMB, REVTYPE_MOD, REVTYPE_CELESTIAL, REVTYPE_DOLMEN,
                                REVTYPE_TUMULUS, REVTYPE_SPRING};
    const char *path = "src/host/fx_hash_golden.txt";
    int writing = 0, fails = 0, lines = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--write") == 0) writing = 1;
        else path = argv[i];
    }
    FILE *golden = fopen(path, writing ? "w" : "r");
    if (!golden) { printf("cannot open %s\n", path); return 1; }
    for (unsigned k = 0; k < sizeof types / sizeof types[0]; k++)
        for (int pp = 0; pp < 4; pp++) {
            int hot = pp == 2, bbd = pp == 3;
            static MasterPerformancePreset p;
            memset(&p, 0, sizeof p);
            p.reverb_type = (uint8_t)types[k];
            p.reverb_decay = 100; p.reverb_damp = 50; p.reverb_predelay = 20;
            p.delay_time = 40; p.delay_feedback = 80; p.delay_degrade = (uint8_t)(pp ? 110 : 36);
            p.delay_pingpong = (uint8_t)(pp & 1);
            p.delay_type = (uint8_t)(bbd ? DELAYTYPE_BBD : DELAYTYPE_TAPE);
            fx_init();
            unsigned h = 2166136261u, seed = 1;
            int32_t d[128], r[128], w[128];
            for (int s = 0; s < 44100 * 8; s += 128) {
                for (int i = 0; i < 128; i++) {
                    seed = seed * 1103515245u + 12345u;
                    int32_t v = (s + i < 44100 * 2) ? (int32_t)(int16_t)(seed >> 16) / 3 : 0;
                    if (hot) v *= 12;
                    d[i] = r[i] = v;
                }
                fx_process_block(&p, d, r, w, 128);
                for (int i = 0; i < 128; i++) {
                    h ^= (unsigned)r[i]; h *= 16777619u;
                    h ^= (unsigned)w[i]; h *= 16777619u;
                }
            }
            char line[64], want[64];
            if (hot)
                snprintf(line, sizeof line, "reverb %d hot         %08x", types[k], h);
            else if (bbd)
                snprintf(line, sizeof line, "reverb %d bbd         %08x", types[k], h);
            else
                snprintf(line, sizeof line, "reverb %d pingpong %d  %08x", types[k], pp, h);
            lines++;
            if (writing) { fprintf(golden, "%s\n", line); continue; }
            if (!fgets(want, sizeof want, golden)) want[0] = 0;
            want[strcspn(want, "\r\n")] = 0;
            if (strcmp(want, line)) { printf("DIFF: got \"%s\", golden \"%s\"\n", line, want); fails++; }
        }
    fclose(golden);
    if (writing) printf("wrote %d lines to %s\n", lines, path);
    else if (fails) printf("%d of %d fx hashes differ\n", fails, lines);
    else printf("all %d fx hashes match\n", lines);
    return fails;
}
