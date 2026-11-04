/* The REC page (2026-11-04): the scratch-pad loop (dsp/krec.h) as a field of standing stones, a page of its own like
 * the SEQ page. A unity-build fragment: ui_page_draw.c includes it at its end (its palette, stones, grass, runes, the
 * knob cards and the confirm slab are theirs).
 *
 *   rows   0 .. 19   the header: REC, its stone (lit recording, flashing armed), the state, the layer, the tempo
 *         21 .. 52   the knob cards: VELOCITY, MOD WHL (LAYER's own), GATE, DENSITY; their grass lit overdubbing
 *         55 .. 193  the field: time left to right (the bars as way-markers on the horizon), pitch as depth (the low
 *                    notes near and big, the high far, small and hazed), velocity as height; layer 1's stones in the
 *                    theme colour, layer 2's cream; the older passes darker, the stones sounding lit; the notes DENSITY
 *                    thins out only outlined; the loop's length of grass along the front; the playhead a line of light
 *        195 .. 222  the ridges: the mod wheel lane (a hill line over the envelope page's speckle) and the bend
 *        225 .. 239  the footer, the Select Encoder's: the place, TRANSPOSE, QUANTIZE, the notes used, DOUBLE, CLEAR
 *
 * Redraw (the screen keeps what it has): the field whole only when the loop changes (a note, a pass, TRANSPOSE,
 * DENSITY, DOUBLE, CLEAR, a popup); otherwise the strips the playhead left and reached, the stones that began or
 * stopped sounding, and the grass band. The ridges and the footer redraw when their values move. */

#include "dsp/dsp_cold.h"
#if defined(__clang__)                          /* all of it for size (a UI frame's, not the sound's) */
#pragma clang attribute push(__attribute__((minsize)), apply_to = function)
#endif

#define RD_FY 55
#define RD_FH 139
#define RD_FX 4
#define RD_FW 232
#define RD_GRASS_H 9                 /* the field's foot, redrawn whole with the playhead (the grass hangs off canvas x) */
#define RD_BACK 20                   /* the row the farthest stones stand on */
#define RD_FRONT (RD_FH - 5)         /* .. and the nearest */
#define RD_RY 195
#define RD_RH 28
#define RD_TY 225
#define RD_TH 15

static uint32_t rd_sig_head, rd_sig_knob[4], rd_sig_field, rd_sig_ridge, rd_sig_foot;
static int32_t rd_ph = -1;           /* the playhead's x drawn last */
static uint8_t rd_sound[KREC_NOTES / 8u];
static uint8_t rd_order[KREC_NOTES]; /* the notes far to near (high to low), worked out with the field */
static int32_t rd_lo, rd_span, rd_total;
static uint32_t rd_frames;            /* frames drawn: the blinks' clock */
static uint8_t rd_pal = 0xFFu;

static uint32_t rd_total_ticks(const Krec *k, uint32_t pos)
{
    uint32_t t = KREC_BAR;
    if (k->len)
        return k->len;
    while (t < KREC_MAX && t <= pos) t *= 2u;        /* the first pass: the field doubles as it grows */
    return t;
}

static uint8_t rd_layer2(const UIStateMachine *sm, const KrecNote *n)
{
    uint8_t c1 = sm->perf->midi_ch[0] & 0x0Fu, c2 = sm->perf->midi_ch[1] & 0x0Fu;
    if (sm->perf->perf_mode == PERF_SPLIT_SYNTH)
        return (uint8_t)(n->note >= sm->perf->split_note);
    return (uint8_t)(sm->perf->perf_mode == PERF_DUAL_SYNTH && c1 != c2 && (n->chp & 0x0Fu) == c2);
}

/* a note's stone: its x, width, foot and height in field rows, and its colour */
typedef struct { int32_t x, w, yb, h; uint16_t c; uint8_t edge, lit; } RdStone;

