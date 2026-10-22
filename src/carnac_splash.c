/* SPDX-License-Identifier: GPL-3.0-only */
/* CARNAC OS: the start-up screen, in pixel art. A row of granite menhirs runs from a great stone at the bottom left up
 * to the right and away into the mist, towards a setting sun sunk into the horizon where the row vanishes, a dark
 * tree line, a few stars. All in the GLOBAL > THEME's colours on black (SP_PAL is SAMHAIN's orange; sp_theme moves it
 * into the others). The stones as at Carnac: bulging slabs narrowing at the foot, one
 * face lit and one in shadow, fissured, flecked with lichen.
 *
 * Pixel art: the scene is a 120 x 120 grid of 2 x 2 screen pixels, every colour from SP_PAL (no blends), gradients and
 * the mist by ordered (Bayer 4 x 4) dithering between two palette colours, 1-pixel dark outlines.
 *
 * A unity-build fragment: #include it after ui_seq_draw.c (the rune face: sq_rune_glyph, SQ_RUNE_W / _H) and the
 * canvas (cv_begin, cv_pset, cv_blit). It draws the screen in two 120-row bands, one canvas each (the FM1's canvas
 * holds 240 x 144, the host shim's 240 x 124).
 *
 * Boot-safe by construction: it only draws. Integer arithmetic, no tables in RAM (the theme's palette on the stack), no
 * state kept between calls, every pixel through cv_pset (which clips); a few milliseconds.
 *
 *   carnac_splash(version, theme)   the whole screen in UITHEME_* theme's colours (out of range: SAMHAIN); version
 *                                   (e.g. SYNTH_VERSION) small at the foot */

#define SP_A 120                     /* the art grid: SP_A x SP_A pixels of 2 x 2 */
#define SP_HZ 48                     /* the horizon's art row: high, the stones stand up against the sky */
#define SP_VX 94                     /* where the rows vanish into the sun, art x (on the horizon) */
#define SP_BAND 120                  /* screen rows a band */

static int32_t sp_y0;                /* the band being drawn: its first screen row */
static const uint16_t *sp_col;       /* the palette being drawn in, RGB565 (sp_theme's) */

/* ----------------------------------------------------------- palette --- */
enum {
    P_NIGHT, P_INDIGO, P_PURPLE, P_MAUVE, P_EMBER, P_GLOW, P_STAR, P_MOON, P_MOONDK, P_FOG, P_FOGDK, P_TREE, P_FIELD,
    P_FIELDDK, P_GRASS, P_OUTLINE, P_SHADOW, P_MID, P_LIT, P_HILITE, P_LICHEN, P_LICHENLT, P_TXTTOP, P_TXTMID, P_TXTLOW,
    P_GRASSLT, P_COUNT
};
/* the synth's orange on black (ui_seq_draw.c SQ_PAL, ORANGE), as a ramp: nothing but black, browns and oranges */
static const uint8_t SP_PAL[P_COUNT][3] = {
    {10, 5, 0}, {26, 12, 0}, {54, 26, 2}, {100, 46, 6}, {176, 80, 10}, {255, 160, 50}, {255, 200, 110},     /* sky */
    {255, 222, 160}, {80, 38, 6}, {140, 66, 14}, {86, 40, 6}, {22, 10, 0}, {60, 30, 3}, {34, 16, 1},       /* sun, mist */
    {86, 44, 6}, {8, 4, 0}, {34, 15, 1}, {104, 50, 8}, {184, 92, 18}, {244, 146, 50}, {140, 72, 16},        /* field, stone */
    {255, 190, 100}, {255, 214, 150}, {255, 140, 30}, {178, 98, 21}, {150, 80, 14},                         /* title, grass */
};

static int32_t sp_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the THEME's splash: each SP_PAL colour sits, by its brightness, between two of ORANGE's page tones (ui_seq_draw.c
 * SQ_PAL: black, SURF, DIM, MID, THEME, TEXT, ACCENT, and ACCENT half way to white) and takes the same place between
 * the theme's own: ORANGE's levels in another hue, as the page palettes are made. FIRE's run ember to flame, GRANITE's
 * grey. ORANGE itself is SP_PAL as drawn. */
static const uint8_t SP_STOP[] = {SQC_BG, SQC_SURF, SQC_DIM, SQC_MID, SQC_THEME, SQC_TEXT, SQC_ACCENT, SQC_ACCENT};
#define SP_NSTOP ((int32_t)sizeof SP_STOP)

