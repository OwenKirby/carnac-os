/* Every page's renderer but SEQ and ARP, for the FM1's 240 x 240 panel, and the screen's one entry point:
 * mvave_fm1_master_specification.txt 3 (the knob frame on top, the encoder menu list in the middle, the visualizer at
 * the bottom, the synth palettes), 2.2 (the knobs), 4 (what each page's visualizer plots), 5.3 (HOME), 6.1 / 6.2 (SAVE,
 * the floating keyboard and the commit dialogs).
 *
 *   y   0..19   header: the layer and its architecture (or the mode, the slot) at the left; the section's pages at the
 *               right ("FLT1  FLT2  FLTMIX", this one lit and underlined); the moon (ui_seq_draw.c sq_draw_moon: the
 *               bar's phase) at the right end
 *   y  22..78   knobs 1..4: each a stone circle (13 stones over 270 degrees, lit up to the value; bipolar values lit
 *               outwards from the top stone) with a pointer, its name above and its value below
 *   y  81..160  the menu list, 6 rows; the row under the Select Encoder cursor on the SEQ page's noisy ground, in white
 *   y 163..234  the page's visualizer (below); MOD1..4 have no menu, so theirs takes y 81..234
 *   y 235..239  the ground: a dotted horizon, with a small stone tracking a playhead on ENV, LFO and OSC pages
 *
 * Every lit bar is a weathered menhir (ui_seq_draw.c sq_stone: slanted, chipped, grained). The theme is the stone rows
 * of Carnac in the autumn and the Salisbury Plain at Halloween: standing stones for knobs,
 * voices, echoes and wavetable frames, a portal tomb for each mod slot, a harvest moon that follows the LFO,
 * a sarsen circle that counts the bar. Everything is black or a dark shade of the palette's hue; lit stones and curves
 * are the only bright pixels, so the panel stays dim.
 *
 *   OSC1 / OSC2   one cycle of the wavetable frame at the live scan (the newest sounding voice's, else the knob's);
 *                 the bank's 8 frames as a row of stones, the scan's place between them on the ground
 *   OSCMIXER      two cycles of the mix: OSC1, OSC2 by the balance (STACK: the blend), the sub at its octave and wave,
 *                 the noise at its colour; SPREAD adds its detuned playheads as dim ghosts
 *   FILTER1 / 2   the response curve of the flavour's circuit (dsp/filter.h: the SH-2's 4-pole loop at its morph
 *                 LP .. BP .. HP, MULTI's at its mode, the MS-20 Sallen-Key at its morph LP .. BP .. HP,
 *                 FORMANT's two bands at their spread); what the morph does on the right; the cutoff as a
 *                 stone on the ground (FORMANT: a second for the upper band)
 *   FILTER MIXER  DUAL FILTER: both curves and their serial / parallel mix. STACK: A and B as stones across the stereo
 *                 field, sized by the blend. BINAURAL: the left and right ears' curves, the phase offset as a dial
 *   ENV1..3       the ADSR as a barrow; the newest voice's stage and level as a stone on the ground
 *   LFO1..3       two cycles of the shape after its delay and fade; the playhead on the ground and a moon whose phase
 *                 is the LFO's
 *   MOD1..3       a portal tomb: source A inscribed on one portal stone, B on the other, the function (and the amount)
 *                 on the capstone; in the chamber the portal, the slot's output over time (the newest voice's, else
 *                 the layer's voice-less pass) as a spiral winding into it. The whole of y 81..234
 *   AMP           every sounding voice as a stone: its height the amp level, its place the pan (layer 2 lighter);
 *                 DRIVE crumbles them, DELAY SEND raises rows of echoes behind them, REVERB SEND a fog over the plain
 *   EFFECTS       the delay's repeats as a receding stone row (time, feedback, the tape's darkening), the reverb as fog
 *                 (pre-delay, decay, damping)
 *   GLOBAL        a sarsen circle of 16 stones, one lit per 16th of the bar, and its heel stone; the tempo
 *   HOME 1 / 2    the voice pool as 8 standing stones, each as on AMP: its height its voice's level, layer 2 outlined
 *                 (the layers' ranges; stones no layer may use crumble)
 *   HOME 3        the keyboard with the split stone and its fade, the velocity split below
 *   SAVE          the performance and its links; the name and the floating keyboard (6.2.2), the dialogs
 *
 * Every popup (the commit dialogs, the preset list, the parameter) is a tablet drawn only by its hewn edge, as
 * moonlight catches a stone on a dark plain: nothing fills it, so the panel stays dark outdoors at night. Its words
 * are in the rune face (SQ_RUNE, ui_seq_draw.c: a 5 x 9 bitmap, hewn capitals, no anti-aliasing; every page's words are
 * in it too), scaled up in blocks; the pick is inside a channel's hewn edge.
 *
 *   PRESET LIST   while the Preset Encoder turns (ui_sm_browse_slot): the whole screen, the stored presets of the
 *                 page's kind seven at a time around the one loading, in the runes at 2x, the pick lit
 *   PARAMETERS    while the Select Encoder moves the cursor (sm->rowpop): the whole screen, one row at a time (2x,
 *                 its neighbours faint), its value at 4x, the page's rows as stones on the plain with the cursor's
 *                 lit, and a moon on an arch behind them placed by the value
 *
 * A Felucca unity-build fragment, not a translation unit: #include it after ui_seq_draw.c and ui_arp_draw.c (the
 * palettes, sq_c / SQC, sq_mix, sq_noise_block, seq_draw, arp_draw) and after the synth core (poly_bank, pool_range,
 * global_lfo1_state, wt_read_morph, lfo_peek_fixed, midi_clock_phase). Statics are prefixed pu_. Like the SEQ page it
 * redraws only the regions whose inputs changed; a region's inputs are hashed, live ones (levels, phases) quantised so a
 * still screen sends nothing over the SPI.
 *
 *   ui_screen_draw(sm, q, a, force)   once per UI frame, any page: SEQ and ARP go to their own renderers. force 1 after
 *                                     something else covered the screen; a page, layer, dialog or palette change
 *                                     forces a full redraw by itself.
 *
 * Reads only: the UI registers, the patches and performance, the voices, the clock. Integer only. */

#include <stddef.h>                          /* offsetof (the MOD page's destination table) */
#include "dsp/attacks.h"                     /* STRIKE: the attack outline on the OSCMIXER page */
/* ----------------------------------------------------------- colours --- */
#define PUC_STONE SQ_STONE[sq_pal]                 /* an unlit stone (ui_seq_draw.c) */
#define PUC_HL SQ_PAL_LANE[sq_pal][SQC_BG]          /* the noisy ground's base: white text sits on it */

/* ------------------------------------------------------------ layout --- */
#define PU_HEAD_H 20
#define PU_KNOB_Y 22
#define PU_KNOB_H 57
#define PU_KNOB_W 60                 /* 4 cells, 0 .. 239 */
#define PU_DIAL_CY 29                /* the stone circle's centre in its cell */
#define PU_DIAL_R 14
#define PU_PTR_R 8
#define PU_VAL_Y 44                  /* the value's band: rows 44 .. 56 */
#define PU_MENU_Y 81
#define PU_ROW_H 13
#define PU_MENU_ROWS 6u
#define PU_MENU_H 80
#define PU_VIZ_Y 163
#define PU_VIZ_H 72
#define PU_TRACK_Y 235
#define PU_TRACK_H 5
/* the visualizer's plot: x 6 .. 233, the ground at y 68 */
#define PV_X0 6
#define PV_W 228
#define PV_GROUND 68
#define PU_DLG_X 30                  /* the commit dialogs, over the menu */
#define PU_DLG_Y 86
#define PU_DLG_W 180
#define PU_DLG_H 70

enum { PUR_HEAD, PUR_KNOB0, PUR_MENU = PUR_KNOB0 + 4, PUR_VIZ, PUR_GLYPH, PUR_TRACK, PUR_DLG, PUR_BROWSE, PUR_ROWPOP,
       PUR_COUNT };
static uint32_t pu_sig[PUR_COUNT];
static uint8_t pu_top;               /* the first menu row shown */

static int pu_dirty(uint32_t r, uint32_t sig, uint32_t force)
{
    sig |= 1u;
    if (!force && pu_sig[r] == sig)
        return 0;
    pu_sig[r] = sig;
    return 1;
}

static uint32_t pu_mix_str(uint32_t h, const char *s)
{
    if (s == NULL)
        return sq_mix(h, 0xA5u);
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return (h ^ 0xFFu) * 16777619u;
}

/* ------------------------------------------------------------ tables --- */
#define PU_SIN SQ_SIN                                /* (ui_seq_draw.c) */
/* the point r px from (cx, cy) at a / 64 turns clockwise from 12 o'clock */
static int32_t pu_px(int32_t cx, int32_t r, int32_t a) { return cx + ((r * PU_SIN[a & 63] + 128) >> 8); }
static int32_t pu_py(int32_t cy, int32_t r, int32_t a) { return cy - ((r * PU_SIN[(a + 16) & 63] + 128) >> 8); }

/* log2(1 + 2^(-d / 2)), Q8, d = 0 .. 16 (generated) */
static const uint8_t PU_LOGADD[17] = {255, 198, 150, 112, 82, 60, 44, 31, 22, 16, 11, 8, 6, 4, 3, 2, 1};

/* the long knob names the 60 px cells cannot hold, as the screen shows them */
static const char *const PU_SHORT[][2] = {
    {"WAVE SCAN", "SCAN"}, {"PITCH MOD", "PCH MOD"}, {"NOISE COLOUR", "COLOUR"}, {"SUB OCTAVE", "SUB OCT"},
    {"CROSS-MOD", "X-MOD"}, {"XMOD TYPE", "X TYPE"}, {"SPREAD MOD", "SPR MOD"}, {"OSC PHASE", "PHASE"},
    {"TYPE MORPH", "MORPH"}, {"RESONANCE", "RESO"}, {"MOD AMOUNT", "MOD AMT"}, {"BALANCE MOD", "BAL MOD"},
    {"ROUTING MOD", "RTG MOD"}, {"BLEND A/B", "BLEND"}, {"BLEND MOD", "BLD MOD"}, {"ENV3>CUTOFF", "ENV3>CUT"},
    {"CUTOFF L/R", "CUT L/R"}, {"PHASE MOD", "PH MOD"}, {"DELAY TIME", "DLY TIME"}, {"DELAY FDBK", "FEEDBACK"},
    {"REVERB TIME", "REV TIME"}, {"REVERB DAMP", "REV DAMP"}, {"DELAY SEND", "DLY SEND"}, {"REVERB SEND", "REV SEND"},
    {"PITCH BEND", "BEND"}, {"MOD WHEEL", "MOD WHL"}, {"PORTA TIME", "PORTA"}, {"CHARACTER", "CHAR"},
};

static int pu_streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static const char *pu_short(const char *label)
{
    uint32_t i;
    for (i = 0; i < sizeof PU_SHORT / sizeof PU_SHORT[0]; i++)
        if (pu_streq(label, PU_SHORT[i][0]))
            return PU_SHORT[i][1];
    return label;
}

/* ------------------------------------------------------------- text --- */
static char *pu_cat(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = '\0';
    return d;
}

/* v as decimal, with a + for positive values when sign is set; at least `digits` digits */
static char *pu_num(char *d, int32_t v, uint32_t sign, uint32_t digits)
{
    char t[6];
    uint32_t n = 0, u;
    if (v < 0) { *d++ = '-'; u = (uint32_t)-v; }
    else { if (sign && v > 0) *d++ = '+'; u = (uint32_t)v; }
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u && n < 5u);
    while (n < digits && n < 5u) t[n++] = '0';
    while (n) *d++ = t[--n];
    *d = '\0';
    return d;
}

/* a name (char_map indices) as text, trailing spaces dropped; "UNTITLED" when it is all spaces */
static const char *pu_name(const uint8_t *name, char *b)
{
    return *ui_sm_name_text(name, b) ? b : "UNTITLED";
}

static const char *pu_perf_name(const MasterPerformancePreset *p, char *b) { return pu_name(p->name, b); }

/* ----------------------------------------------------------- drawing --- */
/* a standing stone w wide on row yb - 1 and h tall: ui_seq_draw.c's weathered menhir, its grain seeded by where it
 * stands (a stone that moves, a voice's, passes its own seed to sq_stone) */
static uint32_t pu_seed(int32_t x, int32_t yb) { return (uint32_t)(x * 31 + yb * 7 + 3); }

static void pu_menhir(int32_t x, int32_t yb, int32_t w, int32_t h, uint16_t c)
{
    sq_stone(x, yb, w, h, h, c, pu_seed(x, yb), 0u, 0u);
}

static void pu_menhir_edge(int32_t x, int32_t yb, int32_t w, int32_t h, uint16_t c)
{
    sq_stone(x, yb, w, h, h, c, pu_seed(x, yb), 1u, 0u);
}

/* the grain of fog, earth and stone (ui_seq_draw.c sq_hash) */
#define pu_hash sq_hash

/* 1 for about one pixel in (mask + 1) at screen (x, y): fog, mist and earth that stay put between redraws */
static uint32_t pu_speck(int32_t x, int32_t y, uint32_t mask) { return ((pu_hash(x, y) >> 24) & mask) == 0u; }

/* the column from y0 down to (not into) y1, specked; oy: the canvas's screen row */
static void pu_fog_col(int32_t x, int32_t y0, int32_t y1, uint32_t mask, uint16_t c, int32_t oy)
{
    for (; y0 < y1; y0++)
        if (pu_speck(x, y0 + oy, mask))
            cv_pset(x, y0, c);
}

/* a dotted horizon */
static void pu_ground(int32_t y, uint16_t c)
{
    int32_t x;
    for (x = PV_X0; x < PV_X0 + PV_W; x += 2)
        cv_pset(x, y, c);
}

#define pu_moon sq_moon                              /* (ui_seq_draw.c) */

/* ------------------------------------------------------------- stone --- */
/* the popups' stone (ui_seq_draw.c SQ_ROCK, sq_rock_tone, sq_rock_in) */
#define PU_ROCK SQ_ROCK
#define pu_rock_tone sq_rock_tone
#define pu_rock_in sq_rock_in

/* a hewn block's outline (pu_rock_in), worked out once a block: each row's l .. r, each column's t .. b (exclusive) */
#define PU_BLK_MAX 240
static int16_t pu_bl[PU_BLK_MAX], pu_br[PU_BLK_MAX], pu_bt[PU_BLK_MAX], pu_bb[PU_BLK_MAX];

/* 1 if (x, y) falls on the block whose outline is in pu_bl .. pu_bb */
static uint32_t pu_block_in(int32_t x, int32_t y, int32_t w, int32_t h)
{
    return x >= 0 && x < w && y >= 0 && y < h && x >= pu_bl[y] && x < pu_br[y] && y >= pu_bt[x] && y < pu_bb[x];
}

/* a hewn block of the dark stone lit from the upper left, for a stone standing in the open (the MOD tomb): its face a tone up, the top and
 * left rims caught in the light, the row in from them a little less, the bottom and right in shadow. Up to
 * PU_BLK_MAX each way. */
static void pu_block_lit(int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t sx, int32_t sy, uint32_t seed)
{
    const uint16_t *t = PU_ROCK[sq_pal];
    int32_t x, y;
    uint32_t k;
    uint16_t c;
    if (w > PU_BLK_MAX) w = PU_BLK_MAX;
    if (h > PU_BLK_MAX) h = PU_BLK_MAX;
    for (y = 0; y < h; y++) {
        pu_bl[y] = (int16_t)pu_rock_in(y, h, seed + 1u);
        pu_br[y] = (int16_t)(w - pu_rock_in(y, h, seed + 2u));
    }
    for (x = 0; x < w; x++) {
        pu_bt[x] = (int16_t)pu_rock_in(x, w, seed + 3u);
        pu_bb[x] = (int16_t)(h - pu_rock_in(x, w, seed + 4u));
    }
    for (y = 0; y < h; y++)
        for (x = pu_bl[y]; x < pu_br[y]; x++) {
            if (!pu_block_in(x, y, w, h))
                continue;
            if (!pu_block_in(x, y - 1, w, h) || !pu_block_in(x - 1, y, w, h))
                c = PUC_STONE;
            else if (!pu_block_in(x, y + 1, w, h) || !pu_block_in(x + 1, y, w, h))
                c = t[0];
            else if (!pu_block_in(x, y - 2, w, h) || !pu_block_in(x - 2, y, w, h))
                c = t[4];
            else {
                k = pu_rock_tone(sx + x0 + x, sy + y0 + y) + 1u;
                c = t[k > 4u ? 4u : k];
            }
            cv_pset(x0 + x, y0 + y, c);
        }
}

/* a hewn block's edge only, w x h at (x0, y0 - oy) (oy: the canvas's screen row): corners knocked off, edges chipped,
 * as moonlight catches a stone on a dark plain. Up to PU_BLK_MAX each way */
static void pu_hewn(int32_t x0, int32_t y0, int32_t w, int32_t h, uint32_t seed, uint16_t c, int32_t oy)
{
    int32_t x, y;
    if (w > PU_BLK_MAX) w = PU_BLK_MAX;
    if (h > PU_BLK_MAX) h = PU_BLK_MAX;
    for (y = 0; y < h; y++) {
        pu_bl[y] = (int16_t)pu_rock_in(y, h, seed + 1u);
        pu_br[y] = (int16_t)(w - pu_rock_in(y, h, seed + 2u));
    }
    for (x = 0; x < w; x++) {
        pu_bt[x] = (int16_t)pu_rock_in(x, w, seed + 3u);
        pu_bb[x] = (int16_t)(h - pu_rock_in(x, w, seed + 4u));
    }
    for (y = 0; y < h; y++)
        for (x = pu_bl[y]; x < pu_br[y]; x++)
            if (pu_block_in(x, y, w, h) && (!pu_block_in(x - 1, y, w, h) || !pu_block_in(x + 1, y, w, h) ||
                                            !pu_block_in(x, y - 1, w, h) || !pu_block_in(x, y + 1, w, h)))
                cv_pset(x0 + x, y0 + y - oy, c);
}

/* a popup's tablet, w x h at (x0, y0 - oy): its hewn edge on the black, a cup mark inside each corner. Nothing
 * fills it, so the panel stays dark */
static void pu_tablet(int32_t x0, int32_t y0, int32_t w, int32_t h, uint32_t seed, int32_t oy)
{
    int32_t k, x, y;
    pu_hewn(x0, y0, w, h, seed, SQC(DIM), oy);
    for (k = 0; k < 4; k++) {
        x = x0 + (k & 1 ? w - 14 : 13);
        y = y0 + (k & 2 ? h - 14 : 13);
        cv_rect(x - 1, y - 1 - oy, 3, 3, PUC_STONE);
        cv_pset(x, y - oy, SQC(BG));
    }
}

/* the commit dialog's tablet: the whole canvas (sx, sy: its place on the screen, for the seed) */
static void pu_slab(int32_t w, int32_t h, int32_t sx, int32_t sy)
{
    pu_hewn(0, 0, w, h, (uint32_t)(sx * 31 + sy), SQC(DIM), 0);
}

/* the pick of a list, of NO / YES: a channel's hewn edge round it */
static void pu_trough(int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy)
{
    pu_hewn(x, y, w, h, (uint32_t)(sx + x * 7 + (sy + y) * 13), PUC_STONE, 0);
}

/* -------------------------------------------------------------- runes --- */
/* (the face: ui_seq_draw.c SQ_RUNE) the popups' words, their top row at y, centred in x0 .. x0 + w */
static int32_t sq_rune_in(int32_t x0, int32_t y, int32_t w, const char *s, uint16_t c, int32_t track)
{
    return sq_rune(x0 + HALF_UP(w - sq_rune_w(s, track)), y, s, c, track);
}