static uint8_t rd_sounding(const Krec *k, const KrecNote *n, uint32_t pos)
{
    uint32_t len = n->len, g = k->set.gate, d;
    if (!k->playing || !k->len || n->len == KREC_LEN_OPEN || !krec_note_kept(k, n))
        return 0;
    len = g >= 64u ? len + ((len * (g - 64u)) >> 6) : (len * g) >> 6;
    if (len < 1u) len = 1u;
    d = (pos + k->len - n->t) % k->len;
    return (uint8_t)(d < len);
}

static void rd_stone_of(const UIStateMachine *sm, const Krec *k, uint32_t i, uint8_t lit, RdStone *s)
{
    const KrecNote *n = &k->n[i];
    int32_t note = (int32_t)n->note + (int32_t)k->set.transpose - 64, depth;
    uint32_t len = n->len == KREC_LEN_OPEN ? KREC_TPB / 4u : n->len;
    uint8_t l2 = rd_layer2(sm, n), cur = (uint8_t)(k->state != KREC_ODUB || (n->chp >> 4) == k->pass);
    depth = ((note - rd_lo) * 256) / rd_span;
    if (depth < 0) depth = 0;
    if (depth > 256) depth = 256;
    s->x = RD_FX + (int32_t)((uint32_t)n->t * RD_FW / (uint32_t)rd_total);
    s->w = (int32_t)(len * RD_FW / (uint32_t)rd_total);
    if (s->w < 6) s->w = 6;
    s->yb = RD_FRONT - (RD_FRONT - RD_BACK) * depth / 256;
    s->h = (10 + n->vel / 8) * (384 - depth) / 384;
    s->c = l2 ? SQC(ACCENT) : SQC(THEME);
    if (!cur) s->c = l2 ? sq_shade(SQC(ACCENT), 2u) : SQC(MID);   /* an older pass */
    if (depth > 170 && !lit) s->c = sq_shade(s->c, 1u);          /* the far ones in the haze */
    s->edge = (uint8_t)!krec_note_kept(k, n);
    s->lit = lit;
}

/* the field's notes far to near, and its pitch range (with the loop's) */
static void rd_order_notes(const Krec *k)
{
    uint32_t i, j, n = k->nn;
    int32_t lo = 127, hi = 0;
    for (i = 0; i < n; i++) {
        uint8_t v = k->n[i].note;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        rd_order[i] = (uint8_t)i;
        for (j = i; j > 0u && k->n[rd_order[j - 1u]].note < v; j--)   /* (insertion: highest first) */
            rd_order[j] = rd_order[j - 1u];
        rd_order[j] = (uint8_t)i;
    }
    if (n == 0u) lo = hi = 60;
    rd_lo = lo;
    rd_span = hi - lo < 12 ? 12 : hi - lo;
}

/* everything of the field inside the canvas begun at field (cx, cy): the plain, the horizon's markers, the stones,
 * the grass (grass: the canvas starts at x 0), the playhead at ph (-1: none) */
