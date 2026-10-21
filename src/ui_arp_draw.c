/* ARP page renderer for the FM1's 240 x 240 panel: mvave_fm1_master_specification.txt 3 (the knob frame on top, the
 * encoder menu list in the middle, the visualizer at the bottom, the synth palettes), 2.2 (the four ARP knobs), 8.1 / 8.2
 * (the 8 captured steps, the menu list) and 8.3 (each step's rhythm).
 *
 *   y   0..20   header: ARP, its transport (carp_playing) and the playing step, REC and LATCH when on, the clock
 *   y  21..53   knob cards: RATE, OCTAVE, STYLE, RHYTHM (STRUM under HOLD) (the live dial values a capture copies)
 *   y  56..124  menu list: LAYER, GATE, SEQ LENGTH, TRANSPOSE MAQ, GENRE BANK, ACCENT VEL (the cursor row in white)
 *   y 127..143  the chord sounding (the playing step's or the chord key's) as names and standing stones (laid out by
 *               its octave range and style), and the transposition
 *   y 146..238  steps 1..8, two rows of four cards: number (an accent mark after it, the octave range as a cairn of
 *               pebbles), chord root; style and the step's RATE; the rhythm's cells (o a box filled to the pattern's GATE, ~ a bar, _ a dot); a TIE card
 *               shows TIE and one bar across; a HOLD card its chord's four stones under one lintel, stepped as it
 *               strums.
 *               The playhead's card is framed (stopped with REC armed: the card the next chord goes into), held step
 *               keys' and the COPY source's cards inverted, steps past SEQ LENGTH dimmed.
 *
 * A Felucca unity-build fragment, not a translation unit: #include it after ui_seq_draw.c (its palette, sq_c / SQC,
 * sq_mix and the canvas calls it uses), UIStateMachine.h and dsp/midi_clock.h. Like the SEQ page it redraws only the
 * regions whose content changed.
 *
 *   arp_draw(sm, q, a, force)   once per UI frame while sm->page == UIPAGE_ARP; force 1 after another page or a dialog
 *                               covered the screen
 *
 * Reads only: the pattern, the UI registers, the ARP engine's transport and playhead (carp_*). Integer only. */

#define AR_HEAD_H 20
#define AR_KNOB_Y 21
#define AR_MENU_Y 56
#define AR_ROW_H 11                  /* 6 rows: 66 px, 2 px margin above, 1 below */
#define AR_NROW 6
#define AR_MENU_H 69
#define AR_CHORD_Y 127
#define AR_CHORD_H 17
#define AR_CELL_Y 146
#define AR_CELL_W 56
#define AR_CELL_H 44
#define AR_CELL_PITCH_X 58           /* 4 cards per row: x 4 .. 234 */
#define AR_CELL_PITCH_Y 48
#define AR_GLYPH_Y 27                /* the rhythm row inside a tablet: a lintel's 2 rows, the stones */
#define AR_GLYPH_H 13

enum { ARR_HEAD, ARR_KNOBS, ARR_MENU, ARR_CHORD, ARR_CELL0, ARR_COUNT = ARR_CELL0 + ARP_NSTEP };
static uint32_t ar_sig[ARR_COUNT];

static int ar_dirty(uint32_t r, uint32_t sig, uint32_t force)
{
    sig |= 1u;
    if (!force && ar_sig[r] == sig)
        return 0;
    ar_sig[r] = sig;
    return 1;
}

/* "07" .. as two digits, "n/len" */
static char *ar_num(char *b, uint32_t v)
{
    uint32_t k = 0;
    if (v >= 100u) b[k++] = (char)('0' + v / 100u);
    if (v >= 10u) b[k++] = (char)('0' + v / 10u % 10u);
    b[k++] = (char)('0' + v % 10u);
    b[k] = '\0';
    return b + k;
}

static uint8_t ar_len(const ArpState *st)
{
    uint8_t n = st->sequence_length;
    return n < 1u ? (uint8_t)1u : n > ARP_NSTEP ? (uint8_t)ARP_NSTEP : n;
}

