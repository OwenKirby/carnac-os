/* Every page's renderer but SEQ and ARP, for the FM1's 240 x 240 panel, and the screen's one entry point:
 * mvave_fm1_master_specification.txt 3 (the knob frame on top, the encoder menu list in the middle, the visualizer at
 * the bottom, the synth palettes), 2.2 (the knobs), 4 (what each page's visualizer plots), 5.3 (HOME), 6.1 / 6.2 (SAVE,
 * the floating keyboard and the commit dialogs).
 *
 *   y   0..19   header: the layer and its architecture (or the mode, the slot) at the left; the section's pages at the
 *               right ("FLT1  FLT2  FLTMIX", this one lit and underlined); the moon (ui_seq_draw.c sq_draw_moon: the
 *               battery) at the right end
 *   y  22..78   knobs 1..4: each a stone circle (13 stones over 270 degrees, lit up to the value; bipolar values lit
 *               outwards from the top stone) with a pointer, its name above and its value below
 *   y  81..160  the menu list, 4 rows in the runes at 2x (2026-10-26, was 6 at 1x); the row under the Select Encoder cursor on the SEQ page's noisy ground, in white
 *   y 163..234  the page's visualizer (below); MOD1..4 have no menu, so theirs takes y 81..234
 *   y 235..239  the ground: a dotted horizon, with a small stone tracking a playhead on ENV, LFO and OSC pages
 *
 * Every lit bar is a weathered menhir (ui_seq_draw.c sq_stone: slanted, chipped, grained). The theme is the stone rows
 * of Carnac in the autumn and the Salisbury Plain at Halloween: standing stones for knobs,
 * voices, echoes and wavetable frames, a harvest moon that follows the LFO,
 * a sarsen circle that counts the bar. Everything is black or a dark shade of the palette's hue; lit stones and curves
 * are the only bright pixels, so the panel stays dim.
 *
 *   OSC1 / OSC2   one cycle of the wavetable frame at the live scan (the newest sounding voice's, else the knob's);
 *                 the bank's 8 frames as a row of stones, the scan's place between them on the ground
 *   OSCMIXER      two cycles of the mix: OSC1, OSC2 by the balance, the sub at its octave and wave, the noise at its
 *                 colour; SPREAD adds its detuned playheads as dim ghosts. STACK: two strata, A (OSC1, sub, noise) the
 *                 earth and B (OSC2 as the cross-mod leaves it) the sky in its mist, ley lines rising between them
 *   FILTER1 / 2   the response curve of the flavour's circuit (dsp/filter.h: the SH-2's 4-pole loop at its morph
 *                 LP .. BP .. HP, MULTI's at its mode, the MS-20 Sallen-Key at its morph LP .. BP .. HP,
 *                 FORMANT's two bands at their spread); what the morph does on the right; the cutoff as a
 *                 stone on the ground (FORMANT: a second for the upper band)
 *   FILTER MIXER  DUAL FILTER: both curves and their serial / parallel mix. STACK: two cairns of two stones, AMP (ENV1 with
 *                 ENV2 on its back) and FILTER (FILTER1's response with FILTER2's on top), each stone as thick as its
 *                 share of the blend, in the grass; ENV3>CUTOFF ghosts where the envelope lifts the filter cairn. BINAURAL: the left and right ears' curves, the phase offset as a dial
 *   ENV1..3       the ADSR as a barrow; the newest voice's stage and level as a stone on the ground
 *   LFO1..3       two cycles of the shape after its delay and fade; the playhead on the ground and a moon whose phase
 *                 is the LFO's
 *   MOD MATRIX    the routes as menu rows (SOURCE[*VIA]>DEST and AMOUNT), + NEW ROUTE last (2026-11-11)
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
#include "dsp/pcm_osc.h"                     /* a PCM wave's name and picture on the OSC / OSCMIXER pages */
#include "dsp/mfx.h"                         /* the MFX page: the effect's sweep as it moves (mfx_peek) */
#include "dsp/fx.h"                          /* the MIX page: the live mix (fx_live) and the loop's level */
#include "dsp/tuning.h"                      /* the TUNING page: each step's cents (tuning_key_cents) */
static void rec_draw(UIStateMachine *sm, uint32_t force);   /* the REC page (ui_rec_draw.c, at the end) */
/* ----------------------------------------------------------- colours --- */
#define PUC_STONE SQ_STONE[sq_pal]                 /* an unlit stone (ui_seq_draw.c) */
#define PUC_HL SQ_PAL_LANE[sq_pal][SQC_BG]          /* the noisy ground's base: white text sits on it */

/* ------------------------------------------------------------ layout --- */
#define PU_HEAD_H 20
#define PU_KNOB_Y 22
#define PU_KNOB_H 57
#define PU_KNOB_W 60                 /* 4 cells, 0 .. 239 */
#define PU_DIAL_CY 30                /* the stone circle's centre in its cell */
#define PU_DIAL_R 17                 /* (2026-10-26, was 14) its stones on rows 12 .. 45 */
#define PU_PTR_R 10
#define PU_VAL_Y 45                  /* the value's band: rows 45 .. 57 */
#define PU_MENU_Y 81
#define PU_ROW_H 20                  /* a row of runes at 2x (18 px) */
#define PU_MENU_ROWS 4u
#define PU_MENU_MAX 64u                 /* rows a menu page may have (MOD's, the longest, ~30) */
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
       PUR_NAMEKB, PUR_COUNT };
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
    {"WAVE SCAN", "SCAN"}, {"PITCH MOD", "PCH MOD"}, {"NOISE COLOUR", "COLOUR"}, {"NOISE LEVEL", "NOISE"}, {"LEVEL MOD", "LVL MOD"}, {"ENV2>CUTOFF", "ENV2>CUT"}, {"LFO2>PITCH", "LFO2>PCH"},
    {"ENV2>PITCH", "ENV2>PCH"}, {"CUT L/R MOD", "L/R MOD"},
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

/* (pu_rune_n, the rune face at 2x: ui_arp_draw.c, which the unity build includes first) */

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
    "OSC 1", "OSC 2", "OSC MIX", "FILTER 1", "FILTER 2", "FLT MIX", "ENV 1", "ENV 2", "ENV 3", "LFO 1", "LFO 2", "LFO 3",
    "MOD MATRIX", "AMP", "EFFECTS", "MIX", "HOME", "HOME", "HOME", "PERFORMANCE", "GLOBAL", "TUNING", "ARP", "SAVE",
    "SEQ", "REC",
};
/* the first page and size of each section (UIStateMachine.c SECTION_FIRST / SECTION_COUNT, by page) */
static void pu_section(uint8_t page, uint8_t *first, uint8_t *n)
{
    static const uint8_t F[] = {UIPAGE_OSC1, UIPAGE_FILTER1, UIPAGE_ENV1, UIPAGE_LFO1, UIPAGE_MOD, UIPAGE_EFFECTS,
                                UIPAGE_HOME1, UIPAGE_GLOBAL, UIPAGE_ARP, UIPAGE_SAVE_COMMIT, UIPAGE_SEQ,
                                UIPAGE_COUNT};   /* (GLOBAL and TUNING: HOME held + a key, their tabs a pair) */
    uint32_t i;
    for (i = 0; F[i + 1] != UIPAGE_COUNT && page >= F[i + 1]; i++) {}
    *first = F[i];
    *n = (uint8_t)(F[i + 1] - F[i]);
}

/* the pages whose parameters belong to a layer (the header names it) */
static uint8_t pu_layer_page(uint8_t page) { return (uint8_t)(page <= UIPAGE_AMP); }

/* AMP's MFX group (2026-11-13): the MFX's wind under the rows, its name in the header, its knobs */
static uint8_t pu_mfx_view(const UIStateMachine *sm) { return ui_sm_mfx_group(sm); }

/* the palette every page is drawn in (ui_seq_draw.c spec 3 note): GLOBAL > THEME's, whichever layer is in focus.
 * SAMHAIN is seq_palette_set's (ORANGE, or RED for the host's text checks) */
static uint8_t pu_page_pal(const UIStateMachine *sm)
{
    return sm->theme == UITHEME_SAMHAIN || sm->theme >= UITHEME_COUNT ? sq_pal_base : seq_theme_pal(sm->theme);
}

/* ------------------------------------------------------------ header --- */
/* each page's name in the header's list of its section's pages */
static const char *const PU_TAB[UIPAGE_COUNT] = {
    "OSC1", "OSC2", "OSCMIX", "FLT1", "FLT2", "FLTMIX", "ENV1", "ENV2", "ENV3", "LFO1", "LFO2", "LFO3",
    "MATRIX", "AMP", "FX", "MIX", "LAYER", "LAYER", "ZONES", "PERF", "GLOBAL", "TUNING", "ARP", "SAVE", "SEQ", "REC",
};
#define PU_TAB_GAP 8                 /* between two pages' names */
/* OSC2's place is the NOISE page while the layer has one oscillator (2026-11-06) */
static const char *pu_tab(const UIStateMachine *sm, uint8_t p)
{
    return ui_sm_noise_page(sm, p, sm->s.active_synth_layer) ? "NOISE" : PU_TAB[p];
}

/* the section's pages at the right, ending at xr ("OSC1  OSC2  OSCMIX"): this one lit and underlined, the others dim,
 * hidden ones left out. Returns where the list starts */
static int32_t pu_head_tabs(const UIStateMachine *sm, int32_t xr, int32_t y)
{
    uint8_t page = sm->page, first, n, i;
    int32_t x, w = 0, k;
    pu_section(page, &first, &n);
    for (i = 0; i < n; i++)
        if (ui_sm_page_visible(sm, (uint8_t)(first + i), sm->s.active_synth_layer))
            w += (w ? PU_TAB_GAP : 0) + sq_rune_w(pu_tab(sm, (uint8_t)(first + i)), 0);
    x = xr - w;
    xr = x;
    for (i = 0; i < n; i++) {
        uint8_t p = (uint8_t)(first + i);
        if (!ui_sm_page_visible(sm, p, sm->s.active_synth_layer))
            continue;
        k = sq_rune(x, y, pu_tab(sm, p), p == page ? SQC(TEXT) : SQC(DIM), 0);
        if (p == page)
            cv_rect(x, y + SQ_RUNE_H + 2, k - x, 1, SQC(THEME));
        x = k + 1 + PU_TAB_GAP;
    }
    return xr;
}