static void rd_paint(const UIStateMachine *sm, const Krec *k, int32_t cx, int32_t cy, int32_t cw, int32_t ch, int grass,
                     int32_t ph, uint32_t pos)
{
    int32_t x, y, i, bars = (int32_t)(rd_total / KREC_BAR);
    for (y = cy < RD_BACK - 10 ? RD_BACK - 10 : cy; y < cy + ch && y < RD_FRONT; y++)   /* the plain's specks */
        for (x = cx < RD_FX ? RD_FX : cx; x < cx + cw && x < RD_FX + RD_FW; x++)
            if ((sq_hash(x, y) >> 24) < (uint32_t)(2 + (y * 10) / RD_FH))
                cv_pset(x - cx, y - cy, PUC_STONE);
    if (bars < 1) bars = 1;
    for (i = 0; i <= bars && cy < 10; i++) {         /* the bars: way-marker stones on the horizon, taller every 4 */
        x = RD_FX + i * RD_FW / bars;
        if (x > RD_FX + RD_FW - 3) x = RD_FX + RD_FW - 3;
        if (x + 3 >= cx && x - 1 < cx + cw)
            sq_stone(x - 1 - cx, 9 - cy, 3, i % 4 ? 3 : 7, i % 4 ? 3 : 7, PUC_STONE, (uint32_t)i, 0u, 0u);
    }
    if (k->nn == 0u) {
        if (cy < 90 && cy + ch > 40) {
            const char *a = k->state == KREC_ARMED ? "PLAY TO RAISE" : "REC TO ARM";
            sq_rune_in(-cx, 50 - cy, 240, a, SQC(MID), 2);
            if (k->state == KREC_ARMED)
                sq_rune_in(-cx, 74 - cy, 240, "THE STONES", SQC(MID), 2);
        }
    } else {
        for (i = 0; i < (int32_t)k->nn; i++) {      /* far to near: each drawn over the ones behind it */
            RdStone s;
            uint32_t n = rd_order[i];
            uint8_t lit = (uint8_t)((rd_sound[n >> 3] >> (n & 7u)) & 1u);
            rd_stone_of(sm, k, n, lit, &s);
            if (s.x + s.w + 3 < cx || s.x - 3 >= cx + cw || s.yb + 2 < cy || s.yb - s.h - 3 >= cy + ch)
                continue;
            if (lit)                                 /* the aura */
                sq_stone(s.x - 2 - cx, s.yb + 1 - cy, s.w + 3, s.h + 4, s.h + 4, sq_shade(SQC(ACCENT), 1u),
                         (uint32_t)n * 7u + 1u, 1u, 0u);
            sq_stone(s.x - cx, s.yb - cy, s.w - 1, s.h, s.h, s.edge ? SQC(DIM) : lit ? SQC(TEXT) : s.c,
                     (uint32_t)k->n[n].t * 7u + k->n[n].note * 131u + 5u, s.edge, 0u);
        }
    }
    if (grass && k->len) {                           /* the loop's length of grass */
        x = RD_FX + (int32_t)((uint32_t)k->len * RD_FW / (uint32_t)rd_total);
        sq_grass(RD_FX, x, RD_FH - cy, 3, k->state == KREC_ODUB ? SQC(THEME) : SQC(MID), 11, 0, 0, 0, 0, 0);
    } else if (grass) {
        for (x = RD_FX; x < RD_FX + RD_FW; x += 2) cv_pset(x, RD_FRONT + 2 - cy, PUC_STONE);
    }
    if (ph >= cx && ph < cx + cw)                    /* the playhead: a line of light across the plain */
        for (y = cy < 4 ? 4 : cy; y < cy + ch && y < RD_FH - 2; y++)
            if (y > RD_FH - 16 || (y % 3) == 0)
                cv_pset(ph - cx, y - cy, y > RD_FH - 8 ? SQC(HI) : SQC(ACCENT));
    (void)pos;
}

static void rd_field_rect(const UIStateMachine *sm, const Krec *k, int32_t x0, int32_t x1, int32_t ph, uint32_t pos)
{
    if (x0 < 0) x0 = 0;
    if (x1 > 240) x1 = 240;
    if (x1 <= x0) return;
    cv_begin((uint32_t)(x1 - x0), RD_FH - RD_GRASS_H, SQC(BG));
    rd_paint(sm, k, x0, 0, x1 - x0, RD_FH - RD_GRASS_H, 0, ph, pos);
    cv_blit((uint32_t)x0, RD_FY);
}

static void rd_grass_band(const UIStateMachine *sm, const Krec *k, int32_t ph, uint32_t pos)
{
    cv_begin(240, RD_GRASS_H, SQC(BG));
    rd_paint(sm, k, 0, RD_FH - RD_GRASS_H, 240, RD_GRASS_H, 1, ph, pos);
    cv_blit(0, RD_FY + RD_FH - RD_GRASS_H);
}

