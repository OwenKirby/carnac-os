/* Polyphonic arpeggiator & chord memory engine (see arp_seq.h). Every name carries the carp_ prefix (Felucca's seq.c has
 * arp_* statics). Integer only; the divisions run once per step, never per frame. Every byte read from the pattern is
 * range-checked first: the UI edits it from the main loop while this runs in the audio context, and it may come from
 * flash. */
#include "arp_seq.h"
#include "midi_clock.h"

#define CARP_CH_NONE 0xFFu
#define CARP_AUD_HOLD 0x8000u        /* req_aud: a hold (carp_hold), in the velocity byte's free top bit */
#define CARP_AUD_KEEP 0x80u          /* req_aud: a latched key's release (carp_latch_up), in the root byte's free top bit */
#define CARP_PULSES_PER_FRAME_MAX MAQ_PULSES_PER_FRAME_MAX

/* 8.3 rhythm_variation_table: row r, column A / B. 0 = _ rest, 1 = o strike, 2 = ~ tie. Rows 7..12's B column is a
 * triplet: 3 cells (the 4th byte unused). */
static const uint8_t CARP_RHYTHM[12][2][4] = {
    {{1, 0, 0, 0}, {1, 1, 0, 0}},    /* 01 */
    {{0, 1, 0, 0}, {1, 0, 1, 0}},    /* 02 */
    {{0, 0, 1, 0}, {1, 0, 0, 1}},    /* 03 */
    {{0, 0, 0, 1}, {0, 1, 1, 0}},    /* 04 */
    {{1, 2, 1, 0}, {0, 1, 0, 1}},    /* 05 */
    {{1, 0, 1, 2}, {0, 0, 1, 1}},    /* 06 */
    {{1, 2, 2, 1}, {1, 0, 0, 0}},    /* 07, B triplet */
    {{0, 1, 2, 1}, {0, 1, 0, 0}},    /* 08, B triplet */
    {{1, 1, 1, 0}, {0, 0, 1, 0}},    /* 09, B triplet */
    {{1, 1, 0, 1}, {1, 1, 0, 0}},    /* 10, B triplet */
    {{1, 0, 1, 1}, {1, 0, 1, 0}},    /* 11, B triplet */
    {{0, 1, 1, 1}, {0, 1, 1, 0}},    /* 12, B triplet */
};
PS_STATIC_ASSERT(ARP_NVAR == 2u * 12u, "one variation per rhythm table row and column");

/* 8.2 TIME DIVISION in 24 PPQN pulses: 2, 1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 (whole notes) */
static const uint8_t CARP_DIV_TICKS[ARPDIV_COUNT] = {192u, 96u, 48u, 24u, 16u, 12u, 8u, 6u, 4u, 3u};

/* ------------------------------------------------------------ helpers --- */
uint16_t carp_div_ticks(uint8_t division)
{
    return CARP_DIV_TICKS[division < ARPDIV_COUNT ? division : (uint8_t)ARPDIV_1_4];
}

uint8_t carp_rhythm(uint8_t variation, uint8_t cells[CARP_NCELL])
{
    uint8_t v = variation < ARP_NVAR ? variation : (uint8_t)0u, row = (uint8_t)(v >> 1), col = (uint8_t)(v & 1u), i;
    uint8_t n = col && row >= 6u ? (uint8_t)3u : (uint8_t)CARP_NCELL;
    for (i = 0u; i < CARP_NCELL; i++)
        cells[i] = i < n ? CARP_RHYTHM[row][col][i] : (uint8_t)CARP_CELL_REST;
    return n;
}