static void sp_stop(uint32_t pal, int32_t i, int32_t c[3])
{
    uint32_t v = SQ_PAL[pal][SP_STOP[i]];
    c[0] = (int32_t)((v >> 11) * 255u / 31u);
    c[1] = (int32_t)(((v >> 5) & 63u) * 255u / 63u);
    c[2] = (int32_t)((v & 31u) * 255u / 31u);
    if (i == SP_NSTOP - 1)                                                          /* the pale one: ACCENT to white */
        c[0] = (c[0] + 255) / 2, c[1] = (c[1] + 255) / 2, c[2] = (c[2] + 255) / 2;
}

static int32_t sp_lum(const int32_t c[3]) { return (77 * c[0] + 150 * c[1] + 29 * c[2]) >> 8; }

static uint16_t sp_565(const int32_t c[3])
{
    return (uint16_t)(((uint32_t)(c[0] >> 3) << 11) | ((uint32_t)(c[1] >> 2) << 5) | (uint32_t)(c[2] >> 3));
}

static void sp_theme(uint32_t theme, uint16_t out[P_COUNT])
{
    uint32_t pal = seq_theme_pal(theme), p;
    for (p = 0; p < P_COUNT; p++) {
        int32_t c[3] = {SP_PAL[p][0], SP_PAL[p][1], SP_PAL[p][2]}, a[3], b[3], la, lb, l = sp_lum(c), i, k;
        if (pal != SQPAL_ORANGE) {
            for (i = 1; i < SP_NSTOP - 1; i++) {                                   /* ORANGE's stops it falls between */
                sp_stop(SQPAL_ORANGE, i, a);
                if (sp_lum(a) >= l)
                    break;
            }
            sp_stop(SQPAL_ORANGE, i - 1, a);
            sp_stop(SQPAL_ORANGE, i, b);
            la = sp_lum(a);
            lb = sp_lum(b);
            l = lb > la ? sp_clamp((l - la) * 256 / (lb - la), 0, 256) : 256;
            sp_stop(pal, i - 1, a);
            sp_stop(pal, i, b);
            for (k = 0; k < 3; k++)
                c[k] = a[k] + (b[k] - a[k]) * l / 256;
        }
        out[p] = sp_565(c);
    }
}

static const uint8_t SP_BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static uint32_t sp_hash(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0xC2B2AE3Du;
    h ^= h >> 13;
    return h;
}


/* a .. b by t256 (0..256), ordered-dithered at art pixel (x, y) */
static uint32_t sp_dith(uint32_t a, uint32_t b, int32_t t256, int32_t x, int32_t y)
{
    return (uint32_t)(sp_clamp(t256, 0, 256) >> 4) > SP_BAYER[y & 3][x & 3] ? b : a;
}

/* one art pixel: a 2 x 2 block, clipped to the band by cv_pset */
static void sp_px(int32_t x, int32_t y, uint32_t p)
{
    uint16_t v = sp_col[p < P_COUNT ? p : P_NIGHT];
    int32_t sy = 2 * y - sp_y0;
    if ((uint32_t)x >= SP_A || sy < -1 || sy >= SP_BAND)
        return;
    cv_pset(2 * x, sy, v);
    cv_pset(2 * x + 1, sy, v);
    cv_pset(2 * x, sy + 1, v);
    cv_pset(2 * x + 1, sy + 1, v);
}