/* -------------------------------------------------------------- head --- */
static void ar_draw_head(const UIStateMachine *sm, const MaqSeq *q, const CarpSeq *a)
{
    char b[16], *e;
    int32_t x, y = CAP_IN(S, AR_HEAD_H), i;
    uint8_t pos = carp_pos(a);
    cv_begin(SQ_MOON_X, AR_HEAD_H, SQC(BG));
    x = sq_text(6, CAP_IN(M, AR_HEAD_H), &AF_M, "ARP", SQC(THEME)) + 10;
    (void)q;
    if (carp_playing(a)) {
        for (i = 0; i < 5; i++)
            cv_rect(x + i, 6 + i, 1, 9 - 2 * i, SQC(THEME));
        e = b;
        if (pos < ARP_NSTEP) {                       /* "PLAY 3/8" */
            b[0] = 'P'; b[1] = 'L'; b[2] = 'A'; b[3] = 'Y'; b[4] = ' ';
            e = ar_num(b + 5, pos + 1u);
            *e++ = '/';
            ar_num(e, ar_len(sm->arp));
        } else {
            b[0] = 'P'; b[1] = 'L'; b[2] = 'A'; b[3] = 'Y'; b[4] = '\0';
        }
        x = sq_text(x + 13, y, &AF_S, b, SQC(THEME));
    } else {
        cv_rect(x, 6, 8, 8, SQC(MID));
        x = sq_text(x + 13, y, &AF_S, "STOP", SQC(MID));
    }
    if (sm->arp_rec)
        x = sq_text(x + 8, y, &AF_S, "REC", SQC(ACCENT));
    if (sm->arp_latch)
        sq_text(x + 8, y, &AF_S, "LATCH", SQC(THEME));
    if (sm->perf->clock_source == CLKSRC_MIDI) {
        sq_text_r(SQ_HEAD_R, y, &AF_S, "MIDI CLK", SQC(MID), SQC(BG));
    } else {
        e = ar_num(b, MIDI_CLOCK_BPM_BASE + sm->perf->int_tempo);
        e[0] = ' '; e[1] = 'B'; e[2] = 'P'; e[3] = 'M'; e[4] = '\0';
        sq_text_r(SQ_HEAD_R, y, &AF_S, b, SQC(MID), SQC(BG));
    }
    cv_blit(0, 0);
}

/* ------------------------------------------------------------- knobs --- */
/* the four knobs pick from lists more than they turn, so they are cards, not stone circles: the SEQ page's knob
 * tablets (ui_seq_draw.c sq_knob_card), cut the same; the grass under them THEME while a step key is held (the knobs
 * edit that step) */
static void ar_draw_knobs(UIStateMachine *sm)
{
    char b[8];
    uint8_t k;
    const char *t;
    UiKnobInfo info;
    cv_begin(240, SQ_KNOB_H, SQC(BG));
    for (k = 0; k < 4u; k++) {
        ui_sm_knob(sm, UIPAGE_ARP, 0, k, &info);
        t = ui_sm_knob_text(sm, UIPAGE_ARP, 0, k);
        if (t == NULL) {
            ar_num(b, sm->arp_knob[k]);
            t = b;
        }
        sq_knob_card(k, AR_KNOB_Y, info.label, t, sm->arp_held ? SQC(THEME) : SQC(MID), 0u);
    }
    cv_blit(0, AR_KNOB_Y);
}

/* -------------------------------------------------------------- menu --- */
/* 8.2's rows, label left and value right; the cursor row on the SEQ page's noisy ground in white (the one white thing
 * on the page), as every page's menu */
static void ar_draw_menu(const UIStateMachine *sm)
{
    char b[8];
    uint8_t r, n = ui_sm_row_count(sm), st;
    int32_t y;
    const char *t;
    uint16_t lc, vc, bg;
    cv_begin(240, AR_MENU_H, SQC(BG));
    for (r = 0; r < n && r < AR_NROW; r++) {
        y = 2 + r * AR_ROW_H;
        st = ui_sm_row_state(sm, r);
        bg = SQC(BG);
        lc = st == UIROW_ACTIVE ? SQC(MID) : SQC(DIM);
        vc = st == UIROW_ACTIVE ? SQC(TEXT) : SQC(DIM);
        if (r == sm->s.current_param_focus) {
            bg = SQ_PAL_LANE[sq_pal][SQC_BG];
            lc = vc = SQC(HI);
            sq_noise_block(2, y, 236, AR_ROW_H, 0u, AR_MENU_Y, 1u);
        }
        t = ui_sm_row_text(sm, r);
        if (t == NULL) {
            ar_num(b, ui_sm_row_value(sm, r));
            t = b;
        }
        sq_text_on(8, y + CAP_IN(S, AR_ROW_H), &AF_S, ui_sm_row_label(sm, r), lc, bg);
        sq_text_r(232, y + CAP_IN(S, AR_ROW_H), &AF_S, t, vc, bg);
    }
    cv_blit(0, AR_MENU_Y);
}