/* the header: what the page is about at the left (the layer and its architecture, the mode, the slot), its section's
 * pages at the right, the battery moon past them (sq_draw_moon) */
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
    if (pu_layer_page(page)) {                       /* "L1  DUAL OSC", in AMP's MFX group "L1  MFX  CHORUS" */
        x = sq_rune(6, y, layer ? "L2" : "L1", SQC(THEME), 0) + 7;
        if (pu_mfx_view(sm)) {
            x = sq_rune(x, y, "MFX", SQC(THEME), 0) + 7;
            sq_rune(x, y, ui_sm_mfx_name(ui_sm_mfx(sm, layer)->type), SQC(MID), 0);
        } else {
            sq_rune(x, y, ui_sm_arch_name(varch_of(sm->engine[layer])), SQC(MID), 0);
        }
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
    } else if (page == UIPAGE_MIX) {                 /* "DUAL  L1 / L2" or "SINGL  DRY / WET" */
        x = sq_rune(6, y, MODE[mode], SQC(THEME), 0) + 7;
        sq_rune(x, y, mode == PERF_SINGL_SYNTH ? "DRY / WET" : "L1 / L2", SQC(MID), 0);
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
    o->bip = (uint8_t)(info.kind == UIKNOB_BIP || info.kind == UIKNOB_TUNE);
    o->list = (uint8_t)(info.names != NULL && n > 0);
    o->text = o->none ? "" : o->list ? ui_sm_knob_text(sm, page, layer, k) : b;
    if (info.kind == UIKNOB_BPM) {                   /* INT TEMPO: 0 .. MIDI_CLOCK_TEMPO_MAX */
        if (v > (int32_t)MIDI_CLOCK_TEMPO_MAX) v = MIDI_CLOCK_TEMPO_MAX;
        o->pos = (v * 48 + MIDI_CLOCK_TEMPO_MAX / 2) / MIDI_CLOCK_TEMPO_MAX;
    } else if (info.kind == UIKNOB_TUNE) {          /* TUNE: -36 .. +36 semitones round the dial */
        if (v > 2 * (int32_t)OSC_COARSE_ZERO) v = 2 * (int32_t)OSC_COARSE_ZERO;
        o->pos = (v * 48 + (int32_t)OSC_COARSE_ZERO) / (2 * (int32_t)OSC_COARSE_ZERO);
    } else if (o->list) {
        if (v > n - 1) v = n - 1;
        o->pos = n > 1 ? (v * 48 + (n - 1) / 2) / (n - 1) : 0;
    } else {
        if (v > (int32_t)VAL_MAX) v = VAL_MAX;
        o->pos = (v * 48 + 63) / 127;
    }
    if (o->none || o->list)
        return;
    if (info.kind == UIKNOB_TUNE)
        ui_sm_tune_text(p, b);                                          /* TUNE: +7, -12 */
    else if (info.kind == UIKNOB_BPM)
        pu_num(b, (int32_t)MIDI_CLOCK_BPM_BASE + v, 0u, 1u);           /* INT TEMPO: 40 .. 255 */
    else if (info.kind == UIKNOB_NOTE)
        ui_sm_note_name((uint8_t)v, b);                                 /* SPLIT NOTE: C4 */
    else if (info.kind == UIKNOB_DTIME)
        ui_sm_dtime_text((uint8_t)v, b);                                /* DELAY TIME: 1/8T, 472MS */
    else if (info.kind == UIKNOB_PORTA && v == 0)
        b[0] = 'O', b[1] = 'F', b[2] = 'F', b[3] = 0;                     /* PORTA TIME 0: no glide */
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

static void pu_knob_sends(UIStateMachine *sm, uint8_t page, uint8_t k, int32_t *g0, int32_t *g1, int32_t *gl);

/* the stone circle: 13 stones every 1/16 turn from -3/8 to +3/8, lit to the value (from the top stone for a bipolar
 * value, the one picked for a list), the stone at the value bigger and brighter, a pointer from the hub */
static void pu_draw_knob(UIStateMachine *sm, uint8_t page, uint8_t k)
{
    char b[8];
    PuKnob o;
    int32_t i, a, p, cx = PU_KNOB_W / 2, cy = PU_DIAL_CY, sx, sy, lead, g0, g1, gl, ghost;
    uint16_t c;
    pu_knob_read(sm, page, k, &o, b);
    if (page == UIPAGE_PERF) {
        pu_draw_knob_tablet(sm, &o, k);
        return;
    }
    pu_knob_sends(sm, page, k, &g0, &g1, &gl);
    ghost = gl >= 0 ? (gl + 2) / 4 : -1;
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
        else if (g0 >= 0 && p >= g0 && p <= g1)
            c = SQC(ACCENT);                         /* where the routes can take it */
        else if (o.list)
            c = PUC_STONE;
        else if (o.bip)
            c = (p >= 24 && p <= o.pos) || (p <= 24 && p >= o.pos) ? SQC(THEME) : PUC_STONE;
        else
            c = p <= o.pos && o.pos > 0 ? SQC(THEME) : PUC_STONE;
        if (i == lead && !o.none)
            pu_menhir(sx - 1, sy + 3, 3, 5, c);
        else if (i == ghost)                         /* the routes' place now */
            pu_menhir_edge(sx - 2, sy + 3, 4, 6, SQC(HI));
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

/* The mod matrix on the other pages (2026-11-11). A knob whose parameter a route goes to: the stones its routes can
 * reach pale. g0 .. g1 the reach (dial positions 0..48), gl the place now (-1: not shown since the routes have no
 * cache to read); -1: no route */
static void pu_knob_sends(UIStateMachine *sm, uint8_t page, uint8_t k, int32_t *g0, int32_t *g1, int32_t *gl)
{
    UiKnobInfo info;
    uint8_t layer = sm->s.active_synth_layer, d = ui_sm_knob_dest(sm, page, layer, k), i;
    const uint8_t *p = ui_sm_knob(sm, page, layer, k, &info);
    const EngineState *e = sm->engine[layer];
    int32_t v, reach = 0;
    *g0 = *g1 = *gl = -1;
    if (d == MDST_NONE || p == NULL || info.names != NULL || !ui_sm_dest_live(sm, layer, d) ||
        ui_sm_dest_routes(e, d) == 0u)
        return;
    v = *p > VAL_MAX ? (int32_t)VAL_MAX : *p;
    for (i = 0u; i < NROUTE; i++)
        if (e->route[i].dest == d && e->route[i].src != MSRC_NONE) {
            int32_t a = ((int32_t)e->route[i].amount - (int32_t)BIPOLAR_CENTER) * 2;
            reach += a < 0 ? -a : a;
        }
    *g0 = ((v - reach < 0 ? 0 : v - reach) * 48 + 63) / 127;
    *g1 = ((v + reach > 127 ? 127 : v + reach) * 48 + 63) / 127;
}

/* the routes to a menu row's destination as standing stones before its value, one each (at most 6), left to right;
 * their right edge at xr, their feet at yb. The width */
static int32_t pu_send_stones(int32_t xr, int32_t yb, uint8_t n, uint16_t c, int draw)
{
    static const int8_t H[6] = {16, 19, 14, 18, 15, 19};
    int32_t i, w, x;
    if (n > 6u) n = 6u;
    w = n ? (int32_t)n * 8 - 2 : 0;
    if (!draw || w == 0)
        return w;
    for (i = 0, x = xr - w; i < (int32_t)n; i++, x += 8)
        sq_stone(x, yb, 6, H[i], H[i], c, (uint32_t)(i * 7 + 11), 0u, 0u);
    return w;
}

static uint32_t pu_knob_sig(UIStateMachine *sm, uint8_t page, uint8_t k, uint32_t base)
{
    int32_t g0, g1, gl;
    pu_knob_sends(sm, page, k, &g0, &g1, &gl);
    base = sq_mix(base, (uint32_t)(g0 & 255) | (uint32_t)(g1 & 255) << 8 | (uint32_t)(gl & 255) << 16);
    char b[8];
    PuKnob o;
    pu_knob_read(sm, page, k, &o, b);
    base = pu_mix_str(base, o.label);
    base = pu_mix_str(base, o.text);
    return sq_mix(base, (uint32_t)o.pos | (uint32_t)o.none << 8 | (uint32_t)o.bip << 9 | (uint32_t)o.list << 10);
}

/* -------------------------------------------------------------- menu --- */
/* the page's rows that are drawn (UIROW_HIDDEN ones take no line, 2026-10-30: GLOBAL ROOT left a gap); pu_top counts
 * these. *fi: the cursor's place among them (UI_FOCUS_NONE: not on one) */
static uint8_t pu_menu_vis(const UIStateMachine *sm, uint8_t *vis, uint8_t *fi)
{
    uint8_t n = ui_sm_row_count(sm), nv = 0, r;
    *fi = UI_FOCUS_NONE;
    for (r = 0; r < n && nv < PU_MENU_MAX; r++) {
        if (ui_sm_row_state(sm, r) == UIROW_HIDDEN)
            continue;
        if (r == sm->s.current_param_focus)
            *fi = nv;
        vis[nv++] = r;
    }
    return nv;
}

static void pu_menu_scroll(const UIStateMachine *sm)
{
    uint8_t vis[PU_MENU_MAX], f, n = pu_menu_vis(sm, vis, &f);
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

/* a MOD MATRIX route row (2026-11-12): the routes come sorted by SOURCE, each source's group under one small stone
 * with the source's name beside it (on the group's first row, and on the top row when the list has scrolled into a
 * group), a ley line down from the stone on the group's other rows; then the destination's full name and, after a
 * knot, the VIA small. + ROUTE (aimed: + its destination) across the row */
#define PU_MR_SX 4                   /* the stone */
#define PU_MR_SW 6
#define PU_MR_X 42                   /* the destination's name */
static void pu_mod_row(const UIStateMachine *sm, uint8_t r, uint8_t ri, uint8_t top, int32_t y, uint16_t lc,
                       int32_t vw)
{
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    const ModRoute *m = &e->route[ri];
    const char *l = ui_sm_row_label(sm, r), *s;
    uint8_t pr = r > 0u ? ui_sm_row_route(sm, (uint8_t)(r - 1u)) : (uint8_t)NROUTE, a;
    int32_t x, k;
    if (m->src == MSRC_NONE) {
        pu_rune_n(6, y + 1, l, lc, 2, (214 - vw) / 12, 0);
        return;
    }
    if (top || pr >= NROUTE || e->route[pr].src != m->src) {
        s = ui_sm_src_short(m->src);
        sq_stone(PU_MR_SX, y + PU_ROW_H - 2, PU_MR_SW, PU_ROW_H - 5, PU_ROW_H - 5, lc, (uint32_t)(m->src * 13 + 5), 0u, 0u);
        pu_rune_n(PU_MR_SX + PU_MR_SW + 3, y + 6, s, lc, 1, 4, 0);
    } else {
        for (k = y; k < y + PU_ROW_H; k += 2)
            cv_pset(PU_MR_SX + PU_MR_SW / 2, k, lc);
    }
    x = pu_rune_n(PU_MR_X, y + 1, l, lc, 2, 9, 0);
    if (m->via != MSRC_NONE && m->via < MSRC_COUNT) {
        for (a = 0u; a < 64u; a += 4u)
            cv_pset(pu_px(x + 7, 3, a), pu_py(y + 10, 3, a), lc);
        pu_rune_n(x + 12, y + 6, ui_sm_src_short(m->via), lc, 1, 4, 0);
    }
}

/* label left, value right; greyed rows dim; the cursor row on the noisy ground, in white; a scroll bar on the right
 * when the page has more rows than fit */
static void pu_draw_save_menu(const UIStateMachine *sm);
static int32_t pu_mfx_rows(const UIStateMachine *sm);   /* (the MFX page, below) */

static void pu_draw_menu(const UIStateMachine *sm)
{
    char b[8];
    uint8_t vis[PU_MENU_MAX], fi, n = pu_menu_vis(sm, vis, &fi), i, r, st, cur;
    int32_t y;
    uint16_t lc, vc;
    cv_begin(240, pu_mfx_view(sm) ? pu_mfx_rows(sm) * PU_ROW_H : PU_MENU_H, SQC(BG));   /* (MFX: the wind under) */
    if (sm->page == UIPAGE_SAVE_COMMIT) {            /* (its one row, the name, is drawn large) */
        pu_draw_save_menu(sm);
        cv_blit(0, PU_MENU_Y);
        return;
    }
    if (n == 0u) {                                   /* (every parameter is a knob) */
        sq_text_in(0, 30 + CAP_IN(S, 13), 240, &AF_S, "ALL ON THE KNOBS", SQC(DIM), SQC(BG));
    }
    for (i = 0; i < PU_MENU_ROWS && pu_top + i < n; i++) {
        r = vis[pu_top + i];
        st = ui_sm_row_state(sm, r);
        y = (int32_t)i * PU_ROW_H;
        cur = (uint8_t)(r == sm->s.current_param_focus);
        lc = st == UIROW_GREYED ? SQC(DIM) : SQC(MID);
        vc = st == UIROW_GREYED ? SQC(DIM) : SQC(TEXT);
        if (cur) {
            lc = vc = SQC(HI);
            sq_noise_block(2, y, 230, PU_ROW_H, 0u, PU_MENU_Y, 1u);
        }
        {   /* label and value at 2x across 6 .. 230, a letter apart: 18 letters in all, which every row's label and
             * longest value keep to (src/host/test_ui_rows_fit.c); past that the label is cut */
            const char *l = ui_sm_row_label(sm, r), *t = pu_row_text(sm, r, b);
            int32_t nt = pu_strlen(t), vw = PU_RUNE_N_W(nt, 2);
            uint8_t d = ui_sm_row_dest(sm, r), ri = ui_sm_row_route(sm, r);
            if (d != MDST_NONE && sm->page != UIPAGE_MOD)   /* the routes here: stones before the value */
                pu_send_stones(230 - vw - 8, y + PU_ROW_H, ui_sm_dest_routes(sm->engine[sm->s.active_synth_layer], d),
                               cur ? SQC(BG) : SQC(DIM), 1);
            if (ri < NROUTE)
                pu_mod_row(sm, r, ri, (uint8_t)(i == 0u), y, lc, vw);
            else
                pu_rune_n(6, y + 1, l, lc, 2, (214 - vw) / 12, 0);
            pu_rune_n(230 - vw, y + 1, t, vc, 2, nt, 0);
        }
    }
    if (n > PU_MENU_ROWS) {                          /* the scroll bar */
        int32_t h = PU_MENU_ROWS * PU_ROW_H, th = h * (int32_t)PU_MENU_ROWS / n, t0 = h * pu_top / n;
        cv_rect(237, 0, 1, h, PUC_STONE);
        cv_rect(236, t0, 3, th, SQC(MID));
    }
    cv_blit(0, PU_MENU_Y);
}

/* the clipboard's name, as the CLIP line shows it */
static const uint8_t *pu_clip_name(const UIStateMachine *sm)
{
    if (sm->clip_kind == UISAVE_PATCH_PRESET) return sm->clip.patch.name;
    if (sm->clip_kind == UISAVE_WHOLE_PERF) return sm->clip.whole.perf.name;
    return sm->clip_name;
}

static uint32_t pu_menu_sig(const UIStateMachine *sm, uint32_t h)
{
    char b[8];
    uint8_t vis[PU_MENU_MAX], fi, n = pu_menu_vis(sm, vis, &fi), i, r;
    h = sq_mix(h, (uint32_t)sm->page | (uint32_t)sm->s.current_param_focus << 8 | (uint32_t)pu_top << 16 | (uint32_t)n << 24);
    for (i = 0; i < PU_MENU_ROWS && pu_top + i < n; i++) {
        r = vis[pu_top + i];
        h = sq_mix(h, ui_sm_row_state(sm, r));
        h = pu_mix_str(h, ui_sm_row_label(sm, r));
        h = pu_mix_str(h, pu_row_text(sm, r, b));
        h = sq_mix(h, ui_sm_dest_routes(sm->engine[sm->s.active_synth_layer], ui_sm_row_dest(sm, r)));
    }
    if (sm->page == UIPAGE_SAVE_COMMIT) {            /* the summary: what the slot holds, the clipboard */
        char n[NAME_LEN + 1];
        h = sq_mix(h, (uint32_t)sm->save_slot | (uint32_t)sm->save_target << 8 | (uint32_t)ui_sm_slot_state(sm, n) << 16 |
                          (uint32_t)sm->clip_kind << 24);
        h = pu_mix_str(sq_mix(h, (uint32_t)sm->clip_layer | (uint32_t)sm->s.active_synth_layer << 8), n);
        if (sm->clip_kind < UISAVE_COUNT)
            h = sq_mix_bytes(h, pu_clip_name(sm),
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
    char nb[13];
    int stones = wave_stones(o->wavebank);
    sq_text(PV_X0, CAP_IN(S, 13), &AF_S, wave_name(o->wavebank, nb), SQC(MID));
    for (x = 0; x < PV_W; x++)                       /* a bank: one cycle (level 1); a PCM wave: its start */
        pu_ys[x] = wave_preview(o->wavebank, morph, (uint32_t)x * (0xFFFFFFFFu / PV_W));
    pu_trace(pu_ys, 0, 37, 20, SQC(THEME));
    /* the bank's frames (a PCM pair: its two loops) as a stone row, the one nearest the scan lit */
    near = stones > 1 ? (int32_t)((morph * (stones - 1) + PU_MORPH_MAX / 2) / PU_MORPH_MAX) : 0;
    for (k = 0; k < stones; k++) {
        sx = stones > 1 ? PV_X0 + 4 + k * (PV_W - 12) / (stones - 1) : PV_X0 + PV_W / 2;
        pu_menhir(sx - 3, PV_GROUND, 6, k == near ? 10 : 7, k == near ? SQC(THEME) : PUC_STONE);
    }
}

/* the resonance's ratio (1..8) as it sounds: the layer's newest voice's, or the knob's with no voice */
static float pu_noise_ratio(const UIStateMachine *sm)
{
    int32_t v = pu_voice(sm->s.active_synth_layer);
    if (v >= 0 && poly_bank[v].mix.wh_ratio > 0.f)
        return poly_bank[v].mix.wh_ratio;
    return osc_mixer_harm_ratio((float)sm->engine[sm->s.active_synth_layer]->noise_harmonic);
}

static uint32_t pn_t;                                /* the mist's clock: a step a frame while the layer sounds */

/* NOISE (2026-11-06, the user's pick of three previz): the resonance as a standing wave of mist over the eight
 * overtone stones, in a drifting fog. The fog is the noise (fine and sparse for white, coarser banks toward brown,
 * thinning as the resonance takes over); past brown the wave condenses out of it, a loose cloud at RING, a crisp
 * line at AIR, with as many crests as the harmonic and a height that breathes with the voice's own noise band. The
 * stone it stands on is lit. Integer hashes and the 64-step sine only: no sinf, no table. */
static void pu_viz_noise(const UIStateMachine *sm)
{
    uint8_t layer = sm->s.active_synth_layer;
    const EngineState *e = sm->engine[layer];
    int32_t c = e->oscmix.noise_colour, lv = e->oscmix.noise_level, v = pu_voice(layer), x, y, k, near;
    int32_t res = c > 32 ? (c - 32) * 256 / 95 : 0;            /* the resonance, 0..256 */
    int32_t coarse = c < 16 ? 1 : 2, drift = (int32_t)(pn_t / 2u), amp = 13 * 256, spread;
    uint32_t dens = (uint32_t)(lv + 20) * (uint32_t)(256 - res / 2) / 148u;   /* the fog: level, thinned by the song */
    float r = pu_noise_ratio(sm);
    static const char *const NAME[5] = {"WHITE", "PINK", "BROWN", "RING", "AIR"};
    sq_text(PV_X0, CAP_IN(S, 13), &AF_S, NAME[c < 16 ? 0 : c < 32 ? 1 : c < 56 ? 2 : c < 80 ? 3 : 4], SQC(MID));
    for (x = 0; x < PV_W; x++) {                    /* the fog: banks 12 x 6 drifting right, thick near the ground */
        int32_t edge = x < 40 ? 90 + x * 4 : x > PV_W - 41 ? 90 + (PV_W - 1 - x) * 4 : 256;
        for (y = 16; y < PV_GROUND - 1; y++) {
            uint32_t bank = pu_hash((x + drift) / 12, y / 6) >> 24;                    /* 0..255 */
            int32_t low = (y - 16) * (y - 16) * 256 / ((PV_GROUND - 17) * (PV_GROUND - 17));
            uint32_t pr = (bank > 50u ? bank - 50u : 0u) * (uint32_t)(60 + low) / 256u * (uint32_t)edge / 256u;
            uint32_t g = pu_hash((x / coarse) * 7 + (int32_t)pn_t * 13, y / coarse) >> 24;
            if (g < pr * dens / 256u)
                cv_pset(PV_X0 + x, y, g * 5u < pr * dens / 256u ? SQC(DIM) : PUC_STONE);
        }
    }
    if (res > 0) {                                   /* the standing wave of mist */
        if (v >= 0) {                                /* its height: the voice's band, as it wavers */
            int32_t n = poly_bank[v].mix.nz_cur[0] >> 4;
            n = n < 0 ? -n : n;
            amp = (13 * 256 * (192 + (n > 9500 ? 9500 : n) * 64 / 9500)) / 256;
        }
        spread = 256 + (256 - res) * 9;              /* Q8 px: ~10 at brown, 1 at AIR */
        for (x = 0; x < PV_W; x++) {
            uint32_t a = (uint32_t)((float)x * r * (65536.f / (float)PV_W)) - pn_t * 600u;   /* cycles, Q16 */
            int32_t sw = PU_SIN[(a >> 10) & 63] * PU_SIN[(x * 32 / PV_W) & 63];                /* x the arch */
            int32_t wy = 30 * 256 - (int32_t)(((int64_t)sw * amp) >> 16), y0 = (wy >> 8) - 30, y1 = (wy >> 8) + 30;
            for (y = y0 < 16 ? 16 : y0; y < (y1 > PV_GROUND - 2 ? PV_GROUND - 2 : y1); y++) {
                int32_t d = (y * 256 - wy) * 256 / spread, dd = (int32_t)(((int64_t)d * d) >> 8);   /* (d / spread)^2, Q8 */
                int32_t pr = dd < 256 ? 256 - dd / 2 : dd < 1024 ? (1024 - dd) * (300 - res) / 1800 : 0;   /* (the halo thins as it rings) */
                uint32_t g = pu_hash(x * 3 + (int32_t)pn_t * 7, y) >> 24;
                if ((int32_t)g < pr * (90 + res * 166 / 256) / 256)
                    cv_pset(PV_X0 + x, y, dd < 64 ? (res > 150 ? SQC(THEME) : SQC(MID)) : dd < 330 ? SQC(MID) : SQC(DIM));
            }
        }
    }
    near = (int32_t)(r + 0.5f) - 1;
    for (k = 0; k < 8; k++) {
        x = PV_X0 + 4 + k * (PV_W - 12) / 7;
        pu_menhir(x - 3, PV_GROUND, 6, k == near ? 10 : 7, k == near && res > 0 ? SQC(THEME) : PUC_STONE);
    }
}

/* the scan's place between the frames, on the ground */
static int32_t pu_track_osc(const UIStateMachine *sm, uint8_t osc)
{
    int32_t voice, m = pu_osc_morph(sm, osc, &voice);
    return PV_X0 + 4 + (int32_t)(((int64_t)m * (PV_W - 12)) / PU_MORPH_MAX);
}

/* -------------------------------------------------------------- mixer --- */
/* STACK: two voices, one over the other. A, the earth: OSC1 with the sub and the noise, the ground under it in fog. B,
 * the sky: OSC2 as the cross-mod leaves it (FM bends its pitch with A, AM swells it, RING folds it, PM pushes it, as
 * dsp/oscmixer.c osc_mixer_osc2 does), at OSC2's octave from OSC1. Between them ley lines rise from A into B, closer
 * together as CROSS-MOD grows. Each trace's height is its share of the FILTER MIXER's BLEND. */
#define PU_STK_X0 (PV_X0 + 12)                       /* (the letters A and B at the left) */
#define PU_STK_W (PV_W - 12)
#define PU_STK_YA 50
#define PU_STK_YB 17

static void pu_viz_stack(const UIStateMachine *sm)
{
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    const OscMixerParams *m = &e->oscmix;
    int32_t voice, morph1 = pu_osc_morph(sm, 0, &voice), morph2 = pu_osc_morph(sm, 1, &voice);
    int32_t ncyc = m->sub_octave == SUBOCT_DOWN2 ? 4 : 2, amt = m->xmod_amt, depth = amt * 258, col = m->noise_colour;
    int32_t oct = (osc_coarse_semis(e->osc[1].coarse) - osc_coarse_semis(e->osc[0].coarse)) / 12;   /* (whole octaves) */
    int32_t x, pa = 4096, pb = 4096, brown = 0, blend = e->fltmix.balance, ha, hb, gap, y;
    uint8_t wave1 = e->osc[0].wavebank, wave2 = e->osc[1].wavebank;
    uint8_t type = m->xmod_type < XMOD_COUNT ? m->xmod_type : (uint8_t)XMOD_FM;
    uint32_t seed = 0x1234567u, sstep = 0xFFFFFFFFu / PU_STK_W, step = sstep * (uint32_t)ncyc, step2, ph2 = 0;
    if (oct > 2) oct = 2;
    if (oct < -2) oct = -2;
    step2 = oct >= 0 ? step << oct : step >> -oct;
    for (x = 0; x < PU_STK_W; x++) {
        uint32_t ph = (uint32_t)x * step, sph = (uint32_t)x * sstep;
        int32_t a1 = wave_preview(wave1, morph1, ph), sub, n, c, s;
        if (m->sub_waveform == SUBWAVE_SQUARE) sub = sph < 0x80000000u ? 24000 : -24000;
        else if (m->sub_waveform == SUBWAVE_CURRENT) sub = wave_preview(wave1, morph1, sph);
        else sub = PU_SIN[sph >> 26] * 128;
        seed = seed * 1664525u + 1013904223u;        /* the noise: white, browned by the colour (a leaky sum) */
        n = (int32_t)(seed >> 17) - 16384;
        brown += (n - brown / 8) / 4;
        s = 127 * a1 + m->sub_level * sub + m->noise_level * ((n * (127 - col) + (brown / 2) * col) / 127);
        pu_ys[x] = s / 127;
        switch (type) {
        case XMOD_AM:
            c = wave_preview(wave2, morph2, ph2);
            c = (c * (32767 - (((32767 - ((a1 + 32768) >> 1)) * depth) >> 15))) >> 15;
            break;
        case XMOD_RING:
            c = wave_preview(wave2, morph2, ph2);
            c += ((((c * a1) >> 15) - c) * depth) >> 15;
            break;
        case XMOD_PM:
            c = wave_preview(wave2, morph2, ph2 + (uint32_t)((int64_t)a1 * amt * 258));
            break;
        default:                                     /* FM: A speeds B up and slows it down (up to ~4x at full) */
            c = wave_preview(wave2, morph2, ph2);
            ph2 += (uint32_t)(((int64_t)step2 * a1 * amt) >> 20);
            break;
        }
        ph2 += step2;
        pu_ys2[x] = c;
        if ((s = pu_ys[x] < 0 ? -pu_ys[x] : pu_ys[x]) > pa) pa = s;
        if ((c = c < 0 ? -c : c) > pb) pb = c;
    }
    ha = 4 + ((127 - blend) * 10) / 127;             /* the blend: A's share, B's */
    hb = 4 + (blend * 10) / 127;
    for (x = 0; x < PU_STK_W; x++) {
        pu_ys[x] = PU_STK_YA - (pu_ys[x] * ha) / pa;
        pu_ys2[x] = PU_STK_YB - (pu_ys2[x] * hb) / pb;
    }
    if (amt) {                                       /* the ley lines, A into B */
        gap = 5 + ((127 - amt) * 22) / 127;
        for (x = gap / 2; x < PU_STK_W; x += gap)
            for (y = pu_ys2[x] + 3; y < pu_ys[x] - 2; y += 2)
                cv_pset(PU_STK_X0 + x, y, amt > 95 ? SQC(DIM) : PUC_STONE);
    }
    for (x = 0; x < PU_STK_W; x++) {                 /* A, the earth */
        pu_fog_col(PU_STK_X0 + x, pu_ys[x] + 2, PV_GROUND, 7u, PUC_STONE, PU_VIZ_Y);
        if (x) cv_line(PU_STK_X0 + x - 1, pu_ys[x - 1], PU_STK_X0 + x, pu_ys[x], SQC(THEME));
    }
    for (x = 0; x < PU_STK_W; x++) {                 /* B, the sky: a thinner mist above it */
        pu_fog_col(PU_STK_X0 + x, 0, pu_ys2[x] - 1, 15u, PUC_STONE, PU_VIZ_Y);
        if (x) cv_line(PU_STK_X0 + x - 1, pu_ys2[x - 1], PU_STK_X0 + x, pu_ys2[x], SQC(ACCENT));
    }
    sq_text(PV_X0, PU_STK_YA - 7 + CAP_IN(S, 13), &AF_S, "A", SQC(MID));
    sq_text(PV_X0, PU_STK_YB - 7 + CAP_IN(S, 13), &AF_S, "B", SQC(MID));
}

/* the mix over one cycle of the sub: two cycles of the oscillators for SUB -1 OCT, four for -2 */
static void pu_viz_oscmix(const UIStateMachine *sm)
{
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    const OscMixerParams *m = &e->oscmix;
    uint8_t arch = varch_of(e), spread = (uint8_t)(arch == VARCH_SPREAD && m->spread);
    int32_t voice, morph1 = pu_osc_morph(sm, 0, &voice), morph2 = pu_osc_morph(sm, 1, &voice);
    int32_t w1 = 127, w2 = 0, ws = m->sub_level, wn = m->noise_level, x, brown = 0, col = m->noise_colour;
    int32_t bc = col < (int32_t)NOISE_BROWN ? col * 2 : 127, wh = col > (int32_t)NOISE_BROWN ? (col - 64) * 2 : 0;
    int32_t ncyc = m->sub_octave == SUBOCT_DOWN2 ? 4 : 2;
    uint8_t wave1 = e->osc[0].wavebank, wave2 = e->osc[1].wavebank;
    uint32_t seed = 0x1234567u, sstep = 0xFFFFFFFFu / PV_W, step = sstep * (uint32_t)ncyc;
    if (arch == VARCH_STACK) { pu_viz_stack(sm); return; }
    if (arch == VARCH_DUAL_OSC || arch == VARCH_FULL) { w2 = m->balance; w1 = 127 - w2; }
    for (x = 0; x < PV_W; x++) {
        uint32_t ph = (uint32_t)x * step, sph = (uint32_t)x * sstep;
        int32_t s = w1 * wave_preview(wave1, morph1, ph), sub, n;
        if (w2) s += w2 * wave_preview(wave2, morph2, ph);
        if (m->sub_waveform == SUBWAVE_SQUARE) sub = sph < 0x80000000u ? 24000 : -24000;
        else if (m->sub_waveform == SUBWAVE_CURRENT) sub = wave_preview(wave1, morph1, sph);
        else sub = PU_SIN[sph >> 26] * 128;
        s += ws * sub;
        seed = seed * 1664525u + 1013904223u;        /* the noise: white, browned by the colour (a leaky sum), */
        n = (int32_t)(seed >> 17) - 16384;           /* past brown ringing OSC1's sine (the whistles) */
        brown += (n - brown / 8) / 4;
        n = (n * (127 - bc) + (brown / 2) * bc) / 127;
        if (wh) n = n * ((127 - wh) * 256 + wh * PU_SIN[ph >> 26]) / (127 * 256);
        s += wn * n;
        pu_ys[x] = s / 127;
        if (spread)                                  /* the detuned playheads: OSC1 stretched, dim behind */
            pu_ys2[x] = w1 * wave_preview(wave1, morph1, ph + (uint32_t)m->spread * (uint32_t)x * (step >> 9)) / 127;
    }
    pu_trace(pu_ys, spread ? pu_ys2 : 0, 36, 24, SQC(THEME));
}

/* ------------------------------------------------------------- attack --- */
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
/* STACK, two layers played as one: A (OSC1 -> FILTER1, ENV1) and B (OSC2 -> FILTER2, ENV2), each a stone in two cairns
 * standing in the grass. AMP at the left: A's stone shaped by ENV1 over time, B's by ENV2 resting on A's back, on one
 * time scale. FILTER at the right: A's by FILTER1's response, B's by FILTER2's on top of it, low to high. Each stone's
 * thickness is its share of the BLEND (so the top line is the pair's sum); ENV3>CUTOFF ghosts the filter cairn's top
 * where the envelope takes both cutoffs. */
#define PU_CRN_W 106                                 /* a cairn's width; AMP at x 6, FILTER at x 128 */
#define PU_CRN_BASE 69                               /* the stones' foot (the turf over it) */

static int32_t pu_env_w(uint8_t t) { return 4 + ((int32_t)t * t * 56) / 16129; }   /* 4 .. 60 px a stage */

/* ENV p's level (Q8, 0 .. 256) t units into the note: the attack easing up, the decay and release falling as a curve,
 * the sustain held 24 units (unit widths as the ENV page's, pu_env_w) */
static int32_t pu_env_at(const EnvParams *p, int32_t t)
{
    int32_t wa = pu_env_w(p->attack), wd = pu_env_w(p->decay), wr = pu_env_w(p->release), s = adsr_sustain_q15(p->sustain) >> 7, u;
    if (t < wa) { u = 256 - (t * 256) / wa; return 256 - (u * u >> 8); }
    t -= wa;
    if (t < wd) { u = 256 - (t * 256) / wd; return s + (((256 - s) * (u * u >> 8)) >> 8); }
    t -= wd;
    if (t < 24) return s;
    t -= 24;
    if (t < wr) { u = 256 - (t * 256) / wr; return (s * (u * u >> 8)) >> 8; }
    return 0;
}

static int32_t pu_env_len(const EnvParams *p) { return pu_env_w(p->attack) + pu_env_w(p->decay) + 24 + pu_env_w(p->release); }

/* a filter's response at column x of a cairn (cutoff c), Q8 0 .. 256: -36 dB at 0, +12 dB (a resonant peak) at 256 */
static int32_t pu_flt_at(const FilterParams *f, int32_t x, int32_t c)
{
    int32_t v = (((pu_filter_l2(f, (x * 127 * 256) / (PU_CRN_W - 1), c) * 771) >> 8) + 36 * 256) / 48;
    return v < 0 ? 0 : v > 256 ? 256 : v;
}

/* one cairn at x0: la[x], lb[x] (Q8) the layers' curves, ha, hb their full heights; A solid and grained, B outlined in
 * its own colour with a speckled face, a dark seam between them */
static void pu_cairn(int32_t x0, const int32_t *la, const int32_t *lb, int32_t ha, int32_t hb)
{
    int32_t x, y, ta, tb, ptb = 0;
    uint16_t ca = SQC(THEME), cb = SQC(ACCENT);
    for (x = 0; x < PU_CRN_W; x++) {
        int32_t chip = (int32_t)(pu_hash(x0 + x, 41) >> 30) == 0;   /* a weathered nick in the top here and there */
        ta = PU_CRN_BASE - ((la[x] * ha) >> 8);
        tb = ta - 1 - ((lb[x] * hb) >> 8);
        for (y = ta + chip; y < PU_CRN_BASE; y++)
            cv_pset(x0 + x, y, sq_shade(ca, (pu_hash(x0 + x, y + 3) >> 29) == 0u ? 2u : (pu_hash(x0 + x, y) >> 30) == 0u));
        if (tb < ta - 1) {
            for (y = tb + 1; y < ta - 1; y++)
                if (pu_speck(x0 + x, y + PU_VIZ_Y, 3u)) cv_pset(x0 + x, y, sq_shade(cb, 2u));
            if (x) cv_line(x0 + x - 1, ptb, x0 + x, tb, cb);
            else cv_pset(x0, tb, cb);
            cv_pset(x0 + x, ta - 2 < tb ? tb : ta - 2, sq_shade(cb, 1u));      /* B's underside on A's back */
        }
        if ((x == 0 || x == PU_CRN_W - 1) && tb < ta - 1)                     /* B's ends */
            cv_line(x0 + x, tb, x0 + x, ta - 2, cb);
        ptb = tb;
    }
}

static void pu_viz_stack_pair(const EngineState *e)
{
    const FilterMixerParams *fm = &e->fltmix;
    int32_t b = fm->balance, ev = (int32_t)fm->env3_cutoff - BIPOLAR_CENTER, x, t, T, ha, hb, c1, c2, g;
    int32_t ta = pu_env_len(&e->env[0]), tb = pu_env_len(&e->env[1]);
    c1 = e->filter[0].cutoff > VAL_MAX ? (int32_t)VAL_MAX : e->filter[0].cutoff;
    c2 = e->filter[1].cutoff > VAL_MAX ? (int32_t)VAL_MAX : e->filter[1].cutoff;
    ha = 8 + ((127 - b) * 38) / 127;                 /* the blend: A's share, B's (at most 54 together) */
    hb = 8 + (b * 38) / 127;
    sq_text(PV_X0, CAP_IN(S, 13), &AF_S, "AMP", SQC(MID));
    sq_text(128, CAP_IN(S, 13), &AF_S, "FILTER", SQC(MID));

    /* AMP: both envelopes on one clock, the longer filling the cairn */
    T = ta > tb ? ta : tb;
    for (x = 0; x < PU_CRN_W; x++) {
        t = (x * T) / (PU_CRN_W - 1);
        pu_ys[x] = pu_env_at(&e->env[0], t);
        pu_ys2[x] = pu_env_at(&e->env[1], t);
    }
    pu_cairn(PV_X0, pu_ys, pu_ys2, ha, hb);

    /* FILTER */
    for (x = 0; x < PU_CRN_W; x++) {
        pu_ys[x] = pu_flt_at(&e->filter[0], x, c1);
        pu_ys2[x] = pu_flt_at(&e->filter[1], x, c2);
    }
    pu_cairn(128, pu_ys, pu_ys2, ha, hb);
    if (ev) {                                        /* ENV3>CUTOFF: where the envelope's peak takes the pair's top */
        int32_t s1 = c1 + ev, s2 = c2 + ev;
        s1 = s1 < 0 ? 0 : s1 > 127 ? 127 : s1;
        s2 = s2 < 0 ? 0 : s2 > 127 ? 127 : s2;
        for (x = 0; x < PU_CRN_W; x += 2) {
            g = PU_CRN_BASE - ((pu_flt_at(&e->filter[0], x, s1) * ha) >> 8) - 1 - ((pu_flt_at(&e->filter[1], x, s2) * hb) >> 8);
            if (g > 12) cv_pset(128 + x, g - 1, SQC(MID));
        }
    }
    /* the grass along both feet */
    sq_grass(PV_X0 - 2, PV_X0 + PU_CRN_W + 2, PU_VIZ_H, 5, SQC(MID), 3, 0, 0, 0, 0, 0);
    sq_grass(126, 128 + PU_CRN_W + 2, PU_VIZ_H, 5, SQC(MID), 9, 0, 0, 0, 0, 0);
}

static void pu_viz_fltmix(const UIStateMachine *sm)
{
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    const FilterMixerParams *fm = &e->fltmix;
    uint8_t arch = varch_of(e);
    int32_t x, c1 = e->filter[0].cutoff, c2 = e->filter[1].cutoff;
    if (c1 > (int32_t)VAL_MAX) c1 = VAL_MAX;
    if (c2 > (int32_t)VAL_MAX) c2 = VAL_MAX;
    if (arch == VARCH_STACK) {
        pu_viz_stack_pair(e);
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

/* the barrow: attack rising fast then easing, decay and release falling as a curve, the sustain level held; an
 * inverted ENV2 / ENV3 drawn upside down, as the mod destinations read it (full at rest, dipping with the note) */
static void pu_viz_env(const UIStateMachine *sm, uint8_t i)
{
    const EnvParams *p = &sm->engine[sm->s.active_synth_layer]->env[i];
    int32_t xs[5], x, u, w, lv, s = adsr_sustain_q15(p->sustain);
    uint8_t inv = (uint8_t)(i > 0u && p->invert);
    pu_env_xs(p, xs);
    for (x = 0; x < PV_W; x++) {
        int32_t sx = PV_X0 + x;
        if (sx < xs[0] || sx >= xs[4]) { pu_ys[x] = pu_env_y(inv ? 32767 : 0); continue; }
        if (sx < xs[1]) { w = xs[1] - xs[0]; u = ((sx - xs[0]) << 15) / w; lv = 32767 - (int32_t)(((int64_t)(32767 - u) * (32767 - u)) >> 15); }
        else if (sx < xs[2]) { w = xs[2] - xs[1]; u = 32767 - ((sx - xs[1]) << 15) / w; lv = s + (int32_t)(((int64_t)(32767 - s) * ((int64_t)u * u >> 15)) >> 15); }
        else if (sx < xs[3]) lv = s;
        else { w = xs[4] - xs[3]; u = 32767 - ((sx - xs[3]) << 15) / w; lv = (int32_t)(((int64_t)s * ((int64_t)u * u >> 15)) >> 15); }
        pu_ys[x] = pu_env_y(inv ? 32767 - lv : lv);
    }
    pu_curve(pu_ys, SQC(THEME), 1u);
    for (x = 1; x < 4; x++)                          /* the stage boundaries, faint, under the curve only */
        if (xs[x] >= PV_X0 && xs[x] < PV_X0 + PV_W)
            pu_fog_col(xs[x], pu_ys[xs[x] - PV_X0] + 2, PV_GROUND, 3u, PUC_STONE, PU_VIZ_Y);
    if (inv)
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
    s = adsr_sustain_q15(p->sustain);
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

/* -------------------------------------------------------------- MFX --- */
/* The wind through the stones (2026-11-01): the MFX page's menu holds TYPE and the type's own rows (1 .. 4), and the
 * plain takes everything under them. Nine standing stones; streamlines of faint dashes drift right through them; a
 * gust (where the effect is in its sweep, mfx_peek) runs the dashes long and bright and churning, and lights the
 * stones it passes. The gusts' path is a worn track on the ground. STONE PHASER: one gust (MANUAL +- DEPTH / 2), a
 * second in ACCENT with the sides apart (L/R PHASE); FEEDBACK narrows it and adds streamlines, and above ~60 eddies
 * curl in it (its whistle); DEPTH 0 stills the wind. ENSEMBLE: three gusts, its three lines (further right as the
 * delay grows: AGE moves them all on), one colour for the mono sum, THEME / MID / ACCENT as WIDTH spreads them;
 * SHIMMER sets the dashes fluttering; AGE raises dust drifting over the plain. CHORUS: two gusts, its two sides'
 * delays (THEME, ACCENT), swinging against each other as WIDTH has them; AGE's dust. ENS+PHASER: the ensemble's three
 * lines as faint gusts, the phaser's sweep the one lit (THEME), eddies in it with COLOUR. TAPE FLANGER: the second
 * deck's gust (a side each with L/R PHASE) sweeping through a faint fixed one, the reference, where they meet; TAPE's
 * dust. OFF: the stones in still air. */
#define PW_MAXH (PU_TRACK_Y - PU_MENU_Y - PU_ROW_H)  /* the plain under one row: 134 */
static const uint8_t PW_SX[9] = {10, 34, 57, 83, 108, 133, 160, 185, 211};   /* the stones: x (of PV_W), height */
static const uint8_t PW_SH[9] = {44, 56, 38, 61, 47, 35, 53, 42, 31};         /* (at a plain of 114) */
static uint32_t pw_t;                                /* the wind's clock: a step a frame while an effect runs */
static int32_t pw_g;                                 /* the ground's row in the plain drawn now */

/* the menu rows over the MFX's wind (AMP's MFX group): as many as there are, up to the list's four */
static int32_t pu_mfx_rows(const UIStateMachine *sm)
{
    uint8_t vis[PU_MENU_MAX], f, n = pu_menu_vis(sm, vis, &f);
    return n < 1u ? 1 : n > PU_MENU_ROWS ? (int32_t)PU_MENU_ROWS : (int32_t)n;
}

/* a bell 0..256 at d px from the gust, s px wide */
static int32_t pw_bell(int32_t d, int32_t s)
{
    int32_t q;
    if (d < 0) d = -d;
    if (d >= s) return 0;
    q = 256 - (d * d * 256) / (s * s);
    return (q * q) >> 8;
}

/* the strongest of the ng gusts at x: its bell (a faint gust's at a third), and which (*gi) */
static int32_t pw_gusts(int32_t x, const int32_t *gx, const uint8_t *faint, int32_t ng, int32_t s, int32_t *gi)
{
    int32_t g, b, best = 0;
    *gi = 0;
    for (g = 0; g < ng; g++) {
        b = pw_bell(x - gx[g], s);
        if (faint[g]) b /= 3;
        if (b > best) { best = b; *gi = g; }
    }
    return best;
}

/* one streamline at row y0: faint dashes drifting right; where a gust passes they run long and bright, faster, and
 * churn; flutter (0..254) shakes them quickly there (ENSEMBLE's SHIMMER) */
static void pw_stream(int32_t y0, const int32_t *gx, const uint16_t *gc, const uint8_t *faint, int32_t ng, int32_t s,
                      int32_t drift, int32_t i, uint8_t still, int32_t flutter)
{
    int32_t x, b, gi, y, per = 13, len, ph;
    for (x = PV_X0; x < PV_X0 + PV_W; x++) {
        b = pw_gusts(x, gx, faint, ng, s, &gi);
        y = y0 + ((PU_SIN[((x * 2) / 5 + i * 11 + (still ? 0 : drift / 2)) & 63] * (1 + (5 * b) / 256)) >> 8);
        if (flutter)
            y += (PU_SIN[(x * 3 + drift * 5 + i * 17) & 63] * ((flutter * b) >> 8) * 3) >> 16;
        len = still ? (b > 50 ? per : 3) : 2 + (b * 9) / 256;
        ph = (x - (still ? 0 : drift * (256 + 2 * b) / 256) + i * 7) % per;
        if (ph < 0) ph += per;
        if (ph >= len || y < 1 || y >= pw_g)
            continue;
        cv_pset(x, y, b > 140 ? gc[gi] : b > 40 ? SQC(DIM) : PUC_STONE);
    }
}

/* an eddy: the feedback's whistle, a curl turning in the gust */
static void pw_eddy(int32_t cx, int32_t cy, int32_t r, int32_t rot, uint16_t c)
{
    int32_t a, rr, x, y;
    for (a = 0; a < 56; a++) {                       /* most of a turn, closing in */
        rr = r - (a * r) / 84;
        x = pu_px(cx, rr, a + rot);
        y = pu_py(cy, rr, a + rot);
        if (y >= 1 && y < pw_g) cv_pset(x, y, a > 44 ? SQC(DIM) : c);
    }
}

/* a share of the sweep (0..1) as x on the plain */
static int32_t pw_x(float u)
{
    u = u < 0.f ? 0.f : u > 1.f ? 1.f : u;
    return PV_X0 + 10 + (int32_t)(u * (float)(PV_W - 20));
}

/* a brigade's delay (mfx_peek's share of its reach) on the plain, from 0.15 (about the shortest the lines reach) */
static int32_t pw_bbd_x(float u) { return pw_x((u - 0.15f) * (1.f / 0.85f)); }

static void pu_viz_mfx(const UIStateMachine *sm)
{
    uint8_t layer = sm->s.active_synth_layer;
    const MfxParams *m = ui_sm_mfx(sm, layer);
    int32_t oy = PU_MENU_Y + pu_mfx_rows(sm) * PU_ROW_H, ph = PU_TRACK_Y - oy;
    int32_t gx[4], ng = 0, s = 30, nw, flutter = 0, drift = (int32_t)(pw_t * 3u), i, x, k, t0 = -1, t1 = -1, eddy = 0;
    uint16_t gc[4];
    uint8_t faint[4] = {0, 0, 0, 0}, still = 1u, t = m->type, dust = 0;
    pw_g = ph - 10;
    nw = 3 + (6 * pw_g) / 104;
    cv_begin(240, ph, SQC(BG));
    for (x = PV_X0; x < PV_X0 + PV_W; x++)           /* the earth */
        pu_fog_col(x, pw_g, ph, 7u, PUC_STONE, oy);
    if (t == MFX_STONE_PHASER) {
        int32_t fb = m->knob[MFX_K_FEEDBACK], c = m->knob[MFX_K_MANUAL], d = m->knob[MFX_K_DEPTH];
        s = 50 - (30 * fb) / 127;
        nw += (5 * fb) / 127 - 2;
        still = (uint8_t)(d == 0);
        for (i = 0; i < (m->lr > 0u && !still ? 2 : 1); i++) {
            float u = mfx_peek(layer, i);
            gx[ng] = pw_x(u < 0.f ? (float)c / 127.f : u);
            gc[ng++] = i ? SQC(ACCENT) : SQC(THEME);
        }
        t0 = pw_x(((float)c - (float)d / 2.f) / 127.f);
        t1 = pw_x(((float)c + (float)d / 2.f) / 127.f);
        eddy = fb;
    } else if (t == MFX_ENSEMBLE || t == MFX_ENS_PHASER) {
        float lo = mfx_peek(layer, 3), hi = mfx_peek(layer, 4);
        uint8_t ens = (uint8_t)(t == MFX_ENSEMBLE);
        still = (uint8_t)(m->knob[ens ? MFX_K_DEPTH : MFX_K_ENS_DEPTH] == 0u);
        flutter = still ? 0 : ens ? (int32_t)m->knob[MFX_K_SHIMMER] * 2 : 90;
        dust = ens ? m->knob[MFX_K_AGE] : m->row[MFX_R_AGE];
        for (i = 0; i < 3; i++) {
            float u = mfx_peek(layer, i);
            gx[ng] = pw_bbd_x(u < 0.f ? lo : u);
            faint[ng] = (uint8_t)!ens;               /* (ENS+PHASER: the phaser's gust is the lit one) */
            gc[ng++] = m->lr < 20u ? SQC(THEME) : i == 0 ? SQC(THEME) : i == 1 ? SQC(MID) : SQC(ACCENT);
        }
        t0 = pw_bbd_x(lo);
        t1 = pw_bbd_x(hi);
        if (!ens) {                                  /* the phaser: its sweep about the middle, its track */
            float u = mfx_peek(layer, 5), d = (float)m->knob[MFX_K_PH_DEPTH] / 127.f;
            gx[ng] = pw_x(u < 0.f ? 0.5f : u);
            gc[ng++] = SQC(THEME);
            still = (uint8_t)(still && d == 0.f);
            t0 = pw_x(0.5f - d / 2.f);
            t1 = pw_x(0.5f + d / 2.f);
            eddy = m->row[MFX_R_COLOUR];
        }
    } else if (t == MFX_CHORUS) {
        float lo = mfx_peek(layer, 3), hi = mfx_peek(layer, 4);
        still = (uint8_t)(m->knob[MFX_K_CH_DEPTH] == 0u);
        dust = m->row[MFX_R_AGE];
        for (i = 0; i < 2; i++) {
            float u = mfx_peek(layer, i);
            gx[ng] = pw_bbd_x(u < 0.f ? (lo + hi) * 0.5f : u);
            gc[ng++] = i ? SQC(ACCENT) : SQC(THEME);
        }
        t0 = pw_bbd_x(lo);
        t1 = pw_bbd_x(hi);
    } else if (t == MFX_BBD_FLANGER) {               /* a gust a side at its delay; FEEDBACK's eddies */
        still = (uint8_t)(m->knob[MFX_K_BF_DEPTH] == 0u);
        for (i = 0; i < 2; i++) {
            float u = mfx_peek(layer, i);
            gx[ng] = pw_x(u < 0.f ? (float)m->knob[MFX_K_BF_CENTER] / 127.f : u);   /* (CENTER's scale) */
            gc[ng++] = i ? SQC(ACCENT) : SQC(THEME);
        }
        s = 46 - (24 * (int32_t)m->knob[MFX_K_BF_FEEDBACK]) / 127;
        eddy = m->knob[MFX_K_BF_FEEDBACK];
    } else if (t == MFX_BBD_ECHO) {                  /* a gust a tap at its echo's distance (the clock's swoop moves
                                                      * them); FEEDBACK's eddies; DUCK: faint while held down */
        float g = mfx_peek(layer, 2);
        for (i = 0; i < (m->lr > 0u ? 2 : 1); i++) {
            float u = mfx_peek(layer, i);
            gx[ng] = pw_x(u < 0.f ? (float)m->knob[MFX_K_QE_TIME] / 254.f : u);
            faint[ng] = (uint8_t)(g >= 0.f && g < 0.5f);
            gc[ng++] = i ? SQC(ACCENT) : SQC(THEME);
        }
        still = 0u;
        flutter = m->knob[MFX_K_QE_ENV] > 64u ? (int32_t)(m->knob[MFX_K_QE_ENV] - 64u) * 3
                                               : (int32_t)(64u - m->knob[MFX_K_QE_ENV]) * 3;
        s = 46 - (24 * (int32_t)m->knob[MFX_K_QE_FEEDBACK]) / 127;
        eddy = m->knob[MFX_K_QE_FEEDBACK];
        dust = m->row[MFX_R_AGE2];
    } else if (t == MFX_PITCH_SHIFT) {               /* the gust stands at the pitch (left lower, right higher) */
        float u = mfx_peek(layer, 0);
        gx[ng] = pw_x(u < 0.f ? 0.5f : u);
        gc[ng++] = SQC(THEME);
        still = 0u;
        flutter = (int32_t)m->knob[MFX_K_PS_FEEDBACK] * 2;
        dust = m->row[MFX_R_AGE];
        t0 = pw_x(0.5f);
        t1 = t0;
    } else if (t == MFX_TAPE_FLANGER) {
        float lo = mfx_peek(layer, 3), hi = mfx_peek(layer, 4), ref = mfx_peek(layer, 2);
        still = (uint8_t)(m->knob[MFX_K_TF_DEPTH] == 0u);
        dust = m->knob[MFX_K_TF_TAPE];
        for (i = 0; i < (m->lr > 0u && !still ? 2 : 1); i++) {      /* the second deck, a side each */
            float u = mfx_peek(layer, i);
            gx[ng] = pw_x(u < 0.f ? (lo + hi) * 0.5f : u);
            gc[ng++] = i ? SQC(ACCENT) : SQC(THEME);
        }
        gx[ng] = pw_x(ref < 0.f ? 0.26f : ref);                     /* the reference deck: where they meet */
        faint[ng] = 1u;
        gc[ng++] = SQC(MID);
        t0 = pw_x(lo);
        t1 = pw_x(hi);
    } else if (t == MFX_METAL) {                     /* a gale: the gust stands at MID FREQ (faint with MIDDLE cut),
                                                      * DIST its turbulence, more wind, the eddies */
        gx[ng] = pw_x((float)m->knob[MFX_K_MT_FREQ] / 127.f);
        faint[ng] = (uint8_t)(m->knob[MFX_K_MT_MIDDLE] < 48u);
        gc[ng++] = SQC(THEME);
        still = 0u;
        flutter = (int32_t)m->knob[MFX_K_MT_DIST] * 3;
        nw += (4 * (int32_t)m->knob[MFX_K_MT_DIST]) / 127;
        eddy = m->knob[MFX_K_MT_DIST];
    }
    if (t0 >= 0) {                                  /* the gusts' path: a worn track on the ground */
        for (x = t0; x <= t1; x++)
            if ((x & 1) == 0) cv_pset(x, pw_g + 2, SQC(DIM));
        cv_rect(t0, pw_g + 1, 1, 3, SQC(MID));
        cv_rect(t1, pw_g + 1, 1, 3, SQC(MID));
    }
    for (k = 0; k < (int32_t)dust / 3; k++) {        /* the dust of its years, drifting over the plain */
        uint32_t h = (uint32_t)(k + 1) * 2654435761u;
        int32_t dx = (int32_t)((h >> 8) % (uint32_t)PV_W), sp = 1 + (int32_t)((h >> 20) & 3u);
        int32_t dy = 4 + (int32_t)((h >> 12) % (uint32_t)(pw_g - 10));
        x = PV_X0 + (dx + (int32_t)(pw_t * (uint32_t)sp) / 2) % PV_W;
        dy += (PU_SIN[(x / 4 + k * 9) & 63] * 2) >> 8;
        cv_pset(x, dy, (h >> 30) == 0u ? SQC(DIM) : PUC_STONE);
    }
    for (i = 0; i < nw; i++)                         /* the wind, behind the stones */
        pw_stream(8 + (i * (pw_g - 12)) / nw, gx, gc, faint, ng, s, drift, i, still, flutter);
    for (k = 0; k < 9; k++) {                        /* the stones: a gust lights the ones it passes */
        int32_t cx = PV_X0 + PW_SX[k] + 4, gi, b = pw_gusts(cx, gx, faint, ng, s, &gi), sh = PW_SH[k] * pw_g / 104;
        uint16_t c = b > 140 ? gc[gi] : b > 40 ? SQC(DIM) : PUC_STONE;
        sq_stone(cx - 5, pw_g, 10, sh, sh, c, (uint32_t)(k * 13 + 5), 0u, 0u);
    }
    if (eddy > 60 && !still) {                       /* the whistle: eddies in the lit gusts */
        int32_t r = 3 + (eddy - 60) / 10, n = eddy > 100 && pw_g > 70 ? 2 : 1;
        for (i = 0; i < ng; i++)
            if (!faint[i])
                for (k = 0; k < n; k++)
                    pw_eddy(gx[i] + (k ? -r - 3 : r), 16 + (i & 1) * 18 + k * 30, r - k * 2,
                            (i & 1 ? -1 : 1) * drift + k * 23, gc[i]);
    }
    cv_blit(0, oy);
}

/* the wind's inputs: the settings, the rows, and while an effect runs its clock and where its gusts are */
static uint32_t pu_mfx_sig(UIStateMachine *sm, uint32_t h)
{
    uint8_t layer = sm->s.active_synth_layer, i;
    const MfxParams *m = ui_sm_mfx(sm, layer);
    h = sq_mix_bytes(h, (const uint8_t *)m, sizeof *m);
    h = sq_mix(h, (uint32_t)pu_mfx_rows(sm));
    if (m->type == MFX_OFF)
        return h;
    pw_t++;
    h = sq_mix(h, pw_t);
    for (i = 0; i < 6u; i++)
        h = sq_mix(h, (uint32_t)(int32_t)(mfx_peek(layer, i) * 256.f));
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
    /* the reverb's fog, behind the stones: as long as the decay, lower as it is damped */
    xr = PV_X0 + 8;
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
    sq_text(PV_X0 + 12, CAP_IN(S, 13), &AF_S, DELAY_TYPE(p) == DELAYTYPE_BBD ? "BBD" : "TAPE", SQC(MID));
    if (DELAY_SYNC(p))
        sq_text(PV_X0 + 46, CAP_IN(S, 13), &AF_S, "SYNC", SQC(MID));
}

/* --------------------------------------------------------------- mix --- */
/* The MIX page (2026-11-02, dsp/fx.h fx_live; ley lines 2026-11-03): at the left the balance's two stones (L1, L2;
 * SINGL: DRY, WET), each as tall as its level (synth_runtime.c mix_targets' law), dark when silent. At the right the
 * delay's and the reverb's stones and the loop between them, a ring of dashes: the upper arc DLY > REV, the lower
 * REV > DLY, each as dense as its amount, running while the loop carries sound; lit in ACCENT once the return reaches
 * its headroom (fx_cross_level: the loop holding itself up). LOOP TONE squashes the ring: DARK sinks it flat, THIN
 * stretches it tall. From each layer's stone a ley line to each effect (SINGL: from DRY, the sends being before it),
 * its dashes as long as that send (the AMP knob and the MOD SENDS, after the balance's fader), the energy running
 * along it toward the effect while the layer sounds, dormant dots while it is silent; the reverb's arc over the
 * delay's. The two effect stones shimmer with what reaches them: glints climbing their faces, motes rising off them,
 * an aura round them when strong, dying away as the delay's feedback and the reverb's decay let the tails die; at
 * rest a faint shimmer at an eighth of the pace. */
static uint32_t pm_t, pm_clk;                        /* frames; the picture's clock (an eighth of the pace at rest) */
static float pm_act[2], pm_glow[2];                  /* each layer's loudest voice 0..1; what reaches DLY, REV 0..1 */
static uint8_t pm_send[2][2];                        /* [layer][DLY, REV]: the send after the balance, 0..127 */

static float pu_mix_gain(uint8_t k, int side)        /* side 0: the first (L1 / DRY), 1: the second */
{
    float lo = k < 64u ? (float)k / 64.f : 1.f, hi = k > 64u ? (float)(127u - (k > 127u ? 127u : k)) / 63.f : 1.f;
    return side ? lo * lo : hi * hi;
}

/* the sends, the layers' loudness and the effects' glow, once a frame (from pu_mix_sig) */
static void pu_mix_update(const UIStateMachine *sm)
{
    const MasterPerformancePreset *p = sm->perf;
    const uint8_t single = (uint8_t)(p->perf_mode == PERF_SINGL_SYNTH), k = fx_live[FX_L_BALANCE];
    const float keep[2] = {0.80f + 0.18f * (float)p->delay_feedback / 127.f,
                           0.86f + 0.13f * (float)p->reverb_decay / 127.f};
    const float x = fx_cross_level() * 0.5f;          /* the loop feeds both */
    int32_t v, l, e;
    pm_act[0] = pm_act[1] = 0.f;
    for (v = 0; v < POLYPHONY_VOICES; v++) {
        float a = (float)pu_voice_level(&poly_bank[v]) * (1.f / 32767.f);
        l = poly_bank[v].layer ? 1 : 0;
        if (a > pm_act[l])
            pm_act[l] = a;
    }
    for (e = 0; e < 2; e++) {
        float in = x;
        for (l = 0; l < 2; l++) {
            const AmpParams *a = &sm->engine[l]->amp;
            float g = single ? (l ? 0.f : 1.f) : pu_mix_gain(k, l);
            int32_t s = (int32_t)(e ? a->reverb_send : a->delay_send) + synth_fx_send_mod[l][e];
            s = s < 0 ? 0 : s > 127 ? 127 : s;
            pm_send[l][e] = (uint8_t)((float)s * g + 0.5f);
            in += (float)pm_send[l][e] * (1.f / 127.f) * pm_act[l];
        }
        if (single)                                   /* WET: what comes back of it */
            in *= pu_mix_gain(k, 1);
        pm_glow[e] *= keep[e];
        if (in > pm_glow[e])
            pm_glow[e] = in > 1.f ? 1.f : in;
    }
}

/* a ley line from (x0, y0) rightward to (x1, y1), bowed up by lift at its middle; str the send 0..1, en the energy
 * running along it (the send while its layer sounds), c its light */
static void pu_ley(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t lift, float str, float en, uint16_t c)
{
    const int32_t n = x1 - x0, per = 12, len = 2 + (int32_t)(str * 7.f);
    const int32_t run = (int32_t)((pm_clk * (uint32_t)(1 + (int32_t)(en * 3.f))) % (uint32_t)per);
    int32_t i, x, y, ph;
    if (str < 0.01f || n <= 0)
        return;
    if (lift < 0)
        lift = 0;
    for (i = 0; i <= n; i++) {
        x = x0 + i;
        y = y0 + (y1 - y0) * i / n - lift * 4 * i * (n - i) / (n * n);
        if (en < 0.02f) {                             /* dormant */
            if (i % 5 == 0)
                cv_pset(x, y, PUC_STONE);
            continue;
        }
        ph = (i - run + per) % per;                   /* the dashes run toward the effect */
        if (ph >= len)
            continue;
        cv_pset(x, y, ph == len - 1 ? (en > 0.5f ? SQC(TEXT) : c) : ph == 0 ? SQC(DIM) : en > 0.2f ? c : SQC(MID));
        if (str > 0.6f && ph > 0)
            cv_pset(x, y + 1, SQC(DIM));
    }
}

/* an effect's stone w x h at x, shimmering with g 0..1 (pm_glow) */
static void pu_ley_stone(int32_t x, int32_t w, int32_t h, float g, int32_t seed)
{
    const int32_t yb = PV_GROUND, top = PV_GROUND - h;
    const uint32_t t = pm_clk;
    int32_t m, n, gx, gy, up, rise;
    uint32_t r;
    if (g > 0.3f) {                                   /* the aura, breathing */
        int32_t a = h + 3 + (int32_t)((t >> 3) & 1u);
        sq_stone(x - 2, yb, w + 4, a, a, g > 0.7f ? SQC(THEME) : SQC(DIM), pu_seed(x, yb), 1u, 0u);
    }
    pu_menhir(x, yb, w, h, g > 0.05f ? SQC(MID) : SQC(DIM));
    n = 3 + (int32_t)(g * (float)(h - h / 4 - 5));    /* the vein: the energy in it, as high as g, bands climbing */
    for (m = 0; m < n; m++) {
        uint32_t b = ((uint32_t)m - t * 2u) % 8u;
        cv_rect(x + w / 2 - 1, yb - 2 - m, 2, 1,
                m == n - 1 || b < 2u ? SQC(TEXT) : b < 5u ? SQC(ACCENT) : g > 0.05f ? SQC(THEME) : SQC(MID));
    }
    n = 1 + (int32_t)(g * 10.f);                      /* glints climbing the face */
    for (m = 0; m < n; m++) {
        r = pu_hash(m, seed);
        gx = x + w / 2 - 2 + (int32_t)(r & 3u);
        gy = yb - 2 - (int32_t)((t * (1u + ((r >> 8) & 1u)) + (r >> 12)) % (uint32_t)(h - h / 4 - 2));
        cv_pset(gx, gy, m & 1 ? SQC(ACCENT) : SQC(TEXT));
        if (g > 0.4f)
            cv_pset(gx, gy + 1, SQC(MID));
    }
    n = (int32_t)(g * 12.f);                          /* motes rising off it, drifting */
    rise = 6 + (int32_t)(g * 18.f);
    for (m = 0; m < n; m++) {
        r = pu_hash(m + 40, seed);
        up = (int32_t)((t * (1u + ((r >> 9) & 1u)) + (r >> 13)) % (uint32_t)rise);
        gx = x - 3 + (int32_t)(r % (uint32_t)(w + 6)) + ((PU_SIN[(t + (r >> 20)) & 63u] * 2) >> 8);
        gy = top + 2 - up;
        if (gy > 0)
            cv_pset(gx, gy, up < rise / 3 ? SQC(TEXT) : up < 2 * rise / 3 ? SQC(THEME) : SQC(DIM));
    }
}

static void pu_viz_mix(const UIStateMachine *sm)
{
    static const int32_t SX[2] = {PV_X0 + 12, PV_X0 + 50};
    const uint8_t single = (uint8_t)(sm->perf->perf_mode == PERF_SINGL_SYNTH), k = fx_live[FX_L_BALANCE];
    const int32_t tone = (int32_t)fx_live[FX_L_TONE] - 64;
    const int32_t cx = PV_X0 + 160, cy = 44, rx = 38, ry = 15 + tone * 4 / 63;
    const int32_t ex[2] = {cx - rx - 12, cx + rx + 2}, ew = 10, eh = 40;   /* the delay, the reverb */
    const float lvl = fx_cross_level();
    int32_t s, a, e, top[2];
    pu_ground(PV_GROUND, PUC_STONE);
    for (a = 0; a < 256; a++) {                       /* the loop: upper arc DLY > REV (left to right), lower back;
                                                       * a quarter step at a time between PU_SIN's 64 */
        int32_t i = a >> 2, f = a & 3, up = i >= 48 || i < 16, j = up ? FX_X_DLY_REV : FX_X_REV_DLY;
        uint32_t amt = fx_cross[j];
        int32_t sx = (PU_SIN[i & 63] * (4 - f) + PU_SIN[(i + 1) & 63] * f) >> 2;
        int32_t sy = (PU_SIN[(i + 16) & 63] * (4 - f) + PU_SIN[(i + 17) & 63] * f) >> 2;
        int32_t x = cx + ((rx * sx + 128) >> 8), y = cy - ((ry * sy + 128) >> 8);
        int32_t pos = up ? (a + 64) & 255 : 256 - ((a - 64) & 255);   /* along the way it runs */
        uint32_t per = 16u, len = amt == 0u ? 1u : 2u + amt * 10u / 127u;
        if ((((uint32_t)pos + (amt && lvl > 0.01f ? pm_clk * 2u : 0u)) % per) >= len)
            continue;
        cv_pset(x, y, amt == 0u ? PUC_STONE : lvl > 0.9f ? SQC(ACCENT) : lvl > 0.25f ? SQC(THEME) : SQC(MID));
        if (amt > 80u)
            cv_pset(x, y + 1, lvl > 0.9f ? SQC(ACCENT) : SQC(MID));
    }
    for (s = 0; s < 2; s++)                           /* the balance's stones' tops */
        top[s] = PV_GROUND - 7 - (int32_t)(pu_mix_gain(k, s) * 46.f);
    for (s = 0; s < (single ? 1 : 2); s++)            /* the ley lines, behind the stones; the reverb's over the delay's */
        for (e = 1; e >= 0; e--) {
            /* the reverb's to its top, peaking clear of the names; the delay's to its flank, half way up */
            int32_t x0 = SX[s] + 8, x1 = e ? ex[1] + ew / 2 : ex[0] - 1, y1 = PV_GROUND - (e ? eh + 1 : eh / 2);
            int32_t mid = (top[s] + y1) / 2, lift = e ? (mid - 16 < 20 ? mid - 16 : 20) : (mid - 22 < 10 ? mid - 22 : 10);
            float str = (float)pm_send[s][e] * (1.f / 127.f);
            pu_ley(x0, top[s], x1, y1, lift, str, str * pm_act[s], s ? SQC(ACCENT) : SQC(THEME));
        }
    for (s = 0; s < 2; s++) {                         /* the balance */
        float g = pu_mix_gain(k, s);
        int32_t h = 6 + (int32_t)(g * 46.f);
        pu_menhir(SX[s], PV_GROUND, 16, h, g < 0.01f ? SQC(DIM) : g > 0.5f ? SQC(THEME) : SQC(MID));
        sq_text(SX[s] + 1, CAP_IN(S, 13), &AF_S, single ? (s ? "WET" : "DRY") : (s ? "L2" : "L1"), SQC(MID));
    }
    for (e = 0; e < 2; e++)
        pu_ley_stone(ex[e], ew, eh, pm_glow[e], 17 + e * 5);
    sq_text(ex[0] - 4, CAP_IN(S, 13), &AF_S, "DLY", SQC(MID));
    sq_text_r(ex[1] + 12, CAP_IN(S, 13), &AF_S, "REV", SQC(MID), SQC(BG));
}

/* the MIX page's inputs: its bytes, the mode, the sends; while anything sounds or glows a new picture each frame, at
 * rest one each eighth (the shimmer) */
static uint32_t pu_mix_sig(UIStateMachine *sm, uint32_t h)
{
    int alive;
    pu_mix_update(sm);
    alive = pm_act[0] + pm_act[1] + pm_glow[0] + pm_glow[1] > 0.02f || fx_cross_level() > 0.01f;
    pm_t++;
    if (alive || (pm_t & 7u) == 0u)
        pm_clk++;
    h = sq_mix_bytes(h, fx_live, FX_L_COUNT);
    h = sq_mix(h, sm->perf->perf_mode);
    h = sq_mix_bytes(h, &pm_send[0][0], 4u);
    return sq_mix(h, pm_clk);
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

/* ------------------------------------------------------------- tuning --- */
/* TUNING (2026-11-13): the octave as a rising alignment, 13 stones from ROOT to its octave. Each step's place in equal
 * temperament is a socket on the ground (17 px a semitone); its stone stands off it by the tuning's cents at the same
 * scale, the pull a line on the ground, so EQUAL stands even and a just or a maqam tuning leans visibly out of line.
 * ROOT and its octave lit, the steps sounding now in the accent, BEND RANGE a span over the row from ROOT, A4 in Hz
 * from MASTER TUNE above it */
#define PT_X0 16                     /* the ROOT stone's socket */
#define PT_STEP 17                   /* px a semitone */

/* the pitch classes sounding now, a bit each from ROOT (bit 12: the octave, with bit 0) */
static uint32_t pt_sounding(uint8_t root)
{
    uint32_t m = 0u;
    int32_t v;
    for (v = 0; v < POLYPHONY_VOICES; v++) {
        const SynthVoiceNode *n = &poly_bank[v];
        if (n->silent || pu_voice_level(n) <= 0 || n->active_note < 0)
            continue;
        m |= 1u << (((n->active_note - (int32_t)root) % 12 + 12) % 12);
    }
    return (m & 1u) ? m | 1u << 12 : m;
}

static void pu_viz_tuning(const UIStateMachine *sm)
{
    char b[16], *e;
    const MasterPerformancePreset *p = sm->perf;
    uint8_t t = PERF_TUNING(p), root = (uint8_t)(PERF_TUNING_ROOT(p) % 12u);
    int32_t mt = (int32_t)ui_sm_instrument(sm)->master_tune - 64, hz10, i, k, bend = p->bend_range;
    uint32_t snd = pt_sounding(root);
    if (t == TUNING_EQUAL || t >= TUNING_USER1)      /* (no ROOT: the table starts from C) */
        root = 0u;
    hz10 = 4400 + (mt * 25415) / 10000 + (mt * mt * 7339) / 10000000;   /* 440 * 2^(cents / 1200), to 0.1 Hz */
    e = pu_cat(b, "A4 ");
    e = pu_num(e, hz10 / 10, 0u, 1u);
    *e++ = '.';
    e = pu_num(e, hz10 % 10, 0u, 1u);
    pu_cat(e, " HZ");
    sq_text(PV_X0, 2 + CAP_IN(S, 13), &AF_S, b, mt ? SQC(THEME) : SQC(MID));
    for (i = PV_X0; i < PV_X0 + PV_W; i++)           /* the earth */
        pu_fog_col(i, PV_GROUND, PU_VIZ_H, 7u, PUC_STONE, PU_VIZ_Y);
    if (bend > 0) {                                  /* BEND RANGE: the span a full bend reaches from ROOT */
        int32_t n = bend > 12 ? 12 : bend, x1 = PT_X0 + n * PT_STEP, y = 22;
        for (i = PT_X0; i <= x1; i += 3)
            cv_pset(i, y, SQC(DIM));
        cv_rect(PT_X0, y - 2, 1, 5, SQC(MID));
        cv_rect(x1, y - 2, 1, 5, SQC(MID));
        if (bend > 12) {                             /* (past the octave: an arrow on) */
            cv_pset(x1 + 2, y - 1, SQC(MID));
            cv_pset(x1 + 3, y, SQC(MID));
            cv_pset(x1 + 2, y + 1, SQC(MID));
        }
    }
    for (i = 0; i <= 12; i++) {
        int32_t c = tuning_key_cents(t, root, (uint8_t)(60 + root + i)), sx = PT_X0 + i * PT_STEP;
        int32_t x = sx + (c * PT_STEP + (c < 0 ? -50 : 50)) / 100, h = 16 + i * 2;
        uint8_t lit = (uint8_t)(i == 0 || i == 12), off = (uint8_t)(c > 2 || c < -2);
        uint16_t col = (snd >> i) & 1u ? SQC(ACCENT) : lit ? SQC(THEME) : off ? SQC(MID) : PUC_STONE;
        cv_rect(sx, PV_GROUND + 2, 1, 2, SQC(DIM));  /* the socket: equal temperament's place */
        if (x != sx)                                 /* the pull, along the ground */
            for (k = x < sx ? x : sx; k <= (x < sx ? sx : x); k++)
                cv_pset(k, PV_GROUND + 1, off ? SQC(MID) : SQC(DIM));
        sq_stone(x - 4, PV_GROUND, 9, h, h, col, (uint32_t)(i * 7 + 3), 0u, 0u);
    }
}

/* the TUNING picture's live inputs: MASTER TUNE (the instrument's), the cents (a USER table can arrive over MIDI), the
 * steps sounding */
static uint32_t pu_tuning_sig(const UIStateMachine *sm, uint32_t h)
{
    uint8_t t = PERF_TUNING(sm->perf), root = (uint8_t)(PERF_TUNING_ROOT(sm->perf) % 12u), i;
    h = sq_mix(h, ui_sm_instrument(sm)->master_tune);
    for (i = 0; i <= 12u; i++)
        h = sq_mix(h, (uint32_t)tuning_key_cents(t, root, (uint8_t)(60 + root + i)));
    return sq_mix(h, pt_sounding(root));
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
 * of HOME and AMP (sq_stone) in the knob tablets' rock (PU_ROCK, a tone up from their face): solid where the live part
 * holds something (saved or not), an outline in the rock's brightest tone where it is still the factory slot, rubble for
 * layer 2 under SINGL */
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
            sq_stone(x - w / 2, yb, w, ph, ph, PU_ROCK[sq_pal][4], 31u + k * 7u, 1u, 0u);
        } else {
            sq_stone(x - w / 2, yb, w, ph, ph, PU_ROCK[sq_pal][3], 31u + k * 7u, 0u, 0u);
        }
        pu_num(pu_cat(b, LBL[k]), v[k], 0u, 3u);
        sq_text_in(x - 28, yb + 2 + CAP_IN(S, 14), 56, &AF_S, b, off ? SQC(DIM) : SQC(TEXT), SQC(BG));
    }
    cap = 5 + total * 6 / (4 * PU_FILL_MAX);         /* 5 .. 11 px: the performance's whole */
    sq_stone(22, yb - ph, 196, cap, cap, PU_ROCK[sq_pal][total ? 3 : 4], 97u, (uint32_t)(total == 0), 0u);
}

/* 6.2.3: what the commit writes and where, what that slot holds now (an overwrite in the accent), and the
 * clipboard. (The name keyboard: pu_draw_namekb, full screen.) */

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
    else
        pu_cat(pu_cat(e, "  "), pu_name(sm->clip_name, n));
}

/* The save page's menu (2026-10-17): what SAVE stores, large: the name (a sequence's and an arp's too since
 * 2026-10-25); then where it goes and what that slot holds (an overwrite in the accent), the clipboard, and the hold
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

/* 6.2.2 / 2026-10-17: the bottom of the save page, the bank on the plain: all its slots left to right (128 patches,
 * 32 of the rest), a short mark for each used one; the chosen slot a
 * standing stone (an outline in the theme when empty, solid in the accent when a save would overwrite it) with its
 * number at its foot, and right above it on the arch the moon, full over an empty slot, a crescent over a used one. */
#define PU_SLOT_X(s, n) (10 + (int32_t)(s) * 220 / ((int32_t)(n) - 1))   /* slot 0 .. n - 1 along the plain */

static void pu_viz_commit(const UIStateMachine *sm)
{
    char n[NAME_LEN + 1], b[4];
    int32_t x;
    {
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
/* The LEDGER (2026-11-13, the user's option A): both list popups are a dense column at 1x, a small stone at each row's
 * head, the row under the cursor on a lit slab (pu_block_lit) at 2x, so most of the list shows at a glance.
 * While the Preset Encoder turns: the stored presets of the page's kind (ui_sm_preset_target), eight rows at 2x
 * (the user asked for the names twice as big) around the one under the cursor (ui_sm_browse_slot: loading or playing; it stays in the middle until the list runs out),
 * each its slot and its name, all alike. The slot under the cursor shows even when the store
 * has nothing in it (UNSAVED: the live patch). A row of dots on the right with a stone riding it: where the cursor is
 * among them all. Drawn whole on each move (the list only changes when the cursor does). */
static const char *const PU_BR_TITLE[UISAVE_COUNT] = {"ARP PATTERN", "SEQUENCE", "PATCH", "PERFORMANCE"};
#define PU_BR_ROWS 8u                 /* the preset list's rows, at 2x (2026-11-13: 13 at 1x before) */
#define PU_BR_PITCH 22              /* a preset row at 2x */
#define PU_LG_Y 30                   /* the ledger's first row */
#define PU_LG_PITCH 13               /* a row at 1x */
#define PU_LG_SLAB 26                /* the pick's slab */

/* the ledger's scroll: dots down the right, a stone riding them at the cursor's place (at of total) */
static void pu_lg_scroll(int32_t at, int32_t total, int32_t oy)
{
    int32_t y;
    for (y = PU_LG_Y; y < 226; y += 3)
        cv_pset(228, y - oy, PUC_STONE);
    y = PU_LG_Y + 12 + (226 - PU_LG_Y - 14) * at / (total > 1 ? total - 1 : 1);
    sq_stone(227, y - oy, 3, 12, 12, SQC(DIM), 5u, 0u, 0u);
}

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
        y = PU_LG_Y;
        for (i = 0; i < (uint8_t)n; i++) {
            on = (uint8_t)(row[i] == cur);
            st = ui_sm_slot_query(sm, tgt, row[i], nm);
            if (st == UISLOT_EMPTY)
                nmt = "UNSAVED";
            else if ((tgt == UISAVE_SEQ_SETTINGS || tgt == UISAVE_ARP_SEQ) && (st != UISLOT_USED || !nm[0]))
                nmt = tgt == UISAVE_SEQ_SETTINGS ? "SEQUENCE" : "PATTERN";   /* (unnamed: what it is) */
            else
                nmt = st != UISLOT_USED ? "-" : nm[0] ? nm : "UNTITLED";
            pu_num(nb, row[i], 0u, 3u);
            if (on) {                                /* the pick: on a lit slab (a name past 13 letters at 1x, whole) */
                pu_block_lit(10, y - oy, 214, PU_LG_SLAB, 0, oy, 901u);
                pu_rune_n(24, y + 4, nb, SQC(HI), 2, 3, oy);
                if (pu_strlen(nmt) <= 13)
                    pu_rune_n(66, y + 4, nmt, st == UISLOT_EMPTY ? SQC(MID) : SQC(HI), 2, 13, oy);
                else
                    pu_rune_n(66, y + 9, nmt, SQC(HI), 1, NAME_LEN, oy);
                y += PU_LG_SLAB + 4;
                continue;
            }
            sq_stone(14, y + 18 - oy, 5, 16, 16, SQC(MID), (uint32_t)row[i] * 5u + 1u, 0u, 0u);
            pu_rune_n(24, y, nb, SQC(DIM), 2, 3, oy);
            pu_rune_n(66, y, nmt, SQC(MID), 2, 13, oy);   /* (every row the same: the user, 2026-11-13) */
            y += PU_BR_PITCH;
        }
        if (total == 1 && ui_sm_slot_query(sm, tgt, cur, nm) == UISLOT_EMPTY)
            pu_rune_n(64, PU_LG_Y + PU_LG_SLAB + 8, "NOTHING SAVED", SQC(DIM), 1, 20, oy);
        if (total > (int32_t)PU_BR_ROWS)
            pu_lg_scroll(at, total, oy);
        cv_blit(0, (uint32_t)oy);
    }
}

/* ----------------------------------------------------- name keyboard --- */
/* 2026-11-13: while SAVE's knobs 3 / 4 turn (ui_sm_keyboard_visible), the whole screen: what is being named at the
 * top, the name in the runes at 2x a cell a letter (the cursor's in white over an accent bar, the empty cells a
 * faint dash), and the keyboard below in char_map's order, eight to a row at 2x (A..Z 0..9 . - _ /) with SPACE a
 * long stone underneath; the letter under the cursor cut into a channel and lit, the rest dimmer. Knob 3 walks the
 * name, knob 4 the keyboard; any button or encoder puts it away (SAVE then stores). */
#define PU_KB_NAME_Y 40
#define PU_KB_GRID_Y 74
#define PU_KB_COL_W 26
#define PU_KB_ROW_H 22
#define PU_KB_COLS 8

static void pu_draw_namekb(const UIStateMachine *sm, uint32_t force)
{
    static const char map[] = UI_CHAR_MAP;
    const uint8_t *name = ui_sm_rename_name(sm);
    uint8_t tgt = sm->save_target < UISAVE_COUNT ? sm->save_target : (uint8_t)UISAVE_WHOLE_PERF, layer;
    uint8_t cur = sm->name_cursor < NAME_LEN ? sm->name_cursor : 0u, ch, i, idx, on;
    char c[2] = {0, 0}, t[24];
    int32_t x, y, w, oy;
    uint32_t h;
    if (name == NULL)
        return;
    layer = sm->s.active_synth_layer;
    ch = name[cur] < CHAR_MAP_LEN ? name[cur] : 0u;
    h = sq_mix(sq_mix(2166136261u, sq_pal), (uint32_t)tgt | (uint32_t)cur << 8 | (uint32_t)layer << 16);
    h = sq_mix_bytes(h, name, NAME_LEN);
    if (!pu_dirty(PUR_NAMEKB, h, force))
        return;
    t[0] = '\0';
    pu_cat(t, PU_BR_TITLE[tgt]);                     /* "PATCH L1 NAME" */
    if (tgt == UISAVE_PATCH_PRESET)
        pu_cat(t + pu_strlen(t), layer ? " L2" : " L1");
    pu_cat(t + pu_strlen(t), " NAME");
    for (oy = 0; oy < 240; oy += PU_POP_BAND) {
        cv_begin(240, PU_POP_BAND, SQC(BG));
        pu_tablet(4, 4, 232, 232, 91u, oy);
        pu_pop_line(16, t, SQC(THEME), 1, oy);
        for (i = 0; i < NAME_LEN; i++) {             /* the name, 13 px a cell */
            uint8_t k = name[i] < CHAR_MAP_LEN ? name[i] : 0u;
            x = 17 + (int32_t)i * 13;
            c[0] = map[k];
            if (i == cur)
                cv_rect(x - 1, PU_KB_NAME_Y + 21 - oy, 12, 2, SQC(ACCENT));
            else if (!k)
                cv_rect(x + 2, PU_KB_NAME_Y + 17 - oy, 6, 1, PUC_STONE);
            if (k)
                pu_rune_n(x, PU_KB_NAME_Y, c, i == cur ? SQC(HI) : SQC(TEXT), 2, 1, oy);
        }
        for (i = 0; i < CHAR_MAP_LEN; i++) {         /* the keyboard: A .. / then SPACE (char_map 0) */
            idx = (uint8_t)(i + 1u < CHAR_MAP_LEN ? i + 1u : 0u);
            on = (uint8_t)(idx == ch);
            if (idx) {
                x = 16 + (int32_t)(i % PU_KB_COLS) * PU_KB_COL_W;
                y = PU_KB_GRID_Y + (int32_t)(i / PU_KB_COLS) * PU_KB_ROW_H;
                w = PU_KB_COL_W;
            } else {
                x = 16 + PU_KB_COL_W;
                y = PU_KB_GRID_Y + (int32_t)((CHAR_MAP_LEN - 1u) / PU_KB_COLS) * PU_KB_ROW_H + 4;
                w = PU_KB_COL_W * (PU_KB_COLS - 2);
            }
            if (on)
                pu_hewn(x, y, w, PU_KB_ROW_H, (uint32_t)(x * 7 + y * 13), SQC(MID), oy);
            else if (!idx)                           /* SPACE: a low stone's outline */
                pu_hewn(x, y, w, PU_KB_ROW_H, 4219u, PUC_STONE, oy);
            if (idx) {
                c[0] = map[idx];
                pu_rune_n(x + (w - 10) / 2, y + 2, c, on ? SQC(HI) : SQC(MID), 2, 1, oy);
            } else {
                pu_rune_n(x + (w - PU_RUNE_N_W(5, 2)) / 2, y + 2, "SPACE", on ? SQC(HI) : SQC(DIM), 2, 5, oy);
            }
        }
        pu_pop_line(214, "KNOB 3 POSITION   KNOB 4 LETTER", SQC(DIM), 1, oy);
        cv_blit(0, (uint32_t)oy);
    }
}

/* ---------------------------------------------------------- parameters --- */
/* 2026-11-13 (the user: numbers need no popup; a list gets a focused one): while the Algorithm Encoder turns a list
 * row (a wave, a filter type, a mode), the page's name small at the top, the parameter's at 2x under it, then the
 * row's own options as the preset list's ledger at 2x, seven round the one it holds, that one on the lit slab; the
 * scroll stone on the right when there are more. */
#define PU_OP_ROWS 7
#define PU_OP_Y 54

/* 2026-11-13 (the user: an easier way to see what SOURCE, VIA, DEST and AMOUNT pick): while a MOD MATRIX knob turns,
 * the same look as the list above. SOURCE / VIA / DEST: that knob's options as the ledger (the sources spelled out,
 * DEST only the architecture's), the route in short at the foot. VIA DEPTH / AMOUNT (the Algorithm Encoder): the
 * route's card, its five fields a row each, the one turning on the slab, and a bar under them (AMOUNT from the
 * middle). */
static const char *const PU_MODF[UIMODF_COUNT] = {"SOURCE", "VIA", "DESTINATION", "VIA DEPTH", "AMOUNT"};
#define PU_MC_Y 58                   /* the card's first row */
#define PU_MC_PITCH 28

/* field f's value as words: a source / destination's name, VIA DEPTH 0..127, AMOUNT -64..+63 */
static const char *pu_mod_text(const UIStateMachine *sm, uint8_t f, char *b)
{
    int32_t v = ui_sm_mod_value(sm, f), n = 0;
    if (f == UIMODF_SRC || f == UIMODF_VIA)
        return ui_sm_src_name((uint8_t)v);
    if (f == UIMODF_DEST)
        return ui_sm_dest_name((uint8_t)v);
    if (f == UIMODF_AMOUNT) {
        v -= BIPOLAR_CENTER;
        if (v != 0) b[n++] = v < 0 ? '-' : '+';
        if (v < 0) v = -v;
    }
    if (v >= 100) b[n++] = (char)('0' + v / 100);
    if (v >= 10) b[n++] = (char)('0' + v / 10 % 10);
    b[n++] = (char)('0' + v % 10);
    b[n] = ' ';
    return b;
}

static void pu_draw_modpop(const UIStateMachine *sm, uint32_t force)
{
    char tt[24], foot[40], nb[8];
    uint8_t f = (uint8_t)(sm->rowpop - UI_ROWPOP_MOD), i, src, via, list;
    uint16_t pos = 0u, n;
    int32_t first, cnt, k, y, oy, len, v, w;
    uint32_t h;
    if (f >= UIMODF_COUNT) f = UIMODF_AMOUNT;
    n = ui_sm_mod_options(sm, f, &pos);
    list = (uint8_t)(n > 1u);
    h = sq_mix(sq_mix(2166136261u, sq_pal), (uint32_t)f | (uint32_t)sm->s.active_synth_layer << 8 |
                                            (uint32_t)ui_sm_route_sel(sm) << 16);
    for (i = 0u; i < UIMODF_COUNT; i++)
        h = sq_mix(h, ui_sm_mod_value(sm, i));
    if (!pu_dirty(PUR_ROWPOP, h, force))
        return;
    tt[0] = ' ';
    pu_cat(tt, sm->s.active_synth_layer ? "L2  " : "L1  ");
    pu_cat(tt + pu_strlen(tt), PU_TITLE[sm->page]);
    src = ui_sm_mod_value(sm, UIMODF_SRC);           /* the foot: LFO2 * WHL > CUT1  +45 */
    via = ui_sm_mod_value(sm, UIMODF_VIA);
    foot[0] = ' ';
    pu_cat(foot, ui_sm_src_short(src));
    if (via != MSRC_NONE) {
        pu_cat(foot + pu_strlen(foot), " * ");
        pu_cat(foot + pu_strlen(foot), ui_sm_src_short(via));
    }
    pu_cat(foot + pu_strlen(foot), " > ");
    pu_cat(foot + pu_strlen(foot), ui_sm_dest_short(ui_sm_mod_value(sm, UIMODF_DEST)));
    pu_cat(foot + pu_strlen(foot), "  ");
    pu_cat(foot + pu_strlen(foot), pu_mod_text(sm, UIMODF_AMOUNT, nb));
    cnt = n < PU_OP_ROWS ? (int32_t)n : PU_OP_ROWS;
    first = (int32_t)pos - cnt / 2;
    if (first > (int32_t)n - cnt) first = (int32_t)n - cnt;
    if (first < 0) first = 0;
    for (oy = 0; oy < 240; oy += PU_POP_BAND) {
        cv_begin(240, PU_POP_BAND, SQC(BG));
        pu_tablet(4, 4, 232, 232, 77u, oy);
        pu_pop_line(16, tt, SQC(THEME), 1, oy);
        pu_pop_line(30, PU_MODF[f], SQC(TEXT), 2, oy);
        if (list) {                                  /* SOURCE / VIA / DEST: the knob's options */
            y = PU_OP_Y;
            for (k = first; k < first + cnt; k++) {
                const char *t = ui_sm_mod_option(sm, f, (uint16_t)k);
                if (k == (int32_t)pos) {
                    pu_block_lit(10, y - oy, 214, PU_LG_SLAB, 0, oy, 903u);
                    pu_rune_n(24, y + 4, t, SQC(HI), 2, 16, oy);
                    y += PU_LG_SLAB + 4;
                    continue;
                }
                sq_stone(14, y + 18 - oy, 5, 16, 16, SQC(MID), (uint32_t)k * 5u + 3u, 0u, 0u);
                pu_rune_n(24, y, t, SQC(MID), 2, 16, oy);
                y += PU_BR_PITCH;
            }
            if ((int32_t)n > cnt)
                pu_lg_scroll((int32_t)pos, (int32_t)n, oy);
            pu_pop_line(222, foot, SQC(MID), 1, oy);
        } else {                                     /* VIA DEPTH / AMOUNT: the route's card */
            y = PU_MC_Y;
            for (i = 0u; i < UIMODF_COUNT; i++) {
                const char *t = pu_mod_text(sm, i, nb);
                uint16_t c = i == f ? SQC(HI) : SQC(MID);
                len = pu_strlen(t);
                if (i == f)
                    pu_block_lit(10, y - 2 - oy, 214, PU_LG_SLAB, 0, oy, 903u);
                else
                    sq_stone(14, y + 17 - oy, 5, 16, 16, SQC(MID), (uint32_t)i * 5u + 3u, 0u, 0u);
                pu_rune_n(24, y + 6, i == UIMODF_DEST ? "DEST" : PU_MODF[i], c, 1, 12, oy);
                pu_rune_n(100, len <= 9 ? y + 2 : y + 6, t, c, len <= 9 ? 2 : 1, 18, oy);
                y += PU_MC_PITCH;
            }
            v = ui_sm_mod_value(sm, f);
            y = PU_MC_Y + 5 * PU_MC_PITCH + 10;
            cv_rect(24, y + 3 - oy, 192, 1, PUC_STONE);
            if (f == UIMODF_AMOUNT) {                /* from the middle, out to -64 / +63 */
                w = (v - (int32_t)BIPOLAR_CENTER) * 96 / 64;
                cv_rect(w < 0 ? 120 + w : 120, y - oy, w < 0 ? -w : w, 7, SQC(THEME));
                cv_rect(119, y - 3 - oy, 2, 13, SQC(TEXT));
            } else {
                cv_rect(24, y - oy, v * 192 / 127, 7, SQC(THEME));
            }
        }
        cv_blit(0, (uint32_t)oy);
    }
}


static void pu_draw_rowpop(const UIStateMachine *sm, uint32_t force)
{
    if (sm->rowpop >= UI_ROWPOP_MOD) {               /* the MOD MATRIX's knobs (2026-11-13) */
        pu_draw_modpop(sm, force);
        return;
    }
    char tt[24], ob[NAME_LEN + 1];
    uint8_t f = sm->s.current_param_focus;
    uint16_t pos = 0u, n = f < ui_sm_row_count(sm) ? ui_sm_row_options(sm, f, &pos) : (uint16_t)0u;
    const char *lab = f < ui_sm_row_count(sm) ? ui_sm_row_label(sm, f) : "";
    int32_t first, cnt, k, y, oy;
    uint32_t h = sq_mix(sq_mix(2166136261u, sq_pal), (uint32_t)sm->page | (uint32_t)f << 8 |
                                                     (uint32_t)sm->s.active_synth_layer << 16);
    h = pu_mix_str(sq_mix(h, (uint32_t)pos | (uint32_t)n << 16), lab);
    if (!pu_dirty(PUR_ROWPOP, h, force))
        return;
    tt[0] = '\0';
    if (pu_layer_page(sm->page))                     /* "L1  FILTER 1" */
        pu_cat(tt, sm->s.active_synth_layer ? "L2  " : "L1  ");
    pu_cat(tt + pu_strlen(tt), ui_sm_noise_page(sm, sm->page, sm->s.active_synth_layer) ? "NOISE"
                               : pu_mfx_view(sm) ? "MFX" : PU_TITLE[sm->page]);
    cnt = n < PU_OP_ROWS ? (int32_t)n : PU_OP_ROWS;  /* the window: the value in the middle until an end is near */
    first = (int32_t)pos - cnt / 2;
    if (first > (int32_t)n - cnt) first = (int32_t)n - cnt;
    if (first < 0) first = 0;
    for (oy = 0; oy < 240; oy += PU_POP_BAND) {
        cv_begin(240, PU_POP_BAND, SQC(BG));
        pu_tablet(4, 4, 232, 232, 77u, oy);
        pu_pop_line(16, tt, SQC(THEME), 1, oy);
        pu_pop_line(30, lab, SQC(TEXT), 2, oy);
        y = PU_OP_Y;
        for (k = first; k < first + cnt; k++) {
            const char *t = ui_sm_row_option(sm, f, (uint16_t)k, ob);
            int32_t len = pu_strlen(t);
            if (k == (int32_t)pos) {                 /* the value it holds: on the lit slab */
                pu_block_lit(10, y - oy, 214, PU_LG_SLAB, 0, oy, 903u);
                if (len <= 16)
                    pu_rune_n(24, y + 4, t, SQC(HI), 2, 16, oy);
                else
                    pu_rune_n(24, y + 9, t, SQC(HI), 1, 32, oy);
                y += PU_LG_SLAB + 4;
                continue;
            }
            sq_stone(14, y + 18 - oy, 5, 16, 16, SQC(MID), (uint32_t)k * 5u + 3u, 0u, 0u);
            pu_rune_n(24, y, t, SQC(MID), len <= 16 ? 2 : 1, 32, oy);
            y += PU_BR_PITCH;
        }
        if ((int32_t)n > cnt)
            pu_lg_scroll((int32_t)pos, (int32_t)n, oy);
        cv_blit(0, (uint32_t)oy);
    }
}

/* -------------------------------------------------------- MOD MATRIX --- */
/* The ley-line map under the routes (2026-11-12): the sources the routes use down the left, their destinations down
 * the right (in page order), a line a route. The cursor's route burns bright, the other lines from its source show,
 * the rest lie faint; a VIA ties a knot halfway, open as wide as its VIA DEPTH. Sparks run the cursor's source's
 * lines: an LFO's at its own rate, an envelope's out and back with its level, the rest a burst on each new note. On
 * + ROUTE the source it would take stands lit. */
#define PM_SX 40                     /* the sources' stones */
#define PM_DX 198                    /* the destinations' */
#define PM_BURST 16u                 /* frames a note's burst runs */
static uint32_t pm_frame, pm_burst_at = 0xFFFFFFFFu, pm_age;
static int32_t pm_voice = -1;

/* node i of n down the map */
static int32_t pm_y(int32_t i, int32_t n) { return 6 + (60 * (2 * i + 1)) / (2 * (n > 0 ? n : 1)); }

/* where along its lines the source's sparks are (0..255 each, *n of them; 0: none now) */
static uint32_t pm_sparks(const UIStateMachine *sm, uint8_t src, int32_t *t)
{
    uint8_t layer = sm->s.active_synth_layer;
    int32_t v = pu_voice(layer);
    if (src >= MSRC_LFO1 && src <= MSRC_LFO3) {
        const LfoRuntimeState *st = pu_lfo_state(sm, (uint8_t)(src - MSRC_LFO1));
        if (st == 0)
            return 0u;
        t[0] = (int32_t)(st->phase_accumulator >> 24);
        t[1] = (t[0] + 128) & 255;
        return 2u;
    }
    if (v < 0)
        return 0u;
    if (src >= MSRC_ENV1 && src <= MSRC_ENV3) {
        int32_t l = pu_env_of(&poly_bank[v], (uint8_t)(src - MSRC_ENV1))->level_q15;
        t[0] = l <= 0 ? 0 : l >= 32767 ? 255 : l >> 7;
        return t[0] > 4 ? 1u : 0u;
    }
    if (pm_frame - pm_burst_at >= PM_BURST)
        return 0u;
    t[0] = (int32_t)((pm_frame - pm_burst_at) * 256u / PM_BURST);
    return 1u;
}

/* a new note on the layer: the bursts start (once a frame, from pu_viz_live) */
static void pm_tick(const UIStateMachine *sm)
{
    int32_t v = pu_voice(sm->s.active_synth_layer);
    pm_frame++;
    if (v >= 0 && (v != pm_voice || poly_bank[v].age_counter < pm_age))
        pm_burst_at = pm_frame;
    pm_voice = v;
    pm_age = v >= 0 ? poly_bank[v].age_counter : 0u;
}

/* the point at t (0..256) along a line from (x0, y0) to (x1, y1), leaving and arriving level */
static void pm_at(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t t, int32_t *x, int32_t *y)
{
    int32_t s = 256 - t, b1 = 3 * s * s / 256 * t / 256, b2 = 3 * s * t / 256 * t / 256, b3 = t * t / 256 * t / 256;
    int32_t cx0 = x0 + 56, cx1 = x1 - 56;
    *x = (s * s / 256 * s / 256 * x0 + b1 * cx0 + b2 * cx1 + b3 * x1) / 256;
    *y = (s * s / 256 * s / 256 * y0 + b1 * y0 + b2 * y1 + b3 * y1) / 256;
}

static void pu_viz_mod(const UIStateMachine *sm)
{
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    uint8_t srcs[MSRC_COUNT], ns = 0u, dsts[MDST_COUNT], nd = 0u, cr = ui_sm_row_route(sm, sm->s.current_param_focus);
    uint8_t lit = MSRC_NONE, ldst = MDST_NONE, i, j, k, used[MSRC_COUNT] = {0}, dused[MDST_COUNT] = {0};
    int32_t sy[MSRC_COUNT], dy[MDST_COUNT], t[2], n, x, y, px, py, step;
    if (cr < NROUTE) {
        lit = e->route[cr].src != MSRC_NONE ? e->route[cr].src : sm->route_hint;
        ldst = e->route[cr].dest;
    }
    for (i = 0u; i < NROUTE; i++)
        if (e->route[i].src != MSRC_NONE && e->route[i].src < MSRC_COUNT) {
            used[e->route[i].src] = 1u;
            if (e->route[i].dest < MDST_COUNT)
                dused[e->route[i].dest] = 1u;
        }
    if (lit < MSRC_COUNT && lit != MSRC_NONE)
        used[lit] = 1u;
    if (ldst < MDST_COUNT && ldst != MDST_NONE)
        dused[ldst] = 1u;
    for (i = 1u; i < MSRC_COUNT; i++)
        if (used[i]) srcs[ns++] = i;
    for (i = 1u; i < MDST_COUNT; i++)
        if (dused[i]) dsts[nd++] = i;
    if (ns == 0u) {
        sq_text_in(0, 30 + CAP_IN(S, 13), 240, &AF_S, "NO ROUTES", SQC(DIM), SQC(BG));
        return;
    }
    for (i = 0u; i < ns; i++) sy[srcs[i]] = pm_y(i, ns);
    for (i = 0u; i < nd; i++) dy[dsts[i]] = pm_y(i, nd);
    /* the lines: faint first, then the lit source's, then the cursor's route on top */
    for (k = 0u; k < 3u; k++)
        for (i = 0u; i < NROUTE; i++) {
            const ModRoute *m = &e->route[i];
            uint8_t lv = (uint8_t)(i == cr ? 2u : m->src == lit ? 1u : 0u);
            uint16_t c = lv == 2u ? SQC(THEME) : lv == 1u ? SQC(MID) : PUC_STONE;
            if (lv != k || m->src == MSRC_NONE || m->src >= MSRC_COUNT || m->dest == MDST_NONE || m->dest >= MDST_COUNT)
                continue;
            for (step = 0, px = PM_SX + 4, py = sy[m->src]; step <= 256; step += 8) {
                pm_at(PM_SX + 4, sy[m->src], PM_DX - 1, dy[m->dest], step, &x, &y);
                if (lv || (step & 8))                /* (the faint ones dotted) */
                    cv_line(px, py, x, y, c);
                if (lv == 2u)
                    cv_line(px, py + 1, x, y + 1, c);
                px = x; py = y;
            }
            if (m->via != MSRC_NONE && m->via < MSRC_COUNT) {   /* the knot: open as the VIA DEPTH */
                int32_t r = 2 + (int32_t)m->via_depth * 4 / VAL_MAX;
                uint8_t a;
                pm_at(PM_SX + 4, sy[m->src], PM_DX - 1, dy[m->dest], 128, &x, &y);
                cv_rect(x - r, y - r, 2 * r + 1, 2 * r + 1, SQC(BG));
                for (a = 0u; a < 64u; a += 2u)
                    cv_pset(pu_px(x, r, a), pu_py(y, r, a), c);
            }
        }
    /* the sparks on the lit source's lines */
    n = lit < MSRC_COUNT && lit != MSRC_NONE ? (int32_t)pm_sparks(sm, lit, t) : 0;
    for (i = 0u; i < NROUTE; i++) {
        const ModRoute *m = &e->route[i];
        if (m->src != lit || m->src == MSRC_NONE || m->dest == MDST_NONE || m->dest >= MDST_COUNT)
            continue;
        for (j = 0u; j < (uint8_t)n; j++) {
            pm_at(PM_SX + 4, sy[m->src], PM_DX - 1, dy[m->dest], t[j], &x, &y);
            cv_rect(x - 1, y - 1, 3, 3, SQC(HI));
            pm_at(PM_SX + 4, sy[m->src], PM_DX - 1, dy[m->dest], t[j] > 12 ? t[j] - 12 : 0, &px, &py);
            cv_line(px, py, x, y, SQC(ACCENT));
        }
    }
    /* the stones and their names (when they have room, or are lit) */
    for (i = 0u; i < ns; i++) {
        uint8_t s = srcs[i], on = (uint8_t)(s == lit);
        const char *nm = ui_sm_src_short(s);
        sq_stone(PM_SX - 3, sy[s] + 4, 7, 8, 8, on ? SQC(THEME) : SQC(DIM), (uint32_t)(s * 7 + 3), 0u, 0u);
        if (on || ns <= 6u)
            pu_rune_n(PM_SX - 7 - PU_RUNE_N_W(pu_strlen(nm), 1), sy[s] - 4, nm, on ? SQC(HI) : SQC(MID), 1, 4, 0);
    }
    for (i = 0u; i < nd; i++) {
        uint8_t d = dsts[i], on = (uint8_t)(d == ldst);
        sq_stone(PM_DX, dy[d] + 4, 7, 8, 8, on ? SQC(THEME) : SQC(DIM), (uint32_t)(d * 5 + 1), 0u, 0u);
        if (on || nd <= 6u)
            pu_rune_n(PM_DX + 11, dy[d] - 4, ui_sm_dest_short(d), on ? SQC(HI) : SQC(MID), 1, 4, 0);
    }
}

/* the map's live inputs: the cursor's route, + ROUTE's source and where the sparks are */
static uint32_t pm_sig(const UIStateMachine *sm, uint32_t h)
{
    const EngineState *e = sm->engine[sm->s.active_synth_layer];
    uint8_t cr = ui_sm_row_route(sm, sm->s.current_param_focus), lit = MSRC_NONE;
    int32_t t[2] = {0, 0};
    uint32_t n;
    pm_tick(sm);
    if (cr < NROUTE)
        lit = e->route[cr].src != MSRC_NONE ? e->route[cr].src : sm->route_hint;
    n = lit != MSRC_NONE && lit < MSRC_COUNT ? pm_sparks(sm, lit, t) : 0u;
    h = sq_mix(h, (uint32_t)cr | (uint32_t)sm->route_hint << 8 | n << 16);
    return sq_mix(h, (uint32_t)(t[0] >> 2) | (uint32_t)(t[1] >> 2) << 8);
}

/* ============================================================== entry === */
/* the visualizer's own inputs beyond the page's data: live values, quantised */
static uint32_t pu_viz_live(UIStateMachine *sm, uint32_t h)
{
    uint8_t page = sm->page;
    int32_t voice;
    if (ui_sm_noise_page(sm, page, sm->s.active_synth_layer)) {   /* the mist drifts while the layer sounds */
        if (pu_voice(sm->s.active_synth_layer) >= 0)
            h = sq_mix(h, ++pn_t);
        return sq_mix(h, (uint32_t)(pu_noise_ratio(sm) * 64.f));
    }
    switch (page) {
    case UIPAGE_OSC1: case UIPAGE_OSC2:
        return sq_mix(h, (uint32_t)pu_osc_morph(sm, (uint8_t)(page - UIPAGE_OSC1), &voice) >> 6);
    case UIPAGE_OSCMIX:
        h = sq_mix(h, (uint32_t)pu_osc_morph(sm, 0u, &voice) >> 6);
        return sq_mix(h, (uint32_t)pu_osc_morph(sm, 1u, &voice) >> 6);
    case UIPAGE_MOD: {
        const EngineState *e = sm->engine[sm->s.active_synth_layer];
        return pm_sig(sm, sq_mix_bytes(h, (const uint8_t *)e->route, sizeof e->route));
    }
    case UIPAGE_AMP:
        return pu_mfx_view(sm) ? pu_mfx_sig(sm, h) : pu_voices_sig(h);
    case UIPAGE_HOME1: case UIPAGE_HOME2:
        return pu_voices_sig(h);
    case UIPAGE_MIX:
        return pu_mix_sig(sm, h);
    case UIPAGE_TUNING:
        return pu_tuning_sig(sm, h);
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
    if (pu_mfx_view(sm)) {                           /* the wind: under the rows, its own canvas */
        pu_viz_mfx(sm);
        return;
    }
    cv_begin(240, PU_VIZ_H, SQC(BG));
    switch (page) {
    case UIPAGE_OSC1: case UIPAGE_OSC2:
        if (ui_sm_noise_page(sm, page, sm->s.active_synth_layer)) pu_viz_noise(sm);
        else pu_viz_osc(sm, (uint8_t)(page - UIPAGE_OSC1));
        break;
    case UIPAGE_OSCMIX: pu_viz_oscmix(sm); break;
    case UIPAGE_FILTER1: case UIPAGE_FILTER2: pu_viz_filter(sm, (uint8_t)(page - UIPAGE_FILTER1)); break;
    case UIPAGE_FLTMIX: pu_viz_fltmix(sm); break;
    case UIPAGE_ENV1: case UIPAGE_ENV2: case UIPAGE_ENV3: pu_viz_env(sm, (uint8_t)(page - UIPAGE_ENV1)); break;
    case UIPAGE_LFO1: case UIPAGE_LFO2: case UIPAGE_LFO3: pu_viz_lfo(sm, (uint8_t)(page - UIPAGE_LFO1)); break;
    case UIPAGE_MOD: pu_viz_mod(sm); break;
    case UIPAGE_AMP: pu_viz_amp(sm); break;
    case UIPAGE_EFFECTS: pu_viz_fx(sm); break;
    case UIPAGE_MIX: pu_viz_mix(sm); break;
    case UIPAGE_GLOBAL: pu_viz_global(sm); break;
    case UIPAGE_TUNING: pu_viz_tuning(sm); break;
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
    if (page == UIPAGE_OSC1 || (page == UIPAGE_OSC2 && !ui_sm_noise_page(sm, page, sm->s.active_synth_layer)))
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

/* kept out of ui_screen_draw: inlined there it grows the -Os flash image by ~640 B (2026-10-26) */
__attribute__((noinline)) static void page_draw(UIStateMachine *sm, uint32_t force)
{
    static uint8_t pal_drawn = 0xFFu;
    uint8_t page = sm->page, layer = sm->s.active_synth_layer, k, dlg = (uint8_t)(sm->dialog != UIDLG_NONE);
    uint8_t tomb = 0u;                               /* (the MOD page's tomb went with the slots, 2026-11-11) */
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
        h = sq_mix_bytes(h, ui_sm_rename_name(sm), NAME_LEN);   /* (a sequence's or arp's: not in the bytes above) */
        if (sm->clip_kind < UISAVE_COUNT)            /* the clipboard's name, as the CLIP line shows it */
            h = sq_mix_bytes(h, pu_clip_name(sm),
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
        const char *vm = ui_sm_vmode_name(sm->perf->voice_mode[singl ? 0u : sm->s.active_synth_layer]);
        pu_cat(l, "< OCT-  L1");
        pu_cat(r, "L2  OCT+ >");
        lc = l2 ? SQC(MID) : SQC(TEXT);
        rc = singl ? SQC(DIM) : l2 ? SQC(TEXT) : SQC(MID);
        sq_rune(120 - sq_rune_w(vm, 0) / 2, y, vm, SQC(THEME), 0);   /* the voicing HOME + MONO / POLY sets */
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
    static uint8_t last_page = 0xFFu, last_layer, last_dlg, last_pop, last_pal = 0xFFu, last_hold = BTN_COUNT, last_mfx;
    uint8_t dlg = (uint8_t)(sm->dialog != UIDLG_NONE), moved = (uint8_t)(sm->page != last_page);
    uint8_t mfx = pu_mfx_view(sm);                   /* (AMP's MFX group: another layout, everything again) */
    uint8_t pop = sm->browse ? 1u : sm->rowpop ? 2u : ui_sm_keyboard_visible(sm) ? 3u : 0u;   /* the popup up: the preset
                                                     * list, the parameter list, SAVE's name keyboard */
    sq_pal = pu_page_pal(sm);                        /* a change redraws everything (page_draw, seq_draw: pal_drawn) */
    uint8_t held = (uint8_t)(sm->hold == BTN_HOME || sm->hold == BTN_SAVE);
    if (!held && last_hold != BTN_COUNT)
        force = 1u;                                  /* the hold strip gone: the page under it again */
    last_hold = held ? sm->hold : (uint8_t)BTN_COUNT;
    if (moved || sm->s.active_synth_layer != last_layer || dlg != last_dlg || pop != last_pop || sq_pal != last_pal ||
        mfx != last_mfx) {
        if (moved)
            pu_top = 0;
        last_mfx = mfx;
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
        else if (pop == 2u)
            pu_draw_rowpop(sm, force);
        else
            pu_draw_namekb(sm, force);
        if (held)
            pu_draw_hold(sm);
        return;
    }
    if (sm->page == UIPAGE_REC)
        rec_draw(sm, force);
    else if (sm->page == UIPAGE_SEQ)
        seq_draw(sm, q, force);
    else if (sm->page == UIPAGE_ARP)
        arp_draw(sm, q, a, force);
    else
        page_draw(sm, force);
    sq_draw_moon(force);                             /* the battery, top right on every page */
    if (held)
        pu_draw_hold(sm);
}

#include "ui_rec_draw.c"                     /* the REC page (2026-11-04): its own screen, as SEQ's */