/* ------------------------------------------------------- sky, field --- */
#define SP_SUNR 16                   /* the setting sun: its radius, art px; it sits on the horizon at SP_VX */
static uint32_t sp_sky(int32_t x, int32_t y)
{
    int32_t t = y * 256 / SP_HZ, dx = x - SP_VX, dy = y - (SP_HZ + 3), g, d2 = dx * dx + dy * dy;
    uint32_t p;
    if (t < 110)
        p = sp_dith(P_NIGHT, P_INDIGO, t * 256 / 110, x, y);
    else if (t < 196)
        p = sp_dith(P_INDIGO, P_PURPLE, (t - 110) * 256 / 86, x, y);
    else
        p = sp_dith(P_PURPLE, P_MAUVE, (t - 196) * 256 / 60, x, y);
    if (d2 <= SP_SUNR * SP_SUNR) {                                                  /* the sun, sunk to its middle */
        int32_t v = (dy + SP_SUNR) * 256 / (2 * SP_SUNR);                           /* 0 its top .. 256 its foot */
        if (dy > -11 && (dy + 11) % 4 == 3)                                         /* the cut bands across it */
            return P_EMBER;
        return v < 128 ? sp_dith(P_MOON, P_GLOW, v * 2, x, y) : sp_dith(P_GLOW, P_EMBER, (v - 128) * 2, x, y);
    }
    if (dy < 0 && d2 < 84 * 84) {                                                   /* rays fanning from the sun */
        int32_t q = dx * 24 / -dy + 400;                                            /* the ray's slope, banded */
        if (q % 9 < 2)
            p = sp_dith(p, P_EMBER, 110 - (int32_t)(d2 / 64), x, y);
    }
    g = 256 - (d2 + dy * dy * 3) * 256 / (52 * 52);                                 /* its glow, low and wide */
    if (g > 200)
        p = sp_dith(P_EMBER, P_GLOW, (g - 200) * 3, x, y);
    else if (g > 0)
        p = sp_dith(p, P_EMBER, g * 5 / 4, x, y);
    if (t < 150 && g <= 0 && (sp_hash((uint32_t)x, (uint32_t)y) % 97u) == 0u)      /* stars, away from the glow */
        p = (sp_hash((uint32_t)y, (uint32_t)x) & 1u) ? P_STAR : P_FOG;
    return p;
}

/* the tree line along the horizon, low behind the row's end */
static int32_t sp_trees(int32_t x)
{
    uint32_t a = sp_hash((uint32_t)x / 4u, 11u) & 7u, b = sp_hash((uint32_t)x / 4u + 1u, 11u) & 7u;
    int32_t h = (int32_t)(a * (4u - (uint32_t)x % 4u) + b * ((uint32_t)x % 4u)) / 4 + 2;
    a = (uint32_t)(x > SP_VX ? x - SP_VX : SP_VX - x);
    if (a < 26u)                                                                   /* a clear horizon under the sun */
        h = h * (int32_t)(a > 12u ? a - 12u : 0u) / 14;
    return h;
}

static uint32_t sp_field(int32_t x, int32_t y)
{
    int32_t t = (y - SP_HZ) * 256 / (SP_A - SP_HZ), k, tall = 1 + t / 100;            /* blades: 1 px far .. 3 near */
    uint32_t p = sp_dith(P_FIELD, P_FIELDDK, t * 3 / 4, x, y);
    for (k = 0; k < tall; k++) {                                                   /* a blade rooted k px below? */
        uint32_t h = sp_hash((uint32_t)x, (uint32_t)(y + k) * 3u);
        int32_t len = 1 + (int32_t)((h >> 8) % (uint32_t)tall);
        if (h % (uint32_t)(11 - 2 * tall) == 0u && k < len)
            return k == len - 1 && t > 110 && (h >> 20) % 3u == 0u ? P_GRASSLT : P_GRASS;
    }
    return p;
}

/* mist: a bank on the horizon and drifts low over the field */
static uint32_t sp_mist(int32_t x, int32_t y, uint32_t p)
{
    int32_t d = y - (SP_HZ + 1), w = 0;
    if (d > -9 && d < 16)
        w = d < 0 ? 170 + d * 170 / 9 : 170 - d * 170 / 16;
    if (y > SP_HZ + 18) {                                                          /* low drifts in the foreground */
        int32_t n = (int32_t)(sp_hash((uint32_t)(x + y * 2) / 12u, (uint32_t)y / 5u) & 127u);
        w += n > 104 ? (n - 104) * 3 : 0;
    }
    if (w <= 0)
        return p;
    return sp_dith(p, w > 140 ? P_FOG : P_FOGDK, w > 140 ? (w - 60) : w * 3 / 2, x, y);
}

/* ------------------------------------------------------------ stones --- */
/* the rows: where each would put its stone at depth step 0 (art x, base y), its stones' height then, and the first
 * step drawn. Every row runs to (SP_VX, SP_HZ); listed back to front. */
static const int16_t SP_ROWS[][4] = {{-74, 86, 52, 1}, {-24, 98, 66, 0}, {26, 117, 84, 0}, {86, 150, 96, 4}};
#define SP_NROW ((int32_t)(sizeof SP_ROWS / sizeof SP_ROWS[0]))
#define SP_NSTEP 20