static void rd_field_whole(const UIStateMachine *sm, const Krec *k, int32_t ph, uint32_t pos)
{
    const int32_t h0 = 70;
    cv_begin(240, (uint32_t)h0, SQC(BG));
    rd_paint(sm, k, 0, 0, 240, h0, 0, ph, pos);
    cv_blit(0, RD_FY);
    cv_begin(240, (uint32_t)(RD_FH - RD_GRASS_H - h0), SQC(BG));
    rd_paint(sm, k, 0, h0, 240, RD_FH - RD_GRASS_H - h0, 0, ph, pos);
    cv_blit(0, (uint32_t)(RD_FY + h0));
    rd_grass_band(sm, k, ph, pos);
}

/* ------------------------------------------------------------- the rest --- */
static void rd_head(const UIStateMachine *sm, const Krec *k)
{
    static const char *const ST[] = {"OFF", "ARMED", "REC", "PLAY", "ODUB"};
    int32_t y = HALF_UP(SQ_HEAD_H - SQ_RUNE_H) - 1, x;
    uint8_t on = (uint8_t)(k->state == KREC_REC || k->state == KREC_ODUB ||
                           (k->state == KREC_ARMED && ((rd_frames >> 3) & 1u)));
    char b[8];
    cv_begin(SQ_MOON_X, SQ_HEAD_H, SQC(BG));
    x = sq_rune(6, y, "REC", SQC(THEME), 0) + 6;
    sq_stone(x, y + 9, 7, 8, 8, on ? SQC(THEME) : PUC_STONE, 77u, 0u, 0u);
    x = sq_rune(x + 11, y, k->state == KREC_PLAY && !k->playing ? "STOP" : ST[k->state < 5u ? k->state : 0u],
                SQC(TEXT), 0) + 8;
    x = sq_rune(x, y, sm->s.active_synth_layer ? "L2" : "L1", SQC(MID), 0) + 8;
    if (k->full && ((rd_frames >> 3) & 1u))
        sq_rune(x, y, "FULL", SQC(ACCENT), 0);
    if (midi_clock_is_external())
        sq_text_r(SQ_HEAD_R, y, &AF_S, "MIDI CLK", SQC(MID), SQC(BG));
    else {
        uint32_t bpm = MIDI_CLOCK_BPM_BASE + sm->perf->int_tempo;
        pu_cat(pu_num(b, (int32_t)bpm, 0u, 1u), " BPM");
        sq_text_r(SQ_HEAD_R, y, &AF_S, b, SQC(MID), SQC(BG));
    }
    cv_blit(0, 0);
}

static void rd_knob_text(const Krec *k, const UIStateMachine *sm, uint32_t i, char *b)
{
    uint32_t v;
    if (i == 0u) { pu_num(b, sm->perf_knob[UIPERF_VELOCITY], 0u, 1u); return; }
    if (i == 1u) { pu_num(b, sm->perf_knob[UIPERF_MODWHEEL], 0u, 1u); return; }
    v = i == 2u ? (uint32_t)k->set.gate * 100u / 64u : (uint32_t)k->set.density * 100u / 127u;
    if (i == 3u && k->set.density >= 127u) { pu_cat(b, "ALL"); return; }
    pu_cat(pu_num(b, (int32_t)v, 0u, 1u), "%");
}