/* ------------------------------------------------------------- chord --- */
/* how the chord shown plays: the playing step's octave range (1..ARP_NOCT) and style while a pattern plays, otherwise
 * the live OCTAVE and STYLE dials (what a chord key plays, and what a capture copies) */
static uint8_t ar_shape(const UIStateMachine *sm, const CarpSeq *a, uint8_t *style)
{
    if (carp_pos(a) != CARP_POS_NONE)
        return carp_shown_shape(a, style);
    *style = sm->arp_knob[2] < ARPSTYLE_COUNT ? sm->arp_knob[2] : (uint8_t)ARPSTYLE_UP;
    return sm->arp_knob[1] < ARP_NOCT ? (uint8_t)(sm->arp_knob[1] + 1u) : (uint8_t)1u;
}

/* The stones between xa and xb (centred there, so more octaves add stones both ways), standing on the horizon: every
 * note a strike draws from, the chord over its octave range (engine order: the chord, then the chord an octave up ..),
 * each as tall as it is high in that range, the chord's own octave in THEME and the octaves above in a darker stone.
 * Left to right is the order they play in: UP rises, DOWN falls, UP-DN rises to the middle and falls (an arch), DN-UP
 * the other way (a valley), RANDOM shuffled. BLOCK and the strums strike the whole chord and climb an octave a strike,
 * so their octaves stand apart, each chord under a lintel: flat for BLOCK, sloping with the strum for STRUM UP / DN.
 * The stones narrow (8 px apart down to 3) to fit. */