/* the rune face sc times the size (blocks, no smoothing: 2x is 10 x 18, 12 px a letter), its top row at y - oy. At
 * most max letters (the rest cut); returns where its ink ends */
#define PU_RUNE_N_W(n, sc) ((n) > 0 ? (n) * (sc) * (SQ_RUNE_W + 1) - (sc) : 0)
static int32_t pu_rune_n(int32_t x, int32_t y, const char *s, uint16_t c, int32_t sc, int32_t max, int32_t oy)
{
    int32_t i, j;
    const uint8_t *g;
    for (; *s && max > 0; s++, max--, x += sc * (SQ_RUNE_W + 1)) {
        g = sq_rune_glyph(*s);
        for (j = 0; j < SQ_RUNE_H; j++)
            for (i = 0; i < SQ_RUNE_W; i++)
                if ((g[j] >> (SQ_RUNE_W - 1 - i)) & 1u)
                    cv_rect(x + sc * i, y + sc * j - oy, sc, sc, c);
    }
    return x - sc;
}

static int32_t pu_strlen(const char *s)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* ------------------------------------------------------------- voices --- */
/* the newest sounding voice of a layer, -1 when none sounds */
static int32_t pu_voice(uint8_t layer)
{
    int32_t v, best = -1;
    for (v = 0; v < POLYPHONY_VOICES; v++) {
        const SynthVoiceNode *n = &poly_bank[v];
        if (n->silent || n->layer != layer)
            continue;
        if (best < 0 || n->age_counter < poly_bank[best].age_counter)
            best = v;
    }
    return best;
}

/* a voice's amp level 0..32767 and pan 0 (L) .. 127 (R) */
static int32_t pu_voice_level(const SynthVoiceNode *n)
{
    float v = n->vol > n->vol_b ? n->vol : n->vol_b;
    if (n->silent) return 0;
    int32_t l = (int32_t)(v * 32767.f);
    if (n->choke) l = (int32_t)(((int64_t)l * n->choke_q15) >> 15);
    return l < 0 ? 0 : l > 32767 ? 32767 : l;
}

static int32_t pu_voice_pan(const SynthVoiceNode *n)
{
    /* the dry gains to the left and right of the mix (both paths): their ratio is the pan */
    float l = n->mix_a[MIX_L] + n->mix_b[MIX_L], r = n->mix_a[MIX_R] + n->mix_b[MIX_R];
    if (l < 0.f) l = 0.f;
    if (r < 0.f) r = 0.f;
    return l + r > 0.f ? (int32_t)(r * 127.f / (l + r)) : 64;
}

/* ------------------------------------------------------------- pages --- */
static const char *const PU_TITLE[UIPAGE_COUNT] = {
    "OSC 1", "OSC 2", "ATTACK", "OSC MIX", "FILTER 1", "FILTER 2", "FLT MIX", "ENV 1", "ENV 2", "ENV 3", "LFO 1", "LFO 2", "LFO 3",
    "MOD 1", "MOD 2", "MOD 3", "AMP", "EFFECTS", "HOME", "HOME", "HOME", "PERFORMANCE", "GLOBAL", "ARP", "SAVE", "SEQ",
};
/* the first page and size of each section (UIStateMachine.c SECTION_FIRST / SECTION_COUNT, by page) */
static void pu_section(uint8_t page, uint8_t *first, uint8_t *n)
{
    static const uint8_t F[] = {UIPAGE_OSC1, UIPAGE_FILTER1, UIPAGE_ENV1, UIPAGE_LFO1, UIPAGE_MOD1, UIPAGE_AMP,
                                UIPAGE_HOME1, UIPAGE_ARP, UIPAGE_SAVE_COMMIT, UIPAGE_SEQ, UIPAGE_COUNT};
    uint32_t i;
    for (i = 0; F[i + 1] != UIPAGE_COUNT && page >= F[i + 1]; i++) {}
    *first = F[i];
    *n = (uint8_t)(F[i + 1] - F[i]);
}

/* the pages whose parameters belong to a layer (the header names it) */
static uint8_t pu_layer_page(uint8_t page) { return (uint8_t)(page <= UIPAGE_AMP); }

/* the palette every page is drawn in (ui_seq_draw.c spec 3 note): GLOBAL > THEME's, whichever layer is in focus.
 * SAMHAIN is seq_palette_set's (ORANGE, or RED for the host's text checks) */
static uint8_t pu_page_pal(const UIStateMachine *sm)
{
    static const uint8_t PAL[UITHEME_COUNT] = {SQPAL_ORANGE, SQPAL_RED, SQPAL_FIRE, SQPAL_SOLSTICE, SQPAL_LICHEN, SQPAL_HEATHER,
                                               SQPAL_RITUAL, SQPAL_GRANITE, SQPAL_OAK};
    return sm->theme == UITHEME_SAMHAIN || sm->theme >= UITHEME_COUNT ? sq_pal_base : PAL[sm->theme];
}

/* ------------------------------------------------------------ header --- */
/* each page's name in the header's list of its section's pages */
static const char *const PU_TAB[UIPAGE_COUNT] = {
    "OSC1", "OSC2", "ATTACK", "OSCMIX", "FLT1", "FLT2", "FLTMIX", "ENV1", "ENV2", "ENV3", "LFO1", "LFO2", "LFO3",
    "MOD1", "MOD2", "MOD3", "AMP", "FX", "LAYER", "LAYER", "ZONES", "PERF", "GLOBAL", "ARP", "SAVE", "SEQ",
};
#define PU_TAB_GAP 8                 /* between two pages' names */

/* the section's pages at the right, ending at xr ("OSC1  OSC2  OSCMIX"): this one lit and underlined, the others dim,
 * hidden ones left out. Returns where the list starts */
static int32_t pu_head_tabs(const UIStateMachine *sm, int32_t xr, int32_t y)
{
    uint8_t page = sm->page, first, n, i;
    int32_t x, w = 0, k;
    pu_section(page, &first, &n);
    for (i = 0; i < n; i++)
        if (ui_sm_page_visible(sm, (uint8_t)(first + i), sm->s.active_synth_layer))
            w += (w ? PU_TAB_GAP : 0) + sq_rune_w(PU_TAB[first + i], 0);
    x = xr - w;
    xr = x;
    for (i = 0; i < n; i++) {
        uint8_t p = (uint8_t)(first + i);
        if (!ui_sm_page_visible(sm, p, sm->s.active_synth_layer))
            continue;
        k = sq_rune(x, y, PU_TAB[p], p == page ? SQC(TEXT) : SQC(DIM), 0);
        if (p == page)
            cv_rect(x, y + SQ_RUNE_H + 2, k - x, 1, SQC(THEME));
        x = k + 1 + PU_TAB_GAP;
    }
    return xr;
}

/* the header: what the page is about at the left (the layer and its architecture, the mode, the slot), its section's
 * pages at the right, the clock moon past them (sq_draw_moon) */
static void pu_draw_head(const UIStateMachine *sm)
{
    static const char *const MODE[PERF_COUNT] = {"SINGL", "DUAL", "SPLIT"};
    static const char *const TARGET[UISAVE_COUNT] = {"ARP", "SEQUENCE", "PATCH", "PERFORMANCE"};
    char b[24], *e;
    uint8_t page = sm->page, layer = sm->s.active_synth_layer, mode = sm->perf->perf_mode;
    int32_t x, y = HALF_UP(PU_HEAD_H - SQ_RUNE_H) - 1;
    cv_begin(SQ_MOON_X, PU_HEAD_H, SQC(BG));
    pu_head_tabs(sm, SQ_HEAD_R, y);
    if (mode >= PERF_COUNT) mode = PERF_SINGL_SYNTH;
    if (pu_layer_page(page)) {                       /* "L1  DUAL OSC" */
        x = sq_rune(6, y, layer ? "L2" : "L1", SQC(THEME), 0) + 7;
        sq_rune(x, y, ui_sm_arch_name(varch_of(sm->engine[layer])), SQC(MID), 0);
    } else if (page >= UIPAGE_HOME1 && page <= UIPAGE_HOME3) {   /* "DUAL  L1 4V" */
        x = sq_rune(6, y, MODE[mode], SQC(THEME), 0) + 7;
        if (page != UIPAGE_HOME3) {
            e = pu_cat(b, page == UIPAGE_HOME2 ? "L2 " : "L1 ");
            e = pu_num(e, ui_sm_layer_voices(sm, page == UIPAGE_HOME2 ? 1u : 0u), 0u, 1u);
            pu_cat(e, "V");
            sq_rune(x, y, b, SQC(MID), 0);
        }
    } else if (page == UIPAGE_EFFECTS) {
        sq_rune(6, y, "DELAY > REVERB", SQC(MID), 0);
    } else if (page == UIPAGE_PERF) {                /* "DUAL  PERF 024": PERF MODE is this page's (2026-10-17) */
        x = sq_rune(6, y, MODE[mode], SQC(THEME), 0) + 7;
        pu_num(b, sm->perf_slot, 0u, 3u);
        sq_rune(x, y, b, SQC(MID), 0);
    } else if (page == UIPAGE_SAVE_COMMIT) {         /* "SAVE  L1 PATCH": a patch is the focused layer's */
        x = sq_rune(6, y, "SAVE", SQC(THEME), 0) + 7;
        if (sm->save_target == UISAVE_PATCH_PRESET)
            x = sq_rune(x, y, layer ? "L2" : "L1", SQC(THEME), 0) + 7;
        sq_rune(x, y, TARGET[sm->save_target < UISAVE_COUNT ? sm->save_target : UISAVE_WHOLE_PERF], SQC(MID), 0);
    }
    cv_blit(0, 0);
}

/* ------------------------------------------------------------- knobs --- */
/* what a knob shows: its byte as text into b (or a list's name) and its place on the dial, 0 .. 48 / 64 turn */
typedef struct {
    const char *label, *text;
    uint8_t kind, bip, list, none;
    int32_t pos;
} PuKnob;

static void pu_knob_read(UIStateMachine *sm, uint8_t page, uint8_t k, PuKnob *o, char *b)
{
    UiKnobInfo info;
    uint8_t layer = sm->s.active_synth_layer;
    const uint8_t *p = ui_sm_knob(sm, page, layer, k, &info);
    int32_t v = p ? *p : 0, n = info.n_enum;
    o->label = info.kind == UIKNOB_NONE ? "" : pu_short(info.label);
    o->kind = info.kind;
    o->none = (uint8_t)(info.kind == UIKNOB_NONE || p == NULL);
    o->bip = (uint8_t)(info.kind == UIKNOB_BIP || info.kind == UIKNOB_OCT);
    o->list = (uint8_t)(info.names != NULL && n > 0);
    o->text = o->none ? "" : o->list ? ui_sm_knob_text(sm, page, layer, k) : b;
    if (v > (int32_t)VAL_MAX) v = VAL_MAX;
    if (o->list) {
        if (v > n - 1) v = n - 1;
        o->pos = n > 1 ? (v * 48 + (n - 1) / 2) / (n - 1) : 0;
    } else {
        o->pos = (v * 48 + 63) / 127;
    }
    if (o->none || o->list)
        return;
    if (info.kind == UIKNOB_OCT)
        pu_num(b, ((v - (int32_t)BIPOLAR_CENTER) * 3) / 63, 1u, 1u);   /* -3 .. +3, as the browser shows it */
    else if (info.kind == UIKNOB_BPM)
        pu_num(b, (int32_t)MIDI_CLOCK_BPM_BASE + v, 0u, 1u);           /* INT TEMPO: 40 .. 167 */
    else if (info.kind == UIKNOB_NOTE)
        ui_sm_note_name((uint8_t)v, b);                                 /* SPLIT NOTE: C4 */
    else if (info.kind == UIKNOB_FINE)
        ui_sm_fine_text((uint8_t)v, b);                                 /* FINE TUNE: +125C */
    else
        pu_num(b, o->bip ? v - (int32_t)BIPOLAR_CENTER : v, o->bip, 1u);
}

/* The PRESET page's knobs pick slots, so they are tablets, as the SEQ and ARP knob cards (ui_seq_draw.c
 * sq_knob_tablet: the same stone, cut the same, at the same x), as tall as the knob row: the name, the slot in the
 * title face, and for a patch the patch's name (EMPTY for a slot never written). Its canvas is the card and the gap
 * left of it (the first and last reach the screen's edges), not the 60 px cell: the cards sit on the SEQ page's 58 px
 * pitch. */
static void pu_draw_knob_tablet(UIStateMachine *sm, const PuKnob *o, uint8_t k)
{
    char nm[NAME_LEN + 1];
    int32_t sx = SQ_KNOB_X0 + (int32_t)k * (SQ_KNOB_W + SQ_KNOB_GAP);
    int32_t c0 = k ? sx - SQ_KNOB_GAP : 0, c1 = k < 3u ? sx + SQ_KNOB_W : 240, x = sx - c0;
    int32_t n, i, j;
    uint8_t st;
    cv_begin((uint32_t)(c1 - c0), PU_KNOB_H, SQC(BG));
    sq_knob_tablet(x, sx, PU_KNOB_H - 3, k, PU_KNOB_Y, o->label, SQC(MID), 0u);
    if (!o->none && o->text) {                       /* "088", as the menu and the dolmen show a slot */
        for (n = 0; o->text[n] && n < 3; n++) {}
        for (i = 0; i < 3 - n; i++) nm[i] = '0';
        for (j = 0; j < n; j++) nm[i + j] = o->text[j];
        nm[i + n] = '\0';
        sq_text_in(x, 14, SQ_KNOB_W, &AF_M, nm, SQC(TEXT), SQC(SURF));
    }
    if (k < 2u && !o->none) {                        /* L1 PATCH, L2 PATCH */
        st = ui_sm_slot_query(sm, UISAVE_PATCH_PRESET, sm->perf->layer_patch[k], nm);
        if (st == UISLOT_EMPTY)
            sq_text_in(x, 33, SQ_KNOB_W, &AF_S, "EMPTY", SQC(DIM), SQC(SURF));
        else if (st == UISLOT_USED && nm[0])
            sq_text_fit(x + 4, 33, &AF_S, nm, SQC(MID), SQC(SURF), SQ_KNOB_W - 8);
    }
    cv_blit((uint32_t)c0, PU_KNOB_Y);
}

/* the stone circle: 13 stones every 1/16 turn from -3/8 to +3/8, lit to the value (from the top stone for a bipolar
 * value, the one picked for a list), the stone at the value bigger and brighter, a pointer from the hub */
static void pu_draw_knob(UIStateMachine *sm, uint8_t page, uint8_t k)
{
    char b[8];
    PuKnob o;
    int32_t i, a, p, cx = PU_KNOB_W / 2, cy = PU_DIAL_CY, sx, sy, lead;
    uint16_t c;
    pu_knob_read(sm, page, k, &o, b);
    if (page == UIPAGE_PERF) {
        pu_draw_knob_tablet(sm, &o, k);
        return;
    }
    cv_begin(PU_KNOB_W, PU_KNOB_H, SQC(BG));
    sq_text_in(0, CAP_IN(S, 13), PU_KNOB_W, &AF_S, o.none ? "-" : o.label, o.none ? SQC(DIM) : SQC(MID), SQC(BG));
    lead = (o.pos + 2) / 4;                          /* the stone nearest the value */
    for (i = 0; i < 13; i++) {
        p = i * 4;
        a = p - 24;
        sx = pu_px(cx, PU_DIAL_R, a);
        sy = pu_py(cy, PU_DIAL_R, a);
        if (o.none)
            c = PUC_STONE;
        else if (i == lead)
            c = SQC(ACCENT);
        else if (o.list)
            c = PUC_STONE;
        else if (o.bip)
            c = (p >= 24 && p <= o.pos) || (p <= 24 && p >= o.pos) ? SQC(THEME) : PUC_STONE;
        else
            c = p <= o.pos && o.pos > 0 ? SQC(THEME) : PUC_STONE;
        if (i == lead && !o.none)
            pu_menhir(sx - 1, sy + 3, 3, 5, c);
        else
            pu_menhir(sx - 1, sy + 2, 2, 3, c);
    }
    if (!o.none) {
        a = o.pos - 24;
        cv_line(cx, cy, pu_px(cx, PU_PTR_R, a), pu_py(cy, PU_PTR_R, a), SQC(MID));
        cv_rect(cx - 1, cy - 1, 2, 2, SQC(TEXT));    /* the hub */
        sq_text_in(0, PU_VAL_Y + CAP_IN(S, 13), PU_KNOB_W, &AF_S, o.text ? o.text : "", SQC(TEXT), SQC(BG));
    }
    cv_blit((uint32_t)(k * PU_KNOB_W), PU_KNOB_Y);
}

static uint32_t pu_knob_sig(UIStateMachine *sm, uint8_t page, uint8_t k, uint32_t base)
{
    char b[8];
    PuKnob o;
    pu_knob_read(sm, page, k, &o, b);
    base = pu_mix_str(base, o.label);
    base = pu_mix_str(base, o.text);
    return sq_mix(base, (uint32_t)o.pos | (uint32_t)o.none << 8 | (uint32_t)o.bip << 9 | (uint32_t)o.list << 10);
}

/* -------------------------------------------------------------- menu --- */
static void pu_menu_scroll(const UIStateMachine *sm)
{
    uint8_t n = ui_sm_row_count(sm), f = sm->s.current_param_focus;
    if (n <= PU_MENU_ROWS) {
        pu_top = 0;
        return;
    }
    if (f != UI_FOCUS_NONE) {
        if (f < pu_top) pu_top = f;
        if (f >= pu_top + PU_MENU_ROWS) pu_top = (uint8_t)(f - PU_MENU_ROWS + 1u);
    }
    if (pu_top > n - PU_MENU_ROWS) pu_top = (uint8_t)(n - PU_MENU_ROWS);
}

static const char *pu_row_text(const UIStateMachine *sm, uint8_t r, char *b)
{
    const char *t = ui_sm_row_text(sm, r);
    if (t != NULL)
        return t;
    pu_num(b, ui_sm_row_value(sm, r), 0u, 1u);
    return b;
}

/* label left, value right; greyed rows dim; the cursor row on the noisy ground, in white; a scroll bar on the right
 * when the page has more rows than fit */
static void pu_draw_save_menu(const UIStateMachine *sm);

static void pu_draw_menu(const UIStateMachine *sm)
{
    char b[8];
    uint8_t n = ui_sm_row_count(sm), i, r, st, cur;
    int32_t y, lw, ty;
    uint16_t lc, vc, bg;
    cv_begin(240, PU_MENU_H, SQC(BG));
    if (sm->page == UIPAGE_SAVE_COMMIT) {            /* (its one row, the name, is drawn large) */
        pu_draw_save_menu(sm);
        cv_blit(0, PU_MENU_Y);
        return;
    }
    if (n == 0u) {                                   /* (the MOD pages: every parameter is a knob) */
        sq_text_in(0, 30 + CAP_IN(S, 13), 240, &AF_S, "ALL ON THE KNOBS", SQC(DIM), SQC(BG));
    }
    for (i = 0; i < PU_MENU_ROWS && pu_top + i < n; i++) {
        r = (uint8_t)(pu_top + i);
        st = ui_sm_row_state(sm, r);
        if (st == UIROW_HIDDEN)
            continue;
        y = 1 + (int32_t)i * PU_ROW_H;
        ty = y + CAP_IN(S, PU_ROW_H);
        cur = (uint8_t)(r == sm->s.current_param_focus);
        bg = SQC(BG);
        lc = st == UIROW_GREYED ? SQC(DIM) : SQC(MID);
        vc = st == UIROW_GREYED ? SQC(DIM) : SQC(TEXT);
        if (cur) {
            bg = PUC_HL;
            lc = vc = SQC(HI);
            sq_noise_block(2, y, 230, PU_ROW_H, 0u, PU_MENU_Y, 1u);
        }
        lw = sq_text_on(8, ty, &AF_S, ui_sm_row_label(sm, r), lc, bg);
        {
            const char *t = pu_row_text(sm, r, b);
            int32_t w = sq_text_w(&AF_S, t), room = 226 - lw - 8;
            if (w <= room)
                sq_text_r(226, ty, &AF_S, t, vc, bg);
            else
                sq_text_fit(226 - room, ty, &AF_S, t, vc, bg, room);
        }
    }
    if (n > PU_MENU_ROWS) {                          /* the scroll bar */
        int32_t h = PU_MENU_ROWS * PU_ROW_H, th = h * (int32_t)PU_MENU_ROWS / n, t0 = h * pu_top / n;
        cv_rect(237, 1, 1, h, PUC_STONE);
        cv_rect(236, 1 + t0, 3, th, SQC(MID));
    }
    cv_blit(0, PU_MENU_Y);
}

