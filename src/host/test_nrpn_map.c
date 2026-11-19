/* Host test: the fixed MIDI numbers (2026-11-19). src/nrpn_map.h's NRPN numbers and UIStateMachine.c's CC map, each
 * checked by the control's name against src/host/nrpn_map_golden.txt, so a row added, moved or dropped on a page can
 * never renumber what an editor or a MIDI rig sends: every line must still name the same control, every control a
 * sound or a performance is made of must have a number, no number twice. A change that means it (a new control, its
 * new number) rewrites the golden list: test_nrpn_map --write, then read the diff (lines may only be added). */
#include <stdio.h>
#include <string.h>
#include "../UIStateMachine.c"

#define GOLDEN "src/host/nrpn_map_golden.txt"
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static const char *const PN[] = {"OSC1", "OSC2", "OSCMIX", "FILTER1", "FILTER2", "FLTMIX", "ENV1", "ENV2", "ENV3",
    "LFO1", "LFO2", "LFO3", "MOD", "AMP", "EFFECTS", "MIX", "HOME1", "HOME2", "HOME3", "PERF", "GLOBAL", "TUNING"};
PS_STATIC_ASSERT(sizeof PN / sizeof PN[0] == UIPAGE_TUNING + 1u, "a name for every page up to TUNING");

/* a control by its name: K1..K4 the knobs (they follow the architecture), the row's label, the MFX's */
static const char *label(uint8_t p, uint8_t item, char *buf)
{
    if (p > UIPAGE_TUNING)
        return "?";
    if (item < CCI_ROW) {
        sprintf(buf, "K%u", item + 1u);
        return buf;
    }
    if (p == UIPAGE_AMP && item >= CCI_MFX) {
        if (item < CCI_MFX_TYPE) {
            sprintf(buf, "MFX K%u", item - CCI_MFX + 1u);
            return buf;
        }
        if (item == CCI_MFX_TYPE)
            return "MFX";
        if (item - CCI_MFX_ROW + 1u >= NROWS(MFX_ROWS))
            return "?";
        sprintf(buf, "MFX %s", MFX_ROWS[item - CCI_MFX_ROW + 1u].label);
        return buf;
    }
    if (item - CCI_ROW >= UI_PAGES[p].nrows)
        return "?";
    return UI_PAGES[p].rows[item - CCI_ROW].label;
}

static void line_of(char *out, const char *kind, unsigned a, unsigned b, uint8_t p, uint8_t item)
{
    char buf[40];
    sprintf(out, "%s %3u %3u  %-8s %3u  %s", kind, a, b, PN[p], item, label(p, item, buf));
}

int main(int argc, char **argv)
{
    static char want[400][80], have[400][80];
    unsigned nw = 0u, nh = 0u, i, j;
    FILE *f;
    char l[120];
    for (i = 0u; i < NRPN_N; i++)
        line_of(have[nh++], "nrpn", NRPN_MAP[i].msb, NRPN_MAP[i].lsb, NRPN_MAP[i].page, NRPN_MAP[i].item);
    for (i = 0u; i < sizeof CC_MAP / sizeof CC_MAP[0]; i++)
        line_of(have[nh++], "cc  ", CC_MAP[i].cc, 0u, CC_MAP[i].page, CC_MAP[i].item);
    if (argc > 1 && !strcmp(argv[1], "--write")) {
        f = fopen(GOLDEN, "wb");
        fprintf(f, "# The fixed MIDI numbers (src/host/test_nrpn_map.c): nrpn MSB LSB / cc number 0, the page, its item, "
                   "the control.\n# Frozen at V1: lines are only ever added. K1..K4: the page's knobs by position.\n");
        for (i = 0u; i < nh; i++)
            fprintf(f, "%s\n", have[i]);
        fclose(f);
        printf("wrote %s: %u lines\n", GOLDEN, nh);
        return 0;
    }
    f = fopen(GOLDEN, "rb");
    CHECK(f != NULL, "no %s", GOLDEN);
    if (!f)
        return 1;
    while (fgets(l, sizeof l, f) && nw < 400u) {
        size_t k = strlen(l);
        while (k && (l[k - 1] == '\n' || l[k - 1] == '\r'))
            l[--k] = '\0';
        if (k && l[0] != '#')
            strcpy(want[nw++], l);
    }
    fclose(f);
    /* every golden line still there, naming the same control (by its number) */
    for (i = 0u; i < nw; i++) {
        for (j = 0u; j < nh && strncmp(want[i], have[j], 13); j++)
            ;
        CHECK(j < nh, "number gone: %s", want[i]);
        if (j < nh)
            CHECK(!strcmp(want[i], have[j]), "number now names another control:\n  was  %s\n  now  %s", want[i], have[j]);
    }
    for (j = 0u; j < nh; j++) {
        for (i = 0u; i < nw && strncmp(want[i], have[j], 13); i++)
            ;
        CHECK(i < nw, "new number not in the golden list (test_nrpn_map --write): %s", have[j]);
    }
    /* no number twice, no control twice, nothing that is not a control */
    for (i = 0u; i < NRPN_N; i++) {
        char b[40];
        CHECK(strcmp(label(NRPN_MAP[i].page, NRPN_MAP[i].item, b), "?"), "NRPN %u:%u is no control", NRPN_MAP[i].msb,
              NRPN_MAP[i].lsb);
        CHECK(NRPN_MAP[i].msb < NRPN_ROUTE, "NRPN %u:%u in the routes' numbers", NRPN_MAP[i].msb, NRPN_MAP[i].lsb);
        for (j = 0u; j < i; j++) {
            CHECK(NRPN_MAP[i].msb != NRPN_MAP[j].msb || NRPN_MAP[i].lsb != NRPN_MAP[j].lsb, "NRPN %u:%u twice",
                  NRPN_MAP[i].msb, NRPN_MAP[i].lsb);
            CHECK(NRPN_MAP[i].page != NRPN_MAP[j].page || NRPN_MAP[i].item != NRPN_MAP[j].item,
                  "%s item %u has two NRPNs", PN[NRPN_MAP[i].page], NRPN_MAP[i].item);
        }
    }
    /* every control of the sound and performance pages has a number */
    for (uint8_t p = 0u; p <= UIPAGE_TUNING; p++) {
        uint8_t n;
        if (p == UIPAGE_MOD || p == UIPAGE_PERF || p == UIPAGE_HOME2)
            continue;
        n = (uint8_t)(CCI_ROW + UI_PAGES[p].nrows);
        for (uint8_t it = 0u; it < n + (p == UIPAGE_AMP ? 5u + NROWS(MFX_ROWS) - 1u : 0u); it++) {
            uint8_t item = it < n ? it : (uint8_t)(CCI_MFX + (it - n)), found = 0u;
            char b[40];
            for (i = 0u; i < NRPN_N; i++)
                found |= (uint8_t)(NRPN_MAP[i].page == p && NRPN_MAP[i].item == item);
            CHECK(found, "%s %s has no NRPN: give it a new number, a new line at the end of its page in src/nrpn_map.h",
                  PN[p], label(p, item, b));
        }
    }
    if (!fails)
        printf("%u NRPNs and %u CCs name the same controls as the golden list; every control numbered once\n",
               (unsigned)NRPN_N, (unsigned)(sizeof CC_MAP / sizeof CC_MAP[0]));
    return fails != 0;
}
