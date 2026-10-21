/* SEQ page renderer for the FM1's 240 x 240 panel: mvave_fm1_master_specification.txt 7.4 (four stacked lane blocks:
 * a 16-step row of stones and a position / event / target / mode footer row each, the knob frame, the loop range's grass; the
 * active lane's knob group in lit stones and grass, paler with the cursor on LANE), 3 (the knob frame on top, the synth palettes) and 7.1 / 7.2 (what the knobs and the
 * playhead show live and in step record).
 *
 * A Felucca unity-build fragment, not a translation unit: #include it after lcd.c and gfx.c (lcd_fill, the cv_* canvas,
 * RGB, AF_S / AF_M, CAP_IN), after UIStateMachine.h and dsp/midi_clock.h (MIDI_CLOCK_BPM_BASE). It draws into
 * Felucca's strip canvas and blits each region on its own (gfx.c: no full framebuffer). Its colours are the synth's
 * own (seq_palette_set), not Felucca's MENU > COLOR theme.
 *
 *   seq_draw(sm, q, force)   once per UI frame while sm->page == UIPAGE_SEQ; force 1 after another page or a dialog
 *                            covered the screen. Only regions whose content changed are redrawn: the SPI moves a full
 *                            screen in ~77 ms at 12 MHz, and a playing sequence changes a few grid rows per step.
 *   seq_palette_set(i)       SQPAL_ORANGE (the default) or SQPAL_RED when seq_draw is called on its own; ui_screen_draw
 *                            draws in the focused layer's palette. The next seq_draw redraws everything.
 *
 * Its words, and every page's, are in the rune face (SQ_RUNE, below): sq_text* in place of Felucca's cv_text*.
 *
 * Reads only: the lanes, the UI registers and the engine's playhead (maq_*). Integer only. */

/* ----------------------------------------------------------- palette --- */
/* Spec 3: orange on black or red on black; white only for the selected text (the footer field under the Select Encoder
 * cursor). MID and DIM are TEXT at 70 % and 42 %, SEL (the cursor chip) THEME at 45 %. 2026-10-16: one palette for
 * every page and both layers, seq_palette_set's (ORANGE by default); OAK and GRANITE are kept for a theme setting. */
enum { SQC_BG, SQC_SURF, SQC_TEXT, SQC_THEME, SQC_ACCENT, SQC_MID, SQC_DIM, SQC_SEL, SQC_HI, SQC_COUNT };
enum { SQPAL_ORANGE, SQPAL_RED, SQPAL_OAK, SQPAL_GRANITE, SQPAL_SOLSTICE, SQPAL_LICHEN, SQPAL_HEATHER, SQPAL_RITUAL,
       SQPAL_FIRE, SQPAL_COUNT };
static const uint16_t SQ_PAL[SQPAL_COUNT][SQC_COUNT] = {
    {RGB(0, 0, 0), RGB(34, 16, 0), RGB(255, 140, 30), RGB(255, 110, 0), RGB(255, 190, 90),
     RGB(178, 98, 21), RGB(107, 59, 13), RGB(115, 50, 0), RGB(255, 255, 255)},                 /* ORANGE */
    {RGB(0, 0, 0), RGB(36, 4, 4), RGB(255, 70, 60), RGB(230, 20, 20), RGB(255, 140, 120),
     RGB(178, 49, 42), RGB(107, 29, 25), RGB(104, 9, 9), RGB(255, 255, 255)},                  /* RED */
    {RGB(0, 0, 0), RGB(26, 16, 7), RGB(235, 175, 105), RGB(215, 135, 55), RGB(250, 215, 160),
     RGB(164, 122, 74), RGB(99, 74, 44), RGB(97, 61, 25), RGB(255, 255, 255)},                 /* OAK */
    {RGB(0, 0, 0), RGB(18, 18, 18), RGB(200, 200, 195), RGB(150, 152, 150), RGB(235, 235, 230),
     RGB(140, 140, 136), RGB(84, 84, 82), RGB(68, 68, 68), RGB(255, 255, 255)},                /* GRANITE */
    /* the themes after GRANITE: ORANGE's levels in another hue (each entry as bright as ORANGE's) */
    {RGB(0, 0, 0), RGB(12, 21, 34), RGB(112, 173, 255), RGB(90, 160, 255), RGB(155, 198, 255),
     RGB(78, 120, 178), RGB(47, 73, 107), RGB(41, 72, 115), RGB(255, 255, 255)},               /* SOLSTICE */
    {RGB(0, 0, 0), RGB(19, 28, 9), RGB(155, 216, 94), RGB(140, 210, 70), RGB(185, 228, 143),
     RGB(108, 151, 66), RGB(65, 91, 40), RGB(63, 95, 32), RGB(255, 255, 255)},                 /* LICHEN */
    {RGB(0, 0, 0), RGB(32, 14, 25), RGB(242, 125, 199), RGB(240, 105, 190), RGB(246, 164, 216),
     RGB(169, 87, 139), RGB(102, 53, 83), RGB(108, 47, 86), RGB(255, 255, 255)},               /* HEATHER */
    {RGB(0, 0, 0), RGB(25, 15, 34), RGB(194, 129, 255), RGB(185, 110, 255), RGB(213, 167, 255),
     RGB(136, 90, 178), RGB(82, 54, 107), RGB(83, 50, 115), RGB(255, 255, 255)},               /* RITUAL */
    /* FIRE: not one hue but a bonfire's, ember red in the dark shades to flame yellow in the bright ones */
    {RGB(0, 0, 0), RGB(27, 4, 0), RGB(250, 201, 81), RGB(242, 170, 53), RGB(255, 226, 122),
     RGB(163, 103, 42), RGB(92, 42, 18), RGB(97, 35, 8), RGB(255, 255, 255)},                  /* FIRE */
};
/* the highlight's tones: SQ_PAL_LANE's BG and SQ_LANE_NOISE are the brighter noise of every page's highlight (the menu
 * row, the SEQ footer field under the cursor, a held ARP step), its words in light tones on it. Its TEXT and ACCENT
 * also light the SEQ knob group's stones and grass with the cursor on LANE. */
static const uint16_t SQ_PAL_LANE[SQPAL_COUNT][SQC_COUNT] = {
    {RGB(110, 42, 0), RGB(90, 34, 0), RGB(255, 200, 140), RGB(255, 140, 30), RGB(255, 232, 200),
     RGB(235, 150, 70), RGB(84, 40, 12), RGB(0, 0, 0), RGB(255, 255, 255)},                    /* ORANGE */
    {RGB(110, 12, 10), RGB(90, 9, 8), RGB(255, 190, 180), RGB(255, 70, 60), RGB(255, 218, 210),
     RGB(235, 110, 100), RGB(84, 16, 14), RGB(0, 0, 0), RGB(255, 255, 255)},                   /* RED */
    {RGB(92, 58, 24), RGB(75, 47, 19), RGB(244, 211, 172), RGB(235, 175, 105), RGB(251, 239, 225),
     RGB(220, 176, 124), RGB(71, 45, 18), RGB(0, 0, 0), RGB(255, 255, 255)},                   /* OAK */
    {RGB(64, 65, 64), RGB(52, 53, 52), RGB(225, 225, 222), RGB(200, 200, 195), RGB(244, 244, 243),
     RGB(194, 194, 190), RGB(50, 50, 50), RGB(0, 0, 0), RGB(255, 255, 255)},                   /* GRANITE */
    {RGB(39, 69, 110), RGB(32, 56, 90), RGB(191, 218, 255), RGB(112, 173, 255), RGB(206, 226, 255),
     RGB(134, 177, 235), RGB(38, 58, 84), RGB(0, 0, 0), RGB(255, 255, 255)},                   /* SOLSTICE */
    {RGB(60, 91, 30), RGB(49, 74, 25), RGB(211, 238, 184), RGB(155, 216, 94), RGB(220, 242, 200),
     RGB(164, 207, 121), RGB(52, 72, 33), RGB(0, 0, 0), RGB(255, 255, 255)},                   /* LICHEN */
    {RGB(104, 45, 82), RGB(85, 37, 67), RGB(249, 197, 230), RGB(242, 125, 199), RGB(250, 210, 236),
     RGB(226, 143, 195), RGB(80, 42, 66), RGB(0, 0, 0), RGB(255, 255, 255)},                   /* HEATHER */
    {RGB(80, 47, 110), RGB(65, 39, 90), RGB(228, 199, 255), RGB(194, 129, 255), RGB(234, 212, 255),
     RGB(192, 146, 235), RGB(65, 44, 84), RGB(0, 0, 0), RGB(255, 255, 255)},                   /* RITUAL */
    {RGB(91, 30, 7), RGB(73, 20, 4), RGB(255, 233, 151), RGB(250, 201, 81), RGB(255, 235, 162),
     RGB(232, 197, 101), RGB(71, 27, 13), RGB(0, 0, 0), RGB(255, 255, 255)},                   /* FIRE */
};
static const uint16_t SQ_LANE_NOISE[SQPAL_COUNT][3] = {          /* darker, lighter, base */
    {RGB(80, 28, 0), RGB(140, 56, 0), RGB(110, 42, 0)},                                        /* ORANGE */
    {RGB(80, 8, 6), RGB(140, 18, 14), RGB(110, 12, 10)},                                       /* RED */
    {RGB(67, 42, 17), RGB(118, 74, 30), RGB(92, 58, 24)},                                      /* OAK */
    {RGB(46, 47, 46), RGB(82, 84, 82), RGB(64, 65, 64)},                                       /* GRANITE */
    {RGB(28, 50, 80), RGB(49, 88, 140), RGB(39, 69, 110)},                                     /* SOLSTICE */
    {RGB(44, 66, 22), RGB(77, 115, 38), RGB(60, 91, 30)},                                      /* LICHEN */
    {RGB(75, 33, 60), RGB(132, 58, 104), RGB(104, 45, 82)},                                    /* HEATHER */
    {RGB(58, 35, 80), RGB(102, 60, 140), RGB(80, 47, 110)},                                    /* RITUAL */
    {RGB(65, 15, 2), RGB(120, 50, 13), RGB(91, 30, 7)},                                        /* FIRE */
};
static uint8_t sq_pal;               /* SQPAL_*: the palette being drawn in */
static uint8_t sq_pal_base;          /* SQPAL_*: seq_palette_set's, the one every page is drawn in */
static uint8_t sq_inv;               /* SQINV_*: how the region being drawn is coloured */
enum { SQINV_OFF, SQINV_LANE };   /* SQINV_LANE: on the highlight's noise (SQ_PAL_LANE) */