static void sp_stone(int32_t row, int32_t k)
{
    uint32_t h = sp_hash((uint32_t)row * 977u + 5u, (uint32_t)k);
    int32_t s = 256 * 10 / (10 + k * 4);                                           /* perspective scale, 256 = step 0 */
    int32_t bx = SP_VX + (SP_ROWS[row][0] - SP_VX) * s / 256 + (int32_t)(h % 5u) - 2;
    int32_t by = SP_HZ + (SP_ROWS[row][1] - SP_HZ) * s / 256;
    int32_t ph = SP_ROWS[row][2] * s / 256 * (150 + (int32_t)((h >> 4) % 110u)) / 256;
    int32_t pw = ph * (70 + (int32_t)((h >> 11) % 100u)) / 256;                   /* needles .. slabs */
    int32_t crown = 3 + (int32_t)((h >> 18) % 6u), slant = (int32_t)((h >> 21) % 7u) - 3;
    int32_t lean = (int32_t)((h >> 24) % 7u) - 3, crack = (int32_t)((h >> 27) % 7u);
    int32_t fog = 256 - s * 2, j, detail;
    if (k == 0 && row == 2)                                                        /* the great stone in front */
        ph = 87, pw = 30, lean = -1, slant = 1, crack = 4;
    else if ((h & 7u) == 3u || (h & 7u) == 6u || (k > 2 && (h & 7u) == 1u))      /* gaps in the row */
        return;
    if (ph < 2 || bx + pw < 0 || bx - pw > SP_A)
        return;
    if (pw < 1)
        pw = 1;
    detail = pw >= 7;
    for (j = 0; j < ph; j++) {
        int32_t t = j * 256 / ph, u = t * crown > 256 ? 256 : t * crown, q = 2 * t - 256, x, xl, xr;
        int32_t prof = 256 - (256 - u) * (256 - u) / 256;                          /* rounded crown */
        int32_t y = by - ph + j, hw;
        prof = prof * (216 + 40 - 40 * q * q / 65536) / 256;                       /* bulging in the middle */
        if (t > 210)
            prof = prof * (256 - (t - 210)) / 256;                                 /* and narrowing at the foot */
        hw = (pw * prof / 256) / 2 + (detail ? (int32_t)(sp_hash(h, (uint32_t)j / 2u) % 3u) - 1 : 0);
        xl = bx + lean * (256 - t) * pw / 1024 - hw;
        xr = xl + 2 * hw;
        if (2 * y + 1 < sp_y0 || 2 * y >= sp_y0 + SP_BAND)
            continue;
        for (x = xl; x <= xr; x++) {
            int32_t rel = (x - xl) * 256 / (xr - xl + 1), cx = (xl + xr) / 2 + crack - 3;
            if (bx < SP_VX)                                                        /* lit on the face turned to the sun */
                rel = 255 - rel;
            uint32_t p, g = sp_hash((uint32_t)x * 7u + h, (uint32_t)y);
            if (slant && j * 3 < slant * (x - (xl + xr) / 2) * ph / (pw + 1))      /* above a broken crown */
                continue;
            if (detail && (x == xl || x == xr || j == 0 ||
                           (slant && (j - 1) * 3 < slant * (x - (xl + xr) / 2) * ph / (pw + 1))))
                p = P_OUTLINE;                                                    /* the outline */
            else if (rel < 18)                                                     /* backlit: a rim towards the sun */
                p = sp_dith(P_HILITE, P_LIT, rel * 14, x, y);
            else if (rel < 50)
                p = sp_dith(P_LIT, P_MID, (rel - 18) * 8, x, y);
            else if (rel < 120)
                p = sp_dith(P_MID, P_SHADOW, (rel - 50) * 256 / 70, x, y);
            else
                p = P_SHADOW;                                                      /* the rest in its own shadow, flat */
            if (detail && p != P_OUTLINE) {
                int32_t w = (int32_t)(sp_hash(h, (uint32_t)j / 3u) % 3u) - 1;
                if (pw >= 14 && x == cx + w && t > 150 - crack * 8)                /* a fissure, on the big ones */
                    p = P_OUTLINE;
                else if (rel < 120 && g % 29u == 0u)                               /* lichen */
                    p = P_LICHEN;
            }
            if (fog > 64)                                                          /* the far ones go into the mist */
                p = fog > 190 ? (rel < 50 ? P_FOG : P_FOGDK) : sp_dith(p, rel < 50 ? P_FOG : P_FOGDK, (fog - 64) * 2, x, y);
            sp_px(x, y, p);
        }
    }
    if (detail) {                                                                  /* grass about its foot */
        int32_t x;
        for (x = bx - pw / 2 - 1; x <= bx + pw / 2 + 1; x++)
            if (sp_hash((uint32_t)x, h) % 3u == 0u)
                sp_px(x, by - 1 - (int32_t)(sp_hash(h, (uint32_t)x) & 1u), P_GRASS);
    }
}