static void rd_knobs(const UIStateMachine *sm, const Krec *k, uint32_t force)
{
    static const char *const L[4] = {"VELOCITY", "MOD WHL", "GATE", "DENSITY"};
    uint32_t i;
    char b[12];
    uint8_t live = (uint8_t)(k->state == KREC_REC || k->state == KREC_ODUB);
    for (i = 0; i < 4u; i++) {
        uint32_t h;
        b[0] = 0;
        rd_knob_text(k, sm, i, b);
        h = pu_mix_str(sq_mix(sq_pal, live | i << 4), b);
        if (!force && h == rd_sig_knob[i]) continue;
        rd_sig_knob[i] = h;
        {
            int32_t x = SQ_KNOB_X0 + (int32_t)i * (SQ_KNOB_W + SQ_KNOB_GAP);
            int32_t c0 = i ? x - SQ_KNOB_GAP : 0, c1 = i < 3u ? x + SQ_KNOB_W : 240;
            cv_begin((uint32_t)(c1 - c0), SQ_KNOB_H, SQC(BG));
            sq_knob_tablet(x - c0, x, SQ_KNOB_H - 3, i, SQ_KNOB_Y, L[i], live ? SQC(THEME) : SQC(MID), 0u);
            sq_text_in(x - c0, 14, SQ_KNOB_W, &AF_S, b, SQC(TEXT), SQC(SURF));
            cv_blit((uint32_t)c0, SQ_KNOB_Y);
        }
    }
}

static void rd_ridges(const Krec *k, int32_t ph)
{
    int32_t x, v, b, mid = 21, w = RD_FW - 28;
    cv_begin(240, RD_RH, SQC(BG));
    sq_text_in(2, 2, 26, &AF_S, "WHL", SQC(DIM), SQC(BG));
    sq_text_in(2, 15, 26, &AF_S, "BND", SQC(DIM), SQC(BG));
    for (x = 0; x < w; x++) {
        uint32_t t = (uint32_t)x * (uint32_t)rd_total / (uint32_t)w, X = (uint32_t)(RD_FX + 28 + x);
        uint8_t wv = k->wheel[t / KREC_WHEEL_T < KREC_WHEEL_N ? t / KREC_WHEEL_T : 0u];
        uint8_t bv = k->bend[t / KREC_BEND_T < KREC_BEND_N ? t / KREC_BEND_T : 0u];
        v = wv == KREC_LANE_NONE ? 0 : wv * 11 / 127;    /* the wheel: a hill line over the envelope page's speckle */
        pu_fog_col((int32_t)X, 12 - v + 2, 13, 7u, PUC_STONE, RD_RY);
        if (wv != KREC_LANE_NONE && v) cv_pset((int32_t)X, 12 - v, SQC(THEME));
        else if (!(x & 3)) cv_pset((int32_t)X, 12, PUC_STONE);
        b = bv == KREC_LANE_NONE ? 0 : ((int32_t)bv - 64) * 6 / 64;   /* the bend: about its rest */
        if (bv != KREC_LANE_NONE && b) cv_pset((int32_t)X, mid - b, SQC(ACCENT));
        else if (!(x & 3)) cv_pset((int32_t)X, mid, PUC_STONE);
    }
    if (ph >= 0) {
        x = RD_FX + 28 + (ph - RD_FX) * w / RD_FW;
        cv_rect(x, 0, 1, RD_RH - 2, SQC(MID));
    }
    cv_blit(0, RD_RY);
}

static void rd_footer(const UIStateMachine *sm, const Krec *k, uint32_t pos)
{
    static const int16_t FX[6] = {2, 50, 88, 126, 168, 204};
    static const int16_t FW[6] = {44, 34, 34, 38, 32, 34};
    static const uint8_t ROW_FIELD[UIREC_NROWS] = {1, 2, 4, 5};
    char f[6][12];
    uint32_t i, cur = 99u;
    uint8_t dbl = krec_can_double(k), clr = (uint8_t)(k->state != KREC_EMPTY);
    if (sm->s.current_param_focus < UIREC_NROWS) cur = ROW_FIELD[sm->s.current_param_focus];
    for (i = 0; i < 6u; i++) f[i][0] = 0;
    if (k->state >= KREC_REC) {                      /* bar.beat / bars */
        char *e = pu_num(f[0], (int32_t)(pos / KREC_BAR + 1u), 0u, 1u);
        e = pu_cat(e, ".");
        e = pu_num(e, (int32_t)(pos % KREC_BAR / KREC_TPB + 1u), 0u, 1u);
        if (k->len) pu_num(pu_cat(e, "/"), (int32_t)((k->len + KREC_BAR - 1u) / KREC_BAR), 0u, 1u);
    } else {
        pu_cat(f[0], "-");
    }
    pu_cat(pu_cat(f[1], "TR "), ui_sm_row_text(sm, 0u));
    pu_cat(pu_cat(f[2], "Q "), ui_sm_row_text(sm, 1u));
    pu_cat(pu_num(f[3], k->nn, 0u, 1u), "/256");
    pu_cat(f[4], "DBL");
    pu_cat(f[5], "CLR");
    cv_begin(240, RD_TH, SQC(BG));
    for (i = 0; i < 6u; i++) {
        uint16_t c = i == cur ? SQC(HI) : (i == 4u && !dbl) || (i == 5u && !clr) ? SQC(DIM) : SQC(MID);
        if (i == cur) sq_noise_block(FX[i], 1, FW[i], 13, 0u, RD_TY, 1u);
        sq_text_in(FX[i], 2, FW[i], &AF_S, f[i], c, i == cur ? SQC(SEL) : SQC(BG));
    }
    cv_blit(0, RD_TY);
}