static uint16_t sq_c(uint32_t t)
{
    return sq_inv == SQINV_LANE ? SQ_PAL_LANE[sq_pal][t] : SQ_PAL[sq_pal][t];
}
#define SQC(t) sq_c(SQC_##t)

static void seq_palette_set(uint32_t i) { sq_pal = sq_pal_base = (uint8_t)(i < SQPAL_COUNT ? i : SQPAL_ORANGE); }


/* -------------------------------------------------------------- runes --- */
/* Every word on the screen is in the rune face: 5 x 9, one byte a row, the leftmost column in bit 4 (generated:
 * src/host/rune_font_gen.py), hewn capitals, no anti-aliasing. Lowercase is drawn as capitals, anything else as ?.
 * Drawn pixel by pixel, so it is the same on the FM1, on the host shim and in the browser. */
#define SQ_RUNE_W 5
#define SQ_RUNE_H 9
static const char SQ_RUNE_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_/?!:+><*#,()%'=[]";
static const uint8_t SQ_RUNE[][SQ_RUNE_H] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x04, 0x0A, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x1E, 0x11, 0x11, 0x12, 0x1E, 0x11, 0x11, 0x11, 0x1E},
    {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F},
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x11, 0x11, 0x12, 0x1C},
    {0x1F, 0x10, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x1F},
    {0x1F, 0x10, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x10},
    {0x0F, 0x10, 0x10, 0x10, 0x13, 0x11, 0x11, 0x11, 0x0F},
    {0x11, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    {0x11, 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x11},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x11, 0x11},
    {0x11, 0x19, 0x19, 0x15, 0x15, 0x15, 0x13, 0x13, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    {0x1E, 0x11, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x11},
    {0x0F, 0x10, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x01, 0x1E},
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
    {0x11, 0x11, 0x0A, 0x0A, 0x04, 0x0A, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x1F, 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10, 0x1F},
    {0x0E, 0x11, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11, 0x0E},
    {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F},
    {0x0E, 0x11, 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    {0x1E, 0x01, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x12, 0x1F, 0x02, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x01, 0x1E},
    {0x0E, 0x10, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x01, 0x02, 0x02, 0x04, 0x04, 0x04, 0x04},
    {0x0E, 0x11, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01, 0x0E},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04},
    {0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
    {0x01, 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10, 0x10},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x04, 0x00, 0x00, 0x04},
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x04},
    {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00},
    {0x00, 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00},
    {0x00, 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00},
    {0x00, 0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01, 0x00},
    {0x00, 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00},
    {0x00, 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08},
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x08, 0x08, 0x04, 0x02},
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x08},
    {0x19, 0x19, 0x02, 0x02, 0x04, 0x08, 0x08, 0x13, 0x13},
    {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00},
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E},
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E},
};

static const uint8_t *sq_rune_glyph(char c)
{
    const char *p;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (p = SQ_RUNE_CHARS; *p; p++)
        if (*p == c)
            return SQ_RUNE[p - SQ_RUNE_CHARS];
    return sq_rune_glyph('?');
}

/* the ink's width of s, track px more between the letters than the face's 1 */
static int32_t sq_rune_w(const char *s, int32_t track)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n ? n * (SQ_RUNE_W + 1 + track) - 1 - track : 0;
}

/* s with its top row at y; returns where its ink ends */
static int32_t sq_rune(int32_t x, int32_t y, const char *s, uint16_t c, int32_t track)
{
    int32_t i, j;
    const uint8_t *g;
    for (; *s; s++, x += SQ_RUNE_W + 1 + track) {
        g = sq_rune_glyph(*s);
        for (j = 0; j < SQ_RUNE_H; j++)
            for (i = 0; i < SQ_RUNE_W; i++)
                if ((g[j] >> (SQ_RUNE_W - 1 - i)) & 1u)
                    cv_pset(x + i, y + j, c);
    }
    return x - 1 - track;
}

/* The page text calls: Felucca's cv_text* in the rune face, the same arguments and placement. y is the line top of f
 * (CAP_IN(S / M, h) centres it), the runes centred on f's capitals' band; f only picks the spacing: AF_S as the preset
 * list's names, AF_M (the page titles) spaced as its title. Returns the pen x past the text, as cv_text does. */
#define SQ_TRACK(f) ((f) == &AF_M ? 2 : 0)
#define SQ_RUNE_TOP(f) ((f) == &AF_M ? AF_M_CAP_Y + HALF_UP(AF_M_CAP_H - SQ_RUNE_H) \
                                     : AF_S_CAP_Y + HALF_UP(AF_S_CAP_H - SQ_RUNE_H))
static int32_t sq_text_w(const aafont_t *f, const char *s)
{
    int32_t w = sq_rune_w(s, SQ_TRACK(f));
    return w ? w + 1 + SQ_TRACK(f) : 0;
}
static int32_t sq_text_on(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    (void)bg;                                        /* (no anti-aliasing: nothing to blend into) */
#ifdef SHIM_TEXT_MAX                                 /* the host shim's layout check */
    if (x < 0 || x + sq_rune_w(s, SQ_TRACK(f)) > cv_w || y + SQ_RUNE_TOP(f) < 0 || y + SQ_RUNE_TOP(f) + SQ_RUNE_H > cv_h)
        shim_overflows++;
#endif
    sq_rune(x, y + SQ_RUNE_TOP(f), s, fg, SQ_TRACK(f));
    return x + sq_text_w(f, s);
}
static int32_t sq_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg)
{
    return sq_text_on(x, y, f, s, fg, 0);
}
/* right-aligned: the advance ends at xr */
static int32_t sq_text_r(int32_t xr, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t w = sq_text_w(f, s);
    sq_text_on(xr - w, y, f, s, fg, bg);
    return xr - w;
}
/* the ink centred across x0 .. x0 + w */
static int32_t sq_text_in(int32_t x0, int32_t y, int32_t w, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    return sq_text_on(x0 + HALF_UP(w - sq_rune_w(s, SQ_TRACK(f))), y, f, s, fg, bg);
}
/* in maxw px of ink, the end cut to ".." when it is longer */
static int32_t sq_text_fit(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg, int32_t maxw)
{
    char b[48];
    uint32_t k = 0;
    while (s[k] && k + 3u < sizeof b) {
        b[k] = s[k];
        k++;
    }
    b[k] = 0;
    if (!s[k] && sq_rune_w(b, SQ_TRACK(f)) <= maxw)
        return sq_text_on(x, y, f, b, fg, bg);
#ifdef SHIM_TEXT_MAX
    shim_cuts++;
#endif
    for (;;) {                                       /* drop characters until "text.." fits */
        while (k && b[k - 1u] == ' ')
            k--;
        b[k] = '.';
        b[k + 1u] = '.';
        b[k + 2u] = 0;
        if (!k || sq_rune_w(b, SQ_TRACK(f)) <= maxw)
            return sq_text_on(x, y, f, b, fg, bg);
        k--;
    }
}