uint8_t carp_chord_notes(uint8_t genre, uint8_t root, uint8_t out[ARP_CHORD_NOTES])
{
    const uint8_t *iv;
    uint8_t i, n = 0u;
    uint16_t v;
    if (genre >= ARP_GENRE_COUNT) genre = 0u;
    if (root > VAL_MAX) root = VAL_MAX;
    iv = arp_chord_iv[genre][root % 12u];
    for (i = 0u; i < ARP_CHORD_NOTES; i++) {         /* ascending in the table, so ascending here */
        v = (uint16_t)(root + iv[i]);
        if (v <= VAL_MAX)
            out[n++] = (uint8_t)v;
    }
    return n;
}

static uint32_t carp_rand(CarpSeq *a)
{
    uint32_t x = a->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    a->rng = x ? x : 0x9E3779B9u;
    return a->rng;
}

/* LAYER (8.2): L2 needs a second layer, BOTH needs DUAL_SYNTH; anything else plays on layer 1. ch[1] = CARP_CH_NONE
 * unless BOTH on two different channels. */
static void carp_channels(const CarpSeq *a, uint8_t ch[2])
{
    uint8_t mode = a->perf->perf_mode, t = a->st->target_layer;
    uint8_t c1 = (uint8_t)(a->perf->midi_ch[0] & 0x0Fu), c2 = (uint8_t)(a->perf->midi_ch[1] & 0x0Fu);
    ch[0] = t == ARPLAYER_L2 && mode != PERF_SINGL_SYNTH ? c2 : c1;
    ch[1] =t == ARPLAYER_BOTH && mode == PERF_DUAL_SYNTH && c2 != c1 ? c2 : (uint8_t)CARP_CH_NONE;
}

static void carp_send(const CarpSeq *a, const uint8_t ch[2], uint8_t kind, uint8_t d1, uint8_t d2)
{
    a->out((uint8_t)(kind | ch[0]), d1, d2);
    if (ch[1] != CARP_CH_NONE)
        a->out((uint8_t)(kind | ch[1]), d1, d2);
}

static uint8_t carp_velocity(const CarpSeq *a)
{
    uint8_t bv = a->perf->base_velocity;
    return bv == 0u ? (uint8_t)1u : bv > VAL_MAX ? (uint8_t)VAL_MAX : bv;
}

/* 8.4 stuck-note shield: Note-Off every pitch the pattern has sounding, and drop what was still scheduled */
static void carp_release(CarpSeq *a)
{
    uint8_t i;
    for (i = 0u; i < ARP_CHORD_NOTES; i++)
        if (a->on[i] != CARP_NOTE_NONE) {
            carp_send(a, a->on_ch, 0x80u, a->on[i], 0u);
            a->on[i] = CARP_NOTE_NONE;
        }
    a->rel_pending = 0u;
    a->nstrum = 0u;
}

/* one pattern note on, into the cache (a note already sounding is struck again, not doubled) */
static void carp_note_on(CarpSeq *a, int16_t note)
{
    uint8_t i, slot = ARP_CHORD_NOTES;
    if (note < 0 || note > (int16_t)VAL_MAX)
        return;                                      /* transposed out of the MIDI range: dropped */
    for (i = 0u; i < ARP_CHORD_NOTES; i++) {
        if (a->on[i] == (uint8_t)note) {
            carp_send(a, a->on_ch, 0x80u, (uint8_t)note, 0u);
            slot = i;
            break;
        }
        if (a->on[i] == CARP_NOTE_NONE && slot == ARP_CHORD_NOTES)
            slot = i;
    }
    if (slot == ARP_CHORD_NOTES)
        return;                                      /* (cannot happen: at most 4 notes per strike, released first) */
    a->on_ch[0] = a->ch[0];
    a->on_ch[1] = a->ch[1];
    carp_send(a, a->ch, 0x90u, (uint8_t)note, a->strike_vel);
    a->on[slot] = (uint8_t)note;
}