/* ------------------------------------------------------------- title --- */
/* s in the rune face, a rune pixel = scale x scale art pixels: a dark outline and a drop, then the ember fill */
static void sp_runes(int32_t x, int32_t y, const char *s, int32_t scale, int32_t track, uint32_t a, uint32_t b,
                     uint32_t c)
{
    int32_t pass, i, j, u, k, x0, h = SQ_RUNE_H * scale;
    for (pass = 0; pass < 2; pass++)
        for (x0 = x, i = 0; s[i]; i++, x0 += (SQ_RUNE_W + 1 + track) * scale) {
            const uint8_t *g = sq_rune_glyph(s[i]);
            for (j = 0; j < SQ_RUNE_H; j++)
                for (u = 0; u < SQ_RUNE_W; u++) {
                    if (!((g[j] >> (SQ_RUNE_W - 1 - u)) & 1u))
                        continue;
                    for (k = 0; k < scale * scale; k++) {
                        int32_t px = x0 + u * scale + k % scale, py = y + j * scale + k / scale, t;
                        if (pass == 0) {
                            sp_px(px - 1, py, P_OUTLINE);
                            sp_px(px + 1, py, P_OUTLINE);
                            sp_px(px, py - 1, P_OUTLINE);
                            sp_px(px + 1, py + 1, P_OUTLINE);
                            sp_px(px, py + 1, P_OUTLINE);
                            continue;
                        }
                        t = (py - y) * 256 / h;
                        sp_px(px, py, t < 128 ? sp_dith(a, b, t * 2, px, py) : sp_dith(b, c, (t - 128) * 2, px, py));
                    }
                }
        }
}

static int32_t sp_runes_w(const char *s, int32_t scale, int32_t track)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n ? (n * (SQ_RUNE_W + 1 + track) - 1 - track) * scale : 0;
}

static void carnac_splash(const char *version, uint32_t theme)
{
    int32_t band, x, y, k, r;
    uint16_t col[P_COUNT];
    sp_theme(theme, col);
    sp_col = col;
    for (band = 0; band < 2 * SP_A; band += SP_BAND) {
        sp_y0 = band;
        cv_begin(240, SP_BAND, 0);
        for (y = band / 2; y < (band + SP_BAND) / 2; y++)
            for (x = 0; x < SP_A; x++) {
                uint32_t p;
                if (y < SP_HZ)
                    p = y >= SP_HZ - sp_trees(x) ? P_TREE : sp_sky(x, y);
                else
                    p = sp_field(x, y);
                sp_px(x, y, sp_mist(x, y, p));
            }
        /* the name across the sky, behind the stones: the great one stands in front of its foot */
        sp_runes((SP_A - sp_runes_w("CARNAC", 3, 0)) / 2, 5, "CARNAC", 3, 0, P_TXTTOP, P_TXTMID, P_TXTLOW);
        for (k = SP_NSTEP - 1; k >= 0; k--)                                       /* far to near */
            for (r = 0; r < SP_NROW; r++)
                if (k >= SP_ROWS[r][3])
                    sp_stone(r, k);
        sp_runes((SP_A - sp_runes_w("OS", 2, 1)) / 2, 35, "OS", 2, 1, P_TXTTOP, P_TXTMID, P_TXTLOW);
        if (version)                                                               /* the foot: version, author */
            sp_runes(3, SP_A - 11, version, 1, 1, P_TXTTOP, P_TXTMID, P_TXTMID);
        sp_runes(SP_A - 3 - sp_runes_w("BY ORBVORGIS", 1, 0), SP_A - 11, "BY ORBVORGIS", 1, 0, P_TXTTOP, P_TXTMID,
                 P_TXTMID);
        cv_blit(0, (uint32_t)band);
    }
    sp_col = 0;
}