/* ------------------------------------------------------------ layout --- */
#define SQ_HEAD_Y 0
#define SQ_HEAD_H 20
#define SQ_HEAD_R 216               /* the header's right-hand text ends here; the moon (sq_draw_moon) is past it */
#define SQ_MOON_X 222
#define SQ_MOON_W 18
#define SQ_MOON_H 20
#define SQ_KNOB_Y 21
#define SQ_KNOB_H 32                 /* knob cards: label row + value row */
#define SQ_KNOB_W 56
#define SQ_KNOB_X0 4                 /* 4 cards of 56 px, 2 px apart: 4 .. 234 */
#define SQ_KNOB_GAP 2
#define SQ_LANE_Y 56                 /* first lane block */
#define SQ_GRID_H 29                 /* top margin, knob frame, step row, loop line */
#define SQ_FOOT_H 14                 /* footer chip row, bottom margin */
#define SQ_LANE_GAP 4                /* the lanes fill the rest of the screen: 56 + 4 x 47 - 4 = 240 */
#define SQ_LANE_H (SQ_GRID_H + SQ_FOOT_H + SQ_LANE_GAP)
#define SQ_LANE_END (SQ_LANE_Y + SEQ_NLANE * SQ_LANE_H - SQ_LANE_GAP)
/* the 16 steps: columns of 14 px, 3 px more between the 4-step groups (the knob groups), no lane number: 4 .. 235 */
#define SQ_STEP_X0 4
#define SQ_STEP_PITCH 14
#define SQ_GROUP_GAP 3
#define SQ_BOX_W 12                  /* the step's stone, 1 px in from its column */
#define SQ_BOX_H 21
#define SQ_BOX_Y 3                   /* .. inside the grid row: rows 3 .. 23 */
#define SQ_FRAME_Y 1                 /* the knob frame: 2 px, rows 1 .. 25, tight round the stones */
#define SQ_FRAME_H (SQ_BOX_H + 4)
#define SQ_TURF_H 3                  /* the loop range's grass (sq_grass): its turf at most 3 rows, on the grid's foot */
#define SQ_CHIP_Y 1                  /* footer: the cursor chip, rows 1 .. 13 */
#define SQ_CHIP_H 13
#define SQ_SLOT_W 8                  /* TARGET's 4 slots: stones of 8 x 11 px, 2 px apart, inside the chip */
#define SQ_SLOT_H 11
#define SQ_SLOT_Y 2
#define SQ_SLOT_PITCH 10

/* footer fields, left to right: position, event, target, scale, root, mode, each a fixed slot sized to its widest
 * value ("g32/n16", "PMut", "[BOTH]" or the 4 slots, "HMin", "C#", "bAC"), 2 px in from the screen's edges. None has
 * a label: each reads by its value alone. SCALE shows on a NOTE lane only, ROOT with a scale that takes one (not OFF,
 * not Genr: the genre brings its own) (2026-10-16). */
enum { SQF_P, SQF_EV, SQF_TGT, SQF_SC, SQF_RT, SQF_MD, SQF_COUNT };
#define SQF_STEPS SQF_COUNT          /* the cursor on STEPS: the knob window's stones lit, no field (on a field they
                                      * light paler: the knobs still edit them) */
#define SQF_NONE (SQF_COUNT + 1)
static const int16_t SQF_X[SQF_COUNT] = {2, 50, 82, 128, 160, 208};
static const int16_t SQF_W[SQF_COUNT] = {44, 28, 42, 28, 16, 30};
static const char *const SQF_LABEL[SQF_COUNT] = {"", "", "", "", "", ""};
/* the SEQ page's rows (UIStateMachine.c SEQ_ROWS: STEPS P EV TARGET SCALE ROOT MD) -> the field the cursor lights */
static const uint8_t SQ_ROW_FIELD[7] = {SQF_STEPS, SQF_P, SQF_EV, SQF_TGT, SQF_SC, SQF_RT, SQF_MD};
#define SQ_POS_X (SQF_X[SQF_P] - 1)  /* the P: field's own region (its n changes every step): its chip and text */
#define SQ_POS_W (SQF_W[SQF_P] + 2)

/* the regions redrawn independently */
enum {
    SQR_HEAD, SQR_KNOBS,
    SQR_GRID0,                               /* a lane's step row, knob frame and loop line */
    SQR_FOOT0 = SQR_GRID0 + SEQ_NLANE,       /* its footer */
    SQR_POS0 = SQR_FOOT0 + SEQ_NLANE,        /* .. of which the P: field alone, when only the playhead moved */
    SQR_COUNT = SQR_POS0 + SEQ_NLANE
};
static uint32_t sq_sig[SQR_COUNT];   /* what each region was last drawn from (FNV-1a); 0 = never drawn */

static uint32_t sq_mix(uint32_t h, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 4u; i++, v >>= 8)
        h = (h ^ (v & 0xFFu)) * 16777619u;
    return h;
}

static uint32_t sq_mix_bytes(uint32_t h, const uint8_t *p, uint32_t n)
{
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

/* 1 when the region must be drawn: its inputs changed since the last time, or force */
static int sq_dirty(uint32_t r, uint32_t sig, uint32_t force)
{
    sig |= 1u;                                       /* (never 0: 0 marks "not drawn yet") */
    if (!force && sq_sig[r] == sig)
        return 0;
    sq_sig[r] = sig;
    return 1;
}

/* ------------------------------------------------------------ values --- */
/* what a step's knob / grid value is: a NOTE lane's first pitch (as its scale plays it), every other lane's data byte */
static uint8_t sq_step_value(const UIStateMachine *sm, const SequencerLane *ln, uint8_t l, uint8_t i)
{
    uint8_t v = ln->event_type == SEQEV_NOTE ? ui_sm_seq_note_shown(sm, l, ln->steps[i].note_buffer[0])
                                             : ln->steps[i].value;
    return v > VAL_MAX ? (uint8_t)VAL_MAX : v;
}

static uint8_t sq_step_tied(const SequencerLane *ln, uint8_t i)
{
    return (uint8_t)(ln->event_type == SEQEV_NOTE &&
                     (ln->steps[i].note_buffer[0] == SEQ_NOTE_TIE || (ln->steps[i].flags & SEQ_FLAG_TIE)));
}

/* a knob card's value as the lane type reads it (7.5): note names, TIMING lengths, ratchet counts, layers */
static const char *sq_value_text(uint8_t ev, uint8_t v, char *b)
{
    static const char *const LAYER[3] = {"L1", "L2", "BOTH"};
    int32_t n, k = 0;
    switch (ev) {
    case SEQEV_NOTE:
        return ui_sm_note_name(v, b);
    case SEQEV_TIMING:
        if (v == 0u) return "SKIP";
        b[0] = 'x';
        n = maq_timing_ticks(v);
        k = 1;
        if (n >= 10) b[k++] = (char)('0' + n / 10);
        b[k++] = (char)('0' + n % 10);
        b[k] = '\0';
        return b;
    case SEQEV_RATCHET:
        b[0] = (char)('0' + maq_ratchet_count(v));
        b[1] = 'x';
        b[2] = '\0';
        return b;
    case SEQEV_LAYER:
        return LAYER[maq_value_to_layer(v)];
    case SEQEV_TRANSPOSE:
        n = (int32_t)v - (int32_t)BIPOLAR_CENTER;    /* semitones, -64 .. +63 */
        b[k++] = n < 0 ? '-' : n > 0 ? '+' : ' ';
        if (n < 0) n = -n;
        if (n >= 10) b[k++] = (char)('0' + n / 10);
        b[k++] = (char)('0' + n % 10);
        b[k] = '\0';
        return b;
    default:
        n = v;
        if (n >= 100) b[k++] = (char)('0' + n / 100);
        if (n >= 10) b[k++] = (char)('0' + n / 10 % 10);
        b[k++] = (char)('0' + n % 10);
        b[k] = '\0';
        return b;
    }
}

/* -------------------------------------------------------------- head --- */
/* SEQ, the transport (a play triangle, a stop square or the record dot and step) and the clock on the right */
static void sq_draw_head(const UIStateMachine *sm, const MaqSeq *q)
{
    char b[12];
    int32_t x, y = CAP_IN(S, SQ_HEAD_H), i;
    uint8_t rec = maq_recording(q), play = maq_playing(q), st = maq_rec_step(q);
    cv_begin(SQ_MOON_X, SQ_HEAD_H, SQC(BG));
    x = sq_text(6, CAP_IN(M, SQ_HEAD_H), &AF_M, "SEQ", SQC(THEME)) + 10;
    if (rec) {
        cv_rrect(x, 6, 9, 9, -4, SQC(ACCENT), SQC(BG));   /* a dot (-r: round in every style) */
        b[0] = 'R'; b[1] = 'E'; b[2] = 'C'; b[3] = ' ';
        b[4] = (char)('0' + (st + 1u) / 10u);
        b[5] = (char)('0' + (st + 1u) % 10u);
        b[6] = '\0';
        sq_text(x + 13, y, &AF_S, b, SQC(ACCENT));
    } else if (play) {
        for (i = 0; i < 5; i++)                      /* a triangle, 9 rows tall */
            cv_rect(x + i, 6 + i, 1, 9 - 2 * i, SQC(THEME));
        sq_text(x + 13, y, &AF_S, "PLAY", SQC(THEME));
    } else {
        cv_rect(x, 6, 8, 8, SQC(MID));
        sq_text(x + 13, y, &AF_S, "STOP", SQC(MID));
    }
    if (sm->perf->clock_source == CLKSRC_MIDI) {
        sq_text_r(SQ_HEAD_R, y, &AF_S, "MIDI CLK", SQC(MID), SQC(BG));
    } else {
        uint32_t bpm = MIDI_CLOCK_BPM_BASE + sm->perf->int_tempo, k = 0;
        if (bpm >= 100u) b[k++] = (char)('0' + bpm / 100u);
        b[k++] = (char)('0' + bpm / 10u % 10u);
        b[k++] = (char)('0' + bpm % 10u);
        b[k++] = ' '; b[k++] = 'B'; b[k++] = 'P'; b[k++] = 'M'; b[k] = '\0';
        sq_text_r(SQ_HEAD_R, y, &AF_S, b, SQC(MID), SQC(BG));
    }
    cv_blit(0, SQ_HEAD_Y);
}

/* ------------------------------------------------------------- knobs --- */
static void sq_knob_card(uint32_t k, uint32_t oy, const char *label, const char *val, uint16_t grass, uint16_t rim);

/* knob k edits the step the playhead is on (live only: in step record the knobs are the lanes at the record step) */
static uint8_t sq_knob_head(const UIStateMachine *sm, const MaqSeq *q, uint8_t k)
{
    uint8_t l, st;
    if (maq_recording(q) || !maq_playing(q))
        return 0u;
    ui_sm_seq_knob_target(sm, k, &l, &st);
    return (uint8_t)(maq_lane_pos(q, l) == st);
}

/* section 3's knob frame: what knobs 1..4 edit now (7.1 the window's steps, 7.2 the four lanes at the record step).
 * The card of the step the playhead is on is framed in ACCENT, as the ARP frames its playing step (2026-10-21). */
static void sq_draw_knobs(UIStateMachine *sm, const MaqSeq *q)
{
    char b[8];
    uint8_t k, l, st;
    UiKnobInfo info;
    cv_begin(240, SQ_KNOB_H, SQC(BG));
    for (k = 0; k < 4u; k++) {
        ui_sm_knob(sm, UIPAGE_SEQ, 0, k, &info);     /* (the label only: SEQ knobs have no plain byte) */
        ui_sm_seq_knob_target(sm, k, &l, &st);
        sq_knob_card(k, SQ_KNOB_Y, info.label, sq_value_text(sm->lanes[l].event_type, ui_sm_seq_knob_get(sm, k), b),
                     SQC(MID), sq_knob_head(sm, q, k) ? SQC(ACCENT) : 0u);
    }
    cv_blit(0, SQ_KNOB_Y);
}

/* -------------------------------------------------------------- grid --- */
static int32_t sq_col_x(uint32_t i) { return SQ_STEP_X0 + (int32_t)i * SQ_STEP_PITCH + (int32_t)(i >> 2) * SQ_GROUP_GAP; }
static int32_t sq_box_x(uint32_t i) { return sq_col_x(i) + 1; }

/* the noisy ground: a w x h block at canvas (x, y) in tone[2], speckled with tone[0] and tone[1], a quarter of the
 * pixels darker and a quarter lighter. ox, oy: the canvas's place on the screen, so the pattern hangs off screen
 * coordinates and neighbouring regions meet up. round: leave the four corner pixels to the canvas. */
static void sq_noise_tones(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ox, uint32_t oy, uint32_t round,
                           const uint16_t *tone)
{
    int32_t i, j;
    uint32_t n;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            if (round && (i == 0 || i == w - 1) && (j == 0 || j == h - 1))
                continue;
            n = ((uint32_t)(x + i) + ox) * 0x9E3779B1u ^ ((uint32_t)(y + j) + oy) * 0x85EBCA77u;
            n ^= n >> 15;
            n *= 0x2C1B3C6Du;
            n = (n >> 28) & 3u;
            cv_pset(x + i, y + j, tone[n < 2u ? n : 2u]);
        }
}