/* ---------------------------------------------------------------- step --- */
/* the root of the first chord in the pattern's SEQ LENGTH steps, ARP_CHORD_EMPTY when it has none */
static uint8_t carp_pattern_ref(const ArpState *st)
{
    uint8_t len = st->sequence_length, i;
    if (len < 1u) len = 1u;
    if (len > ARP_NSTEP) len = ARP_NSTEP;
    for (i = 0u; i < len; i++)
        if (ARP_ROOT_IS_CHORD(st->steps[i].chord_root))
            return st->steps[i].chord_root;
    return ARP_CHORD_EMPTY;
}

/* a held chord key plays live: the captured pattern is not playing (stopped, or nothing captured in its length) */
static uint8_t carp_live_now(const CarpSeq *a)
{
    return (uint8_t)(a->live_root != CARP_NOTE_NONE && (!a->running || carp_pattern_ref(a->st) == ARP_CHORD_EMPTY));
}

/* what a strike adds to the chord: the transposition, none for a live step (the held key is the pitch) */
static int16_t carp_shift(const CarpSeq *a) { return a->live ? 0 : a->transpose; }

/* gate, octave range and style, range-checked */
static void carp_knobs(CarpSeq *a, uint8_t gate, uint8_t oct, uint8_t style)
{
    a->gate = gate > VAL_MAX ? (uint8_t)VAL_MAX : gate;
    a->oct = oct < ARP_NOCT ? (uint8_t)(oct + 1u) : 1u;
    a->style = style < ARPSTYLE_COUNT ? style : (uint8_t)ARPSTYLE_UP;
}

/* the step's cells from this frame: one TIME DIVISION split evenly between them */
static void carp_cells_start(CarpSeq *a)
{
    uint32_t frames = ((uint32_t)carp_div_ticks(a->st->time_division) * midi_clock_period_q8()) >> 8;   /* 192 ticks at 40 BPM: ~529k */
    a->cell_frames = a->ncell ? frames / a->ncell : frames;
    if (a->cell_frames == 0u) a->cell_frames = 1u;
    a->step_start = a->frame;
    a->cell_due = a->frame;
}

/* load the step to play now (the held key's live chord and knobs, or the playhead's step) and fire its first cell on
 * this frame. The caller has released what was sounding. */
static void carp_load(CarpSeq *a)
{
    const ArpState *st = a->st;
    uint8_t i, root = ARP_CHORD_EMPTY, var = 0u;
    uint32_t k;
    a->live = carp_live_now(a);
    a->accent = 0u;
    if (a->live) {
        k = a->req_knobs;
        root = a->live_root;
        carp_knobs(a, (uint8_t)k, (uint8_t)(k >> 8), (uint8_t)(k >> 16));
        var = (uint8_t)(k >> 24);
    } else if (a->pos < ARP_NSTEP) {
        const ArpChordStep *s = &st->steps[a->pos];
        root = s->chord_root;
        carp_knobs(a, s->gate_length, s->octave_range, s->style);
        var = s->variation;
        a->accent = (uint8_t)(s->accent != 0u);
    }
    a->nchord = ARP_ROOT_IS_CHORD(root) ? carp_chord_notes(st->current_genre_bank, root, a->chord) : 0u;
    a->ncell = a->ncell_var = carp_rhythm(var, a->cells);
    a->cell = 0u;
    if (a->nchord == 0u) {
        a->ncell = 0u;                               /* an empty step: a rest */
        return;
    }
    a->nshown = a->nchord;                           /* the display shows the step's chord as it sounds */
    for (i = 0u; i < a->nchord; i++) {
        int16_t v = (int16_t)(a->chord[i] + carp_shift(a));
        a->shown[i] = (uint8_t)(v < 0 ? 0 : v > (int16_t)VAL_MAX ? (int16_t)VAL_MAX : v);
    }
    a->shown_oct = a->oct;
    a->shown_style = a->style;
    carp_channels(a, a->ch);
    a->vel = a->live ? a->live_vel : carp_velocity(a);
    carp_cells_start(a);
}