static void ar_draw_stones(const uint8_t *notes, uint8_t n, uint8_t oct, uint8_t style, int32_t xa, int32_t xb)
{
    int16_t pool[CARP_POOL_MAX], lo, hi;
    uint8_t slot[CARP_POOL_MAX], m = (uint8_t)(n * oct), k, g, i, t;
    uint8_t chord = (uint8_t)(style == ARPSTYLE_BLOCK || style == ARPSTYLE_STRUM_UP || style == ARPSTYLE_STRUM_DN ||
                              style == ARPSTYLE_HOLD);
    int32_t p, w, gap = 0, total, x0, x, sh, span, hmax = chord ? 13 : 15, sx[CARP_POOL_MAX], top[CARP_POOL_MAX];
    uint32_t r;
    uint16_t c;
    if (m == 0u)
        return;
    for (k = 0; k < m; k++)
        pool[k] = (int16_t)(notes[k % n] + 12 * (k / n));
    lo = hi = pool[0];
    for (k = 1; k < m; k++) {
        if (pool[k] < lo) lo = pool[k];
        if (pool[k] > hi) hi = pool[k];
    }
    for (k = 0; k < m; k++) {                        /* the place each note stands in */
        g = (uint8_t)(k / n);
        i = (uint8_t)(k % n);
        t = (uint8_t)(m - 1u - k);
        switch (style) {
        case ARPSTYLE_DOWN: slot[k] = t; break;
        case ARPSTYLE_UPDN: slot[k] = k & 1u ? (uint8_t)(m - 1u - k / 2u) : (uint8_t)(k / 2u); break;
        case ARPSTYLE_DNUP: slot[k] = t & 1u ? (uint8_t)(m - 1u - t / 2u) : (uint8_t)(t / 2u); break;
        case ARPSTYLE_STRUM_DN: slot[k] = (uint8_t)(g * n + n - 1u - i); break;
        default: slot[k] = k; break;                 /* UP, BLOCK, STRUM UP, and RANDOM before its shuffle */
        }
    }
    if (style == ARPSTYLE_RANDOM) {                  /* shuffled, the same way for the same chord */
        r = sq_hash(pool[0] * 31 + m, pool[m - 1u]);
        for (k = (uint8_t)(m - 1u); k > 0u; k--) {
            r = sq_hash((int32_t)(r >> 1), k);
            i = (uint8_t)(r % (k + 1u));
            t = slot[k]; slot[k] = slot[i]; slot[i] = t;
        }
    }
    for (p = 8; p > 3; p--) {                        /* the widest stones that fit */
        gap = chord ? p / 2 + 1 : 0;
        if (m * p + (oct - 1) * gap <= xb - xa)
            break;
    }
    gap = chord ? p / 2 + 1 : 0;
    w = p >= 7 ? p - 3 : p >= 5 ? p - 2 : p - 1;
    total = m * p + (oct - 1) * gap - (p - w);
    x0 = xa + (xb - xa - total) / 2;
    if (x0 < xa)
        x0 = xa;
    span = hi > lo + 12 ? hi - lo : 12;              /* (an octave at least, so a close chord stands even) */
    for (k = 0; k < m; k++) {
        x = x0 + slot[k] * p + (chord ? (int32_t)(slot[k] / n) * gap : 0);
        sh = 5 + ((int32_t)(pool[k] - lo) * (hmax - 5) + span / 2) / span;
        c = k < n ? SQC(THEME) : sq_shade(SQC(THEME), 2u);
        sq_stone(x, AR_CHORD_H - 1, w, sh, sh, c, (uint32_t)pool[k] * 7u + k, 0u, 0u);
        sx[slot[k]] = x;
        top[slot[k]] = AR_CHORD_H - 1 - sh;
    }
    if (!chord)
        return;
    for (g = 0; g < oct; g++) {                      /* each octave's lintel, resting on its stones */
        int32_t a0 = g * n, a1 = a0 + n - 1, ya = top[a0], yb = top[a1], d = 0, y, j;
        c = g == 0u ? SQC(THEME) : sq_shade(SQC(THEME), 2u);
        if (style == ARPSTYLE_BLOCK || style == ARPSTYLE_HOLD || n == 1u) {
            for (j = a0; j <= a1; j++)
                if (top[j] < ya) ya = top[j];
            yb = ya;
        }
        for (j = a0; j <= a1; j++) {                 /* raised over any stone standing above the slope */
            y = a1 == a0 ? ya : ya + (yb - ya) * (j - a0) / (a1 - a0);
            if (y - top[j] > d) d = y - top[j];
        }
        for (x = sx[a0]; x < sx[a1] + w; x++) {
            y = (sx[a1] == sx[a0] ? ya : ya + (yb - ya) * (x - sx[a0]) / (sx[a1] - sx[a0])) - d - 2;
            cv_rect(x, y < 0 ? 0 : y, 1, 2, c);
        }
    }
}

/* "CHORD  C3 E3 G3 C4", the same notes as standing stones on a dotted horizon (ar_draw_stones: their octave range and
 * the style's order), and, right, the transposition ("+2"), or with TRANSPOSE MAQ on the MAQ's offset: the
 * transposition plus the step's chord root ("MAQ +7") */
static void ar_draw_chord(const UIStateMachine *sm, const CarpSeq *a)
{
    char b[24], nb[6];
    uint8_t notes[ARP_CHORD_NOTES], n = carp_shown(a, notes), i, k = 0, style, oct = ar_shape(sm, a, &style);
    uint8_t maq = (uint8_t)(sm->arp->transpose_maq_option == 1u);
    int8_t tr = maq ? carp_maq_transpose(a) : carp_transpose(a);
    int32_t y = CAP_IN(S, AR_CHORD_H - 3), x, xr = 234;
    const char *s;
    cv_begin(240, AR_CHORD_H, SQC(BG));
    for (x = 6; x < 234; x += 2)                     /* the horizon */
        cv_pset(x, AR_CHORD_H - 1, SQ_STONE[sq_pal]);
    x = sq_text(6, y, &AF_S, "CHORD", SQC(MID)) + 8;
    for (i = 0; i < n; i++) {
        if (i) b[k++] = ' ';
        for (s = ui_sm_note_name(notes[i], nb); *s; s++) b[k++] = *s;
    }
    if (n == 0u) { b[k++] = '-'; b[k++] = '-'; }
    b[k] = '\0';
    x = sq_text(x, y, &AF_S, b, SQC(TEXT)) + 6;
    if (tr != 0) {
        k = 0;
        if (maq) { b[k++] = 'M'; b[k++] = 'A'; b[k++] = 'Q'; b[k++] = ' '; }
        b[k++] = tr < 0 ? '-' : '+';
        ar_num(b + k, (uint32_t)(tr < 0 ? -tr : tr));
        xr = sq_text_r(234, y, &AF_S, b, SQC(ACCENT), SQC(BG)) - 6;
    }
    ar_draw_stones(notes, n, oct, style, x, xr);
    cv_blit(0, AR_CHORD_Y);
}