/* every page's highlight (ui_arp_draw.c, ui_page_draw.c: the menu row under the Select Encoder cursor; the SEQ footer
 * field under it) is a rounded block of the brighter noise */
static void sq_noise_block(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ox, uint32_t oy, uint32_t round)
{
    sq_noise_tones(x, y, w, h, ox, oy, round, SQ_LANE_NOISE[sq_pal]);
}

/* ------------------------------------------------------------- stone --- */
/* Every lit bar on every page (the steps here; the voices, frames, echoes and stones of ui_page_draw.c) is a menhir of
 * the alignments, weathered by four thousand years: its shoulders knocked off unevenly, a slight taper, here and there
 * a chip out of a flank, and a coarse grain of darker patches, grit and a crack. Lit by nothing: the grain only darkens.
 * The grain hangs off the stone (its seed, its own rows and columns), so it travels with the stone. */

/* 32 noisy bits for (x, y), the same every time */
static uint32_t sq_hash(int32_t x, int32_t y)
{
    uint32_t n = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u;
    n ^= n >> 15;
    n *= 0x2C1B3C6Du;
    return n;
}

/* c at full, 3/4, 1/2 and 1/4 (t 0 .. 3), channel by channel (RGB565) */
static uint16_t sq_shade(uint16_t c, uint32_t t)
{
    switch (t) {
    case 0u: return c;
    case 1u: return (uint16_t)(c - ((c >> 2) & 0x39E7u));
    case 2u: return (uint16_t)((c >> 1) & 0x7BEFu);
    default: return (uint16_t)((c >> 2) & 0x39E7u);
    }
}

/* how far a w x h stone's flank stands in at row r from its top, on the left (side 0) or the right. The top is
 * hewn at a slant: one shoulder (the seed picks which) falls away deep and long, the other is only knocked off; the
 * stone tapers a little toward its top, and here and there a chip is out of a flank. */
static int32_t sq_stone_in(int32_t r, int32_t w, int32_t h, uint32_t seed, uint32_t side)
{
    uint32_t s = sq_hash((int32_t)seed, 7);
    int32_t deep = (int32_t)((s >> 4) & 1u) == (int32_t)side, k, d, in;
    if (deep) {                                      /* the slant: down to half the width, over up to half the height */
        k = h / 4 + (int32_t)((s >> 8) % (uint32_t)(h / 4 + 1));
        d = w / 3 + (int32_t)((s >> 16) % (uint32_t)(w / 6 + 1));
    } else {
        k = 1 + (int32_t)((s >> 12) % 3u);
        d = 1 + (int32_t)((s >> 20) % (uint32_t)(w / 5 + 1));
    }
    if (k < 1)
        k = 1;
    in = r < k ? ((k - r) * d + k - 1) / k : 0;
    in += ((h - 1 - r) * (w / 8 + 1)) / (2 * h);
    if (h >= 8 && w >= 6 && r >= k && (sq_hash(r >> 1, (int32_t)(s >> 8) + (int32_t)side) >> 29) == 0u)
        in++;
    return in;
}

/* A stone w wide standing on row yb - 1, h tall, of which the bottom `fill` rows are drawn (fill < h: a stone filling
 * to a level, the empty part left to an outline drawn first). edge: the outline only, a few specks inside. decay
 * 0 .. 255: the stone crumbling, from sound to rubble (its top falling away column by column, its face pitted
 * through, the fallen bits at its foot). Stones under 4 x 5 are too small for any of it: a block, the top corners off. */
static void sq_stone(int32_t x, int32_t yb, int32_t w, int32_t h, int32_t fill, uint16_t c, uint32_t seed, uint32_t edge,
                     uint32_t decay)
{
    int32_t r, i, j, l, rt, pl = 0, pr = 0, top = yb - h, cx = -1, cy0 = 0, cy1 = 0, loss;
    uint32_t n, t, g;
    if (h <= 0 || w <= 0 || fill <= 0)
        return;
    if (fill > h)
        fill = h;
    if (w < 4 || h < 5) {
        if (edge) {
            cv_rect(x + 1, top, w - 2, 1, c);
            cv_rect(x, top + 1, 1, h - 1, c);
            cv_rect(x + w - 1, top + 1, 1, h - 1, c);
        } else if (h > 2 && w > 2) {
            cv_rect(x + 1, yb - fill, w - 2, 1, c);
            cv_rect(x, yb - fill + 1, w, fill - 1, c);
        } else {
            cv_rect(x, yb - fill, w, fill, c);
        }
        return;
    }
    if (h >= 12 && w >= 6 && !edge) {                /* a crack, wandering down from somewhere on the face */
        n = sq_hash((int32_t)seed, 31);
        cx = 2 + (int32_t)(n % (uint32_t)(w - 4));
        cy1 = h / 3 + (int32_t)((n >> 8) % (uint32_t)(h / 2));
        cy0 = cy1 - h / 4 - (int32_t)((n >> 16) % (uint32_t)(h / 4 + 1));
    }
    for (r = 0; r < h; r++) {
        l = sq_stone_in(r, w, h, seed, 0u);
        rt = w - sq_stone_in(r, w, h, seed, 1u);
        if (rt - l < 2) {
            l = (l + rt) / 2 - 1;
            rt = l + 2;
        }
        j = h - 1 - r;                               /* rows up from the foot */
        if (r >= h - fill)
            for (i = l; i < rt; i++) {
                if (decay) {
                    /* the top falls away in uneven columns, the face pits through, worse near the top */
                    loss = (int32_t)(((sq_hash((i >> 1) + 5, (int32_t)seed) >> 24) * decay * (uint32_t)h) >> 17);
                    if (r < loss)
                        continue;
                    if ((sq_hash((i >> 1) + (int32_t)seed * 3, (j >> 1) + 77) >> 24) <
                        (decay * (uint32_t)(h + 2 * r)) / (uint32_t)(8 * h))
                        continue;
                }
                n = sq_hash(i + (int32_t)seed * 131, j);
                if (edge && !(i == l || i == rt - 1 || r == 0 || i < pl || i >= pr || r == h - fill)) {
                    if ((n >> 27) == 0u)
                        cv_pset(x + i, top + r, sq_shade(c, 3u));
                    continue;
                }
                g = sq_hash((i >> 1) + (int32_t)seed * 7, (j >> 1) + 300) >> 29;   /* the coarse grain, 2 x 2 */
                t = g < 6u ? 0u : 1u;
                if ((n >> 27) == 0u)                 /* grit */
                    t = 2u;
                if (i == l && t < 1u)                /* the flank turned from the eye */
                    t = 1u;
                if (i == cx + (int32_t)(sq_hash(j >> 2, (int32_t)seed) >> 31) && j >= cy0 && j < cy1)
                    t = 3u;
                cv_pset(x + i, top + r, sq_shade(c, t));
            }
        pl = l;
        pr = rt;
    }
    if (decay)                                       /* the rubble at its foot */
        for (i = 0; i < (int32_t)(decay * (uint32_t)w) >> 7; i++) {
            n = sq_hash(i, (int32_t)seed + 501);
            r = (int32_t)(n % (uint32_t)(w + 6)) - 3;
            cv_pset(x + r, yb - 1 - (int32_t)((n >> 8) & 1u), sq_shade(c, 1u + ((n >> 12) & 1u)));
        }
}