/* the step after the playhead, in the pattern's SEQ LENGTH loop */
static uint8_t carp_next_pos(const CarpSeq *a)
{
    uint8_t len = a->st->sequence_length;
    if (len < 1u) len = 1u;
    if (len > ARP_NSTEP) len = ARP_NSTEP;
    return a->pos == CARP_POS_NONE || a->pos + 1u >= len ? (uint8_t)0u : (uint8_t)(a->pos + 1u);
}

/* the playhead's step is a TIE that carries on the chord loaded before it (not after a rest, a live step, or a start) */
static uint8_t carp_tied(const CarpSeq *a)
{
    return (uint8_t)(a->running && a->pos < ARP_NSTEP && a->st->steps[a->pos].chord_root == ARP_CHORD_TIE &&
                     a->nchord != 0u && !a->live && !carp_live_now(a));
}

/* a TIE step (8.1): the chord before it carries on. A one-note style runs its rhythm again (the note order runs on);
 * BLOCK and the strums strike nothing, their chord sustaining from the source step (tie_hold). */
static void carp_tie(CarpSeq *a)
{
    a->accent = 0u;
    a->ncell = a->tie_hold ? (uint8_t)0u : a->ncell_var;
    a->cell = 0u;
    carp_cells_start(a);
}

/* the next step: shield, the playhead on (while the transport runs), then load it */
static void carp_step_begin(CarpSeq *a)
{
    if (a->running)
        a->pos = carp_next_pos(a);
    a->ticks_left = (uint8_t)carp_div_ticks(a->st->time_division);
    if (carp_tied(a)) {
        carp_tie(a);                                 /* the chord before it sounds on: no Note-Offs */
        return;
    }
    a->tie_hold = 0u;
    carp_release(a);                                 /* 8.4: Note-Offs before this step's Note-Ons */
    carp_load(a);
}

/* the note a one-note style strikes: the chord spread over the octave range, ascending, taken in the style's order */
static int16_t carp_pick(CarpSeq *a)
{
    uint8_t n = (uint8_t)(a->nchord * a->oct), i, p;
    uint8_t c = a->counter;
    if (n <= 1u)
        i = 0u;
    else
        switch (a->style) {
        case ARPSTYLE_DOWN: i = (uint8_t)(n - 1u - c % n); break;
        case ARPSTYLE_UPDN: case ARPSTYLE_DNUP:      /* 1 2 3 4 3 2 1 2 ..: the ends once */
            p = (uint8_t)(c % (uint8_t)(2u * n - 2u));
            i = p < n ? p : (uint8_t)(2u * n - 2u - p);
            if (a->style == ARPSTYLE_DNUP) i = (uint8_t)(n - 1u - i);
            break;
        case ARPSTYLE_RANDOM: i = (uint8_t)(carp_rand(a) % n); break;
        default: i = (uint8_t)(c % n); break;        /* UP */
        }
    return (int16_t)(a->chord[i % a->nchord] + 12 * (i / a->nchord) + carp_shift(a));
}