static uint32_t pu_menu_sig(const UIStateMachine *sm, uint32_t h)
{
    char b[8];
    uint8_t n = ui_sm_row_count(sm), i, r;
    h = sq_mix(h, (uint32_t)sm->page | (uint32_t)sm->s.current_param_focus << 8 | (uint32_t)pu_top << 16 | (uint32_t)n << 24);
    for (i = 0; i < PU_MENU_ROWS && pu_top + i < n; i++) {
        r = (uint8_t)(pu_top + i);
        h = sq_mix(h, ui_sm_row_state(sm, r));
        h = pu_mix_str(h, ui_sm_row_label(sm, r));
        h = pu_mix_str(h, pu_row_text(sm, r, b));
    }
    if (sm->page == UIPAGE_SAVE_COMMIT) {            /* the summary: what the slot holds, the clipboard */
        char n[NAME_LEN + 1];
        h = sq_mix(h, (uint32_t)sm->save_slot | (uint32_t)sm->save_target << 8 | (uint32_t)ui_sm_slot_state(sm, n) << 16 |
                          (uint32_t)sm->clip_kind << 24);
        h = pu_mix_str(sq_mix(h, (uint32_t)sm->clip_layer | (uint32_t)sm->s.active_synth_layer << 8), n);
        if (sm->clip_kind < UISAVE_COUNT)
            h = sq_mix_bytes(h, sm->clip_kind == UISAVE_PATCH_PRESET ? sm->clip.patch.name : sm->clip.whole.perf.name,
                             NAME_LEN);
    }
    return h;
}

/* ===================================================== visualizers === */
/* Each draws into the 240 x PU_VIZ_H canvas the caller began; y 0 is screen row PU_VIZ_Y. */

/* --------------------------------------------------------- oscillator --- */
static int32_t pu_osc_morph(const UIStateMachine *sm, uint8_t osc, int32_t *voice)
{
    uint8_t layer = sm->s.active_synth_layer;
    int32_t v = pu_voice(layer);
    *voice = v;
    if (v >= 0)
        return osc ? poly_bank[v].osc2.morph_q12 : poly_bank[v].osc1.morph_q12;
    return scan_q8_to_morph_q12((int32_t)sm->engine[layer]->osc[osc].scan << 8);
}

#define PU_MORPH_MAX ((NUM_MORPH_STEPS - 1) << 12)

static int32_t pu_ys[PV_W], pu_ys2[PV_W];

/* a scope trace of PV_W samples (Q15) about row mid, scaled so its peak reaches amp px: a quiet or spiky wave still
 * fills the plot. ghost: a second trace on the same scale, dim, or NULL. */
static void pu_trace(int32_t *v, int32_t *ghost, int32_t mid, int32_t amp, uint16_t c)
{
    int32_t x, pk = 4096, a;
    for (x = 0; x < PV_W; x++) {
        a = v[x] < 0 ? -v[x] : v[x];
        if (a > pk) pk = a;
        if (ghost) { a = ghost[x] < 0 ? -ghost[x] : ghost[x]; if (a > pk) pk = a; }
    }
    for (x = PV_X0; x < PV_X0 + PV_W; x += 4)        /* the zero line */
        cv_pset(x, mid, PUC_STONE);
    for (x = 0; x < PV_W; x++) {
        v[x] = mid - (v[x] * amp) / pk;
        if (ghost) ghost[x] = mid - (ghost[x] * amp) / pk;
    }
    if (ghost)
        for (x = 1; x < PV_W; x++)
            cv_line(PV_X0 + x - 1, ghost[x - 1], PV_X0 + x, ghost[x], PUC_STONE);
    for (x = 1; x < PV_W; x++)
        cv_line(PV_X0 + x - 1, v[x - 1], PV_X0 + x, v[x], c);
}

static void pu_viz_osc(const UIStateMachine *sm, uint8_t osc)
{
    const OscParams *o = &sm->engine[sm->s.active_synth_layer]->osc[osc];
    int32_t voice, morph = pu_osc_morph(sm, osc, &voice), x, k, sx, near;
    int bank = o->wavebank % NUM_BANKS;
    sq_text(PV_X0, CAP_IN(S, 13), &AF_S, wavetable_bank_names[bank], SQC(MID));
    for (x = 0; x < PV_W; x++)                       /* one cycle, level 1 (256 samples) */
        pu_ys[x] = wt_read_morph(bank, morph, 1, (uint32_t)x * (0xFFFFFFFFu / PV_W));
    pu_trace(pu_ys, 0, 37, 20, SQC(THEME));
    /* the bank's frames as a stone row, the one nearest the scan lit */
    near = (morph + 2048) >> 12;
    for (k = 0; k < NUM_MORPH_STEPS; k++) {
        sx = PV_X0 + 4 + k * (PV_W - 12) / (NUM_MORPH_STEPS - 1);
        pu_menhir(sx - 3, PV_GROUND, 6, k == near ? 10 : 7, k == near ? SQC(THEME) : PUC_STONE);
    }
}

/* the scan's place between the frames, on the ground */
static int32_t pu_track_osc(const UIStateMachine *sm, uint8_t osc)
{
    int32_t voice, m = pu_osc_morph(sm, osc, &voice);
    return PV_X0 + 4 + (int32_t)(((int64_t)m * (PV_W - 12)) / PU_MORPH_MAX);
}

/* -------------------------------------------------------------- mixer --- */
/* the mix over one cycle of the sub: two cycles of the oscillators for SUB -1 OCT, four for -2 */
static void pu_viz_oscmix(const UIStateMachine *sm)
{
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    const OscMixerParams *m = &e->oscmix;
    uint8_t arch = varch_of(e), spread = (uint8_t)(arch == VARCH_SPREAD && m->spread);
    int32_t voice, morph1 = pu_osc_morph(sm, 0, &voice), morph2 = pu_osc_morph(sm, 1, &voice);
    int32_t w1 = 127, w2 = 0, ws = m->sub_level, wn = m->noise_level, x, brown = 0, col = m->noise_colour;
    int32_t ncyc = m->sub_octave == SUBOCT_DOWN2 ? 4 : 2;
    int bank1 = e->osc[0].wavebank % NUM_BANKS, bank2 = e->osc[1].wavebank % NUM_BANKS;
    uint32_t seed = 0x1234567u, sstep = 0xFFFFFFFFu / PV_W, step = sstep * (uint32_t)ncyc;
    if (arch == VARCH_DUAL_OSC) { w2 = m->balance; w1 = 127 - w2; }
    if (arch == VARCH_STACK) { w2 = e->fltmix.balance; w1 = 127 - w2; }
    for (x = 0; x < PV_W; x++) {
        uint32_t ph = (uint32_t)x * step, sph = (uint32_t)x * sstep;
        int32_t s = w1 * wt_read_morph(bank1, morph1, 1, ph), sub, n;
        if (w2) s += w2 * wt_read_morph(bank2, morph2, 1, ph);
        if (m->sub_waveform == SUBWAVE_SQUARE) sub = sph < 0x80000000u ? 24000 : -24000;
        else if (m->sub_waveform == SUBWAVE_CURRENT) sub = wt_read_morph(bank1, morph1, 1, sph);
        else sub = PU_SIN[sph >> 26] * 128;
        s += ws * sub;
        seed = seed * 1664525u + 1013904223u;        /* the noise: white, browned by the colour (a leaky sum) */
        n = (int32_t)(seed >> 17) - 16384;
        brown += (n - brown / 8) / 4;
        s += wn * ((n * (127 - col) + (brown / 2) * col) / 127);
        pu_ys[x] = s / 127;
        if (spread)                                  /* the detuned playheads: OSC1 stretched, dim behind */
            pu_ys2[x] = w1 * wt_read_morph(bank1, morph1, 1, ph + (uint32_t)m->spread * (uint32_t)x * (step >> 9)) / 127;
    }
    pu_trace(pu_ys, spread ? pu_ys2 : 0, 36, 24, SQC(THEME));
}

/* ------------------------------------------------------------- attack --- */
/* STRIKE's attack across the width, dim at its full length, bright as it plays: from START, at LEVEL, faded by DECAY
 * (the engine's 1/(2 + d^2/16) a tick, exp(-ticks / T) here through pu_exp2). A mu-law byte's magnitude is its loudness
 * on a log scale: the outline reads as the sound's envelope. */
static int64_t pu_exp2(int32_t l);                 /* below (the filter plot's) */
static void pu_viz_attack(const UIStateMachine *sm)
{
    const OscMixerParams *m = &sm->engine[sm->s.active_synth_layer]->oscmix;
    const AttackInfo *ai = &attack_info[m->atk_sample % ATK_COUNT];
    const uint8_t *p = attack_data + ai->offset;
    uint32_t start = (uint32_t)ai->length * m->atk_start / 254u, i, end;
    int32_t x, t16 = 2 * 16 + m->atk_decay * m->atk_decay; /* 16 T, T = 2 + d^2 / 16 ticks */
    for (x = 0; x < PV_W; x++) {
        int32_t peak = 0, sign = x & 1 ? -1 : 1;
        i = (uint32_t)x * ai->length / PV_W;
        end = (uint32_t)(x + 1) * ai->length / PV_W;
        for (; i < end; i++) {
            int32_t q = (int8_t)p[i];
            if (q < 0) q = -q;
            if (q > peak) peak = q;
        }
        pu_ys2[x] = sign * peak * 258;
        if ((uint32_t)x * ai->length / PV_W < start) {
            pu_ys[x] = 0;
            continue;
        }
        int32_t v = peak * 258 * m->atk_level / 127;
        if (m->atk_decay < 127u) {                       /* ticks since START: bytes / rate, 16 samples a tick */
            int64_t ticks = ((int64_t)((uint32_t)x * ai->length / PV_W - start) << 16) / (int64_t)ai->rate_q16 / 16;
            int32_t l = (int32_t)(-(ticks * 16 * 5909) / t16); /* -ticks / T in octaves, Q12 (5909 = 4096 log2 e) */
            v = l < -6 * 4096 ? 0 : (int32_t)((v * pu_exp2(l)) >> 12);
        }
        pu_ys[x] = sign * v;
    }
    pu_trace(pu_ys, pu_ys2, 36, 24, SQC(THEME));
}

/* ------------------------------------------------------------- filter --- */
/* log2 of v >= 1, Q8 (a linear mantissa with its parabolic correction: within 0.01 octave) */
static int32_t pu_log2(uint64_t v)
{
    int32_t m = 63, f;
    if (v == 0u)
        return 0;
    while (!(v >> m))
        m--;
    f = m >= 8 ? (int32_t)((v >> (m - 8)) & 0xFFu) : (int32_t)((v << (8 - m)) & 0xFFu);
    return m * 256 + f + ((f * (256 - f) * 89) >> 16);
}

/* 2^(l / 4096) in Q12, l within +-6 octaves */
static int64_t pu_exp2(int32_t l)
{
    int32_t n = l >> 12, f = l & 4095;
    int64_t mant = 4096 + f - ((((int64_t)f * (4096 - f)) >> 12) * 355 >> 10);
    return n >= 0 ? mant << n : mant >> -n;
}

/* log2 a + b's power sum, from log2 a and log2 b (Q8) */
static int32_t pu_logadd(int32_t a, int32_t b)
{
    int32_t m = a > b ? a : b, d = a > b ? a - b : b - a;
    return d >= 16 * 128 ? m : m + PU_LOGADD[(d + 64) >> 7];
}

/* |H|^2 of one SVF core as log2, Q8, at knob position kq8 (Q8) for cutoff c (knob units): weights Q6 (64 = 1),
 * damping Q12. 13.4 knob steps an octave (filter.c: 20 Hz .. 14 kHz over 0 .. 127). */
static int32_t pu_svf(int32_t kq8, int32_t c, int32_t damp, int32_t wl, int32_t wb, int32_t wh)
{
    int32_t l = ((kq8 - (c << 8)) * 305) >> 8;
    int64_t r, r2, dre, dim, nre, nim;
    if (l > 6 * 4096) l = 6 * 4096;
    if (l < -6 * 4096) l = -6 * 4096;
    r = pu_exp2(l);
    r2 = (r * r) >> 12;
    dre = 4096 - r2;
    dim = (r * damp) >> 12;
    nre = (int64_t)wl * 4096 - (int64_t)wh * r2;
    nim = (int64_t)wb * r;
    return pu_log2((uint64_t)(nre * nre + nim * nim) + 1u) - pu_log2((uint64_t)(dre * dre + dim * dim) + 1u) - 12 * 256;
}

/* MULTI's |H|^2 as log2, Q8: the loop's input and four stages G = 1 / (1 + j r), tapped by the
 * mode (tap[0] the input, tap[1..4] the stages), over the loop 1 + fb G^4, times the makeup at its input.
 * G and its powers Q14, the taps Q7. */
static int32_t pu_ladder(int32_t kq8, int32_t c, const int16_t *tap, int32_t fb, int32_t drive_q8)
{
    int32_t l = ((kq8 - (c << 8)) * 305) >> 8, i;
    int64_t r, d, gre, gim, pre = 1 << 14, pim = 0, t, nre = (int64_t)tap[0] << 14, nim = 0, dre, dim;
    if (l > 6 * 4096) l = 6 * 4096;
    if (l < -6 * 4096) l = -6 * 4096;
    r = pu_exp2(l);
    d = (int64_t)4096 * 4096 + r * r;
    gre = ((int64_t)1 << 38) / d;
    gim = -((r << 26) / d);
    for (i = 0; i < 4; i++) {
        t = (pre * gre - pim * gim) >> 14;
        pim = (pre * gim + pim * gre) >> 14;
        pre = t;
        nre += pre * tap[i + 1];
        nim += pim * tap[i + 1];
    }
    dre = (1 << 14) + ((fb * pre) >> 12);
    dim = (fb * pim) >> 12;
    return pu_log2((uint64_t)(nre * nre + nim * nim) + 1u) - pu_log2((uint64_t)(dre * dre + dim * dim) + 1u) -
           14 * 256 + 2 * (pu_log2((uint64_t)drive_q8) - 8 * 256);
}

/* the morph's LP / BP / HP weights, Q6 (64 = 1): 0 LP .. 64 BP .. 127 HP, crossfaded (filter.c lbh_weights) */
static void pu_lbh(int32_t m, int32_t *wl, int32_t *wb, int32_t *wh)
{
    int32_t f = (m * 128) / 127;
    *wl = f <= 64 ? 64 - f : 0;
    *wb = f <= 64 ? f : 128 - f;
    *wh = f <= 64 ? 0 : f - 64;
}

/* one filter page's response at kq8, as filter.c's circuits give it (dsp/filter.h), DRIVE aside (it holds the level):
 * the SH-2's 4-pole loop tapped by its morph (LP24 y4, BP 4 (y2 - 2 y3 + y4), HP24 (1 - H)^4), MULTI's by its mode,
 * the MS-20's Sallen-Key 2-pole (1 / (s^2 + (2 - fb) s + 1)) at its morph's mix of LP, BP and HP, FORMANT's two bands,
 * spread apart */
static int32_t pu_filter_l2(const FilterParams *f, int32_t kq8, int32_t c)
{
    int32_t res = f->resonance > 115u ? 115 : f->resonance, damp = (((127 - res) * 180) + 1200) >> 3;
    int32_t m = f->morph > VAL_MAX ? (int32_t)VAL_MAX : f->morph, fb, wl, wb, wh, i;
    int16_t tap[5] = {0, 0, 0, 0, 0};
    switch (f->flavour) {
    case FLT_MULTI:
        fb = filter_feedback_q12(FLT_MULTI, res);
        filter_multi_taps(m, tap);
        return pu_ladder(kq8, c, tap, fb, 256 + ((fb * 64) >> 12));
    case FLT_MS20:
        fb = 8192 - filter_feedback_q12(FLT_MS20, res);
        pu_lbh(m, &wl, &wb, &wh);
        return pu_svf(kq8, c, fb < 64 ? 64 : fb, wl, wb, wh);
    case FLT_FORMANT:
        return pu_logadd(pu_svf(kq8, c, damp, 0, 64, 0), pu_svf(kq8, c + filter_formant_gap(m), damp, 0, 64, 0)) - 2 * 256;
    default: {                                       /* SH-2 */
        static const int16_t LP[5] = {0, 0, 0, 0, 128}, BP[5] = {0, 0, 512, -1024, 512}, HP[5] = {128, -512, 768, -512, 128};
        pu_lbh(m, &wl, &wb, &wh);
        for (i = 0; i < 5; i++)
            tap[i] = (int16_t)((wl * LP[i] + wb * BP[i] + wh * HP[i]) / 64);
        return pu_ladder(kq8, c, tap, filter_feedback_q12(FLT_MULTI, res), 256);
    }
    }
}

/* log2 |H|^2 (Q8) to a row: 0 dB at y 30, 1 px a dB (+24 .. -38 dB) */
static int32_t pu_db_y(int32_t l2)
{
    int32_t y = 30 - ((l2 * 771) >> 16);
    return y < 4 ? 4 : y > PV_GROUND - 1 ? PV_GROUND - 1 : y;
}

static int32_t pu_col_kq8(int32_t x) { return (x * 127 * 256) / (PV_W - 1); }

/* a curve: its line, and the ground under it in fog when fog is set */
static void pu_curve(const int32_t *ys, uint16_t c, uint32_t fog)
{
    int32_t x;
    for (x = 0; x < PV_W; x++) {
        if (fog)
            pu_fog_col(PV_X0 + x, ys[x] + 2, PV_GROUND, 7u, PUC_STONE, PU_VIZ_Y);
        if (x)
            cv_line(PV_X0 + x - 1, ys[x - 1], PV_X0 + x, ys[x], c);
    }
}

static void pu_db_axis(void)
{
    int32_t x;
    for (x = PV_X0; x < PV_X0 + PV_W; x += 4)        /* 0 dB */
        cv_pset(x, 30, PUC_STONE);
}

static int32_t pu_cut_x(int32_t c) { return PV_X0 + (c * (PV_W - 1)) / 127; }