/* -------------------------------------------------------------- rock --- */
/* an unlit stone: between SURF and DIM, so a ring of them is a faint outline on black */
static const uint16_t SQ_STONE[SQPAL_COUNT] = {RGB(66, 34, 6), RGB(66, 14, 12), RGB(61, 46, 27), RGB(52, 52, 51),
                                              RGB(28, 44, 66), RGB(39, 56, 23), RGB(63, 31, 51), RGB(50, 32, 66),
                                              RGB(54, 17, 7)};

/* the tablets' stone (the popups, ui_page_draw.c; the ARP steps), darkest first: [0] the grooves and the bottom of a
 * channel, [2] a tablet's face. Monochrome in the palette's own hue (no grey), darker than the noisy ground */
static const uint16_t SQ_ROCK[SQPAL_COUNT][5] = {
    {RGB(14, 5, 0), RGB(36, 14, 0), RGB(48, 19, 0), RGB(62, 25, 0), RGB(76, 31, 0)},             /* ORANGE */
    {RGB(14, 2, 0), RGB(36, 5, 2), RGB(48, 7, 4), RGB(62, 9, 6), RGB(76, 12, 8)},                /* RED */
    {RGB(12, 7, 3), RGB(30, 19, 8), RGB(41, 26, 10), RGB(53, 33, 13), RGB(64, 40, 16)},          /* OAK */
    {RGB(8, 8, 8), RGB(21, 21, 21), RGB(28, 29, 28), RGB(37, 37, 37), RGB(45, 46, 45)},          /* GRANITE */
    {RGB(5, 9, 14), RGB(13, 23, 36), RGB(17, 30, 48), RGB(22, 39, 62), RGB(27, 48, 76)},         /* SOLSTICE */
    {RGB(8, 12, 4), RGB(20, 30, 10), RGB(26, 40, 13), RGB(34, 51, 17), RGB(42, 63, 21)},         /* LICHEN */
    {RGB(13, 6, 10), RGB(34, 15, 27), RGB(45, 20, 36), RGB(58, 26, 46), RGB(72, 31, 57)},        /* HEATHER */
    {RGB(10, 6, 14), RGB(26, 16, 36), RGB(35, 21, 48), RGB(45, 27, 62), RGB(55, 33, 76)},        /* RITUAL */
    {RGB(11, 2, 0), RGB(28, 4, 0), RGB(38, 6, 0), RGB(49, 10, 1), RGB(61, 15, 2)},               /* FIRE */
};

/* the stone's tone at screen (x, y), 0 .. 4: two coarse mottles over a fine grit */
static uint32_t sq_rock_tone(int32_t x, int32_t y)
{
    uint32_t m = (sq_hash(x / 7, y / 5 + 911) >> 31) + (sq_hash((x + 3) / 4, (y + 1) / 3 + 577) >> 31);
    uint32_t f = sq_hash(x, y) >> 29;
    return 1u + m + (f == 7u) - (f == 0u);
}

/* how far a hewn edge stands in from the tablet's box at place i along an edge n long: the corners knocked off at
 * 45 degrees, and here and there a chip, 4 px long */
static int32_t sq_rock_in(int32_t i, int32_t n, uint32_t seed)
{
    int32_t c = i < n - 1 - i ? i : n - 1 - i, k = c < 4 ? 4 - c : 0;
    uint32_t r = sq_hash(i >> 2, (int32_t)seed) >> 29;
    int32_t chip = r == 0u ? 2 : r < 3u ? 1 : 0;
    return k > chip ? k : chip;
}

/* ----------------------------------------------------------- tablet --- */
/* A tablet of the dark stone (the ARP steps, both pages' knob cards, the SEQ knob group): a w x h box, its edges hewn,
 * the corners knocked off, a chip here and there, the same shape for the same seed every time. sq_tab_shape cuts it;
 * its rows then reach from sq_tl[y] to sq_tr[y] (exclusive), its columns from sq_tt[x] to sq_tb[x]. */
#define SQ_TAB_W 60
#define SQ_TAB_H 56
enum { SQTAB_ROCK, SQTAB_LIT, SQTAB_RUIN };
static int8_t sq_tl[SQ_TAB_H], sq_tr[SQ_TAB_H], sq_tt[SQ_TAB_W], sq_tb[SQ_TAB_W];
static int32_t sq_tw, sq_th;

static void sq_tab_shape(int32_t w, int32_t h, uint32_t seed)
{
    int32_t k;
    sq_tw = w < SQ_TAB_W ? w : SQ_TAB_W;
    sq_th = h < SQ_TAB_H ? h : SQ_TAB_H;
    for (k = 0; k < sq_th; k++) {
        sq_tl[k] = (int8_t)sq_rock_in(k, sq_th, seed + 1u);
        sq_tr[k] = (int8_t)(sq_tw - sq_rock_in(k, sq_th, seed + 2u));
    }
    for (k = 0; k < sq_tw; k++) {
        sq_tt[k] = (int8_t)sq_rock_in(k, sq_tw, seed + 3u);
        sq_tb[k] = (int8_t)(sq_th - sq_rock_in(k, sq_tw, seed + 4u));
    }
}

static int sq_tab_in(int32_t x, int32_t y)
{
    return x >= 0 && x < sq_tw && y >= 0 && y < sq_th && x >= sq_tl[y] && x < sq_tr[y] && y >= sq_tt[x] &&
           y < sq_tb[x];
}

/* how far (x, y) lies inside the tablet's edge, 0 .. 3 (3: deeper) */
static int32_t sq_tab_depth(int32_t x, int32_t y)
{
    int32_t d;
    for (d = 1; d <= 3; d++)
        if (!sq_tab_in(x - d, y) || !sq_tab_in(x + d, y) || !sq_tab_in(x, y - d) || !sq_tab_in(x, y + d))
            return d - 1;
    return 3;
}

/* the tablet last cut, its corner at canvas (x0, y0), at screen (sx, sy) (the grain hangs off the screen). kind
 * SQTAB_ROCK: the stone's face; SQTAB_LIT: the brighter noise of the highlight; SQTAB_RUIN: only its outline left and
 * that broken. rim: its edge lit, 2 px; 0 for none */
static void sq_tablet(int32_t x0, int32_t y0, uint32_t kind, uint16_t rim, uint32_t sx, uint32_t sy)
{
    int32_t x, y, d;
    uint32_t t;
    for (y = 0; y < sq_th; y++)
        for (x = sq_tl[y]; x < sq_tr[y]; x++) {
            if (!sq_tab_in(x, y))
                continue;
            d = sq_tab_depth(x, y);
            if (rim && d < 2) {
                cv_pset(x0 + x, y0 + y, rim);
            } else if (kind == SQTAB_RUIN) {
                if (d == 0 && (sq_hash(((int32_t)sx + x) / 3, ((int32_t)sy + y) / 3) >> 30) != 0u)
                    cv_pset(x0 + x, y0 + y, SQC(DIM));
            } else if (kind == SQTAB_LIT) {
                t = (sq_hash((int32_t)sx + x, (int32_t)sy + y) >> 28) & 3u;
                cv_pset(x0 + x, y0 + y, SQ_LANE_NOISE[sq_pal][t < 2u ? t : 2u]);
            } else {
                t = sq_rock_tone((int32_t)sx + x, (int32_t)sy + y);
                cv_pset(x0 + x, y0 + y, SQ_ROCK[sq_pal][d == 0 && t > 1u ? t - 1u : t]);   /* the hewn edge darker */
            }
        }
}

static void sq_grass(int32_t x0, int32_t x1, int32_t yb, int32_t hmax, uint16_t c, int32_t salt,
                     int32_t dx, int32_t dy, int32_t dw, int32_t dh, uint16_t dark);

/* knob k's tablet, h tall, at canvas x (screen sx, oy: the grain hangs off the screen), cut the same on every page that
 * has one, its name along the top and a strip of grass under it (yb = h + 3): MID, as the grass of the lanes not being
 * edited (THEME when the page marks the card live). rim: its outline's colour, 0 for none. The caller writes the value. */
static void sq_knob_tablet(int32_t x, int32_t sx, int32_t h, uint32_t k, uint32_t oy, const char *label, uint16_t grass,
                           uint16_t rim)
{
    sq_tab_shape(SQ_KNOB_W, h, 4201u + k * 13u);
    sq_tablet(x, 0, SQTAB_ROCK, rim, (uint32_t)sx, oy);
    sq_text_in(x, 1, SQ_KNOB_W, &AF_S, label, SQC(MID), SQC(SURF));
    sq_grass(x + 4, x + SQ_KNOB_W - 4, h + 3, 2, grass, 50 + (int32_t)k, 0, 0, 0, 0, 0);
}