/* an o cell: shield, strike as the style says, schedule the gate end over this cell and the ~ cells after it */
static void carp_strike(CarpSeq *a)
{
    uint8_t hold = 1u, i, n, up, last = 1u;
    uint8_t chord = (uint8_t)(a->style == ARPSTYLE_BLOCK || a->style == ARPSTYLE_STRUM_UP || a->style == ARPSTYLE_STRUM_DN);
    uint32_t gate;
    int16_t base;
    carp_release(a);
    a->strike_vel = a->vel;
    if (a->accent) {                                 /* an accented step's first strike */
        uint8_t av = a->st->accent_velocity;
        a->strike_vel = av == 0u ? (uint8_t)1u : av > VAL_MAX ? (uint8_t)VAL_MAX : av;
        a->accent = 0u;
    }
    while (a->cell + hold < a->ncell && a->cells[a->cell + hold] == CARP_CELL_TIE)
        hold++;
    for (i = (uint8_t)(a->cell + 1u); i < a->ncell; i++)
        if (a->cells[i] == CARP_CELL_STRIKE)
            last = 0u;
    gate = a->gate >= VAL_MAX ? 0u : ((uint32_t)hold * a->cell_frames * ((uint32_t)a->gate + 1u)) >> 7;
    if (a->gate < VAL_MAX && gate == 0u) gate = 1u;
    a->tie_hold = (uint8_t)(chord && last && a->running && !a->live && a->pos < ARP_NSTEP &&
                            a->st->steps[carp_next_pos(a)].chord_root == ARP_CHORD_TIE);
    if (a->tie_hold)
        gate = 0u;                                   /* the step's last chord sustains into the TIE after it */
    if (chord) {
        base = (int16_t)(12 * (a->counter % a->oct) + carp_shift(a));   /* the chord climbs an octave per strike */
        n = a->nchord;
        if (a->style == ARPSTYLE_BLOCK || n == 1u) {
            for (i = 0u; i < n; i++)
                carp_note_on(a, (int16_t)(a->chord[i] + base));
        } else {                                     /* a strum: the first note now, the rest strum_frames apart */
            up = (uint8_t)(a->style == ARPSTYLE_STRUM_UP);
            for (i = 0u; i < n; i++) {
                int16_t v = (int16_t)(a->chord[up ? i : n - 1u - i] + base);
                a->strum[i] = (uint8_t)(v < 0 ? 0xFF : v > (int16_t)VAL_MAX ? 0xFF : v);
            }
            a->strum_frames = a->cell_frames / (2u * n);
            if (a->strum_frames == 0u) a->strum_frames = 1u;
            if (gate != 0u && gate <= a->strum_frames * (uint32_t)(n - 1u))
                gate = a->strum_frames * (uint32_t)(n - 1u) + 1u;   /* every strummed note sounds a frame at least */
            if (a->strum[0] != 0xFFu) carp_note_on(a, a->strum[0]);
            a->nstrum = n;
            a->strum_i = 1u;
            a->strum_due = a->frame + a->strum_frames;
        }
    } else {
        carp_note_on(a, carp_pick(a));
    }
    a->counter++;
    if (gate != 0u) {
        a->rel_pending = 1u;
        a->rel_due = a->frame + gate;
    }
}

/* the step's cells, gate ends and strum notes that are due by this frame, in time order */
static void carp_due(CarpSeq *a)
{
    uint8_t guard = 3u * CARP_NCELL + ARP_CHORD_NOTES;
    while (guard--) {
        uint8_t rel = (uint8_t)(a->rel_pending && (int32_t)(a->frame - a->rel_due) >= 0);
        uint8_t strum = (uint8_t)(a->strum_i < a->nstrum && (int32_t)(a->frame - a->strum_due) >= 0);
        uint8_t cell = (uint8_t)(a->cell < a->ncell && (int32_t)(a->frame - a->cell_due) >= 0);
        if (strum && (!cell || (int32_t)(a->cell_due - a->strum_due) > 0)) {
            if (a->strum[a->strum_i] != 0xFFu) carp_note_on(a, a->strum[a->strum_i]);
            a->strum_i++;
            a->strum_due += a->strum_frames;
        } else if (rel && (!cell || (int32_t)(a->cell_due - a->rel_due) >= 0)) {
            carp_release(a);                         /* the gate's end (before a strike on the same frame) */
        } else if (cell) {
            if (a->cells[a->cell] == CARP_CELL_STRIKE)
                carp_strike(a);
            else if (a->cells[a->cell] == CARP_CELL_REST && !a->tie_hold)
                carp_release(a);                     /* _ silences a legato strike (not one tied on); ~ holds it */
            a->cell++;
            a->cell_due = a->step_start + a->cell_frames * a->cell;
        } else {
            break;
        }
    }
}

