/* The few pieces of Felucca's gfx.c that the synth's screens (ui_seq_draw.c, ui_arp_draw.c, ui_page_draw.c) use and
 * X0X's smaller gfx.c lacks. #include it right after X0X's gfx.c, before the screens.
 *
 * The screens draw every word in their own rune face (ui_seq_draw.c sq_text*), so Felucca's anti-aliased faces are not
 * needed: AF_S / AF_M are kept only as the size tags the screens pass around, with the line metrics of Felucca's
 * Inter Tight S and M (its generated ui_fonts.h) that CAP_IN and SQ_RUNE_TOP place the runes by.
 *
 * cv_frame and cv_rrect are Felucca's (gfx.c, GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
 * Instruments), in its FLAT style (corners always rounded), with the corner blend inlined. */
#pragma once

typedef struct { uint8_t h, asc; } aafont_t;            /* line height, baseline */
static const aafont_t AF_S = {15, 12};
static const aafont_t AF_M = {19, 15};
#define AF_S_CAP_Y 3                                    /* the ink of H: rows 3 .. 11 of the line */
#define AF_S_CAP_H 9
#define AF_M_CAP_Y 4                                    /* rows 4 .. 14 */
#define AF_M_CAP_H 11
#define HALF_UP(v) ((v) >> 1)                           /* half, a pixel left over going left / up (also below 0) */
#define CAP_IN(F, h) (HALF_UP((h) - AF_##F##_CAP_H) - AF_##F##_CAP_Y)

/* the battery as the header's moon shows it (ui_seq_draw.c sq_draw_moon); synth_main_fm1.c reads it */
static int32_t carnac_battery_moon(void);
#define UI_BATTERY_MOON() carnac_battery_moon()

/* a 1 px outline */
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

/* a + (b - a) * a16 / 16 per channel, rounded */
static uint16_t cv_mix16(uint16_t a, uint16_t b, uint32_t a16)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    uint32_t out = 0, k;
    for (k = 0; k < 3u; k++) {
        int32_t x = (a >> SH[k]) & MK[k], d = (((b >> SH[k]) & MK[k]) - x) * (int32_t)a16;
        out |= (uint32_t)(x + (d + (d >= 0 ? 8 : -8)) / 16) << SH[k];
    }
    return (uint16_t)out;
}

/* coverage (0..16) of corner pixel (i, j) of a radius-r corner, (0, 0) the outermost: 4 x 4 samples */
static uint32_t rr_cov(int32_t r, int32_t i, int32_t j)
{
    int32_t sx, sy, n = 0;
    for (sy = 0; sy < 4; sy++)
        for (sx = 0; sx < 4; sx++) {
            int32_t dx = r * 8 - ((i * 4 + sx) * 2 + 1), dy = r * 8 - ((j * 4 + sy) * 2 + 1);   /* 1/8 px */
            n += dx * dx + dy * dy <= r * r * 64;
        }
    return (uint32_t)n;
}

/* a filled rectangle with rounded corners (r <= 8, -r the same), the corners anti-aliased against `under` */
static void cv_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t under)
{
    int32_t i, j;
    if (r < 0) r = -r;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x + r, y, w - 2 * r, h, c);
    cv_rect(x, y + r, r, h - 2 * r, c);
    cv_rect(x + w - r, y + r, r, h - 2 * r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t a = rr_cov(r, i, j);
            uint16_t px = a >= 16u ? c : a == 0u ? under : cv_mix16(under, c, a);
            cv_pset(x + i, y + j, px);
            cv_pset(x + w - 1 - i, y + j, px);
            cv_pset(x + i, y + h - 1 - j, px);
            cv_pset(x + w - 1 - i, y + h - 1 - j, px);
        }
}