/* DOUBLE? / CLEAR? over the field, and the words UNDO leaves */
static void rd_popup(const UIStateMachine *sm, const Krec *k)
{
    char s[24];
    int32_t i, y0 = 90;
    cv_begin(PU_DLG_W, PU_DLG_H, SQC(BG));
    pu_slab(PU_DLG_W, PU_DLG_H, PU_DLG_X, y0);
    if (sm->rec_ask) {
        uint32_t L = (k->len + KREC_BAR - 1u) / KREC_BAR;
        s[0] = 0;
        if (sm->rec_ask == 1u)
            pu_cat(pu_num(pu_cat(pu_num(s, (int32_t)L, 0u, 1u), " BARS > "), (int32_t)(2u * L), 0u, 1u), " BARS");
        else
            pu_cat(pu_cat(pu_num(s, k->nn, 0u, 1u), " NOTES"), "");
        sq_rune_in(0, 13, PU_DLG_W, sm->rec_ask == 1u ? "DOUBLE?" : "CLEAR?", SQC(THEME), 2);
        sq_rune_in(0, 28, PU_DLG_W, s, SQC(MID), 0);
        for (i = 0; i < 2; i++) {
            int32_t x = 30 + i * 66;
            uint8_t on = (uint8_t)(sm->dialog_choice == (uint8_t)i);
            if (on) pu_trough(x, 44, 54, 15, PU_DLG_X, y0);
            sq_rune_in(x, 47, 54, i ? "YES" : "NO", on ? SQC(TEXT) : SQC(DIM), 2);
        }
    } else {
        sq_rune_in(0, 26, PU_DLG_W, sm->rec_msg == 1u ? "UNDONE" : "NOTHING TO UNDO", SQC(THEME), 2);
    }
    cv_blit(PU_DLG_X, (uint32_t)y0);
}