/* TRANSPOSE MAQ (8.2): the MAQ NOTE lanes follow the pattern's chord roots, as semitones from the root of its first
 * sounding step (so the MAQ plays as written on that chord). The root is the one of the step the next pulse plays: the
 * MAQ reads this a frame late (8.4), and a MAQ step that starts on the same pulse as an ARP step must take that step's
 * root, not the one before it. A rest or a TIE keeps the root before it. */
static void carp_follow(CarpSeq *a)
{
    const ArpState *st = a->st;
    uint8_t len = st->sequence_length, p, ref = carp_pattern_ref(st), r;
    if (len < 1u) len = 1u;
    if (len > ARP_NSTEP) len = ARP_NSTEP;
    if (ref == ARP_CHORD_EMPTY) {
        a->follow = 0;                               /* no chord in the pattern: nothing to follow */
        return;
    }
    p = a->running ? a->pos : (uint8_t)CARP_POS_NONE;
    if (p == CARP_POS_NONE || a->ticks_left == 1u)   /* the next pulse moves the playhead (as carp_step_begin does) */
        p = p == CARP_POS_NONE || p + 1u >= len ? (uint8_t)0u : (uint8_t)(p + 1u);
    r = st->steps[p].chord_root;
    if (ARP_ROOT_IS_CHORD(r))
        a->follow = (int8_t)(r - ref);
}

/* every note of the pattern off, the playhead to before step 1 (the next pulse plays it) */
static void carp_rewind(CarpSeq *a)
{
    carp_release(a);
    a->follow = 0;
    a->pos = CARP_POS_NONE;
    a->ticks_left = 1u;
    a->ncell = 0u;
    a->cell = 0u;
    a->nchord = 0u;                                  /* a TIE on step 1 rests at the start */
    a->tie_hold = 0u;
    a->counter = 0u;
    a->live = 0u;
}

/* a chord key's block chord off */
static void carp_aud_off(CarpSeq *a)
{
    uint8_t i;
    for (i = 0u; i < ARP_CHORD_NOTES; i++)
        if (a->aud_on[i] != CARP_NOTE_NONE) {
            carp_send(a, a->aud_ch, 0x80u, a->aud_on[i], 0u);
            a->aud_on[i] = CARP_NOTE_NONE;
        }
}

/* the held key starts playing live: stopped, on its own step grid from this frame, the note order from its start;
 * playing (an empty pattern), inside the running step, on the transport's grid */
static void carp_live_start(CarpSeq *a)
{
    carp_aud_off(a);
    carp_release(a);
    if (!a->running) {
        a->counter = 0u;                             /* the key lands inside a pulse: one more, so the step is never cut */
        a->ticks_left = (uint8_t)(carp_div_ticks(a->st->time_division) + 1u);
    }
    carp_load(a);
}

/* a chord key's request: the chord or hold before it off, then the new one: a hold plays live while the pattern is
 * not playing, anything else sounds as a block chord */
static void carp_aud_apply(CarpSeq *a, uint32_t req)
{
    uint8_t root = (uint8_t)(req & 0x7Fu), vel = (uint8_t)((req >> 8) & 0x7Fu), i, n, notes[ARP_CHORD_NOTES];
    carp_aud_off(a);
    if (req & CARP_AUD_KEEP)
        return;                                      /* LATCH: the key let go, its hold kept */
    if (a->live_root != CARP_NOTE_NONE) {            /* any other request ends the hold before it */
        a->live_root = CARP_NOTE_NONE;
        if (a->live) {
            carp_release(a);
            a->ncell = 0u;
            a->nchord = 0u;
            a->live = 0u;
        }
    }
    if (vel == 0u || !ARP_ROOT_IS_CHORD(root))
        return;
    if (req & CARP_AUD_HOLD) {
        a->live_root = root;
        a->live_vel = vel;
        if (carp_live_now(a)) {
            carp_live_start(a);
            return;
        }
    }
    if (vel > VAL_MAX) vel = VAL_MAX;
    carp_channels(a, a->aud_ch);
    n = carp_chord_notes(a->st->current_genre_bank, root, notes);
    for (i = 0u; i < n; i++) {
        carp_send(a, a->aud_ch, 0x90u, notes[i], vel);
        a->aud_on[i] = notes[i];
        a->shown[i] = notes[i];
    }
    a->nshown = n;
    a->shown_oct = 1u;                               /* a block chord, as it sounds */
    a->shown_style = ARPSTYLE_BLOCK;
}