static void pu_viz_filter(const UIStateMachine *sm, uint8_t i)
{
    static const char *const FLV[FLT_COUNT] = {"SH-2", "MULTI", "MS-20", "FORMANT"};
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    const FilterParams *f = &e->filter[i];
    int32_t x, c = f->cutoff > VAL_MAX ? (int32_t)VAL_MAX : f->cutoff;
    pu_db_axis();
    for (x = 0; x < PV_W; x++)
        pu_ys[x] = pu_db_y(pu_filter_l2(f, pu_col_kq8(x), c));
    pu_curve(pu_ys, SQC(THEME), 1u);
    sq_text(PV_X0, CAP_IN(S, 13), &AF_S, FLV[f->flavour < FLT_COUNT ? f->flavour : 0u], SQC(MID));
    {                                                /* what the morph does in this circuit, where it sits */
        char b[12], *e;
        uint8_t m = f->morph > VAL_MAX ? (uint8_t)VAL_MAX : f->morph;
        int32_t t;
        switch (f->flavour) {
        case FLT_MULTI:                              /* the mode nearest the knob */
            pu_cat(b, filter_multi_mode_name(m));
            break;
        case FLT_FORMANT:                            /* the bands' spread, in octaves to a tenth */
            t = (filter_formant_gap(m) * 100 + 67) / 134;
            e = pu_num(b, t / 10, 0u, 1u);
            *e++ = '.';
            pu_cat(pu_num(e, t % 10, 0u, 1u), " OCT");
            break;
        default:                                     /* SH-2, MS-20: the output nearest the morph */
            pu_cat(b, m < 32u ? "LP" : m < 96u ? "BP" : "HP");
            break;
        }
        sq_text_r(PV_X0 + PV_W, CAP_IN(S, 13), &AF_S, b, SQC(MID), SQC(BG));
    }
    if (f->flavour == FLT_FORMANT) {                 /* the second band's own stone */
        int32_t c2 = c + filter_formant_gap(f->morph > VAL_MAX ? (int32_t)VAL_MAX : f->morph);
        pu_menhir(pu_cut_x(c2 > 127 ? 127 : c2) - 1, PV_GROUND + 1, 3, 6, SQC(MID));
    }
    pu_menhir(pu_cut_x(c) - 1, PV_GROUND + 1, 3, 6, SQC(ACCENT));   /* the cutoff, a stone on the ground */
}

/* ------------------------------------------------------- filter mixer --- */
static void pu_viz_fltmix(const UIStateMachine *sm)
{
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    const FilterMixerParams *fm = &e->fltmix;
    uint8_t arch = varch_of(e);
    int32_t x, c1 = e->filter[0].cutoff, c2 = e->filter[1].cutoff;
    if (c1 > (int32_t)VAL_MAX) c1 = VAL_MAX;
    if (c2 > (int32_t)VAL_MAX) c2 = VAL_MAX;
    if (arch == VARCH_STACK) {                       /* A and B across the stereo field */
        int32_t w = fm->stack_width, b = fm->balance, xa, xb, ha, hb;
        pu_ground(PV_GROUND, PUC_STONE);
        sq_text(PV_X0, CAP_IN(S, 13), &AF_S, "L", SQC(DIM));
        sq_text_r(PV_X0 + PV_W, CAP_IN(S, 13), &AF_S, "R", SQC(DIM), SQC(BG));
        xa = pu_cut_x(64 - w / 2);
        xb = pu_cut_x(64 + (w + 1) / 2);
        ha = 8 + (127 - b) * 40 / 127;
        hb = 8 + b * 40 / 127;
        pu_menhir(xa - 8, PV_GROUND, 16, ha, SQC(THEME));
        pu_menhir_edge(xb - 8, PV_GROUND, 16, hb, SQC(ACCENT));
        sq_text_in(xa - 10, PV_GROUND - ha - 14 + CAP_IN(S, 13), 20, &AF_S, "A", SQC(MID), SQC(BG));
        sq_text_in(xb - 10, PV_GROUND - hb - 14 + CAP_IN(S, 13), 20, &AF_S, "B", SQC(MID), SQC(BG));
        return;
    }
    pu_db_axis();
    if (arch == VARCH_BINAURAL) {                    /* the two ears' cutoffs, CUTOFF L/R apart, and the phase offset */
        int32_t off = ((int32_t)fm->bin_cutoff_offset - (int32_t)BIPOLAR_CENTER) / 2, k, a;
        for (x = 0; x < PV_W; x++) {
            pu_ys2[x] = pu_db_y(pu_filter_l2(&e->filter[0], pu_col_kq8(x), c1 - off));
            pu_ys[x] = pu_db_y(pu_filter_l2(&e->filter[0], pu_col_kq8(x), c1 + off));
        }
        pu_curve(pu_ys2, SQC(MID), 0u);
        pu_curve(pu_ys, SQC(THEME), 1u);
        sq_text(PV_X0, CAP_IN(S, 13), &AF_S, "L", SQC(MID));
        sq_text(PV_X0 + 12, CAP_IN(S, 13), &AF_S, "R", SQC(THEME));
        for (k = 0; k < 16; k++)                     /* the phase offset on a ring of 16 stones */
            cv_rect(pu_px(220, 9, k * 4), pu_py(12, 9, k * 4), 1, 1, PUC_STONE);
        a = fm->bin_mod_phase >> 1;
        cv_line(220, 12, pu_px(220, 9, a), pu_py(12, 9, a), SQC(ACCENT));
        return;
    }
    /* DUAL FILTER: both curves dim, the mix bright: serial (1 into 2) .. parallel (balanced), log-interpolated */
    {
        int32_t b = fm->balance, r = fm->routing;
        int32_t g1 = b < 127 ? 2 * (pu_log2((uint32_t)(127 - b)) - pu_log2(127u)) : -64 * 256;
        int32_t g2 = b > 0 ? 2 * (pu_log2((uint32_t)b) - pu_log2(127u)) : -64 * 256;
        for (x = 0; x < PV_W; x++) {
            int32_t k = pu_col_kq8(x), l1 = pu_filter_l2(&e->filter[0], k, c1), l2 = pu_filter_l2(&e->filter[1], k, c2);
            int32_t ls = l1 + l2, lp = pu_logadd(l1 + g1, l2 + g2);
            pu_ys[x] = pu_db_y(ls + ((lp - ls) * r) / 127);
            pu_ys2[x] = pu_db_y(l1);
        }
        pu_curve(pu_ys2, PUC_STONE, 0u);
        for (x = 0; x < PV_W; x++)
            pu_ys2[x] = pu_db_y(pu_filter_l2(&e->filter[1], pu_col_kq8(x), c2));
        pu_curve(pu_ys2, PUC_STONE, 0u);
        pu_curve(pu_ys, SQC(THEME), 1u);
        sq_text(PV_X0, CAP_IN(S, 13), &AF_S, r < 43 ? "SERIAL" : r < 85 ? "SER / PAR" : "PARALLEL", SQC(MID));
    }
}

/* ----------------------------------------------------------- envelope --- */
static int32_t pu_env_w(uint8_t t) { return 4 + ((int32_t)t * t * 56) / 16129; }   /* 4 .. 60 px a stage */
#define PU_ENV_TOP 8
#define PU_ENV_HOLD 40

static int32_t pu_env_y(int32_t lvl_q15) { return PV_GROUND - 1 - (((PV_GROUND - 1 - PU_ENV_TOP) * lvl_q15) >> 15); }

/* the x where each stage starts: attack, decay, sustain, release, end */
static void pu_env_xs(const EnvParams *p, int32_t *xs)
{
    xs[0] = PV_X0 + 2;
    xs[1] = xs[0] + pu_env_w(p->attack);
    xs[2] = xs[1] + pu_env_w(p->decay);
    xs[3] = xs[2] + PU_ENV_HOLD;
    xs[4] = xs[3] + pu_env_w(p->release);
}

/* the barrow: attack rising fast then easing, decay and release falling as a curve, the sustain level held */
static void pu_viz_env(const UIStateMachine *sm, uint8_t i)
{
    const EnvParams *p = &sm->engine[sm->s.active_synth_layer]->env[i];
    int32_t xs[5], x, u, w, lv, s = ((int32_t)(p->sustain > VAL_MAX ? VAL_MAX : p->sustain) << 15) / 127;
    pu_env_xs(p, xs);
    for (x = 0; x < PV_W; x++) {
        int32_t sx = PV_X0 + x;
        if (sx < xs[0] || sx >= xs[4]) { pu_ys[x] = PV_GROUND - 1; continue; }
        if (sx < xs[1]) { w = xs[1] - xs[0]; u = ((sx - xs[0]) << 15) / w; lv = 32767 - (int32_t)(((int64_t)(32767 - u) * (32767 - u)) >> 15); }
        else if (sx < xs[2]) { w = xs[2] - xs[1]; u = 32767 - ((sx - xs[1]) << 15) / w; lv = s + (int32_t)(((int64_t)(32767 - s) * ((int64_t)u * u >> 15)) >> 15); }
        else if (sx < xs[3]) lv = s;
        else { w = xs[4] - xs[3]; u = 32767 - ((sx - xs[3]) << 15) / w; lv = (int32_t)(((int64_t)s * ((int64_t)u * u >> 15)) >> 15); }
        pu_ys[x] = pu_env_y(lv);
    }
    pu_curve(pu_ys, SQC(THEME), 1u);
    for (x = 1; x < 4; x++)                          /* the stage boundaries, faint */
        pu_fog_col(xs[x], PU_ENV_TOP, PV_GROUND, 3u, PUC_STONE, PU_VIZ_Y);
    if (i > 0u && p->invert)
        sq_text_r(PV_X0 + PV_W, CAP_IN(S, 13), &AF_S, "INVERTED", SQC(MID), SQC(BG));
}

static const ADSR *pu_env_of(const SynthVoiceNode *n, uint8_t i)
{
    return i == 0u ? &n->amp_env : i == 1u ? &n->mod_env : &n->aux_env;
}

/* where the newest voice's envelope is along the barrow; -1 when no voice sounds */
static int32_t pu_track_env(const UIStateMachine *sm, uint8_t i)
{
    const EnvParams *p = &sm->engine[sm->s.active_synth_layer]->env[i];
    int32_t v = pu_voice(sm->s.active_synth_layer), xs[5], l, s;
    const ADSR *a;
    if (v < 0)
        return -1;
    a = pu_env_of(&poly_bank[v], i);
    pu_env_xs(p, xs);
    l = a->level_q15 < 0 ? 0 : a->level_q15 > 32767 ? 32767 : a->level_q15;
    s = ((int32_t)(p->sustain > VAL_MAX ? VAL_MAX : p->sustain) << 15) / 127;
    switch (a->state) {
    case STATE_ATTACK: return xs[0] + (((xs[1] - xs[0]) * l) >> 15);
    case STATE_DECAY: return s >= 32767 ? xs[2] : xs[1] + (((xs[2] - xs[1]) * (32767 - l)) / (32767 - s + 1));
    case STATE_SUSTAIN: return (xs[2] + xs[3]) / 2;
    case STATE_RELEASE: return s <= 0 ? xs[3] : xs[3] + (((xs[4] - xs[3]) * (s - (l < s ? l : s))) / s);
    default: return -1;
    }
}

/* ---------------------------------------------------------------- LFO --- */
/* the playhead of LFO i: LFO1's for the layer, LFO2 / 3 the newest voice's; 0 when there is none */
static const LfoRuntimeState *pu_lfo_state(const UIStateMachine *sm, uint8_t i)
{
    uint8_t layer = sm->s.active_synth_layer;
    int32_t v;
    if (i == 0u)
        return &global_lfo1_state[layer];
    v = pu_voice(layer);
    return v >= 0 ? &poly_bank[v].poly_lfos.engines[i - 1u] : 0;
}

#define PU_LFO_X0 (PV_X0 + 2)
#define PU_LFO_W 200                 /* two cycles; the moon sits to the right */

static void pu_viz_lfo(const UIStateMachine *sm, uint8_t i)
{
    static const int16_t RND[3] = {14000, -11000, 6000};
    const LfoParams *p = &sm->engine[sm->s.active_synth_layer]->lfo[i];
    LfoRuntimeState st = {0};
    int32_t x, y, py = 0, lead = 0, fade = (int32_t)p->fade - (int32_t)BIPOLAR_CENTER, amp, sx;
    uint32_t step = (0xFFFFFFFFu / PU_LFO_W) * 2u;
    uint8_t rel = (uint8_t)(p->delay > LFO_REL_DELAY);
    if (!rel)
        lead = (int32_t)p->delay * 40 / LFO_REL_DELAY;   /* the delay: a flat lead-in */
    for (x = PU_LFO_X0; x < PU_LFO_X0 + PU_LFO_W + 4; x += 4)
        cv_pset(x, 34, PUC_STONE);
    for (x = 0; x < PU_LFO_W; x++) {
        int32_t xx = x - lead, s = 0;
        if (xx >= 0) {
            uint32_t ph = (uint32_t)xx * step;
            uint32_t cyc = (uint32_t)(((uint64_t)xx * step) >> 32);   /* which of the two cycles: its random values */
            st.current_random = RND[cyc & 1u];
            st.next_random = RND[(cyc & 1u) + 1u];
            s = lfo_peek_fixed(&st, p->shape, ph);
            amp = 32767;                             /* the fade: + ramps in, - dies away */
            if (fade > 0) { int32_t len = fade * PU_LFO_W / 64; if (xx < len) amp = (xx << 15) / len; }
            else if (fade < 0) { int32_t len = -fade * PU_LFO_W / 64; amp = xx < len ? ((len - xx) << 15) / len : 0; }
            s = (s * amp) >> 15;
        }
        y = 34 - ((s * 24) >> 15);
        sx = PU_LFO_X0 + x;
        if (x) cv_line(sx - 1, py, sx, y, xx >= 0 ? SQC(THEME) : SQC(MID));
        py = y;
    }
    if (rel)
        sq_text(PV_X0, CAP_IN(S, 13), &AF_S, "ON RELEASE", SQC(MID));
    if (p->clock_sync)
        sq_text_r(PU_LFO_X0 + PU_LFO_W, CAP_IN(S, 13), &AF_S, "SYNC", SQC(MID), SQC(BG));
}

/* the moon: a glyph of its own (it changes with the LFO, the curve does not) */
#define PU_MOON_X 212
#define PU_MOON_Y 22
#define PU_MOON_W 26
#define PU_MOON_H 28

static uint32_t pu_lfo_phase(const UIStateMachine *sm, uint8_t i, uint8_t *live)
{
    const LfoRuntimeState *st = pu_lfo_state(sm, i);
    *live = (uint8_t)(st != 0);
    return st ? st->phase_accumulator : 0u;
}

static void pu_glyph_moon(const UIStateMachine *sm, uint8_t i)
{
    uint8_t live;
    uint32_t ph = pu_lfo_phase(sm, i, &live);
    cv_begin(PU_MOON_W, PU_MOON_H, SQC(BG));
    pu_moon(PU_MOON_W / 2, PU_MOON_H / 2, 9, live ? ph : 0x80000000u, live ? SQC(THEME) : PUC_STONE, SQC(SURF));
    cv_blit(PU_MOON_X, PU_VIZ_Y + PU_MOON_Y);
}

static int32_t pu_track_lfo(const UIStateMachine *sm, uint8_t i)
{
    const LfoParams *p = &sm->engine[sm->s.active_synth_layer]->lfo[i];
    uint8_t live;
    uint32_t ph = pu_lfo_phase(sm, i, &live);
    int32_t lead = p->delay > LFO_REL_DELAY ? 0 : (int32_t)p->delay * 40 / LFO_REL_DELAY;
    if (!live)
        return -1;
    return PU_LFO_X0 + lead + (int32_t)(((uint64_t)ph * (PU_LFO_W / 2)) >> 32);
}

/* ------------------------------------------------------------ mod slot --- */
#define PU_HIST 100
static int8_t pu_hist[PU_HIST];
static uint8_t pu_hist_slot = 0xFFu;

/* the slot's output, sampled once a UI frame into a scrolling history: the newest voice's, or with none sounding the
 * layer's own voice-less pass (LFO1 and the knobs still move it). synth_mod_watch keeps the slot running even when
 * nothing reads it. */
static void pu_hist_push(const UIStateMachine *sm, uint8_t slot)
{
    uint8_t layer = sm->s.active_synth_layer;
    int32_t v = pu_voice(layer), o, i;
    if (slot != pu_hist_slot) {
        for (i = 0; i < PU_HIST; i++) pu_hist[i] = 0;
        pu_hist_slot = slot;
    }
    o = (v >= 0 ? poly_bank[v].mod_fn[slot].out_q15 : synth_mod_layer_out(layer, slot)) >> 9;   /* -64 .. +63 */
    for (i = 0; i < PU_HIST - 1; i++)
        pu_hist[i] = pu_hist[i + 1];
    pu_hist[PU_HIST - 1] = (int8_t)(o < -64 ? -64 : o > 63 ? 63 : o);
}

/* the sources as the capstone's equation names them */
static const char *const PU_SRC_SHORT[MSRC_COUNT] = {
    "NONE", "VEL", "KEY", "WHEEL", "TOUCH", "ENV1", "ENV2", "ENV3", "LFO1", "LFO2", "LFO3",
    "MOD1", "MOD2", "MOD3", "CC1", "CC2",
};
/* the functions as equations, \1 standing for source A and \2 for B (MFN_* order) */
static const char *const PU_FN_EQ[MFN_COUNT] = {
    "\1 * \2", "\1 + \2", "\1 - \2", "MAX(\1, \2)", "MIN(\1, \2)", "SAMPLE \2 ON \1", "QUANTIZE \2 TO \1",
    "ABS(\1 - \2)", "LAG \1 BY \2", "DIODE(\1 - \2)",
};

/* The MOD pages have no menu (every parameter is on the knobs), so the tomb takes everything below the knobs:
 * PU_MOD_Y .. PU_TRACK_Y, drawn as two bands (one canvas holds 240 x 124). Tomb rows y 0 .. PU_MOD_H - 1, band k's
 * canvas row 0 at tomb row k * PU_MOD_BAND. The capstone x 4 .. 235, y 1 .. 17; the portal stones x 12 .. 65 and
 * 174 .. 227 down to the ground; the chamber between them, the portal's eye at its middle. */
#define PU_MOD_Y PU_MENU_Y
#define PU_MOD_H (PU_TRACK_Y - PU_MENU_Y)
#define PU_MOD_BAND (PU_MOD_H / 2)
#define PU_MOD_GROUND (PU_MOD_H - 4)
#define PU_TOMB_CAP_Y 1
#define PU_TOMB_CAP_H 36
#define PU_TOMB_TOP (PU_TOMB_CAP_Y + PU_TOMB_CAP_H)
#define PU_TOMB_STONE_W 50
#define PU_TOMB_A_X 12
#define PU_TOMB_B_X 178
#define PU_TOMB_CX 120
#define PU_TOMB_CY ((PU_TOMB_TOP + PU_MOD_GROUND) / 2)
#define PU_TOMB_R 50                 /* the rim of the portal, a circle filling the chamber */

/* sin of a / 1024 turns, Q8, between SQ_SIN's 64 steps */
static int32_t pu_sin1k(int32_t a)
{
    int32_t i = (a >> 4) & 63, f = a & 15;
    return PU_SIN[i] + (((PU_SIN[(i + 1) & 63] - PU_SIN[i]) * f) >> 4);
}

/* s cut into the stone, centred in x0 .. x0 + w: lit, the runes glow in the groove; cold, only the groove */
static void pu_carve(int32_t x0, int32_t y, int32_t w, const char *s, uint8_t lit)
{
    int32_t x = x0 + HALF_UP(w - sq_rune_w(s, 0));
    sq_rune(x + 1, y + 1, s, lit ? PU_ROCK[sq_pal][0] : PUC_STONE, 0);
    sq_rune(x, y, s, lit ? SQC(THEME) : PU_ROCK[sq_pal][0], 0);
}

/* s cut into the capstone at twice the size, centred across it: lit, the runes glow in the groove */
static void pu_carve_big(int32_t y, const char *s, uint8_t lit)
{
    int32_t n = 0, i, j, a_, k, x;
    const uint8_t *g;
    uint16_t c = lit ? SQC(THEME) : PU_ROCK[sq_pal][0];
    while (s[n])
        n++;
    for (k = 0; k < 2; k++) {                        /* the shadow, then the rune */
        x = 120 - (n * 12 - 2) / 2 + 1 - k;
        for (i = 0; i < n; i++, x += 12) {
            g = sq_rune_glyph(s[i]);
            for (j = 0; j < SQ_RUNE_H * 2; j++)
                for (a_ = 0; a_ < SQ_RUNE_W * 2; a_++)
                    if ((g[j / 2] >> (SQ_RUNE_W - 1 - a_ / 2)) & 1u)
                        cv_pset(x + a_, y + j - k, k ? c : PUC_STONE);
        }
    }
}