SYNTH_COLD static void rec_draw(UIStateMachine *sm, uint32_t force)
{
    static uint8_t popped;
    Krec *k = sm->rec;
    uint32_t pos, h, i;
    int32_t ph = -1;
    uint8_t pop;
    if (k == 0)
        return;
    rd_frames++;
    if (rd_pal != sq_pal) { rd_pal = sq_pal; force = 1u; }
    if (force) lcd_fill(0, 0, 240, 240, SQC(BG));
    pos = krec_pos(k, k->frame_now);
    rd_total = (int32_t)rd_total_ticks(k, pos);
    if (k->state == KREC_REC || (k->playing && k->len))
        ph = RD_FX + (int32_t)(pos * RD_FW / (uint32_t)rd_total);
    h = sq_mix(sq_pal, k->state | (uint32_t)k->playing << 4 | (uint32_t)k->full << 5 | (uint32_t)sm->s.active_synth_layer << 6 |
                       (uint32_t)sm->perf->int_tempo << 8 | (uint32_t)midi_clock_is_external() << 16 |
                       ((rd_frames >> 3) & 1u) << 17);
    if (force || h != rd_sig_head) { rd_sig_head = h; rd_head(sm, k); }
    rd_knobs(sm, k, force);
    /* the field: whole when the loop changed */
    pop = (uint8_t)((sm->dialog == UIDLG_CONFIRM && sm->rec_ask) || sm->rec_msg);
    h = sq_mix_bytes(sq_mix(sq_pal, k->nn | (uint32_t)k->len << 9 | (uint32_t)k->pass << 25 | (uint32_t)pop << 30),
                     (const uint8_t *)&k->set, sizeof k->set);
    h = sq_mix(h, (uint32_t)rd_total | (uint32_t)k->state << 16 | (uint32_t)(k->state != KREC_PLAY) << 20 |
                  (uint32_t)sm->perf->perf_mode << 21);
    if (k->nn) {                                     /* (every note's start, length: an overdub's note-off, an UNDO) */
        uint32_t a = 0;
        for (i = 0; i < k->nn; i++) a = a * 31u + k->n[i].t + ((uint32_t)k->n[i].len << 13) + k->n[i].note;
        h = sq_mix(h, a);
    }
    if (force || h != rd_sig_field) {
        rd_sig_field = h;
        rd_order_notes(k);
        for (i = 0; i < k->nn; i++)
            if (rd_sounding(k, &k->n[i], pos)) rd_sound[i >> 3] |= (uint8_t)(1u << (i & 7u));
            else rd_sound[i >> 3] &= (uint8_t)~(1u << (i & 7u));
        rd_field_whole(sm, k, ph, pos);
        rd_ph = ph;
        popped = 0;
    } else {
        /* the stones that began or stopped sounding, then the playhead's strips */
        for (i = 0; i < k->nn; i++) {
            uint8_t now = rd_sounding(k, &k->n[i], pos), was = (uint8_t)((rd_sound[i >> 3] >> (i & 7u)) & 1u);
            if (now != was) {
                RdStone s;
                if (now) rd_sound[i >> 3] |= (uint8_t)(1u << (i & 7u));
                else rd_sound[i >> 3] &= (uint8_t)~(1u << (i & 7u));
                rd_stone_of(sm, k, i, now, &s);
                if (!pop) rd_field_rect(sm, k, s.x - 4, s.x + s.w + 4, ph, pos);
            }
        }
        if (ph != rd_ph && !pop) {
            if (rd_ph >= 0) rd_field_rect(sm, k, rd_ph - 1, rd_ph + 2, ph, pos);
            if (ph >= 0) rd_field_rect(sm, k, ph - 1, ph + 2, ph, pos);
            rd_grass_band(sm, k, ph, pos);
        }
        rd_ph = ph;
    }
    if (pop && (!popped || sm->dialog_choice != (popped >> 1))) {
        rd_popup(sm, k);
        popped = (uint8_t)(1u | sm->dialog_choice << 1);
    }
    /* the ridges and the footer */
    h = sq_mix_bytes(sq_mix(sq_pal, (uint32_t)(ph + 1) | (uint32_t)rd_total << 9), k->wheel, 32);
    h = sq_mix(h, k->touch[0] | k->touch[1] << 1);
    if (k->touch[0] || k->touch[1] || force || h != rd_sig_ridge) { rd_sig_ridge = h; rd_ridges(k, ph); }
    h = sq_mix(sq_mix_bytes(sq_mix(sq_pal, pos / KREC_TPB | (uint32_t)k->nn << 16), (const uint8_t *)&k->set, sizeof k->set),
               (uint32_t)sm->s.current_param_focus | (uint32_t)krec_can_double(k) << 8 | (uint32_t)k->state << 9 |
               (uint32_t)k->len << 12);
    if (force || h != rd_sig_foot) { rd_sig_foot = h; rd_footer(sm, k, pos); }
}

#if defined(__clang__)
#pragma clang attribute pop
#endif