/* --------------------------------------------------------------- API --- */
void carp_init(CarpSeq *a, ArpState *st, const MasterPerformancePreset *perf, MaqMidiOut out)
{
    uint8_t i;
    a->st = st;
    a->perf = perf;
    a->out = out;
    for (i = 0u; i < ARP_CHORD_NOTES; i++) {
        a->on[i] = CARP_NOTE_NONE;
        a->aud_on[i] = CARP_NOTE_NONE;
    }
    a->on_ch[0] = a->on_ch[1] = CARP_CH_NONE;
    a->aud_ch[0] = a->aud_ch[1] = CARP_CH_NONE;
    a->ch[0] = a->ch[1] = CARP_CH_NONE;
    a->vel = a->strike_vel = 1u;
    a->accent = 0u;
    a->tie_hold = 0u;
    a->ncell_var = 0u;
    a->rel_pending = 0u;
    a->nstrum = a->strum_i = 0u;
    a->pos = CARP_POS_NONE;
    a->ticks_left = 1u;
    a->ncell = a->cell = 0u;
    a->nchord = 0u;
    a->counter = 0u;
    a->req_aud = 0u;
    a->aud_serial = 0u;
    a->live_root = CARP_NOTE_NONE;
    a->live_vel = 1u;
    a->live = 0u;
    a->req_knobs = (uint32_t)ARP_GATE_DEFAULT | (uint32_t)ARPSTYLE_UP << 16;   /* the UI's knobs at power-up */
    a->req_transpose = 0;
    a->transpose = 0;
    a->follow = 0;
    a->nshown = 0u;
    a->shown_oct = 1u;
    a->shown_style = ARPSTYLE_UP;
    a->running = 0u;
    a->frame = 0u;
    a->pulse_seen = midi_clock_pulse_count();
    a->start_seen = midi_clock_start_count();
    a->rng = 0x6C8E9CF5u;
}

void carp_frame(CarpSeq *a, uint32_t frame, uint8_t run)
{
    uint32_t pulses = midi_clock_pulse_count(), n = pulses - a->pulse_seen, req = a->req_aud, starts;
    a->frame = frame;
    a->pulse_seen = pulses;
    /* Stopped, no request, no held key, nothing scheduled (most frames): only the writes the full frame would make.
     * Bit-exact; it skips the transport, the knobs, the pulses and carp_due, which change nothing then. */
#if !defined(SYNTH_SEQ_NO_IDLE)                     /* test_seq_idle.c builds it both ways */
    if (!run && !a->running && (uint16_t)(req >> 16) == a->aud_serial && !a->live && a->live_root == CARP_NOTE_NONE &&
        !a->rel_pending && a->strum_i >= a->nstrum && a->cell >= a->ncell) {
        a->transpose = a->req_transpose;
        a->start_seen = midi_clock_start_count();
        if (a->st->transpose_maq_option == 1u)
            carp_follow(a);
        return;
    }
#endif
    if ((uint16_t)(req >> 16) != a->aud_serial) {
        a->aud_serial = (uint16_t)(req >> 16);
        carp_aud_apply(a, req);
    }
    a->transpose = a->req_transpose;
    starts = midi_clock_start_count();
    run = run ? 1u : 0u;
    if (run != a->running || (run && midi_clock_is_external() && starts != a->start_seen)) {
        a->running = run;                            /* start (or MIDI Start): from step 1; stop: every note off */
        carp_rewind(a);
        a->rng ^= frame * 0x9E3779B9u;
        if (a->rng == 0u) a->rng = 0x9E3779B9u;
        if (!run && a->live_root != CARP_NOTE_NONE)
            carp_live_start(a);                      /* stopped with a chord key held: it plays on, live */
    }
    a->start_seen = starts;
    if (a->live) {                                   /* a held key follows the knobs from its next strike */
        uint32_t k = a->req_knobs;
        carp_knobs(a, (uint8_t)k, (uint8_t)(k >> 8), (uint8_t)(k >> 16));
    }
    if (a->running || a->live_root != CARP_NOTE_NONE) {
        if (n > CARP_PULSES_PER_FRAME_MAX)
            n = CARP_PULSES_PER_FRAME_MAX;
        while (n--)
            if (--a->ticks_left == 0u)
                carp_step_begin(a);
    }
    carp_due(a);
    if (a->st->transpose_maq_option == 1u)
        carp_follow(a);
}