/* knob k's card on the SEQ and ARP knob row (at screen row oy): its tablet, the value under the name */
static void sq_knob_card(uint32_t k, uint32_t oy, const char *label, const char *val, uint16_t grass, uint16_t rim)
{
    int32_t x = SQ_KNOB_X0 + (int32_t)k * (SQ_KNOB_W + SQ_KNOB_GAP);
    sq_knob_tablet(x, x, SQ_KNOB_H - 3, k, oy, label, grass, rim);
    sq_text_in(x, 14, SQ_KNOB_W, &AF_S, val, SQC(TEXT), SQC(SURF));
}

/* -------------------------------------------------------------- grass --- */
/* 0 .. 255 at screen (x, y): value noise on a cw x ch lattice, bilinear between its corners */
static uint32_t sq_vnoise(int32_t x, int32_t y, int32_t cw, int32_t ch, int32_t salt)
{
    int32_t gx = x / cw, gy = y / ch, fx = x % cw, fy = y % ch;
    int32_t a = (int32_t)(sq_hash(gx * 7 + salt, gy * 13) >> 24), b = (int32_t)(sq_hash(gx * 7 + 7 + salt, gy * 13) >> 24);
    int32_t c = (int32_t)(sq_hash(gx * 7 + salt, gy * 13 + 13) >> 24);
    int32_t d = (int32_t)(sq_hash(gx * 7 + 7 + salt, gy * 13 + 13) >> 24);
    int32_t top = a * cw + (b - a) * fx, bot = c * cw + (d - c) * fx;
    return (uint32_t)((top * ch + (bot - top) * fy) / (cw * ch));
}

/* A strip of grass where a flat rule would be: the turf (at most SQ_TURF_H rows, its top uneven, thinning to the ends)
 * across canvas x0 .. x1 - 1 standing on row yb - 1, blades up to hmax px out of it in tufts, the tall ones bent. c the
 * grass's colour, shaded by its grain; salt keeps one strip's blades off another's. Where a blade crosses the box
 * (dx, dy, dw, dh) it is drawn in dark instead, a silhouette on whatever bright thing is there (dw 0: no box). The grass
 * hangs off canvas x, so a strip that grows or shrinks keeps its blades where they were. */
static void sq_grass(int32_t x0, int32_t x1, int32_t yb, int32_t hmax, uint16_t c, int32_t salt,
                     int32_t dx, int32_t dy, int32_t dw, int32_t dh, uint16_t dark)
{
    int32_t x, r, e, t, h, lean, k, px, py;
    uint32_t n, tuft;
    for (x = x0; x < x1; x++) {
        e = x - x0 < x1 - 1 - x ? x - x0 : x1 - 1 - x;           /* how far in from the nearer end */
        t = SQ_TURF_H - 1 + (int32_t)(sq_vnoise(x + 300, salt, 6, 1, salt) >> 7);
        if (t > SQ_TURF_H) t = SQ_TURF_H;
        if (t > e + 1) t = e + 1;
        for (r = 0; r < t; r++) {                    /* the turf: dark at the root, a grain through it */
            n = sq_hash(x, salt * 5 + r);
            cv_pset(x, yb - 1 - r, sq_shade(c, r == 0 ? 2u : (n >> 30) == 0u ? 1u : 0u));
        }
        n = sq_hash(x, salt + 900);
        tuft = sq_vnoise(x + 41, salt, 7, 1, salt + 17) >> 6;    /* 0 .. 3: bare patches, thick tufts */
        if (e < 1 || (n >> 28) >= 3u + tuft * 3u)
            continue;
        h = 1 + (int32_t)((n >> 8) % (2u + tuft));
        if (h > hmax) h = hmax;
        if (h > e + 1) h = e + 1;
        lean = h >= 3 ? (int32_t)((n >> 16) % 3u) - 1 : 0;
        for (k = 0; k < h; k++) {
            px = x + (k >= (h + 1) / 2 ? lean : 0);
            py = yb - 1 - t - k;
            if (dw && px >= dx && px < dx + dw && py >= dy && py < dy + dh)
                cv_pset(px, py, dark);
            else
                cv_pset(px, py, sq_shade(c, k == h - 1 || !((n >> 20) & 1u) ? 0u : 1u));
        }
    }
}

/* -------------------------------------------------------------- moon --- */
/* sin of a / 64 turns, Q8 (generated: round(256 sin(2 pi i / 64))) */
static const int16_t SQ_SIN[64] = {
    0, 25, 50, 74, 98, 121, 142, 162, 181, 198, 213, 226, 237, 245, 251, 255, 256, 255, 251, 245, 237, 226, 213, 198,
    181, 162, 142, 121, 98, 74, 50, 25, 0, -25, -50, -74, -98, -121, -142, -162, -181, -198, -213, -226, -237, -245,
    -251, -255, -256, -255, -251, -245, -237, -226, -213, -198, -181, -162, -142, -121, -98, -74, -50, -25,
};

/* the near side's seas, Q8 of the moon's radius (x right, y down) and their radii: Imbrium, Serenitatis,
 * Tranquillitatis, Procellarum, Crisium, Nubium, Fecunditatis, Frigoris */
static const int16_t SQ_MARE[8][3] = {
    {-82, -97, 77}, {51, -92, 51}, {87, -10, 61}, {-148, 5, 87}, {179, -67, 33}, {-41, 92, 51}, {138, 67, 36}, {-13, -179, 31},
};

/* 1 if (x, y) from the centre of a moon of radius r falls on one of its seas */
static int sq_mare(int32_t x, int32_t y, int32_t r)
{
    int32_t u = x * 256 / r, v = y * 256 / r, k, du, dv;
    for (k = 0; k < 8; k++) {
        du = u - SQ_MARE[k][0];
        dv = v - SQ_MARE[k][1];
        if (du * du + dv * dv < SQ_MARE[k][2] * SQ_MARE[k][2])
            return 1;
    }
    return 0;
}

/* c at 3/4 its brightness (each RGB565 channel): the seas' shade of a moon lit in c */
static uint16_t sq_sea(uint16_t c)
{
    return (uint16_t)(((((c >> 11) & 31u) * 3u / 4u) << 11) | ((((c >> 5) & 63u) * 3u / 4u) << 5) | ((c & 31u) * 3u / 4u));
}

/* a disc lit from the right as the moon waxes (phase 0 .. 1/2 turn) and from the left as it wanes; phase is Q32. Its
 * seas (the near side's maria) a shade darker than the lit face */
static void sq_moon(int32_t cx, int32_t cy, int32_t r, uint32_t phase, uint16_t lit, uint16_t dark)
{
    int32_t y, x, hw, k = SQ_SIN[((phase >> 26) + 16u) & 63u];    /* cos(phase), Q8: the terminator's place */
    uint32_t wax = phase < 0x80000000u;
    uint16_t sea = sq_sea(lit);
    for (y = -r; y <= r; y++) {
        for (hw = r; hw > 0 && hw * hw + y * y > r * r + r / 2; hw--) {}
        for (x = -hw; x <= hw; x++) {
            int32_t t = (k * hw) >> 8;              /* terminator at x = t: lit to its right while waxing */
            int on = wax ? x > t : x < -t;
            cv_pset(cx + x, cy + y, !on ? dark : sq_mare(x, y, r) ? sea : lit);
        }
    }
}

/* The clock in every page's header, at its right end (the headers leave x SQ_MOON_X .. 239 to it): a moon going through
 * its phases once a bar, new on the downbeat, full on beat 3. With no clock to follow (the MIDI clock stopped) it hangs
 * full and unlit. Redrawn on its own when the phase moves a 16th, or with force. */
static uint32_t sq_moon_sig;

static void sq_draw_moon(uint32_t force)
{
    uint32_t ph = 0, sig;
    uint8_t live = midi_clock_phase(0u, &ph);
    ph = live ? ph & 0xF0000000u : 0x80000000u;
    sig = sq_mix(sq_mix(2166136261u, ph >> 28 | (uint32_t)live << 4 | (uint32_t)sq_pal << 8), 0x300Du) | 1u;
    if (!force && sig == sq_moon_sig)
        return;
    sq_moon_sig = sig;
    cv_begin(SQ_MOON_W, SQ_MOON_H, SQC(BG));
    sq_moon(SQ_MOON_W / 2, SQ_MOON_H / 2, 7, ph, live ? SQC(THEME) : SQ_STONE[sq_pal], SQC(SURF));
    cv_blit(SQ_MOON_X, 0);
}

static void sq_lintel(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t seed);

/* one step as a stone (7.4): its outline, then a stone inside it growing with the value (0 none, 127 filling it). In the
 * loop window the outline is MID, outside it DIM (7.4: steps outside the line drop their border brightness); the
 * playhead's step is ACCENT. lit: one of the knob window's four (SQLIT_GROUP), brighter, outline and stone a step up;
 * SQLIT_LANE with the cursor on a footer field, its outline paler still. A skipped step is gone (the playhead steps over it), a muted one crumbles, a tie is an
 * empty stone with a capstone laid on it from the stone before (a dolmen: ties in a row share one long capstone). The
 * stone's grain is its lane's and step's own. */