/* ------------------------------------------------------------- steps --- */
/* a tablet's second line: the style (3 letters at most) left, the RATE right (whole notes, then 1/2 .. 32nds; T a
 * triplet), 42 px at the widest of the 44 there */
static const char *const AR_STYLE_SHORT[ARPSTYLE_COUNT] = {"BLK", "UP", "DN", "U-D", "D-U", "RND", "ST+", "ST-", "HLD"};
static const char *const AR_RATE_SHORT[ARPDIV_COUNT] = {"2", "1", "1/2", "1/4", "4T", "1/8", "8T", "16", "16T", "32"};

/* Each step is a tablet of dark stone (ui_seq_draw.c sq_tablet: SQ_ROCK, the popups' stone): its edges hewn, the
 * corners knocked off, a chip here and there, the same shape for the step every time */
static void ar_tablet_shape(uint8_t i)
{
    sq_tab_shape(AR_CELL_W, AR_CELL_H, 4001u + i * 13u);
}

/* a cup-and-ring mark (the accent), 7 x 7 at (x, y): a ring round a cup */
static void ar_cup(int32_t x, int32_t y, uint16_t c)
{
    cv_rect(x + 2, y, 3, 1, c);
    cv_rect(x + 2, y + 6, 3, 1, c);
    cv_rect(x, y + 2, 1, 3, c);
    cv_rect(x + 6, y + 2, 1, 3, c);
    cv_pset(x + 1, y + 1, c);
    cv_pset(x + 5, y + 1, c);
    cv_pset(x + 1, y + 5, c);
    cv_pset(x + 5, y + 5, c);
    cv_pset(x + 3, y + 3, c);
}

/* step i's tablet: number and chord root, style and octaves, the rhythm as a row of stones. live: within SEQ LENGTH;
 * head: playing (or the record cursor); held: its step key is down or it is the COPY source (a capture, a knob turn or
 * a copy lands here). The rhythm (8.3): a strike a standing stone, filled from its foot to the gate; a tie a lintel
 * laid from the stone before onto a stone of its own; a rest a pebble on the ground. A TIE step is one lintel across
 * on two stones. */