/* the slot's equation, cut into the capstone: twice the size when it fits, else at the stones' size */
static void pu_tomb_equation(const ModSlotParams *m, int32_t oy)
{
    char b[48], *d = b;
    const char *t = PU_FN_EQ[m->func < MFN_COUNT ? m->func : 0u], *nm;
    uint8_t lit = (uint8_t)(m->src_a != MSRC_NONE || m->src_b != MSRC_NONE);
    int32_t n;
    for (; *t; t++) {
        if (*t == '\1' || *t == '\2') {
            uint8_t src = *t == '\1' ? m->src_a : m->src_b;
            for (nm = PU_SRC_SHORT[src < MSRC_COUNT ? src : 0u]; *nm; nm++)
                *d++ = *nm;
        } else {
            *d++ = *t;
        }
    }
    *d = '\0';
    n = (int32_t)(d - b);
    if (n * 12 - 2 <= 224)
        pu_carve_big(PU_TOMB_CAP_Y + (PU_TOMB_CAP_H - 2 * SQ_RUNE_H) / 2 - oy, b, lit);
    else
        pu_carve(4, PU_TOMB_CAP_Y + (PU_TOMB_CAP_H - SQ_RUNE_H) / 2 - oy, 232, b, lit);
}

/* The destinations (2026-10-17): every destination the architecture has (the page's rows, UIStateMachine.c MOD_ROWS)
 * stands as a stone in a ring round the portal, in the table's order from 12 o'clock clockwise, so a destination always
 * stands in the same place. A stone this slot feeds is lit, taller the more it gets, and a ray runs out to it from the
 * eye: the slot's output travelling outward (the newest at the eye), swinging wider and brighter the more of it the
 * destination gets, turned over for a negative amount. A stone another source feeds is an outline; one nothing feeds a
 * low stub. The Select Encoder's stone is in the accent, and its name, amount (or what feeds it) under the capstone. */
#define PU_MRING_CY 104
#define PU_MRING_RX 102
#define PU_MRING_RY 40
#define PU_SEL_Y (PU_TOMB_TOP + 3)

/* s with a dark edge round its runes, so it reads over the portal */
static void pu_rune_edged(int32_t x, int32_t y, const char *s, uint16_t c)
{
    sq_rune(x - 1, y, s, SQC(BG), 0);
    sq_rune(x + 1, y, s, SQC(BG), 0);
    sq_rune(x, y - 1, s, SQC(BG), 0);
    sq_rune(x, y + 1, s, SQC(BG), 0);
    sq_rune(x, y, s, c, 0);
}

static int32_t pu_hist_at_q4(int32_t t);
static void pu_rad_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, uint16_t dim, int32_t smoke_y);

/* stone k of n: its foot on the ring */
static void pu_ring_at(int32_t k, int32_t n, int32_t *x, int32_t *y)
{
    int32_t a = n > 0 ? (k * 1024 + 512) / n : 0;
    *x = PU_TOMB_CX + ((PU_MRING_RX * pu_sin1k(a)) >> 8);
    *y = PU_MRING_CY - ((PU_MRING_RY * pu_sin1k(a + 256)) >> 8);
}

/* how much of the slot a destination gets, -64 .. +64 (a unipolar amount 0 .. 127 halved), from its row's value */
static int32_t pu_dest_amt(const UIStateMachine *sm, uint8_t r)
{
    char b[8];
    const char *t = pu_row_text(sm, r, b);
    int32_t v = 0, neg = 0;
    if (*t == '-') { neg = 1; t++; }
    else if (*t == '+') t++;
    for (; *t >= '0' && *t <= '9'; t++)
        v = v * 10 + (*t - '0');
    if (v > 64) v = (v + 1) / 2;                     /* a 0 .. 127 amount */
    return neg ? -v : v;
}

/* the ray to a stone at (x1, y1) from the eye: the output's history along it, bent across it by amt */
static void pu_ray(int32_t x1, int32_t y1, int32_t amt, int32_t oy)
{
    int32_t dx = x1 - PU_TOMB_CX, dy = y1 - PU_MRING_CY, len = 0, s, a = amt < 0 ? -amt : amt, px = 0, py = 0;
    int32_t swing = 1 + a * 9 / 64;                  /* 1 .. 10 px at full */
    uint16_t c = a > 44 ? SQC(ACCENT) : a > 20 ? SQC(THEME) : SQC(MID);
    while ((len + 1) * (len + 1) <= dx * dx + dy * dy)
        len++;
    if (len < 14)
        return;
    for (s = 10; s < len - 4; s++) {                 /* joined point to point: a swing never breaks the line */
        int32_t v = pu_hist_at_q4((s - 10) * ((PU_HIST - 1) << 4) / (len - 14)), d = v * swing / 64;
        int32_t x, y;
        if (amt < 0) d = -d;
        x = PU_TOMB_CX + dx * s / len - dy * d / len;
        y = PU_MRING_CY + dy * s / len + dx * d / len - oy;
        if (s > 10)
            pu_rad_line(px, py, x, y, c, c, -1000);
        px = x;
        py = y;
    }
}

static void pu_viz_mod_ring(const UIStateMachine *sm, int32_t oy, uint8_t sealed)
{
    int32_t n = ui_sm_row_count(sm), f = sm->s.current_param_focus, k, x, y;
    char b[40], v[8];
    for (k = 0; k < n; k++) {                        /* the rays first, under the stones */
        int32_t amt;
        if (sealed || ui_sm_mod_route(sm, (uint8_t)k) != UIMOD_HERE)
            continue;
        amt = pu_dest_amt(sm, (uint8_t)k);
        if (amt == 0)
            continue;
        pu_ring_at(k, n, &x, &y);
        pu_ray(x, y - 4, amt, oy);
    }
    for (k = 0; k < n; k++) {
        uint8_t route = ui_sm_mod_route(sm, (uint8_t)k), cur = (uint8_t)(k == f);
        int32_t a = route == UIMOD_HERE ? pu_dest_amt(sm, (uint8_t)k) : 0, h;
        pu_ring_at(k, n, &x, &y);
        if (a < 0) a = -a;
        if (route == UIMOD_HERE) {
            h = 7 + a * 9 / 64;                      /* 7 .. 16 px */
            sq_stone(x - 3, y - oy, 6, h, h, cur ? SQC(ACCENT) : a ? SQC(THEME) : SQC(MID), (uint32_t)(k * 7 + 5), 0u, 0u);
        } else if (route == UIMOD_OTHER) {
            sq_stone(x - 3, y - oy, 6, 9, 9, cur ? SQC(ACCENT) : SQC(DIM), (uint32_t)(k * 7 + 5), 1u, 0u);
        } else {
            sq_stone(x - 3, y - oy, 6, cur ? 9 : 4, cur ? 9 : 4, cur ? SQC(ACCENT) : PUC_STONE, (uint32_t)(k * 7 + 5),
                     (uint32_t)cur, 0u);
        }
    }
    if (n == 0) {
        pu_rune_edged(120 - HALF_UP(sq_rune_w("NO DESTINATION", 0)), PU_SEL_Y - oy, "NO DESTINATION", SQC(DIM));
        return;
    }
    if (f < n) {                                     /* the stone under the cursor, by name */
        const char *t = pu_row_text(sm, (uint8_t)f, v);
        uint8_t route = ui_sm_mod_route(sm, (uint8_t)f);
        pu_cat(pu_cat(pu_cat(b, "> "), ui_sm_row_label(sm, (uint8_t)f)), "  ");
        pu_cat(b + pu_strlen(b), t);
        pu_rune_edged(120 - HALF_UP(sq_rune_w(b, 0)), PU_SEL_Y - oy, b,
                      route == UIMOD_HERE ? SQC(THEME) : SQC(MID));
    }
}

/* what the list shows, for the dirty check */
static uint32_t pu_mod_dests_sig(const UIStateMachine *sm, uint32_t h)
{
    char b[8];
    uint8_t n = ui_sm_row_count(sm), r;
    h = sq_mix(h, (uint32_t)n | (uint32_t)sm->s.current_param_focus << 8 | (uint32_t)sm->s.active_synth_layer << 16);
    for (r = 0; r < n; r++)
        h = pu_mix_str(sq_mix(pu_mix_str(h, ui_sm_row_label(sm, r)), ui_sm_row_state(sm, r)), pu_row_text(sm, r, b));
    return h;
}

/* the radiance: the output's history wound out of the eye the way the spiral wound it, as closed rings. Ring k (0 at
 * the eye) holds PU_RAD_PER frames, the newest innermost, going round it clockwise from 12 o'clock, so each ring is a
 * stretch of the waveform bent around the eye: pushed out where it was high, in where it was low, more on the outer
 * rings. A ring's light is its value within the history's range (bright high, faint low), dimming toward the rim; the
 * eye glows with the value now. As the frames age the shapes travel outward. */
#define PU_RAD_RINGS 6
#define PU_RAD_PER 8                 /* frames a ring: PU_RAD_RINGS * PU_RAD_PER of the PU_HIST */
#define PU_RAD_R0 7
#define PU_RAD_STEP_Q4 55            /* 3.4 px a ring, Q4 */
static uint16_t pu_rad_col(int32_t v, int32_t lo, int32_t span, int32_t fade)   /* v in lo .. lo + span - 1 */
{
    static const uint8_t lv[5] = {SQC_SURF, SQC_DIM, SQC_MID, SQC_THEME, SQC_ACCENT};
    int32_t l = (v - lo) * 5 / span - fade;
    if (l < 0)
        return PUC_STONE;
    return sq_c(lv[l > 4 ? 4 : l]);
}

/* frame t (Q4, 0 = the newest) of the history, between its frames */
static int32_t pu_hist_at_q4(int32_t t)
{
    int32_t i = PU_HIST - 1 - (t >> 4), f = t & 15, a, b;
    if (i < 1)
        return pu_hist[0];
    a = pu_hist[i];
    b = pu_hist[i - 1];
    return a + (((b - a) * f) >> 4);
}