static void carp_request(CarpSeq *a, uint8_t root, uint8_t vel, uint32_t hold)
{
    uint32_t serial = ((a->req_aud >> 16) + 1u) & 0xFFFFu;
    if (vel > VAL_MAX) vel = VAL_MAX;
    a->req_aud = (uint32_t)root | (uint32_t)vel << 8 | hold | serial << 16;   /* one store: the frame sees all of it or none */
}

void carp_audition(CarpSeq *a, uint8_t root, uint8_t vel) { carp_request(a, root, vel, 0u); }
void carp_hold(CarpSeq *a, uint8_t root, uint8_t vel) { carp_request(a, root, vel, vel ? CARP_AUD_HOLD : 0u); }

void carp_set_knobs(CarpSeq *a, const uint8_t knobs[4])
{
    a->req_knobs = (uint32_t)knobs[0] | (uint32_t)knobs[1] << 8 | (uint32_t)knobs[2] << 16 | (uint32_t)knobs[3] << 24;
}

void carp_set_transpose(CarpSeq *a, int8_t semitones) { a->req_transpose = semitones; }
void carp_latch_up(CarpSeq *a, uint8_t root) { carp_request(a, (uint8_t)(root | CARP_AUD_KEEP), 0u, 0u); }

int8_t carp_maq_transpose(const CarpSeq *a)
{
    int16_t t = (int16_t)(a->transpose + a->follow);
    if (a->st->transpose_maq_option != 1u)
        return 0;
    return (int8_t)(t < -127 ? -127 : t > 127 ? 127 : t);
}
int8_t carp_transpose(const CarpSeq *a) { return a->req_transpose; }
uint8_t carp_pos(const CarpSeq *a) { return a->running ? a->pos : (uint8_t)CARP_POS_NONE; }

uint8_t carp_rec_step(const CarpSeq *a)
{
    uint8_t len = a->st->sequence_length, pos = a->pos;
    if (!a->running)
        return CARP_POS_NONE;
    if (len < 1u) len = 1u;
    if (len > ARP_NSTEP) len = ARP_NSTEP;
    if (pos >= len)
        return 0u;                                   /* before the first step (or past a shortened length): step 1 next */
    if (2u * (uint16_t)a->ticks_left < carp_div_ticks(a->st->time_division))
        pos = pos + 1u >= len ? (uint8_t)0u : (uint8_t)(pos + 1u);   /* the step's second half: the next one */
    return pos;
}

uint8_t carp_shown(const CarpSeq *a, uint8_t out[ARP_CHORD_NOTES])
{
    uint8_t i, n = a->nshown < ARP_CHORD_NOTES ? a->nshown : (uint8_t)ARP_CHORD_NOTES;
    for (i = 0u; i < n; i++)
        out[i] = a->shown[i];
    return n;
}

uint8_t carp_shown_shape(const CarpSeq *a, uint8_t *style)
{
    *style = a->shown_style;
    return a->shown_oct;
}