static void ar_draw_cell(const ArpState *st, uint8_t i, uint8_t live, uint8_t head, uint8_t held)
{
    const ArpChordStep *s = &st->steps[i];
    char b[12], nb[6];
    uint8_t cells[CARP_NCELL], nc, c, tie = (uint8_t)(s->chord_root == ARP_CHORD_TIE);
    uint8_t empty = (uint8_t)!ARP_ROOT_IS_CHORD(s->chord_root);
    uint8_t gate = st->gate_length > VAL_MAX ? (uint8_t)VAL_MAX : st->gate_length;
    uint32_t sx = (uint32_t)(SQ_KNOB_X0 + (i & 3u) * AR_CELL_PITCH_X), sy = (uint32_t)(AR_CELL_Y + (i >> 2) * AR_CELL_PITCH_Y);
    uint32_t seed, ruin = live || held ? 0u : 120u;
    int32_t gx = 5, gw = AR_CELL_W - 10, cw, x, w, k, yb = AR_GLYPH_Y + AR_GLYPH_H, sh = AR_GLYPH_H - 2, fill;
    uint16_t txt, dim, lit, num;
    ar_tablet_shape(i);
    cv_begin(AR_CELL_W, AR_CELL_H, SQC(BG));
    sq_tablet(0, 0, held ? SQTAB_LIT : live ? SQTAB_ROCK : SQTAB_RUIN, head ? SQC(ACCENT) : 0u, sx, sy);
    if (held)
        sq_inv = SQINV_LANE;                         /* the highlight's tones; its words in white */
    txt = held ? SQC(HI) : live ? SQC(TEXT) : SQC(DIM);
    num = held ? SQC(HI) : live ? SQC(MID) : SQC(DIM);
    dim = held || live ? SQC(MID) : SQC(DIM);
    lit = held ? SQC(TEXT) : live ? SQC(THEME) : SQC(DIM);
    ar_num(b, i + 1u);
    x = sq_text_on(6, CAP_IN(S, 16), &AF_S, b, num, SQC(BG));
    if (s->accent && !empty) {
        ar_cup(x + 2, 4, held ? SQC(HI) : live ? SQC(ACCENT) : dim);
        x += 9;
    }
    if (!empty)                                      /* the octave range: a cairn of 1 .. 4 pebbles */
        for (k = 0; k < (int32_t)(s->octave_range < ARP_NOCT ? s->octave_range + 1u : 1u); k++)
            cv_rect(x + 2, 11 - 3 * k, 3, 2, dim);
    sq_text_r(AR_CELL_W - 6, CAP_IN(S, 16), &AF_S, tie ? "TIE" : empty ? "--" : ui_sm_note_name(s->chord_root, nb),
              empty && !tie ? dim : txt, SQC(BG));
    if (tie) {                                       /* the chord before runs on: a lintel across, on two stones */
        sq_stone(gx, yb, 6, sh, sh, dim, i * 8u + 1u, 0u, ruin);
        sq_stone(gx + gw - 6, yb, 6, sh, sh, dim, i * 8u + 2u, 0u, ruin);
        cv_rect(gx - 1, AR_GLYPH_Y, gw + 2, 2, txt);
    }
    if (!empty || tie)                               /* the step's RATE, right */
        sq_text_r(AR_CELL_W - 6, 13 + CAP_IN(S, 14), &AF_S, AR_RATE_SHORT[s->rate < ARPDIV_COUNT ? s->rate : ARP_RATE_DEFAULT],
                  dim, SQC(BG));
    if (!empty)
        sq_text_on(6, 13 + CAP_IN(S, 14), &AF_S, AR_STYLE_SHORT[s->style < ARPSTYLE_COUNT ? s->style : (uint8_t)ARPSTYLE_UP],
                   dim, SQC(BG));
    if (!empty && s->style == ARPSTYLE_HOLD) {      /* the chord's stones under one lintel: held */
        uint8_t up, sgap = carp_hold_strum(s->variation, &up);
        int32_t d = sgap == 0u ? 0 : 1 + sgap / 4u, hk;
        cw = gw / (int32_t)ARP_CHORD_NOTES;
        w = cw - 3;
        for (c = 0; c < ARP_CHORD_NOTES; c++) {     /* stepped as it strums: rising up, falling down */
            hk = sh - 2 - d * (up ? (int32_t)(ARP_CHORD_NOTES - 1u - c) : (int32_t)c);
            sq_stone(gx + (int32_t)c * cw + 2, yb, w, hk, hk, lit, i * 8u + c + 17u, 0u, ruin);
        }
        cv_rect(gx - 1, AR_GLYPH_Y, gw + 2, 2, txt);
    } else if (!empty) {
        nc = carp_rhythm(s->variation, cells);
        cw = gw / nc;
        w = cw - 3;
        for (c = 0; c < nc; c++) {
            x = gx + (int32_t)c * cw + 2;
            seed = i * 8u + c + 17u;
            if (cells[c] == CARP_CELL_STRIKE) {      /* a standing stone, filled from its foot to the gate */
                fill = (sh * ((int32_t)gate + 1) + 64) / 128;
                sq_stone(x, yb, w, sh, sh, dim, seed, 1u, ruin);
                if (fill > 0)
                    sq_stone(x, yb, w, sh, fill, lit, seed, 0u, ruin);
            } else if (cells[c] == CARP_CELL_TIE) {  /* a lintel from the stone before onto one of its own */
                sq_stone(x, yb, w, sh, sh, dim, seed, 1u, ruin);
                cv_rect(x - cw + 1, AR_GLYPH_Y, cw + w - 2, 2, txt);
            } else {                                 /* a rest: a pebble on the ground */
                sq_stone(x + w / 2 - 2, yb, 4, 2, 2, dim, seed, 0u, 0u);
            }
        }
    }
    cv_blit(sx, sy);
    sq_inv = SQINV_OFF;
}