/* a ring's segment, a pixel at a time: behind the list's rows (tomb rows .. smoke_y, the chamber's width) in dim */
static void pu_rad_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, uint16_t dim, int32_t smoke_y)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1,
            err = dx + dy, e2;
    for (;;) {
        cv_pset(x0, y0, y0 < smoke_y ? dim : c);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* smoke_y: the canvas row the list's rows end at (the portal above it is dimmed, as through smoked glass) */
static void pu_portal_radiate(int32_t oy, int32_t smoke_y)
{
    int32_t k, j, n, a, r, v, t, x, y, px = 0, py = 0, cy = PU_MRING_CY - oy, lo = 127, hi = -128, span, i;
    uint16_t c;
    for (i = 0; i < PU_HIST; i++) {                  /* the light follows the history's own range: any movement shows */
        if (pu_hist[i] < lo) lo = pu_hist[i];
        if (pu_hist[i] > hi) hi = pu_hist[i];
    }
    span = hi - lo + 1;
    if (span < 16) {                                 /* a still output: a steady glow by its level */
        lo = -64;
        span = 128;
    }
    for (k = PU_RAD_RINGS - 1; k >= 0; k--) {
        int32_t base_q4 = (PU_RAD_R0 << 4) + PU_RAD_STEP_Q4 * k, bend = 2 + k / 2;   /* the bend: 2 .. 7 px at full */
        n = 12 + 2 * k;                              /* points round the ring */
        for (j = 0; j <= n; j++) {
            a = (j % n) * 1024 / n;
            /* the ring's frames round it, closed: the last stretch blends back into its first frame */
            t = (k * PU_RAD_PER << 4) + ((j % n) * PU_RAD_PER << 4) / n;
            v = pu_hist_at_q4(t);
            if (j * 8 > n * 7) {
                int32_t w = (j * 8 - n * 7) * 16 / n, v0 = pu_hist_at_q4(k * PU_RAD_PER << 4);   /* 0 .. 16 */
                v = v + (((v0 - v) * w) >> 4);
            }
            r = (base_q4 + ((v * bend) << 4) / 64) >> 4;
            x = PU_TOMB_CX + ((r * pu_sin1k(a)) >> 8);
            y = cy - ((r * pu_sin1k(a + 256)) >> 8);
            c = pu_rad_col(v, lo, span, k * 3 / PU_RAD_RINGS);
            if (j)
                pu_rad_line(px, py, x, y, c, pu_rad_col(v, lo, span, k * 3 / PU_RAD_RINGS + 2), smoke_y);
            px = x;
            py = y;
        }
    }
    c = pu_rad_col(pu_hist[PU_HIST - 1], lo, span, -1);
    r = 2 + (pu_hist[PU_HIST - 1] + 64) / 40;         /* the eye: 2 .. 5 px */
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                cv_pset(PU_TOMB_CX + x, cy + y, c);
}

/* A portal tomb (2026-10-17): the capstone cut with the slot's equation ("LFO1 * ENV1"); under it the portal radiates the
 * slot's output (the newest voice's, else the layer's) over its last frames (pu_portal_radiate), ringed by the
 * destination stones with a ray out to each one the slot feeds (pu_viz_mod_ring); at amount 0 the portal is sealed
 * (dark) and no ray runs. One band of it, its top at tomb row oy. */
static void pu_viz_mod_band(UIStateMachine *sm, uint8_t slot, int32_t oy)
{
    const ModSlotParams *m = &sm->engine[sm->s.active_synth_layer]->mod[slot];
    uint8_t sealed = (uint8_t)(m->amount == BIPOLAR_CENTER);
    int32_t x, y, y0, y1;
    /* the dark of the chamber: a few motes drifting in it */
    y0 = PU_TOMB_TOP > oy ? PU_TOMB_TOP : oy;
    y1 = PU_MOD_GROUND < oy + PU_MOD_BAND ? PU_MOD_GROUND : oy + PU_MOD_BAND;
    for (y = y0; y < y1; y++)
        for (x = 4; x < 236; x++)
            if (pu_speck(x, y + PU_MOD_Y, 63u))
                cv_pset(x, y - oy, SQC(SURF));
    if (!sealed)                                     /* at amount 0 the slot does nothing: the portal is sealed */
        pu_portal_radiate(oy, -1000);
    pu_ground(PU_MOD_GROUND - oy, PUC_STONE);
    if (oy < PU_TOMB_TOP) {
        pu_block_lit(4, PU_TOMB_CAP_Y - oy, 232, PU_TOMB_CAP_H, 0, PU_MOD_Y + oy, 0x7037u);
        pu_tomb_equation(m, oy);
    }
    pu_viz_mod_ring(sm, oy, sealed);
}

/* the whole tomb, band by band */
static void pu_viz_mod(UIStateMachine *sm, uint8_t slot)
{
    int32_t oy;
    for (oy = 0; oy < PU_MOD_H; oy += PU_MOD_BAND) {
        cv_begin(240, PU_MOD_H - oy < PU_MOD_BAND ? PU_MOD_H - oy : PU_MOD_BAND, SQC(BG));
        pu_viz_mod_band(sm, slot, oy);
        cv_blit(0, PU_MOD_Y + oy);
    }
}

/* ---------------------------------------------------------------- AMP --- */
/* Each sounding voice a stone: its height the level, its place the pan; layer 2's outlined in the lighter accent. The
 * amp's knobs weather the plain. DRIVE crumbles the stones, sound at 0, rubble at 127. DELAY SEND raises rows of
 * fainter stones behind them, the echoes: a row for each third of the send, each further back (higher, smaller,
 * drawn in toward the middle). REVERB SEND brings a fog in over the plain, higher and thicker with the send, and a
 * thin mist across the stones' feet. */
#define PU_AMP_W 12

/* one voice's stone: row k (0 the voices themselves, 1.. the echoes behind), at size s / 256 */
static void pu_amp_stone(const SynthVoiceNode *n, int32_t v, int32_t k, int32_t s, uint32_t decay, uint16_t c)
{
    int32_t l = pu_voice_level(n), mid = PV_X0 + PV_W / 2, w = (PU_AMP_W * s) >> 8, h, x;
    if (l <= 0)
        return;
    if (w < 4)
        w = 4;
    h = ((2 + ((l * 54) >> 15)) * s) >> 8;
    x = mid + (((pu_cut_x(pu_voice_pan(n)) - mid) * s) >> 8) - w / 2;
    if (x < PV_X0) x = PV_X0;
    if (x > PV_X0 + PV_W - w) x = PV_X0 + PV_W - w;
    sq_stone(x, PV_GROUND - 7 * k, w, h, h, c, (uint32_t)(v * 8 + k + 11), (uint32_t)n->layer, decay);
}

static void pu_viz_amp(const UIStateMachine *sm)
{
    const AmpParams *a = &sm->engine[sm->s.active_synth_layer]->amp;
    int32_t v, k, x, y, top, rows = ((int32_t)a->delay_send * 3 + 126) / 127, fog = (int32_t)a->reverb_send * 50 / 127;
    uint32_t decay = (uint32_t)(a->drive > VAL_MAX ? VAL_MAX : a->drive) * 2u;
    if (fog > 0)                                     /* the fog, behind everything: thick low down, thinning upward */
        for (x = PV_X0; x < PV_X0 + PV_W; x++) {
            top = PV_GROUND - fog + ((PU_SIN[(x / 3) & 63] * 3) >> 8) + ((PU_SIN[(x / 11 + 9) & 63] * 4) >> 8);
            for (y = top < 4 ? 4 : top; y < PV_GROUND; y++) {
                int32_t d = (PV_GROUND - y) * 4 / (fog + 1);     /* 0 at the ground .. 3 at the top */
                if (pu_speck(x, y + PU_VIZ_Y, a->reverb_send > 90u ? (4u << d) - 1u : (8u << d) - 1u))
                    cv_pset(x, y, PUC_STONE);
            }
        }
    for (k = rows; k >= 1; k--) {                    /* the echoes' rows, the furthest first, each on its own horizon */
        for (x = PV_X0 + 2 * k; x < PV_X0 + PV_W - 2 * k; x += 2 + k)
            cv_pset(x, PV_GROUND - 7 * k, PUC_STONE);
        for (v = 0; v < POLYPHONY_VOICES; v++)
            pu_amp_stone(&poly_bank[v], v, k, 256 - 56 * k, decay, k == 1 ? SQC(DIM) : PUC_STONE);
    }
    pu_ground(PV_GROUND, PUC_STONE);
    sq_text(PV_X0, CAP_IN(S, 13), &AF_S, "L", SQC(DIM));
    sq_text_r(PV_X0 + PV_W, CAP_IN(S, 13), &AF_S, "R", SQC(DIM), SQC(BG));
    cv_rect(PV_X0 + PV_W / 2, PV_GROUND + 1, 1, 3, SQC(DIM));
    for (v = 0; v < POLYPHONY_VOICES; v++)
        pu_amp_stone(&poly_bank[v], v, 0, 256, decay, poly_bank[v].layer ? SQC(ACCENT) : SQC(THEME));
    if (fog > 0)                                     /* the mist round their feet, in front */
        for (x = PV_X0; x < PV_X0 + PV_W; x++)
            for (y = PV_GROUND - 1 - fog / 4; y < PV_GROUND; y++)
                if (pu_speck(x + 7, y + PU_VIZ_Y, 7u))
                    cv_pset(x, y, SQC(DIM));
}

static uint32_t pu_voices_sig(uint32_t h)
{
    int32_t v;
    for (v = 0; v < POLYPHONY_VOICES; v++) {
        const SynthVoiceNode *n = &poly_bank[v];
        int32_t l = pu_voice_level(n);
        h = sq_mix(h, (uint32_t)(l >> 10) | (uint32_t)(l > 0 ? pu_voice_pan(n) >> 1 : 0) << 8 | (uint32_t)n->layer << 16);
    }
    return h;
}

/* ------------------------------------------------------------ effects --- */
static void pu_viz_fx(const UIStateMachine *sm)
{
    static const char *const REV[REVTYPE_COUNT] = {"SPRING", "TOMB", "MOD", "CELESTIAL", "DOLMEN", "TUMULUS"};
    const MasterPerformancePreset *p = sm->perf;
    int32_t sp = 5 + (int32_t)p->delay_time * 34 / 127, fb = (int32_t)p->delay_feedback * 124 / 127;
    int32_t x, h = 52, k, xr, len, y, top, dark = 1 + p->delay_degrade / 43;
    int32_t fog_h = 50 - (int32_t)p->reverb_damp * 26 / 127;
    pu_ground(PV_GROUND, PUC_STONE);
    /* the reverb's fog, behind the stones: from the pre-delay, as long as the decay, lower as it is damped */
    xr = PV_X0 + 8 + (int32_t)p->reverb_predelay * 30 / 127;
    len = 24 + (int32_t)p->reverb_decay * 170 / 127;
    for (x = xr; x < xr + len && x < PV_X0 + PV_W; x++) {
        int32_t d = x - xr;
        top = PV_GROUND - fog_h + (fog_h * d) / len;
        for (y = top; y < PV_GROUND; y++)
            if (pu_speck(x, y + PU_VIZ_Y, d < len / 3 ? 3u : d < 2 * len / 3 ? 7u : 15u))
                cv_pset(x, y, PUC_STONE);
    }
    /* the dry stone, then each repeat a stone row receding: shorter by the feedback, darker as the tape dulls it */
    pu_menhir(PV_X0 + 2, PV_GROUND, 7, h, SQC(TEXT));
    x = PV_X0 + 4;
    for (k = 1; k < 40; k++) {
        h = (h * fb) >> 7;
        x += sp;
        if (h < 2 || x > PV_X0 + PV_W - 5)
            break;
        pu_menhir(x, PV_GROUND, 5, h, k * dark < 3 ? SQC(THEME) : k * dark < 7 ? SQC(MID) : SQC(DIM));
    }
    sq_text_r(PV_X0 + PV_W, CAP_IN(S, 13), &AF_S, REV[p->reverb_type < REVTYPE_COUNT ? p->reverb_type : 0u], SQC(MID),
              SQC(BG));
    sq_text(PV_X0 + 12, CAP_IN(S, 13), &AF_S, p->delay_type == DELAYTYPE_BBD ? "BBD" : "TAPE", SQC(MID));
    if (p->delay_sync)
        sq_text(PV_X0 + 46, CAP_IN(S, 13), &AF_S, "SYNC", SQC(MID));
}

/* ------------------------------------------------------------- global --- */
/* the sarsen circle: a glyph of its own, one stone lit per 16th of the bar, the heel stone outside it to the north-east */
#define PU_RING_X 78
#define PU_RING_W 84
#define PU_RING_CX 42
#define PU_RING_CY 38

static uint32_t pu_beat(uint8_t *live)
{
    uint32_t ph = 0;
    *live = midi_clock_phase(0u, &ph);
    return *live ? ph >> 28 : 0u;
}

static void pu_glyph_ring(void)
{
    uint8_t live;
    uint32_t beat = pu_beat(&live), k;
    cv_begin(PU_RING_W, PU_VIZ_H, SQC(BG));
    for (k = 0; k < 16u; k++) {
        int32_t a = (int32_t)k * 4, x = pu_px(PU_RING_CX, 26, a), y = pu_py(PU_RING_CY, 26, a);
        uint16_t c = live && k == beat ? SQC(ACCENT) : k % 4u == 0u ? SQC(MID) : PUC_STONE;
        pu_menhir(x - 2, y + 3, 4, live && k == beat ? 7 : 6, c);
    }
    pu_menhir(pu_px(PU_RING_CX, 36, 6) - 2, pu_py(PU_RING_CY, 36, 6) + 4, 5, 8, SQC(MID));   /* the heel stone */
    cv_blit(PU_RING_X, PU_VIZ_Y);
}

static void pu_viz_global(const UIStateMachine *sm)
{
    char b[12];
    const MasterPerformancePreset *p = sm->perf;
    uint8_t midi = (uint8_t)(p->clock_source == CLKSRC_MIDI);
    if (midi) {
        sq_text(PV_X0, 20 + CAP_IN(M, 20), &AF_M, "MIDI", SQC(THEME));
    } else {
        pu_num(b, MIDI_CLOCK_BPM_BASE + p->int_tempo, 0u, 1u);
        sq_text(PV_X0, 20 + CAP_IN(M, 20), &AF_M, b, SQC(THEME));
    }
    sq_text(PV_X0, 40 + CAP_IN(S, 13), &AF_S, midi ? "CLOCK" : "BPM", SQC(MID));
    pu_num(pu_cat(b, "CH "), p->midi_ch[0] + 1, 0u, 1u);
    sq_text_r(PV_X0 + PV_W, 20 + CAP_IN(S, 13), &AF_S, b, SQC(MID), SQC(BG));
    sq_text_r(PV_X0 + PV_W - 34, 20 + CAP_IN(S, 13), &AF_S, "L1", SQC(DIM), SQC(BG));
    if (p->perf_mode != PERF_SINGL_SYNTH) {
        pu_num(pu_cat(b, "CH "), p->midi_ch[1] + 1, 0u, 1u);
        sq_text_r(PV_X0 + PV_W, 36 + CAP_IN(S, 13), &AF_S, b, SQC(MID), SQC(BG));
        sq_text_r(PV_X0 + PV_W - 34, 36 + CAP_IN(S, 13), &AF_S, "L2", SQC(DIM), SQC(BG));
    }
}

/* --------------------------------------------------------------- home --- */
/* the voice pool as Carnac's alignment: 8 places falling in height, each voice's stone drawn as on AMP (its height
 * the level, layer 2's outlined in the lighter accent), an idle voice a low unlit stone; the places no layer may use
 * (the voice caps) crumble to a few specks. A low moon, a few leaves on the wind. */
static void pu_viz_home(const UIStateMachine *sm)
{
    static const uint8_t H[POLYPHONY_VOICES] = {64, 59, 54, 50, 46, 42, 39, 36};   /* the sky is theirs: up to 4 px under the top */
    int first[NLAYER], count[NLAYER];
    int32_t v, k, x, y;
    uint8_t mode = sm->perf->perf_mode;
    pool_range(0, &first[0], &count[0]);
    pool_range(1, &first[1], &count[1]);
    if (mode == PERF_SINGL_SYNTH)
        count[1] = 0;
    for (x = PV_X0; x < PV_X0 + PV_W; x++)           /* the earth */
        pu_fog_col(x, PV_GROUND, PU_VIZ_H, 7u, PUC_STONE, PU_VIZ_Y);
    for (v = 0; v < POLYPHONY_VOICES; v++) {
        const SynthVoiceNode *n = &poly_bank[v];
        uint8_t l1 = (uint8_t)(v >= first[0] && v < first[0] + count[0]), l2 = (uint8_t)(v >= first[1] && v < first[1] + count[1]);
        int32_t h = H[v], lvl = pu_voice_level(n), fill;
        x = 12 + v * 28;
        if (!l1 && !l2) {                            /* no layer may use it: a ruin */
            for (y = PV_GROUND - h; y < PV_GROUND; y++)
                for (k = 0; k < 14; k++)
                    if (pu_speck(x + k, y + PU_VIZ_Y, 7u))
                        cv_pset(x + k, y, PUC_STONE);
            continue;
        }
        if (lvl <= 0) {                              /* idle: a low unlit stone holds its place */
            sq_stone(x + 1, PV_GROUND, PU_AMP_W, 4, 4, l2 && !l1 ? SQC(DIM) : PUC_STONE, (uint32_t)(v * 8 + 11), 0u, 0u);
            continue;
        }
        /* sounding: as on AMP, the stone's height its level (up to its place in the falling row), layer 2's outlined
         * in the lighter accent; the same grain as that voice's stone on AMP */
        fill = 2 + ((lvl * (h - 2)) >> 15);
        sq_stone(x + 1, PV_GROUND, PU_AMP_W, fill, fill, n->layer ? SQC(ACCENT) : SQC(THEME), (uint32_t)(v * 8 + 11),
                 (uint32_t)n->layer, 0u);
    }
}

/* HOME View 3: the keyboard (its 75 white keys, 3 px each) with the split stone and the fade between the layers, and
 * the velocity range below with its own split */
static int32_t pu_note_x(int32_t note)
{
    static const uint8_t WH[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};    /* white keys below, within the octave */
    int32_t w = (note / 12) * 7 + WH[note % 12];
    return 7 + w * 3 + ((0x54Au >> (note % 12)) & 1 ? 2 : 0);
}

static void pu_split_bar(int32_t y, int32_t thr, int32_t fade, int32_t (*xof)(int32_t), int32_t x_end)
{
    int32_t lo = thr - (fade >> 1), hi = thr + (fade >> 1), xl, xh, x;
    if (lo < 0) lo = 0;
    if (hi > 127) hi = 127;
    xl = xof(lo);
    xh = xof(hi);
    cv_rect(7, y, xl - 7, 2, SQC(THEME));
    cv_rect(xh, y, x_end - xh, 2, SQC(ACCENT));
    for (x = xl; x < xh; x++)                        /* the fade: the layers dithered into each other */
        cv_rect(x, y, 1, 2, ((x - xl) * 8 / (xh - xl + 1)) > (int32_t)((x * 5 + y) & 7) ? SQC(ACCENT) : SQC(THEME));
}

static int32_t pu_vel_x(int32_t v) { return 7 + (v * 224) / 127; }

static void pu_viz_split(const UIStateMachine *sm)
{
    char b[8];
    const MasterPerformancePreset *p = sm->perf;
    int32_t n, x, sx;
    for (n = 0; n < 128; n++) {                      /* the keys: white ones as 2 px slats, black ones short and dark */
        if ((0x54Au >> (n % 12)) & 1)
            continue;
        cv_rect(pu_note_x(n), 14, 2, 14, PUC_STONE);
    }
    for (n = 0; n < 128; n++)
        if ((0x54Au >> (n % 12)) & 1)
            cv_rect(pu_note_x(n), 14, 1, 8, SQC(DIM));
    pu_split_bar(30, p->split_note, p->split_fade, pu_note_x, 232);
    sx = pu_note_x(p->split_note > VAL_MAX ? (int32_t)VAL_MAX : p->split_note);
    pu_menhir(sx - 2, 13, 5, 12, SQC(TEXT));         /* the split stone, standing over its key */
    sq_text(7, CAP_IN(S, 12) - 1, &AF_S, "L1", SQC(THEME));
    sq_text_r(232, CAP_IN(S, 12) - 1, &AF_S, "L2", SQC(ACCENT), SQC(BG));
    ui_sm_note_name(p->split_note > VAL_MAX ? (uint8_t)VAL_MAX : p->split_note, b);
    x = sx + 6;
    if (x > 196) x = sx - 6 - sq_text_w(&AF_S, b);
    sq_text(x, CAP_IN(S, 12) - 1, &AF_S, b, SQC(TEXT));
    /* velocity */
    sq_text(7, 40 + CAP_IN(S, 13), &AF_S, "VELOCITY", SQC(MID));
    for (x = 7; x < 232; x += 2)
        cv_pset(x, 56, PUC_STONE);
    pu_split_bar(58, p->vel_split, p->vel_fade, pu_vel_x, 232);
    sx = pu_vel_x(p->vel_split > VAL_MAX ? (int32_t)VAL_MAX : p->vel_split);
    pu_menhir(sx - 2, 58, 5, 8, SQC(TEXT));
    pu_num(b, p->vel_split, 0u, 1u);
    sq_text_r(232, 40 + CAP_IN(S, 13), &AF_S, b, SQC(TEXT), SQC(BG));
}

/* --------------------------------------------------------------- save --- */
/* the performance as a dolmen: its name on the capstone, the four links it rests on as stones (6.1); layer 2's stone
 * dark under SINGL */
/* how much each part of the performance holds, 0..PU_FILL_MAX: a patch and the sequencer by the bytes changed from
 * their factory slot (an init patch or the factory C3 row reads 0), the arp by the steps holding a chord */
#define PU_FILL_MAX 16
static int32_t pu_fill_diff(const uint8_t *a, const uint8_t *b, uint32_t n, int32_t full)
{
    int32_t d = 0;
    while (n--)
        d += *a++ != *b++;
    d = d * PU_FILL_MAX / full;
    return d > PU_FILL_MAX ? PU_FILL_MAX : d;
}

static void pu_preset_fill(const UIStateMachine *sm, int32_t f[4])
{
    static EngineState def_e;
    static SequencerLane def_s[SEQ_NLANE];
    static uint8_t ready;
    uint8_t k, n = 0u;
    if (!ready) {
        engine_defaults(&def_e);
        seq_defaults(def_s);
        ready = 1u;
    }
    f[0] = pu_fill_diff((const uint8_t *)sm->engine[0], (const uint8_t *)&def_e, sizeof def_e, 48);
    f[1] = pu_fill_diff((const uint8_t *)sm->engine[1], (const uint8_t *)&def_e, sizeof def_e, 48);
    f[2] = pu_fill_diff((const uint8_t *)sm->lanes, (const uint8_t *)def_s, sizeof def_s, 96);
    for (k = 0; k < ARP_NSTEP; k++)
        n = (uint8_t)(n + (sm->arp->steps[k].chord_root != ARP_CHORD_EMPTY));
    f[3] = n * PU_FILL_MAX / ARP_NSTEP;
}

/* HOME's performance browser (6.1): the performance is a dolmen, its name above. The capstone is the performance,
 * resting on four pillars, the parts it links: L1 and L2's patches, the sequence and the arp. Each pillar is as thick
 * as its part holds (pu_preset_fill) and the capstone as thick as all four together. The stones are the voice stones
 * of HOME and AMP (sq_stone): lit in the theme where the live part holds something (saved or not), an outline where
 * it is still the factory slot, rubble for layer 2 under SINGL */
static void pu_viz_preset(const UIStateMachine *sm)
{
    static const char *const LBL[4] = {"L1 ", "L2 ", "SEQ ", "ARP "};
    char b[NAME_LEN + 1];
    const MasterPerformancePreset *p = sm->perf;
    uint8_t v[4], k, off;
    int32_t f[4], x, w, y, total = 0, cap, yb = 52, ph = 20;
    v[0] = p->layer_patch[0]; v[1] = p->layer_patch[1]; v[2] = p->seq_link; v[3] = p->arp_link;
    pu_preset_fill(sm, f);
    sq_text_in(0, CAP_IN(M, 22), 240, &AF_M, pu_perf_name(p, b), SQC(THEME), SQC(BG));
    pu_ground(yb, PUC_STONE);
    for (k = 0; k < 4u; k++) {
        off = (uint8_t)(k == 1u && p->perf_mode == PERF_SINGL_SYNTH);
        x = 36 + k * 56;
        w = 4 + f[k] * 10 / PU_FILL_MAX;             /* 4 .. 14 px */
        if (!off)
            total += f[k];
        if (off) {                                   /* layer 2 under SINGL: rubble where its pillar would stand */
            for (y = yb - 6; y < yb; y++)
                for (w = 0; w < 10; w++)
                    if (pu_speck(x - 5 + w, y + PU_VIZ_Y, 7u))
                        cv_pset(x - 5 + w, y, PUC_STONE);
        } else if (f[k] == 0) {
            sq_stone(x - w / 2, yb, w, ph, ph, SQC(MID), 31u + k * 7u, 1u, 0u);
        } else {
            sq_stone(x - w / 2, yb, w, ph, ph, SQC(THEME), 31u + k * 7u, 0u, 0u);
        }
        pu_num(pu_cat(b, LBL[k]), v[k], 0u, 3u);
        sq_text_in(x - 28, yb + 2 + CAP_IN(S, 14), 56, &AF_S, b, off ? SQC(DIM) : SQC(TEXT), SQC(BG));
    }
    cap = 5 + total * 6 / (4 * PU_FILL_MAX);         /* 5 .. 11 px: the performance's whole */
    sq_stone(22, yb - ph, 196, cap, cap, total ? SQC(THEME) : SQC(MID), 97u, (uint32_t)(total == 0), 0u);
}

/* 6.2.2: the name, its character cursor, and while RENAME is focused the floating keyboard (the character under
 * the cursor picked out on the noisy ground, in white). Otherwise 6.2.3: what the commit writes and where, what that
 * slot holds now (an overwrite in the accent), and the clipboard. ARP_SEQ / SEQ_SETTINGS have no name: the
 * performance's shows dimmed. */
#define PU_CELL_W 16
#define PU_CELL_H 14

static const char *const PU_SAVE_TARGET[UISAVE_COUNT] = {"ARP", "SEQUENCE", "PATCH", "PERFORMANCE"};

/* the clipboard as one line: "CLIP  L1 PATCH  GLASS PAD", "CLIP  SEQ", "CLIP  EMPTY" */
static void pu_clip_text(const UIStateMachine *sm, char *b)
{
    static const char *const KIND[UISAVE_COUNT] = {"ARP", "SEQ", "PATCH", "PERF "};
    char n[NAME_LEN + 1];
    char *e = pu_cat(b, "CLIP  ");
    if (sm->clip_kind >= UISAVE_COUNT) {
        pu_cat(e, "EMPTY");
        return;
    }
    if (sm->clip_kind == UISAVE_PATCH_PRESET)
        e = pu_cat(e, sm->clip_layer ? "L2 " : "L1 ");
    e = pu_cat(e, KIND[sm->clip_kind]);
    if (sm->clip_kind == UISAVE_PATCH_PRESET)
        pu_cat(pu_cat(e, "  "), pu_name(sm->clip.patch.name, n));
    else if (sm->clip_kind == UISAVE_WHOLE_PERF)
        pu_cat(pu_cat(e, " "), pu_name(sm->clip.whole.perf.name, n));
}

/* The save page's menu (2026-10-17): what SAVE stores, large: the name (or the kind, for a sequence or an arp, which
 * have none); then where it goes and what that slot holds (an overwrite in the accent), the clipboard, and the hold
 * that copies and pastes, so it is never hidden */
static void pu_draw_save_menu(const UIStateMachine *sm)
{
    char b[48], n[NAME_LEN + 1], *e;
    const uint8_t *name = ui_sm_rename_name(sm);
    uint8_t tgt = sm->save_target < UISAVE_COUNT ? sm->save_target : (uint8_t)UISAVE_WHOLE_PERF, st;
    int32_t len, sc;
    const char *big = name != NULL ? pu_name(name, n) : PU_SAVE_TARGET[tgt];
    len = pu_strlen(big);
    if (len == 0)
        big = "NO NAME", len = 7;
    sc = PU_RUNE_N_W(len, 2) <= 224 ? 2 : 1;
    pu_rune_n(120 - HALF_UP(PU_RUNE_N_W(len, sc)), 6, big, name != NULL ? SQC(THEME) : SQC(MID), sc, len, 0);
    st = ui_sm_slot_state(sm, n);
    e = pu_num(pu_cat(b, "SLOT "), sm->save_slot, 0u, 3u);
    if (st == UISLOT_USED)
        pu_cat(pu_cat(e, "   OVERWRITES "), n[0] ? n : "IN USE");
    else if (st == UISLOT_EMPTY)
        pu_cat(e, "   EMPTY");
    sq_text_in(0, 30 + CAP_IN(S, 13), 240, &AF_S, b, st == UISLOT_USED ? SQC(ACCENT) : SQC(TEXT), SQC(BG));
    pu_clip_text(sm, b);
    sq_text_in(0, 46 + CAP_IN(S, 13), 240, &AF_S, b, ui_sm_paste_ok(sm) ? SQC(MID) : SQC(DIM), SQC(BG));
    sq_text_in(0, 62 + CAP_IN(S, 13), 240, &AF_S, "HOLD SAVE + OCT-  COPY  OCT+  PASTE", SQC(DIM), SQC(BG));
}

/* the slot field's moon, r px, at (cx, cy) above the horizon yh: full over an empty slot, a thin crescent in the
 * accent over one a save would overwrite (the rest of its disc faint) */
static void pu_save_moon(int32_t cx, int32_t cy, int32_t r, uint8_t over, int32_t yh)
{
    int32_t x, y, hw;
    for (y = -r; y <= r && cy + y < yh; y++) {
        for (hw = r; hw > 0 && hw * hw + y * y > r * r + r / 2; hw--) {}
        for (x = -hw; x <= hw; x++) {
            uint16_t c;
            if (over)
                c = x * 4 > hw * 2 ? SQC(ACCENT) : SQC(SURF);   /* a waning sliver */
            else
                c = sq_mare(x, y, r) ? SQC(THEME) : SQC(TEXT);
            cv_pset(cx + x, cy + y, c);
        }
    }
}

/* 6.2.2 / 2026-10-17: the bottom of the save page. While knobs 3 / 4 turn, the name a cell a letter and the keyboard;
 * otherwise the bank on the plain: all its slots left to right (128 patches, 32 of the rest), a short mark for each used one; the chosen slot a
 * standing stone (an outline in the theme when empty, solid in the accent when a save would overwrite it) with its
 * number at its foot, and right above it on the arch the moon, full over an empty slot, a crescent over a used one. */
#define PU_SLOT_X(s, n) (10 + (int32_t)(s) * 220 / ((int32_t)(n) - 1))   /* slot 0 .. n - 1 along the plain */

static void pu_viz_commit(const UIStateMachine *sm)
{
    static const char map[] = UI_CHAR_MAP;
    const uint8_t *name = ui_sm_rename_name(sm);
    char c[2] = {0, 0}, n[NAME_LEN + 1], b[4];
    uint8_t cur = sm->name_cursor < NAME_LEN ? sm->name_cursor : 0u, kb = ui_sm_keyboard_visible(sm), i, ch;
    int32_t x, y;
    if (!kb) {
        enum { YH = 56, RX = 112, RY = 42, MR = 7 };
        uint8_t tgt = sm->save_target < UISAVE_COUNT ? sm->save_target : (uint8_t)UISAVE_WHOLE_PERF;
        uint8_t here = ui_sm_slot_state(sm, n) == UISLOT_USED, ns = ui_sm_bank_slots(tgt);
        int32_t k, mx, my, dx, q, r2;
        for (x = 120 - RX + 2; x < 120 + RX; x += 3) {   /* the arch, faint dots */
            dx = x - 120;
            q = RY * RY - RY * RY * dx * dx / (RX * RX);
            for (r2 = 0; (r2 + 1) * (r2 + 1) <= q; r2++) {}
            cv_pset(x, YH - r2, PUC_STONE);
        }
        mx = PU_SLOT_X(sm->save_slot, ns);               /* the moon on it, right over the chosen slot */
        dx = mx - 120;
        q = RY * RY - RY * RY * dx * dx / (RX * RX);
        for (r2 = 0; (r2 + 1) * (r2 + 1) <= q; r2++) {}
        my = YH - r2;
        if (my > YH - 20 - MR - 3)                   /* (near the horizon: kept clear of the stone) */
            my = YH - 20 - MR - 3;
        pu_save_moon(mx, my, MR, here, YH);
        pu_ground(YH, PUC_STONE);
        for (k = 0; k < (int32_t)ns; k++) {          /* the bank: a mark for each used slot */
            if (k == sm->save_slot || ui_sm_slot_query(sm, tgt, (uint8_t)k, n) != UISLOT_USED)
                continue;
            cv_rect(PU_SLOT_X(k, ns), YH - 5, 1, 5, SQC(MID));
        }
        x = PU_SLOT_X(sm->save_slot, ns);            /* the chosen slot, a stone in front of the moon */
        sq_stone(x - 3, YH, 7, 20, 20, here ? SQC(ACCENT) : SQC(THEME), (uint32_t)(sm->save_slot * 7 + 3), here ? 0u : 1u, 0u);
        pu_num(b, sm->save_slot, 0u, 3u);
        x = x - 16 < 0 ? 0 : x + 16 > 240 ? 208 : x - 16;
        sq_text_in(x, YH + 2 + CAP_IN(S, 13), 32, &AF_S, b, SQC(TEXT), SQC(BG));
        return;
    }
    if (name == NULL)
        name = sm->perf->name;
    for (i = 0; i < NAME_LEN; i++) {                 /* the name, one cell a character, the cursor's underlined */
        ch = name[i] < CHAR_MAP_LEN ? name[i] : 0u;
        x = 8 + i * 14;
        c[0] = map[ch];
        if (i == cur) {
            cv_rect(x, 15, 12, 2, SQC(ACCENT));
            sq_text_in(x, CAP_IN(S, 14), 12, &AF_S, ch ? c : "_", SQC(HI), SQC(BG));
        } else {
            cv_rect(x + 2, 15, 8, 1, PUC_STONE);
            if (ch) sq_text_in(x, CAP_IN(S, 14), 12, &AF_S, c, SQC(TEXT), SQC(BG));
        }
    }
    ch = name[cur] < CHAR_MAP_LEN ? name[cur] : 0u;
    for (i = 0; i < CHAR_MAP_LEN; i++) {             /* the keyboard: A..Z, 0..9 . - _ / and SPACE last */
        uint8_t idx = (uint8_t)(i + 1u < CHAR_MAP_LEN ? i + 1u : 0u), row = (uint8_t)(i / 14u), col = (uint8_t)(i % 14u);
        x = 8 + col * PU_CELL_W;
        y = 22 + row * PU_CELL_H;
        c[0] = map[idx];
        if (idx == ch) {
            sq_noise_block(x, y, idx ? PU_CELL_W - 1 : PU_CELL_W * 2 - 1, PU_CELL_H - 1, 0u, PU_VIZ_Y, 1u);
            sq_text_in(x, y + CAP_IN(S, PU_CELL_H - 1), idx ? PU_CELL_W - 1 : PU_CELL_W * 2 - 1, &AF_S, idx ? c : "SPC",
                       SQC(HI), PUC_HL);
        } else {
            sq_text_in(x, y + CAP_IN(S, PU_CELL_H - 1), idx ? PU_CELL_W - 1 : PU_CELL_W * 2 - 1, &AF_S, idx ? c : "SPC",
                       SQC(MID), SQC(BG));
        }
    }
}

/* -------------------------------------------------------------- dialog --- */
/* 6.2.2 / 6.2.3: the confirm popup (NO / YES, the pick in a channel cut in the stone) over a SAVE: ARE YOU SURE? to an
 * empty or unknown slot, OVERWRITE? (in the accent) with the slot and what it holds; over a PASTE: PASTE? and what it
 * replaces. Then WRITING, and SAVED [NAME] TO SLOT [N] (a patch's or the performance's name, else the kind) */
static void pu_draw_dialog(const UIStateMachine *sm)
{
    static const char *const OVER[UISAVE_COUNT] = {"OVER LIVE ARP", "OVER LIVE SEQ", "OVER L1 PATCH", "OVER WHOLE PERF"};
    static const char *const KIND[UISAVE_COUNT] = {"ARP PATTERN", "SEQUENCE", "", ""};
    char b[NAME_LEN + 1], s[40], *e;
    const char *title;
    uint8_t tgt = sm->save_target < UISAVE_COUNT ? sm->save_target : (uint8_t)UISAVE_WHOLE_PERF, warn = 0u;
    int32_t k;
    cv_begin(PU_DLG_W, PU_DLG_H, SQC(BG));
    pu_slab(PU_DLG_W, PU_DLG_H, PU_DLG_X, PU_DLG_Y);
    if (sm->dialog == UIDLG_CONFIRM) {
        if (sm->save_action == UIACT_PASTE) {
            title = "PASTE?";
            pu_cat(s, OVER[tgt]);
            if (tgt == UISAVE_PATCH_PRESET && sm->s.active_synth_layer)
                s[6] = '2';                          /* "OVER L2 PATCH" */
        } else {
            e = pu_num(pu_cat(s, "SLOT "), sm->save_slot, 0u, 3u);
            warn = (uint8_t)(ui_sm_slot_state(sm, b) == UISLOT_USED);
            title = warn ? "OVERWRITE?" : "ARE YOU SURE?";
            pu_cat(pu_cat(e, "  "), warn ? (b[0] ? b : "IN USE") : PU_SAVE_TARGET[tgt]);
        }
        sq_rune_in(0, 13, PU_DLG_W, title, warn ? SQC(ACCENT) : SQC(THEME), 2);
        sq_rune_in(0, 28, PU_DLG_W, s, SQC(MID), 0);
        for (k = 0; k < 2; k++) {
            int32_t x = 30 + k * 66;
            uint8_t on = (uint8_t)(sm->dialog_choice == (uint8_t)k);
            if (on)
                pu_trough(x, 44, 54, 15, PU_DLG_X, PU_DLG_Y);
            sq_rune_in(x, 47, 54, k ? "YES" : "NO", on ? SQC(TEXT) : SQC(DIM), 2);
        }
    } else if (sm->dialog == UIDLG_WRITING) {
        sq_rune_in(0, 30, PU_DLG_W, "WRITING", SQC(THEME), 2);
    } else {
        const uint8_t *nm = ui_sm_rename_name(sm);
        sq_rune_in(0, 12, PU_DLG_W, "SAVED", SQC(MID), 2);
        sq_rune_in(0, 29, PU_DLG_W, nm != NULL ? pu_name(nm, b) : KIND[tgt], SQC(THEME), 1);
        pu_num(pu_cat(s, "TO SLOT "), sm->save_slot, 0u, 3u);
        sq_rune_in(0, 46, PU_DLG_W, s, SQC(MID), 0);
    }
    cv_blit(PU_DLG_X, PU_DLG_Y);
}

/* -------------------------------------------------------------- popups --- */
/* Both popups take the whole screen while they are up, in two bands (one canvas holds 240 x 120), on black: a tablet's
 * hewn edge round the screen, the words in the rune face at 1x, 2x and 4x, a stone or two lit. */
#define PU_POP_BAND 120

/* the plain's ground in a popup: the dotted horizon at y, fog thinning upward below it to y1 */
static void pu_pop_ground(int32_t y, int32_t y1, int32_t oy)
{
    int32_t x, j;
    for (x = 14; x < 226; x += 2)
        cv_pset(x, y - oy, PUC_STONE);
    for (j = y + 1; j < y1; j++)
        for (x = 14; x < 226; x++)
            if (pu_speck(x, j, j > y1 - 8 ? 31u : j > y1 - 18 ? 63u : 127u))
                cv_pset(x, j - oy, PUC_STONE);
}

/* s centred on the screen at y - oy, sc times the size or smaller until it fits 212 px; returns the scale used */
static int32_t pu_pop_line(int32_t y, const char *s, uint16_t c, int32_t sc, int32_t oy)
{
    int32_t n = pu_strlen(s);
    if (n > 34) n = 34;
    while (sc > 1 && PU_RUNE_N_W(n, sc) > 212)
        sc--;
    pu_rune_n(120 - HALF_UP(PU_RUNE_N_W(n, sc)), y, s, c, sc, n, oy);
    return sc;
}

/* --------------------------------------------------------- preset list --- */
/* While the Preset Encoder turns: the stored presets of the page's kind (ui_sm_preset_target), seven rows around the
 * one under the cursor (ui_sm_browse_slot: loading or playing; it stays in the middle until the list runs out), each
 * its slot and its name in the runes at 2x; the cursor's lit, the rest dimmer. The slot under the cursor shows
 * even when the store has nothing in it (UNSAVED: the live patch). A row of dots on the right with a stone riding it:
 * where the cursor is among them all. Drawn whole on each move (the list only changes when the cursor does). */
static const char *const PU_BR_TITLE[UISAVE_COUNT] = {"ARP PATTERN", "SEQUENCE", "PATCH", "PERFORMANCE"};
#define PU_BR_ROWS 7u
#define PU_BR_ROW_Y 40
#define PU_BR_ROW_H 26

static uint8_t pu_br_has(const uint8_t *has, int32_t s) { return (uint8_t)((has[s >> 3] >> (s & 7)) & 1u); }

static void pu_draw_browse(const UIStateMachine *sm, uint32_t force)
{
    char nm[NAME_LEN + 1], b[12], nb[4];
    uint8_t has[(VAL_MAX + 1u) / 8u], row[PU_BR_ROWS], tgt = ui_sm_preset_target(sm), cur = ui_sm_browse_slot(sm);
    uint8_t layer = sm->s.active_synth_layer, i, st, on;
    int32_t s, n = 0, up = 0, dn = 0, above, below, at = 0, total = 0, y, oy;
    const char *t, *nmt;
    if (!pu_dirty(PUR_BROWSE, sq_mix(sq_mix(2166136261u, sq_pal), (uint32_t)tgt | (uint32_t)cur << 8 | (uint32_t)layer << 16),
                  force))
        return;
    for (s = 0; s < (int32_t)sizeof has; s++)
        has[s] = 0u;
    for (s = 0; s <= (int32_t)VAL_MAX; s++)          /* which slots the list holds, and where the cursor sits in it */
        if (s == cur || ui_sm_slot_query(sm, tgt, (uint8_t)s, nm) != UISLOT_EMPTY) {
            has[s >> 3] |= (uint8_t)(1u << (s & 7));
            total++;
            if (s < cur) at++;
        }
    for (s = cur - 1; s >= 0 && up < (int32_t)PU_BR_ROWS - 1; s--)
        up += pu_br_has(has, s);
    for (s = cur + 1; s <= (int32_t)VAL_MAX && dn < (int32_t)PU_BR_ROWS - 1; s++)
        dn += pu_br_has(has, s);
    above = (int32_t)PU_BR_ROWS / 2;                 /* the cursor in the middle, unless an end of the list is near */
    if (above < (int32_t)PU_BR_ROWS - 1 - dn) above = (int32_t)PU_BR_ROWS - 1 - dn;
    if (above > up) above = up;
    below = (int32_t)PU_BR_ROWS - 1 - above;
    if (below > dn) below = dn;
    for (s = cur; s >= 0 && n <= above; s--)         /* the rows, top first: the cursor's at index above */
        if (pu_br_has(has, s)) row[above - n++] = (uint8_t)s;
    for (s = cur + 1; s <= (int32_t)VAL_MAX && n < above + 1 + below; s++)
        if (pu_br_has(has, s)) row[n++] = (uint8_t)s;

    t = PU_BR_TITLE[tgt < UISAVE_COUNT ? tgt : UISAVE_PATCH_PRESET];
    if (tgt == UISAVE_PATCH_PRESET) {                /* "PATCH  L1": a patch loads into the focused layer */
        pu_cat(pu_cat(b, "PATCH  "), layer ? "L2" : "L1");
        t = b;
    }
    for (oy = 0; oy < 240; oy += PU_POP_BAND) {
        cv_begin(240, PU_POP_BAND, SQC(BG));
        pu_tablet(4, 4, 232, 232, 77u, oy);
        pu_pop_line(16, t, SQC(THEME), 1, oy);
        for (i = 0; i < (uint8_t)n; i++) {
            y = PU_BR_ROW_Y + (int32_t)i * PU_BR_ROW_H;
            on = (uint8_t)(row[i] == cur);
            st = ui_sm_slot_query(sm, tgt, row[i], nm);
            if (st == UISLOT_EMPTY)
                nmt = "UNSAVED";
            else if (tgt == UISAVE_SEQ_SETTINGS || tgt == UISAVE_ARP_SEQ)
                nmt = tgt == UISAVE_SEQ_SETTINGS ? "SEQUENCE" : "PATTERN";
            else
                nmt = st != UISLOT_USED ? "-" : nm[0] ? nm : "UNTITLED";
            pu_num(nb, row[i], 0u, 3u);
            pu_rune_n(20, y, nb, on ? SQC(THEME) : SQC(DIM), 2, 3, oy);
            pu_rune_n(68, y, nmt, on ? SQC(TEXT) : st == UISLOT_EMPTY ? SQC(DIM) : SQC(MID), 2, NAME_LEN, oy);
        }
        if (total == 1 && ui_sm_slot_query(sm, tgt, cur, nm) == UISLOT_EMPTY)
            pu_rune_n(68, PU_BR_ROW_Y + PU_BR_ROW_H + 4, "NOTHING SAVED", SQC(DIM), 1, 20, oy);
        if (total > (int32_t)PU_BR_ROWS) {           /* the dots on the right and the stone that rides them */
            int32_t len = (int32_t)PU_BR_ROWS * PU_BR_ROW_H - 10, th = len * (int32_t)PU_BR_ROWS / total;
            if (th < 6) th = 6;
            for (y = 0; y < len; y += 3)
                cv_pset(228, PU_BR_ROW_Y - 4 + y - oy, PUC_STONE);
            y = PU_BR_ROW_Y - 4 + (len - th) * at / (total - 1);
            pu_menhir(227, y + th - oy, 3, th, SQC(DIM));
        }
        cv_blit(0, (uint32_t)oy);
    }
}

/* ---------------------------------------------------------- parameters --- */
/* While the Select Encoder moves the cursor: one parameter at a time. The page's name at the top (and the layer's on
 * a layer page); the row under the cursor in the runes at 2x, the rows either side of it faint above and below; its
 * value carved at 4x. Along the bottom the page's rows stand as a row of stones on the plain, the cursor's lit and
 * taller; behind them the moon rides a low arch with the value, rising from the left horizon (the range's bottom),
 * overhead at its middle (a bipolar 0), setting on the right (its top), half sunk behind the horizon at either end.
 * Its phase goes with it: a waxing crescent at the left, full overhead, a waning crescent at the right; its seas
 * (the near side's maria) a shade darker, its unlit part a faint disc. */
#define PU_RP_HORIZON 206
#define PU_RP_ARC_RX 92
#define PU_RP_ARC_RY 46
#define PU_RP_MOON_R 8

/* the arch's point at place 0..127 (0 the left horizon, 127 the right): sin / cos from SQ_SIN (64 a turn, Q8),
 * stepped between its entries */
static void pu_arc_at(int32_t place, int32_t *x, int32_t *y)
{
    int32_t a = place * 32 / 127, fa = place * 32 - a * 127;
    int32_t sn = SQ_SIN[a & 63], cs = SQ_SIN[(a + 16) & 63];
    sn += (SQ_SIN[(a + 1) & 63] - sn) * fa / 127;
    cs += (SQ_SIN[(a + 17) & 63] - cs) * fa / 127;
    *x = 120 - PU_RP_ARC_RX * cs / 256;
    *y = PU_RP_HORIZON - PU_RP_ARC_RY * sn / 256;
}

/* the moon at (cx, cy - oy), its phase by place 0..127 (a 64th of a turn from 4 to 60: crescent, full at 64,
 * crescent), nothing of it at or below the horizon */
static void pu_rp_moon(int32_t cx, int32_t cy, int32_t place, int32_t oy)
{
    int32_t a = 4 + (place * 56 + 63) / 127, c = SQ_SIN[(a + 16) & 63], x, y, hw, lit;
    uint16_t col;
    for (y = -PU_RP_MOON_R; y <= PU_RP_MOON_R; y++) {
        if (cy + y >= PU_RP_HORIZON)
            break;
        for (hw = PU_RP_MOON_R; hw > 0 && hw * hw + y * y > PU_RP_MOON_R * PU_RP_MOON_R + PU_RP_MOON_R / 2; hw--) {}
        for (x = -hw; x <= hw; x++) {
            lit = a < 32 ? x * 256 > c * hw : x * 256 < -c * hw;   /* the terminator: cos(phase) of the row */
            col = !lit ? SQC(SURF) : sq_mare(x, y, PU_RP_MOON_R) ? SQC(THEME) : SQC(TEXT);   /* the seas (ui_seq_draw.c) */
            cv_pset(cx + x, cy + y - oy, col);
        }
    }
}

static void pu_draw_rowpop(const UIStateMachine *sm, uint32_t force)
{
    char b[8], tt[24];
    uint8_t vis[32], n = ui_sm_row_count(sm), nv = 0, r, f = sm->s.current_param_focus;
    int32_t ci = 0, i, y, oy, mx, my, gap, w, place = 0, sc;
    uint32_t h = sq_mix(sq_mix(2166136261u, sq_pal), (uint32_t)sm->page | (uint32_t)f << 8 |
                                                     (uint32_t)sm->s.active_synth_layer << 16);
    const char *val = "";
    for (r = 0; r < n && nv < sizeof vis; r++) {
        if (ui_sm_row_state(sm, r) == UIROW_HIDDEN)
            continue;
        if (r == f)
            ci = nv;
        vis[nv++] = r;
    }
    for (i = 0; i < nv; i++)
        h = pu_mix_str(sq_mix(h, ui_sm_row_state(sm, vis[i])), ui_sm_row_label(sm, vis[i]));
    if (f < n) {
        val = pu_row_text(sm, f, b);
        place = ui_sm_row_place(sm, f);
        h = sq_mix(pu_mix_str(h, val), (uint32_t)place);
    }
    if (!pu_dirty(PUR_ROWPOP, h, force))
        return;
    tt[0] = '\0';
    if (pu_layer_page(sm->page))                     /* "L1  FILTER 1" */
        pu_cat(tt, sm->s.active_synth_layer ? "L2  " : "L1  ");
    pu_cat(tt + pu_strlen(tt), PU_TITLE[sm->page]);
    pu_arc_at(place, &mx, &my);
    gap = nv > 1 ? 200 / (nv - 1) : 0;
    if (gap > 26) gap = 26;
    w = gap * (nv > 1 ? nv - 1 : 0);
    for (oy = 0; oy < 240; oy += PU_POP_BAND) {
        cv_begin(240, PU_POP_BAND, SQC(BG));
        pu_tablet(4, 4, 232, 232, 77u, oy);
        pu_pop_line(16, tt, SQC(THEME), 1, oy);
        if (nv) {
            y = 40;
            if (ci > 0)
                pu_pop_line(y, ui_sm_row_label(sm, vis[ci - 1]), SQC(DIM), 1, oy);
            y += SQ_RUNE_H + 8;
            sc = pu_pop_line(y, ui_sm_row_label(sm, vis[ci]), SQC(TEXT), 2, oy);
            y += SQ_RUNE_H * sc + 8;
            if (ci + 1 < nv)
                pu_pop_line(y, ui_sm_row_label(sm, vis[ci + 1]), SQC(DIM), 1, oy);
            y += SQ_RUNE_H;                          /* the value, in the room left above the arch */
            pu_pop_line(y + HALF_UP(150 - y - SQ_RUNE_H * 4), val, SQC(THEME), 4, oy);
            for (i = 2; i < 127; i += 3) {           /* the arch, faint dots */
                int32_t ax, ay;
                pu_arc_at(i, &ax, &ay);
                cv_pset(ax, ay - oy, PUC_STONE);
            }
            pu_rp_moon(mx, my, place, oy);
            for (i = 0; i < nv && i < 24; i++) {     /* the stones, one a row, in front of the moon */
                int32_t sh = 16 + (int32_t)((pu_hash(i, 5) >> 28) & 7u), x = 120 - HALF_UP(w) + i * gap - 1;
                if (i == ci)
                    pu_menhir(x - 2, PU_RP_HORIZON - oy, 7, sh + 12, SQC(THEME));
                else
                    pu_menhir(x - 1, PU_RP_HORIZON - oy, 5, sh, SQC(DIM));
            }
        } else {
            pu_pop_line(90, "ALL ON THE KNOBS", SQC(DIM), 1, oy);
        }
        pu_pop_ground(PU_RP_HORIZON, 226, oy);
        cv_blit(0, (uint32_t)oy);
    }
}

/* ============================================================== entry === */
/* the visualizer's own inputs beyond the page's data: live values, quantised */
static uint32_t pu_viz_live(UIStateMachine *sm, uint32_t h)
{
    uint8_t page = sm->page;
    int32_t voice;
    switch (page) {
    case UIPAGE_OSC1: case UIPAGE_OSC2:
        return sq_mix(h, (uint32_t)pu_osc_morph(sm, (uint8_t)(page - UIPAGE_OSC1), &voice) >> 6);
    case UIPAGE_OSCMIX:
        h = sq_mix(h, (uint32_t)pu_osc_morph(sm, 0u, &voice) >> 6);
        return sq_mix(h, (uint32_t)pu_osc_morph(sm, 1u, &voice) >> 6);
    case UIPAGE_MOD1: case UIPAGE_MOD2: case UIPAGE_MOD3:
        pu_hist_push(sm, (uint8_t)(page - UIPAGE_MOD1));
        h = sq_mix(pu_mod_dests_sig(sm, h), sm->engine[sm->s.active_synth_layer]->mod[page - UIPAGE_MOD1].amount);
        return sq_mix_bytes(h, (const uint8_t *)pu_hist, PU_HIST);
    case UIPAGE_AMP: case UIPAGE_HOME1: case UIPAGE_HOME2:
        return pu_voices_sig(h);
    case UIPAGE_PERF:                                /* the dolmen measures the live patches, sequence and arp */
        h = sq_mix_bytes(h, (const uint8_t *)sm->engine[0], sizeof(EngineState));
        h = sq_mix_bytes(h, (const uint8_t *)sm->engine[1], sizeof(EngineState));
        h = sq_mix_bytes(h, (const uint8_t *)sm->lanes, sizeof(SequencerLane) * SEQ_NLANE);
        return sq_mix_bytes(h, (const uint8_t *)sm->arp, sizeof(ArpState));
    default:
        return h;
    }
}

static void pu_draw_viz(UIStateMachine *sm)
{
    uint8_t page = sm->page;
    if (page >= UIPAGE_MOD1 && page <= UIPAGE_MOD3) {   /* the tomb: everything below the knobs, its own canvases */
        pu_viz_mod(sm, (uint8_t)(page - UIPAGE_MOD1));
        return;
    }
    cv_begin(240, PU_VIZ_H, SQC(BG));
    switch (page) {
    case UIPAGE_OSC1: case UIPAGE_OSC2: pu_viz_osc(sm, (uint8_t)(page - UIPAGE_OSC1)); break;
    case UIPAGE_ATTACK: pu_viz_attack(sm); break;
    case UIPAGE_OSCMIX: pu_viz_oscmix(sm); break;
    case UIPAGE_FILTER1: case UIPAGE_FILTER2: pu_viz_filter(sm, (uint8_t)(page - UIPAGE_FILTER1)); break;
    case UIPAGE_FLTMIX: pu_viz_fltmix(sm); break;
    case UIPAGE_ENV1: case UIPAGE_ENV2: case UIPAGE_ENV3: pu_viz_env(sm, (uint8_t)(page - UIPAGE_ENV1)); break;
    case UIPAGE_LFO1: case UIPAGE_LFO2: case UIPAGE_LFO3: pu_viz_lfo(sm, (uint8_t)(page - UIPAGE_LFO1)); break;
    case UIPAGE_AMP: pu_viz_amp(sm); break;
    case UIPAGE_EFFECTS: pu_viz_fx(sm); break;
    case UIPAGE_GLOBAL: pu_viz_global(sm); break;
    case UIPAGE_HOME1: case UIPAGE_HOME2: pu_viz_home(sm); break;
    case UIPAGE_HOME3: pu_viz_split(sm); break;
    case UIPAGE_PERF: pu_viz_preset(sm); break;
    case UIPAGE_SAVE_COMMIT: pu_viz_commit(sm); break;
    default: break;
    }
    cv_blit(0, PU_VIZ_Y);
}

/* the ground's playhead stone: x, or -1 for none */
static int32_t pu_track_x(const UIStateMachine *sm)
{
    uint8_t page = sm->page;
    if (page == UIPAGE_OSC1 || page == UIPAGE_OSC2)
        return pu_track_osc(sm, (uint8_t)(page - UIPAGE_OSC1));
    if (page >= UIPAGE_ENV1 && page <= UIPAGE_ENV3)
        return pu_track_env(sm, (uint8_t)(page - UIPAGE_ENV1));
    if (page >= UIPAGE_LFO1 && page <= UIPAGE_LFO3)
        return pu_track_lfo(sm, (uint8_t)(page - UIPAGE_LFO1));
    return -1;
}

static void pu_draw_track(int32_t x)
{
    cv_begin(240, PU_TRACK_H, SQC(BG));
    pu_ground(PU_TRACK_H - 1, PUC_STONE);
    if (x >= 0)
        pu_menhir(x - 1, PU_TRACK_H, 3, PU_TRACK_H, SQC(ACCENT));
    cv_blit(0, PU_TRACK_Y);
}

/* the glyph a page animates on its own (the moon, the sarsen circle): its sig, 0 for none */
static uint32_t pu_glyph_sig(const UIStateMachine *sm, uint32_t h)
{
    uint8_t page = sm->page, live;
    if (page >= UIPAGE_LFO1 && page <= UIPAGE_LFO3)
        return sq_mix(h, pu_lfo_phase(sm, (uint8_t)(page - UIPAGE_LFO1), &live) >> 27 | (uint32_t)live << 8);
    if (page == UIPAGE_GLOBAL)
        return sq_mix(h, pu_beat(&live) | (uint32_t)live << 8);
    return 0u;
}

static void page_draw(UIStateMachine *sm, uint32_t force)
{
    static uint8_t pal_drawn = 0xFFu;
    uint8_t page = sm->page, layer = sm->s.active_synth_layer, k, dlg = (uint8_t)(sm->dialog != UIDLG_NONE);
    uint8_t tomb = (uint8_t)(page >= UIPAGE_MOD1 && page <= UIPAGE_MOD3);   /* no menu: the viz has it all */
    uint32_t base = sq_mix(2166136261u, sq_pal), h, g;
    int viz_drawn;
    if (pal_drawn != sq_pal) {
        pal_drawn = sq_pal;
        force = 1u;
    }
    if (force) {                                     /* the gaps between the regions */
        lcd_fill(0, PU_HEAD_H, 240, PU_KNOB_Y - PU_HEAD_H, SQC(BG));
        lcd_fill(0, PU_KNOB_Y + PU_KNOB_H, 240, PU_MENU_Y - PU_KNOB_Y - PU_KNOB_H, SQC(BG));
        if (!tomb)
            lcd_fill(0, PU_MENU_Y + PU_MENU_H, 240, PU_VIZ_Y - PU_MENU_Y - PU_MENU_H, SQC(BG));
    }
    base = sq_mix(base, (uint32_t)page | (uint32_t)layer << 8);
    h = sq_mix(base, (uint32_t)sm->perf->perf_mode | (uint32_t)sm->perf_slot << 8 | (uint32_t)sm->save_target << 16);
    h = sq_mix(h, varch_of(sm->engine[layer]) | (uint32_t)ui_sm_layer_voices(sm, 0u) << 8 |
                      (uint32_t)ui_sm_layer_voices(sm, 1u) << 16);
    if (pu_dirty(PUR_HEAD, h, force))
        pu_draw_head(sm);
    for (k = 0; k < 4u; k++)
        if (pu_dirty(PUR_KNOB0 + k, pu_knob_sig(sm, page, k, base), force))
            pu_draw_knob(sm, page, k);
    if (sm->browse || sm->rowpop)                    /* a popup covers the rest (ui_screen_draw draws it) */
        return;
    pu_menu_scroll(sm);
    if (!tomb && !dlg && pu_dirty(PUR_MENU, pu_menu_sig(sm, base), force)) {   /* (MOD: the tomb has its place) */
        pu_draw_menu(sm);
    } else if (!tomb && dlg && pu_dirty(PUR_MENU, sq_mix(base, 0xD1A1u), force)) {
        lcd_fill(0, PU_MENU_Y, 240, PU_MENU_H, SQC(BG));   /* a dialog: the list steps back */
        pu_sig[PUR_DLG] = 0u;
    }
    /* the visualizer: everything it reads of the patch and the performance, the cursor (SAVE's keyboard), its live
     * values */
    h = sq_mix_bytes(base, (const uint8_t *)sm->engine[layer], sizeof(EngineState));
    h = sq_mix_bytes(h, (const uint8_t *)sm->perf, sizeof(MasterPerformancePreset));
    if (page == UIPAGE_SAVE_COMMIT) {                /* (the keyboard shows with the cursor on RENAME) */
        char n[NAME_LEN + 1];
        h = sq_mix(h, (uint32_t)ui_sm_keyboard_visible(sm) | (uint32_t)sm->name_cursor << 8 | (uint32_t)sm->save_slot << 16 |
                          (uint32_t)sm->save_target << 24);
        h = sq_mix(h, (uint32_t)ui_sm_slot_state(sm, n) | (uint32_t)sm->clip_kind << 8 | (uint32_t)sm->clip_layer << 16);
        h = sq_mix_bytes(h, (const uint8_t *)n, NAME_LEN);   /* what the slot holds (a save can change it) */
        if (sm->clip_kind < UISAVE_COUNT)            /* the clipboard's name, as the CLIP line shows it */
            h = sq_mix_bytes(h, sm->clip_kind == UISAVE_PATCH_PRESET ? sm->clip.patch.name : sm->clip.whole.perf.name,
                             NAME_LEN);
    }
    h = sq_mix(h, (uint32_t)ui_sm_layer_voices(sm, 0u) | (uint32_t)ui_sm_layer_voices(sm, 1u) << 8);
    h = pu_viz_live(sm, h);
    viz_drawn = pu_dirty(PUR_VIZ, h, force);
    if (viz_drawn)
        pu_draw_viz(sm);
    g = pu_glyph_sig(sm, base);
    if (g && pu_dirty(PUR_GLYPH, g, force || viz_drawn)) {
        if (page == UIPAGE_GLOBAL)
            pu_glyph_ring();
        else
            pu_glyph_moon(sm, (uint8_t)(page - UIPAGE_LFO1));
    }
    {
        int32_t tx = pu_track_x(sm);
        if (pu_dirty(PUR_TRACK, sq_mix(base, (uint32_t)(tx + 1)), force))
            pu_draw_track(tx);
    }
    if (dlg && pu_dirty(PUR_DLG, sq_mix(base, (uint32_t)sm->dialog | (uint32_t)sm->dialog_choice << 8 |
                                                    (uint32_t)sm->save_action << 16 | (uint32_t)sm->save_slot << 24), force))
        pu_draw_dialog(sm);
}

/* ------------------------------------------------------------ hold --- */
/* 2026-10-17: while HOME or SAVE is held (sm->hold), a strip along the bottom of every page names what OCT- / OCT+
 * do, so a held button's second job is never hidden. HOME: L1 / L2, the focused one lit (L2 dim under SINGL). SAVE:
 * COPY what SAVE would save here, PASTE the clipboard over it (dim while the clipboard holds another kind). Drawn
 * every frame while held (the page under it may redraw); its going redraws the page. */
#define PU_HOLD_Y 214
#define PU_HOLD_H 26

static void pu_draw_hold(const UIStateMachine *sm)
{
    static const char *const KIND[UISAVE_COUNT] = {"ARP", "SEQ", "PATCH", "PERF"};
    char l[24], r[24];
    uint16_t lc, rc;
    int32_t y = 9;
    cv_begin(240, PU_HOLD_H, SQC(BG));
    pu_hewn(1, 1, 238, PU_HOLD_H - 2, 4217u, SQC(MID), 0);
    if (sm->hold == BTN_HOME) {
        uint8_t l2 = (uint8_t)(sm->s.active_synth_layer == 1u), singl = (uint8_t)(sm->perf->perf_mode == PERF_SINGL_SYNTH);
        pu_cat(l, "< OCT-  L1");
        pu_cat(r, "L2  OCT+ >");
        lc = l2 ? SQC(MID) : SQC(TEXT);
        rc = singl ? SQC(DIM) : l2 ? SQC(TEXT) : SQC(MID);
    } else {
        uint8_t k = ui_sm_save_kind(sm);
        UIStateMachine *m = (UIStateMachine *)sm;    /* (paste_ok reads save_target: ask it for this page's kind) */
        uint8_t keep = m->save_target, ok;
        m->save_target = k < UISAVE_COUNT ? k : (uint8_t)UISAVE_WHOLE_PERF;
        ok = ui_sm_paste_ok(m);
        m->save_target = keep;
        pu_cat(pu_cat(l, "< OCT-  COPY "), KIND[k < UISAVE_COUNT ? k : UISAVE_WHOLE_PERF]);
        pu_cat(r, "PASTE  OCT+ >");
        lc = SQC(TEXT);
        rc = ok ? SQC(TEXT) : SQC(DIM);
    }
    sq_rune(10, y, l, lc, 0);
    sq_rune(230 - sq_rune_w(r, 0), y, r, rc, 0);
    cv_blit(0, PU_HOLD_Y);
}

static void ui_screen_draw(UIStateMachine *sm, const MaqSeq *q, const CarpSeq *a, uint32_t force)
{
    static uint8_t last_page = 0xFFu, last_layer, last_dlg, last_pop, last_pal = 0xFFu, last_hold = BTN_COUNT;
    uint8_t dlg = (uint8_t)(sm->dialog != UIDLG_NONE), moved = (uint8_t)(sm->page != last_page);
    uint8_t pop = sm->browse ? 1u : sm->rowpop ? 2u : 0u;   /* the popup up: the preset list, the parameter list */
    sq_pal = pu_page_pal(sm);                        /* a change redraws everything (page_draw, seq_draw: pal_drawn) */
    /* a MOD page's slot runs every control pass, so its tomb has an output to show even when nothing reads it */
    synth_mod_watch = sm->page >= UIPAGE_MOD1 && sm->page <= UIPAGE_MOD3 ? (int8_t)(sm->page - UIPAGE_MOD1) : (int8_t)-1;
    uint8_t held = (uint8_t)(sm->hold == BTN_HOME || sm->hold == BTN_SAVE);
    if (!held && last_hold != BTN_COUNT)
        force = 1u;                                  /* the hold strip gone: the page under it again */
    last_hold = held ? sm->hold : (uint8_t)BTN_COUNT;
    if (moved || sm->s.active_synth_layer != last_layer || dlg != last_dlg || pop != last_pop || sq_pal != last_pal) {
        if (moved)
            pu_top = 0;
        last_pal = sq_pal;
        last_page = sm->page;
        last_layer = sm->s.active_synth_layer;
        last_dlg = dlg;
        last_pop = pop;
        force = 1u;                                  /* (the list closing: everything under it again) */
    }
    if (pop) {                                       /* a popup: the whole screen, nothing else drawn under it */
        if (pop == 1u)
            pu_draw_browse(sm, force);
        else
            pu_draw_rowpop(sm, force);
        if (held)
            pu_draw_hold(sm);
        return;
    }
    if (sm->page == UIPAGE_SEQ)
        seq_draw(sm, q, force);
    else if (sm->page == UIPAGE_ARP)
        arp_draw(sm, q, a, force);
    else
        page_draw(sm, force);
    sq_draw_moon(force);                             /* the clock, top right on every page */
    if (held)
        pu_draw_hold(sm);
}