enum { SQLIT_OFF, SQLIT_GROUP, SQLIT_LANE };
static void sq_draw_step(const UIStateMachine *sm, const SequencerLane *ln, uint8_t l, uint8_t i, uint8_t in_loop,
                         uint8_t head, uint8_t lit)
{
    const SequencerStep *s = &ln->steps[i];
    int32_t x = sq_box_x(i), yb = SQ_BOX_Y + SQ_BOX_H, h, w, x0;
    uint32_t seed = (uint32_t)l * 16u + i + 1u;
    uint8_t chord = (uint8_t)(ln->event_type == SEQEV_NOTE && s->note_buffer[1] <= VAL_MAX);
    uint16_t edge = head ? SQC(ACCENT) : !in_loop ? SQC(DIM) : chord ? SQC(THEME) : SQC(MID);
    uint16_t fill = head ? SQC(ACCENT) : in_loop ? SQC(THEME) : SQC(DIM);
    if (lit && !head) {
        edge = !in_loop ? SQC(MID) : lit == SQLIT_LANE ? SQ_PAL_LANE[sq_pal][SQC_TEXT] : SQC(TEXT);
        fill = in_loop ? SQC(TEXT) : SQC(MID);
    }
    if (s->flags & SEQ_FLAG_SKIP)                    /* the playhead steps over it: nothing there */
        return;
    h = ((int32_t)sq_step_value(sm, ln, l, i) * SQ_BOX_H + 63) / 127;
    w = (SQ_BOX_W * (h + SQ_BOX_H) + SQ_BOX_H) / (2 * SQ_BOX_H);
    if (s->flags & SEQ_FLAG_MUTE) {                  /* still there, but crumbling: its outline and its stone */
        sq_stone(x, yb, SQ_BOX_W, SQ_BOX_H, SQ_BOX_H, edge, seed, 1u, 170u);
        sq_stone(x + (SQ_BOX_W - w) / 2, yb, w, h, h, sq_shade(fill, 1u), seed, 0u, 170u);
        return;
    }
    sq_stone(x, yb, SQ_BOX_W, SQ_BOX_H, SQ_BOX_H, edge, seed, 1u, 0u);
    if (sq_step_tied(ln, i)) {                       /* the capstone, from the stone before onto this one */
        x0 = i ? sq_box_x(i - 1u) : x;
        sq_lintel(x0 - 1, SQ_BOX_Y - 1, x + SQ_BOX_W + 1 - (x0 - 1), 3, fill, seed);   /* (resting on the shoulders) */
        return;
    }
    /* the stone grows with the value, whole: 0 .. SQ_BOX_H rows tall, half to all of the outline's width, standing
     * centred on the outline's foot */
    sq_stone(x + (SQ_BOX_W - w) / 2, yb, w, h, h, fill, seed, 0u, 0u);
}

/* the footer field the Select Encoder cursor is on, for lane l (SQF_NONE: none; only the active lane has one) */
static uint8_t sq_cursor_field(const UIStateMachine *sm, uint8_t l)
{
    if (l != sm->s.active_seq_lane || sm->s.current_param_focus >= sizeof SQ_ROW_FIELD)
        return SQF_NONE;
    return SQ_ROW_FIELD[sm->s.current_param_focus];
}

/* a lintel of stone, w x h at canvas (x, y), in c: its ends knocked off, a grain through it, the top lit */
static void sq_lintel(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t seed)
{
    int32_t i, j, in;
    uint32_t n;
    for (j = 0; j < h; j++) {
        in = j == 0 ? 1 + (int32_t)((sq_hash((int32_t)seed, 3) >> 31)) : j == h - 1 ? 1 : 0;
        for (i = in; i < w - in; i++) {
            n = sq_hash(i + (int32_t)seed * 37, j);
            if (j == 0 && (n >> 29) == 0u)
                continue;                            /* a chip out of its top */
            cv_pset(x + i, y + j, sq_shade(c, j == h - 1 ? 2u : (n >> 28) == 0u ? 1u : 0u));
        }
    }
}

/* the loop's grass across x0 .. x1 (c), with the knob group's part (gx0 .. gx1) grown in gc whether it is in the
 * loop or not */
static void sq_grass_split(int32_t x0, int32_t x1, int32_t gx0, int32_t gx1, uint16_t c, uint16_t gc, int32_t salt)
{
    if (x0 < gx0)
        sq_grass(x0, x1 < gx0 ? x1 : gx0, SQ_GRID_H, 5, c, salt, 0, 0, 0, 0, 0);
    if (x1 > gx1)
        sq_grass(x0 > gx1 ? x0 : gx1, x1, SQ_GRID_H, 5, c, salt, 0, 0, 0, 0, 0);
    sq_grass(gx0, gx1, SQ_GRID_H, 5, gc, salt + 1, 0, 0, 0, 0, 0);
}

static void sq_draw_grid(const UIStateMachine *sm, const MaqSeq *q, uint8_t l)
{
    const SequencerLane *ln = &sm->lanes[l];
    uint8_t lo, hi, i, rec = maq_recording(q), pos = maq_lane_pos(q, l), active = (uint8_t)(l == sm->s.active_seq_lane);
    uint8_t g = (uint8_t)(sm->seq_step0 <= UISEQ_STEP0_MAX ? sm->seq_step0 : 0u), lit = SQLIT_OFF;
    int32_t fx = sq_box_x(g) - 2, fw = sq_box_x(g + 3u) + SQ_BOX_W + 2 - fx, x0, x1;
    uint16_t gc = active ? SQC(THEME) : SQC(MID);
    maq_loop_window(ln, &lo, &hi);
    if (active && !rec)                              /* the knob window (7.4): its stones and its grass lit */
        lit = sq_cursor_field(sm, l) == SQF_STEPS ? (uint8_t)SQLIT_GROUP : (uint8_t)SQLIT_LANE;
    cv_begin(240, SQ_GRID_H, SQC(BG));
    for (i = 0; i < SEQ_NSTEP; i++)
        sq_draw_step(sm, ln, l, i, (uint8_t)(i >= lo && i <= hi), (uint8_t)(i == pos),
                     i >= g && i < g + 4u ? lit : (uint8_t)SQLIT_OFF);
    /* the loop range (7.4): grass from the first step's stone to the last step's, along the grid's foot, its blades
     * up round the stones' feet; under the knob window ACCENT (as under the knob cards it ties to), paler with the
     * cursor on a footer field */
    x0 = sq_box_x(lo);
    x1 = sq_box_x(hi) + SQ_BOX_W;
    if (lit)
        sq_grass_split(x0, x1, fx + 1, fx + fw - 1, gc,
                       lit == SQLIT_LANE ? SQ_PAL_LANE[sq_pal][SQC_ACCENT] : SQC(ACCENT), 7 + l * 3);
    else
        sq_grass(x0, x1, SQ_GRID_H, 5, gc, 7 + l * 3, 0, 0, 0, 0, 0);
    if (rec) {                                       /* step record: the record step framed in every lane (2 px top
                                                      * and bottom, 1 px sides: its neighbours are 2 px away) */
        fx = sq_col_x(maq_rec_step(q));
        cv_rect(fx, SQ_FRAME_Y, SQ_STEP_PITCH, 2, SQC(ACCENT));
        cv_rect(fx, SQ_FRAME_Y + SQ_FRAME_H - 2, SQ_STEP_PITCH, 2, SQC(ACCENT));
        cv_rect(fx, SQ_FRAME_Y, 1, SQ_FRAME_H, SQC(ACCENT));
        cv_rect(fx + SQ_STEP_PITCH - 1, SQ_FRAME_Y, 1, SQ_FRAME_H, SQC(ACCENT));
    }
    cv_blit(0, (uint32_t)(SQ_LANE_Y + l * SQ_LANE_H));
}

/* ------------------------------------------------------------ footer --- */
/* TARGET's 4-slot layout (7.4), one small stone per lane from x: picked a solid stone, pickable its outline, not
 * pickable (TRANSPOSE: not a NOTE lane; LAYER: a lane with no layer) a ruin. On the cursor chip the stones take the
 * brighter tones, in the plain palette. */
static void sq_draw_slots(int32_t x, uint8_t on, uint8_t ok, uint8_t sel)
{
    uint32_t i, seed;
    int32_t sx, yb = SQ_SLOT_Y + SQ_SLOT_H;
    uint8_t inv = sq_inv;
    uint16_t c_on, c_ok, c_no;
    if (sel)
        sq_inv = SQINV_OFF;
    c_on = sel ? SQC(ACCENT) : SQC(TEXT);
    c_ok = sel ? SQC(THEME) : SQC(MID);
    c_no = sel ? SQC(MID) : SQC(DIM);
    sq_inv = inv;
    for (i = 0; i < SEQ_NLANE; i++) {
        sx = x + (int32_t)i * SQ_SLOT_PITCH;
        seed = 101u + i;
        if ((on >> i) & 1u)
            sq_stone(sx, yb, SQ_SLOT_W, SQ_SLOT_H, SQ_SLOT_H, (ok >> i) & 1u ? c_on : c_no, seed, 0u, 0u);
        else if ((ok >> i) & 1u)
            sq_stone(sx, yb, SQ_SLOT_W, SQ_SLOT_H, SQ_SLOT_H, c_ok, seed, 1u, 0u);
        else
            sq_stone(sx, yb, SQ_SLOT_W, SQ_SLOT_H, SQ_SLOT_H, c_no, seed, 0u, 170u);
    }
}