/* ------------------------------------------------------------ entry --- */
static void arp_draw(UIStateMachine *sm, const MaqSeq *q, const CarpSeq *a, uint32_t force)
{
    static uint8_t pal_drawn = 0xFFu;
    uint32_t h, base = sq_mix(2166136261u, sq_pal);
    uint8_t i, r, shape, notes[ARP_CHORD_NOTES], n, pos = carp_pos(a), len = ar_len(sm->arp), cur = pos;
    if (pal_drawn != sq_pal) {
        pal_drawn = sq_pal;
        force = 1u;
    }
    if (force)
        lcd_fill(0, 0, 240, 240, SQC(BG));           /* the gaps between the regions, once */
    h = sq_mix(base, (uint32_t)carp_playing(a) | (uint32_t)pos << 8 | (uint32_t)len << 16 |
                         (uint32_t)sm->perf->clock_source << 24);
    h = sq_mix(h, sm->perf->int_tempo | (uint32_t)sm->arp_rec << 8 | (uint32_t)sm->arp_latch << 9);
    if (ar_dirty(ARR_HEAD, h, force))
        ar_draw_head(sm, q, a);
    h = sq_mix_bytes(base, sm->arp_knob, 4u);
    h = sq_mix(h, sm->arp_held != 0u);
    if (ar_dirty(ARR_KNOBS, h, force))
        ar_draw_knobs(sm);
    h = sq_mix(base, sm->s.current_param_focus | (uint32_t)sm->perf->perf_mode << 8);
    h = sq_mix(h, (uint32_t)sm->arp->target_layer | (uint32_t)sm->arp->gate_length << 8 |
                      (uint32_t)sm->arp->sequence_length << 16 | (uint32_t)sm->arp->transpose_maq_option << 24);
    h = sq_mix(h, sm->arp->current_genre_bank | (uint32_t)sm->arp->accent_velocity << 8);
    for (r = 0; r < ui_sm_row_count(sm); r++)
        h = sq_mix(h, ui_sm_row_state(sm, r));
    if (ar_dirty(ARR_MENU, h, force))
        ar_draw_menu(sm);
    n = carp_shown(a, notes);
    h = sq_mix_bytes(base, notes, n);
    h = sq_mix(h, (uint32_t)n | (uint32_t)(uint8_t)carp_transpose(a) << 8 | (uint32_t)sm->arp->transpose_maq_option << 16 |
                      (uint32_t)(uint8_t)carp_maq_transpose(a) << 24);
    r = ar_shape(sm, a, &shape);
    h = sq_mix(h, (uint32_t)r | (uint32_t)shape << 8);
    if (ar_dirty(ARR_CHORD, h, force))
        ar_draw_chord(sm, a);
    if (sm->arp_rec && !carp_playing(a))
        cur = (uint8_t)(sm->arp_rec_cursor % ARP_NSTEP);   /* step entry: where the next chord goes */
    for (i = 0; i < ARP_NSTEP; i++) {
        uint8_t live = (uint8_t)(i < len), head = (uint8_t)(i == cur);
        uint8_t held = (uint8_t)(((sm->arp_held >> i) & 1u) || i == sm->arp_copy_src);
        h = sq_mix_bytes(base, (const uint8_t *)&sm->arp->steps[i], sizeof(ArpChordStep));
        h = sq_mix(h, (uint32_t)live | (uint32_t)head << 1 | (uint32_t)held << 2 | (uint32_t)sm->arp->gate_length << 8);
        if (ar_dirty(ARR_CELL0 + i, h, force))
            ar_draw_cell(sm->arp, i, live, head, held);
    }
}