/* "g06/n05  Note  [L1]  FOr" (7.4). The field under the Select Encoder cursor sits on a SEL chip in
 * white, on the active lane only. TARGET is the 4 slots for a lane target and is left empty when it offers one value or
 * none (ui_sm_seq_target_count). n = the playhead's step, 1-based ("--" stopped). only_p: just the P: field, in its own
 * small region (the playhead moved, nothing else did). */
static void sq_draw_foot(const UIStateMachine *sm, const MaqSeq *q, uint8_t l, uint8_t only_p)
{
    const SequencerLane *ln = &sm->lanes[l];
    char b[12], tgt[13];
    const char *val[SQF_COUNT];
    uint8_t f, active = (uint8_t)(l == sm->s.active_seq_lane), pos = maq_lane_pos(q, l), ps = ln->prescale, on, ok;
    uint8_t cur = sq_cursor_field(sm, l), last = only_p ? (uint8_t)SQF_P : (uint8_t)(SQF_COUNT - 1);
    uint8_t slots = ui_sm_seq_target_slots(sm, l, &on, &ok), tgt_shown = (uint8_t)(ui_sm_seq_target_count(sm, l) > 1u);
    int32_t y = SQ_CHIP_Y + CAP_IN(S, SQ_CHIP_H), x, lw, ox = only_p ? SQ_POS_X : 0, w = only_p ? SQ_POS_W : 240;
    uint16_t bg, lc, vc;
    if (ps > 99u) ps = 99u;
    b[0] = 'g'; b[1] = (char)('0' + ps / 10u); b[2] = (char)('0' + ps % 10u); b[3] = '/'; b[4] = 'n';
    if (pos < SEQ_NSTEP) { b[5] = (char)('0' + (pos + 1u) / 10u); b[6] = (char)('0' + (pos + 1u) % 10u); }
    else { b[5] = '-'; b[6] = '-'; }
    b[7] = '\0';
    val[SQF_P] = b;
    val[SQF_EV] = ui_sm_seq_ev_name(ln->event_type);
    val[SQF_TGT] = slots ? "" : ui_sm_seq_target_text(sm, l, tgt);
    val[SQF_MD] = ui_sm_seq_mode_name(ln->playback_mode);
    val[SQF_SC] = ui_sm_seq_scale_name(ln->scale);
    val[SQF_RT] = ui_sm_seq_root_name(ln->scale_root);
    cv_begin((uint32_t)w, SQ_FOOT_H, SQC(BG));
    for (f = SQF_P; f <= last; f++) {
        if (f == SQF_TGT && !tgt_shown)
            continue;
        if ((f == SQF_SC || f == SQF_RT) && ln->event_type != SEQEV_NOTE)
            continue;
        if (f == SQF_RT && (ln->scale == SEQSCALE_OFF || ln->scale == SEQSCALE_GENRE))
            continue;
        x = SQF_X[f] - ox;
        bg = SQC(BG);
        lc = SQC(MID);
        vc = active ? SQC(TEXT) : SQC(MID);
        if (f == cur) {                          /* the highlight every page uses: a rounded block of the noise */
            bg = SQ_PAL_LANE[sq_pal][SQC_BG];
            lc = vc = SQC(HI);                       /* the one white thing on the page: the selected text */
            sq_noise_block(x - 1, SQ_CHIP_Y, SQF_W[f] + 2, SQ_CHIP_H, (uint32_t)ox,
                           (uint32_t)(SQ_LANE_Y + l * SQ_LANE_H + SQ_GRID_H), 1u);
        }
        lw = *SQF_LABEL[f] ? sq_text_on(x, y, &AF_S, SQF_LABEL[f], lc, bg) - x + 1 : 0;
        if (f == SQF_TGT && slots)
            sq_draw_slots(x + lw, on, ok, (uint8_t)(f == cur));
        else
            sq_text_fit(x + lw, y, &AF_S, val[f], vc, bg, SQF_W[f] - lw);
    }
    cv_blit((uint32_t)ox, (uint32_t)(SQ_LANE_Y + l * SQ_LANE_H + SQ_GRID_H));
    sq_inv = SQINV_OFF;
}

/* the gaps the regions leave (between header and knobs, under the knobs, between the lanes) and the band under the last
 * lane: background once, on a forced redraw */
static void sq_draw_gaps(void)
{
    uint32_t l;
    lcd_fill(0, SQ_HEAD_Y + SQ_HEAD_H, 240, SQ_KNOB_Y - SQ_HEAD_H, SQC(BG));
    lcd_fill(0, SQ_KNOB_Y + SQ_KNOB_H, 240, SQ_LANE_Y - SQ_KNOB_Y - SQ_KNOB_H, SQC(BG));
    for (l = 0; l + 1u < SEQ_NLANE; l++)
        lcd_fill(0, SQ_LANE_Y + l * SQ_LANE_H + SQ_GRID_H + SQ_FOOT_H, 240, SQ_LANE_GAP, SQC(BG));
#if SQ_LANE_END < 240                                /* (the lanes reach the bottom: no band, and no empty fill) */
    lcd_fill(0, SQ_LANE_END, 240, 240 - SQ_LANE_END, SQC(BG));
#endif
}

/* ------------------------------------------------------------ entry --- */
static void seq_draw(UIStateMachine *sm, const MaqSeq *q, uint32_t force)
{
    static uint8_t pal_drawn = 0xFFu;
    uint32_t h, pos_sig, base = sq_mix(2166136261u, sq_pal);
    uint8_t l, k, lane, st, rec = maq_recording(q), on, ok;
    if (pal_drawn != sq_pal) {                       /* a palette change redraws everything, the gaps too */
        pal_drawn = sq_pal;
        force = 1u;
    }
    if (force)
        sq_draw_gaps();
    h = sq_mix(base, (uint32_t)rec | (uint32_t)maq_playing(q) << 1 | (uint32_t)maq_rec_step(q) << 8 |
                         (uint32_t)sm->perf->int_tempo << 16 | (uint32_t)sm->perf->clock_source << 24);
    if (sq_dirty(SQR_HEAD, h, force))
        sq_draw_head(sm, q);
    h = sq_mix(base, (uint32_t)rec | (uint32_t)sm->seq_step0 << 8 | (uint32_t)sm->s.active_seq_lane << 16 |
                         (uint32_t)maq_rec_step(q) << 24);
    for (k = 0; k < 4u; k++) {
        ui_sm_seq_knob_target(sm, k, &lane, &st);
        h = sq_mix(h, (uint32_t)ui_sm_seq_knob_get(sm, k) | (uint32_t)sm->lanes[lane].event_type << 8 |
                          (uint32_t)sq_knob_head(sm, q, k) << 16);
    }
    if (sq_dirty(SQR_KNOBS, h, force))
        sq_draw_knobs(sm, q);
    for (l = 0; l < SEQ_NLANE; l++) {
        const SequencerLane *ln = &sm->lanes[l];
        uint8_t active = (uint8_t)(l == sm->s.active_seq_lane), cur = sq_cursor_field(sm, l);
        h = sq_mix_bytes(base, (const uint8_t *)ln->steps, sizeof ln->steps);
        h = sq_mix(h, (uint32_t)ln->loop_start | (uint32_t)ln->loop_end << 8 | (uint32_t)ln->event_type << 16 |
                          (uint32_t)maq_lane_pos(q, l) << 24);
        h = sq_mix(h, (uint32_t)active | (uint32_t)rec << 1 | (uint32_t)(cur == SQF_STEPS) << 2 |
                          (uint32_t)maq_rec_step(q) << 8 | (uint32_t)(active ? sm->seq_step0 : 0u) << 16);
        h = sq_mix(h, (uint32_t)ln->scale | (uint32_t)ln->scale_root << 8 |   /* the stones stand as the scale plays */
                          (uint32_t)(sm->arp ? sm->arp->current_genre_bank : 0u) << 16);
        if (sq_dirty(SQR_GRID0 + l, h, force))
            sq_draw_grid(sm, q, l);
        /* the footer without the playhead, then the P: field with it: a step redraws only the small P: region. TARGET
         * reads the other lanes and the performance mode, so its slots and whether it shows are in the signature. */
        ui_sm_seq_target_slots(sm, l, &on, &ok);
        h = sq_mix(base, (uint32_t)ln->event_type | (uint32_t)ln->target_mask << 8 | (uint32_t)ln->playback_mode << 16 |
                             (uint32_t)ln->prescale << 24);
        h = sq_mix(h, (uint32_t)active | (uint32_t)cur << 8 | (uint32_t)on << 16 | (uint32_t)ok << 20 |
                          (uint32_t)(ui_sm_seq_target_count(sm, l) > 1u) << 24);
        h = sq_mix(h, (uint32_t)ln->scale | (uint32_t)ln->scale_root << 8);
        pos_sig = sq_mix(h, maq_lane_pos(q, l));
        if (sq_dirty(SQR_FOOT0 + l, h, force)) {
            sq_draw_foot(sm, q, l, 0u);
            sq_dirty(SQR_POS0 + l, pos_sig, 1u);             /* (drawn with it) */
        } else if (sq_dirty(SQR_POS0 + l, pos_sig, force)) {
            sq_draw_foot(sm, q, l, 1u);
        }
    }
}
