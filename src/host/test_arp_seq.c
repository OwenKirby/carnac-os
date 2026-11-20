/* Host test for the ARP chord engine and its page controls (spec 8.1 - 8.4, 2.2's ARP knobs, 6.3).
 * Drives midi_clock, maq_frame and carp_frame frame by frame at 120 BPM (918.75 frames per 24 PPQN tick) and records
 * every MIDI message each engine sends, apart. Exit code 0 = pass. Test scaffolding only.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/test_arp_seq.c -o test_arp_seq */
#include <stdio.h>
#include <string.h>
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/midi_clock.h"
#include "dsp/maq_seq.h"
#include "dsp/arp_seq.h"
#include "dsp/arp_chords.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define QUARTER 22050u               /* 24 ticks at 120 BPM */
#define CELL 5512u                   /* a quarter's 4 cells: 5512.5 frames */
#define NEV 4096
#define G_UTIL_MAJ 18u               /* arp_genre_names: Utility_M7, a 4-note chord on every key (the J-6's Utility_Maj is a triad) */
#define G_TRAD_MAJ 6u
#define FM1_LOW 53u                  /* F3, the FM1's lowest key */

typedef struct { uint32_t frame; uint8_t st, d1, d2; } Ev;
typedef struct { Ev ev[NEV]; int n; int8_t sounding[16][128]; int stuck; } Log;
static Log la, lm;                   /* the ARP's messages, the MAQ's */
static uint32_t now;

static void log_ev(Log *g, uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t kind = st & 0xF0u, ch = st & 0x0Fu;
    if (g->n < NEV) { g->ev[g->n].frame = now; g->ev[g->n].st = st; g->ev[g->n].d1 = d1; g->ev[g->n].d2 = d2; g->n++; }
    if (kind == 0x90u) { if (g->sounding[ch][d1] != 0) g->stuck++; g->sounding[ch][d1]++; }
    if (kind == 0x80u) { if (g->sounding[ch][d1] != 1) g->stuck++; g->sounding[ch][d1]--; }
}
static void sink_arp(uint8_t st, uint8_t d1, uint8_t d2) { log_ev(&la, st, d1, d2); }
static void sink_maq(uint8_t st, uint8_t d1, uint8_t d2) { log_ev(&lm, st, d1, d2); }

static MaqSeq q;
static CarpSeq a;
static SequencerLane lanes[SEQ_NLANE];
static UIStateMachine sm;

/* the render's order (synth_runtime.c): clock, MAQ, ARP (its own transport, synced to the MAQ's bar), then the
 * 1-frame-late MAQ transpose and the ARP's bar for the MAQ */
static void run(uint32_t frames)
{
    while (frames--) {
        midi_clock_frame(now);
        maq_frame(&q, now);
        carp_frame(&a, now, &q);
        maq_set_ext_transpose(&q, carp_maq_transpose(&a));
        maq_set_sync(&q, carp_running(&a), carp_origin(&a));
        now++;
    }
}

static void reset(void)
{
    preset_defaults();
    midi_clock_init();
    seq_defaults(lanes);
    memset(&la, 0, sizeof la);
    memset(&lm, 0, sizeof lm);
    now = 0;
    maq_init(&q, lanes, &perf_state, sink_maq);
    carp_init(&a, &arp_state, &perf_state, sink_arp);
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], lanes, &arp_state);
    ui_sm_attach_seq(&sm, &q);
    ui_sm_attach_arp(&sm, &a);
}

static int count(const Log *g, uint8_t kind) { int i, n = 0; for (i = 0; i < g->n; i++) n += (g->ev[i].st & 0xF0u) == kind; return n; }
static int nth(const Log *g, uint8_t kind, int k) { int i; for (i = 0; i < g->n; i++) if ((g->ev[i].st & 0xF0u) == kind && k-- == 0) return i; return -1; }
static int all_off(const Log *g) { int c, n; for (c = 0; c < 16; c++) for (n = 0; n < 128; n++) if (g->sounding[c][n]) return 0; return 1; }

/* one captured step: root's chord with these knob values, 1/4 long (gate: the pattern's GATE) */
static void step(uint8_t i, uint8_t root, uint8_t gate, uint8_t oct, uint8_t style, uint8_t var)
{
    ArpChordStep *s = &arp_state.steps[i];
    s->chord_root = root; s->rate = ARP_RATE_DEFAULT; s->octave_range = oct; s->style = style; s->variation = var;
    arp_state.gate_length = gate;
}

/* both transports, as PLAY off the SEQ and ARP pages does */
static void play(uint8_t on) { maq_set_playing(&q, on); carp_set_playing(&a, on); }

/* PLAY pressed on another page (the page kept) */
static void play_on(uint8_t page)
{
    uint8_t p = sm.page;
    ui_sm_goto(&sm, page);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    ui_sm_goto(&sm, p);
}

/* the frame of the k-th ARP note-on and its note */
static uint32_t on_frame(int k) { int i = nth(&la, 0x90, k); return i >= 0 ? la.ev[i].frame : 0xFFFFFFFFu; }
static uint8_t on_note(int k) { int i = nth(&la, 0x90, k); return i >= 0 ? la.ev[i].d1 : 0xFFu; }
static int near(uint32_t f, uint32_t want) { return f + 2u >= want && f <= want + 2u; }

int main(void)
{
    int g, p, i, k;
    uint8_t c[ARP_CHORD_NOTES], cells[CARP_NCELL], n;

    /* ---- the chord bank: the Roland J-6's 100 chord sets (manual pp. 25-33), 1..4 ascending notes a key ---- */
    for (g = 0; g < (int)ARP_GENRE_COUNT; g++)
        for (p = 0; p < 12; p++) {
            int8_t off[ARP_CHORD_NOTES];
            uint8_t m = arp_chord_off((uint8_t)g, (uint8_t)p, off), q;
            CHECK(m >= 1 && m <= ARP_CHORD_NOTES, "%s pc %d: %u notes", arp_genre_names[g], p, m);
            for (q = 1; q < m; q++)
                CHECK(off[q] > off[q - 1] && off[q] - off[0] <= 40, "%s pc %d: voice %u", arp_genre_names[g], p, q);
        }
    CHECK(strcmp(arp_genre_names[0], "Pop_1") == 0 && strcmp(arp_genre_names[ARP_GENRE_COUNT - 2u], "Modern_2") == 0 &&
          strcmp(arp_genre_names[G_UTIL_MAJ], "Utility_M7") == 0, "J-6 genre order");
    n = carp_chord_notes(0, 48, c);              /* J-6 set 1 on C: Cadd9 = C3 G3 D4 E4 */
    CHECK(n == 4 && c[0] == 48 && c[1] == 55 && c[2] == 62 && c[3] == 64, "Pop_1 C3 = the J-6's Cadd9");
    n = carp_chord_notes(0, 53, c);              /* .. on F: FM9 = F2 A3 E4 G4, a key an octave up plays it there */
    CHECK(n == 4 && c[0] == 53 && c[1] == 69 && c[2] == 76 && c[3] == 79, "Pop_1 F3 = the J-6's FM9 an octave up");
    n = carp_chord_notes(13, 60, c);             /* Oct Stack: 2 notes, C4 and C5 */
    CHECK(n == 2 && c[0] == 72 && c[1] == 84, "Oct_Stack C4 = a two-note stack (%u)", n);
    CHECK(strcmp(arp_genre_names[ARP_GENRE_PHRYGIAN], "Phrygian") == 0 && (arp_genre_scale[ARP_GENRE_PHRYGIAN] & 15u) ==
          SEQSCALE_PHRYGIAN, "Phrygian after the spec's list, on the Phrygian scale");
    { ArpState d; arp_defaults(&d);
      CHECK(d.current_genre_bank == ARP_GENRE_PHRYGIAN && d.steps[3].rate == ARPRATE_1_16 && ARP_TIMING(&d) == ARPDIV_1_4 && d.gate_length == ARP_GATE_DEFAULT,
            "the default pattern: Phrygian, 1/4 steps, the default GATE"); }
    n = carp_chord_notes(ARP_GENRE_PHRYGIAN, 49, c);
    CHECK(n == 4 && c[1] - c[0] == 7 && c[2] - c[0] == 11 && c[3] - c[0] == 16, "Phrygian's bII: a major seventh");
    n = carp_chord_notes(G_TRAD_MAJ, 48, c);
    CHECK(n == 4 && c[0] == 48 && c[1] == 64 && c[2] == 67 && c[3] == 72, "Trad_Maj C3 = spec {C3, E4, G4, C5}");
    n = carp_chord_notes(G_TRAD_MAJ, 120, c);
    CHECK(n == 1 && c[0] == 120, "voices above 127 are dropped (%u)", n);   /* 120 + 16 > 127 */

    /* ---- rhythm table and divisions ---- */
    CHECK(carp_rhythm(0, cells) == 4 && cells[0] == 1 && cells[1] == 0, "01A = o _ _ _");
    CHECK(carp_rhythm(8, cells) == 4 && cells[0] == 1 && cells[1] == 2 && cells[2] == 1 && cells[3] == 0, "05A = o ~ o _");
    CHECK(carp_rhythm(13, cells) == 3 && cells[0] == 1 && cells[1] == 0 && cells[2] == 0, "07T = o _ _, 3 cells");
    CHECK(carp_rhythm(11, cells) == 4, "06B is straight: 4 cells");
    CHECK(carp_rhythm(24, cells) == 4 && cells[0] == 1 && cells[1] == 1 && cells[2] == 1 && cells[3] == 1, "ALL = o o o o");
    CHECK(carp_div_ticks(ARPDIV_2) == 192 && carp_div_ticks(ARPDIV_1_4) == 24 && carp_div_ticks(ARPDIV_1_4T) == 16 &&
          carp_div_ticks(ARPDIV_1_32) == 3 && carp_div_ticks(ARPDIV_1_32T) == 2, "TIMING in pulses");

    /* ---- UP over 01B: two strikes a step, the order runs on across steps, gate 96 ---- */
    reset();
    step(0, 48, 96, 0, ARPSTYLE_UP, 1);              /* C3 major, o o _ _ */
    arp_state.sequence_length = 1;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    carp_chord_notes(G_UTIL_MAJ, 48, c);
    play(1);
    run(QUARTER * 2u + 10u);
    CHECK(count(&la, 0x90) == 5, "2 strikes x 2 steps + the third step's first, got %d", count(&la, 0x90));
    CHECK(on_frame(0) == 0 && on_note(0) == c[0] && on_note(1) == c[1] && on_note(2) == c[2] && on_note(3) == c[3] &&
          on_note(4) == c[0], "UP: %u %u %u %u %u", on_note(0), on_note(1), on_note(2), on_note(3), on_note(4));
    CHECK(near(on_frame(1), CELL) && near(on_frame(2), QUARTER), "cells at 1/16: %u %u", on_frame(1), on_frame(2));
    k = nth(&la, 0x80, 0);
    CHECK(k >= 0 && near(la.ev[k].frame, (CELL * 97u) >> 7), "GATE 96: off at 97/128 of the cell, got %u",
          k >= 0 ? la.ev[k].frame : 0);
    CHECK(la.ev[0].d2 == perf_state.base_velocity && (la.ev[0].st & 0x0Fu) == perf_state.midi_ch[0], "base velocity, L1");
    CHECK(count(&lm, 0x90) > 0, "the MAQ plays alongside, started together");
    play(0);
    run(2);
    CHECK(la.stuck == 0 && all_off(&la), "stop releases everything, no double on / orphan off (%d)", la.stuck);

    /* ---- the other orders and the octave range ---- */
    {
        static const uint8_t want_dn[] = {3, 2, 1, 0, 3}, want_ud[] = {0, 1, 2, 3, 2, 1, 0, 1};
        reset();
        step(0, 48, 64, 0, ARPSTYLE_DOWN, 1);
        arp_state.sequence_length = 1;
        arp_state.current_genre_bank = G_UTIL_MAJ;
        carp_chord_notes(G_UTIL_MAJ, 48, c);
        play(1);
        run(QUARTER * 2u + 10u);
        for (i = 0; i < 5; i++) CHECK(on_note(i) == c[want_dn[i]], "DOWN strike %d: %u", i, on_note(i));
        reset();
        step(0, 48, 64, 0, ARPSTYLE_UPDN, 1);
        arp_state.sequence_length = 1;
        arp_state.current_genre_bank = G_UTIL_MAJ;
        play(1);
        run(QUARTER * 4u - 10u);
        for (i = 0; i < 8; i++) CHECK(on_note(i) == c[want_ud[i]], "UP-DN strike %d: %u", i, on_note(i));
        reset();
        step(0, 48, 64, 1, ARPSTYLE_UP, 1);          /* 2 octaves: 8 notes */
        arp_state.sequence_length = 1;
        arp_state.current_genre_bank = G_UTIL_MAJ;
        play(1);
        run(QUARTER * 4u + 10u);
        CHECK(on_note(4) == c[0] + 12 && on_note(7) == c[3] + 12 && on_note(8) == c[0], "UP over 2 octaves: %u %u %u",
              on_note(4), on_note(7), on_note(8));
        CHECK(la.stuck == 0, "no stuck notes (%d)", la.stuck);
    }

    /* ---- BLOCK climbs an octave per strike within the range; STRUM staggers ---- */
    reset();
    step(0, 48, 64, 1, ARPSTYLE_BLOCK, 1);
    arp_state.sequence_length = 1;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    carp_chord_notes(G_UTIL_MAJ, 48, c);
    play(1);
    run(CELL + 10u);
    CHECK(count(&la, 0x90) == 8 && on_frame(3) == 0 && on_note(3) == c[3] && on_note(4) == c[0] + 12 &&
          near(on_frame(4), CELL), "BLOCK: the chord, then an octave up (%d on)", count(&la, 0x90));
    reset();
    step(0, 48, 64, 0, ARPSTYLE_STRUM_UP, 0);
    arp_state.sequence_length = 1;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    play(1);
    run(CELL);
    CHECK(count(&la, 0x90) == 4 && on_note(0) == c[0] && on_note(3) == c[3] && near(on_frame(1), CELL / 8u) &&
          near(on_frame(3), 3u * (CELL / 8u)), "STRUM UP: cell / 8 apart, %u %u", on_frame(1), on_frame(3));
    reset();
    step(0, 48, 64, 0, ARPSTYLE_STRUM_DN, 0);
    arp_state.sequence_length = 1;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    play(1);
    run(CELL);
    CHECK(on_note(0) == c[3] && on_note(3) == c[0], "STRUM DN: top first");

    /* ---- tie, triplet, empty step, sequence length ---- */
    reset();
    step(0, 48, 64, 0, ARPSTYLE_UP, 8);              /* 05A: o ~ o _ */
    arp_state.sequence_length = 1;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    play(1);
    run(QUARTER - 10u);
    k = nth(&la, 0x80, 0);
    CHECK(count(&la, 0x90) == 2 && near(on_frame(1), 2u * CELL + 1u), "05A strikes on cells 1 and 3: %u", on_frame(1));
    CHECK(k >= 0 && near(la.ev[k].frame, (2u * CELL * 65u) >> 7), "~ extends the gate over 2 cells, off at %u",
          k >= 0 ? la.ev[k].frame : 0);
    reset();
    step(0, 48, 64, 0, ARPSTYLE_UP, 19);             /* 5.10: o o _ (3 cells) */
    arp_state.sequence_length = 1;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    play(1);
    run(QUARTER - 10u);                              /* cells o o _ o: the rhythm runs round at RATE, no squeeze */
    CHECK(count(&la, 0x90) == 3 && near(on_frame(1), CELL) && near(on_frame(2), 3u * CELL),
          "3-cell rhythm: cells one RATE long, %u %u", on_frame(1), on_frame(2));
    reset();
    step(0, 48, 64, 0, ARPSTYLE_UP, 0);
    step(2, 50, 64, 0, ARPSTYLE_UP, 0);              /* step 2 (index 1) empty, step 4 beyond the length */
    step(3, 60, 64, 0, ARPSTYLE_UP, 0);
    arp_state.sequence_length = 3;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    play(1);
    for (i = 0; i < 4; i++) { run(1); n = carp_pos(&a); CHECK(n == (uint8_t)(i % 3), "playhead %d: %u", i, n); run(QUARTER - 1u); }
    {                                                /* UP runs on across steps: strikes 0, 1, 2 of the order */
        uint8_t c50[ARP_CHORD_NOTES];
        carp_chord_notes(G_UTIL_MAJ, 48, c);
        carp_chord_notes(G_UTIL_MAJ, 50, c50);
        CHECK(count(&la, 0x90) == 3 && on_note(0) == c[0] && on_note(1) == c50[1] && near(on_frame(1), 2u * QUARTER) &&
              on_note(2) == c[2], "an empty step rests, length 3 loops (%d on)", count(&la, 0x90));
    }

    /* ---- two views (2026-11-13) on the FM1's 32 keys, F3 .. C6: CHORDS every key a chord key, STEPS the 16 white
     * keys (F3 G3 A3 B3 C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 = keys 0 2 4 6 7 9 11 12 14 16 18 19 21 23 24 26) ---- */
    reset();
    CHECK(ui_sm_arp_key(&sm, 0, FM1_LOW, 1, 100) == 0, "not on the ARP page: plays");
    ui_sm_arp_key(&sm, 0, FM1_LOW, 0, 0);
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);
    CHECK(sm.page == UIPAGE_ARP && sm.arp_view == UIARP_CHORDS, "ARP opens the CHORDS view");
    CHECK(strcmp(ui_sm_row_label(&sm, 0), "GENRE BANK") == 0 && ui_sm_row_count(&sm) == 4, "CHORDS: 4 rows, GENRE BANK first");
    CHECK(ui_sm_arp_key(&sm, 31, FM1_LOW + 31u, 1, 100) == 1 && sm.arp_chord_key == FM1_LOW + 31u, "C6, the top key: a chord key");
    ui_sm_arp_key(&sm, 31, FM1_LOW + 31u, 0, 0);
    CHECK(ui_sm_arp_key(&sm, 20, FM1_LOW + 20u, 1, 100) == 1 && sm.arp_chord_key == FM1_LOW + 20u && sm.arp_held == 0,
          "C#5 (a black key): a chord key too");
    ui_sm_arp_key(&sm, 20, FM1_LOW + 20u, 0, 0);
    CHECK(sm.arp_chord_key == CARP_NOTE_NONE, "released: silent");
    {   /* the chord follows its key's register: the same chord an octave apart */
        uint8_t lo[ARP_CHORD_NOTES], hi[ARP_CHORD_NOTES], nl = carp_chord_notes(arp_state.current_genre_bank, 60, lo);
        uint8_t nh = carp_chord_notes(arp_state.current_genre_bank, 72, hi);
        CHECK(nl == nh && nl > 0 && hi[0] == lo[0] + 12u, "C5's chord is C4's an octave up");
    }
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);
    CHECK(sm.arp_view == UIARP_STEPS && strcmp(ui_sm_row_label(&sm, sm.s.current_param_focus), "CHORD") == 0,
          "ARP again: STEPS, the cursor on CHORD");
    CHECK(ui_sm_arp_key(&sm, 0, FM1_LOW, 1, 100) == 1 && sm.arp_held == 0x0001 && sm.arp_sel == 0, "F3 = step 1");
    ui_sm_arp_key(&sm, 0, FM1_LOW, 0, 0);
    CHECK(sm.arp_held == 0 && sm.arp_sel == 0, "released: step 1 stays selected");
    CHECK(ui_sm_arp_key(&sm, 1, FM1_LOW + 1u, 1, 100) == 1 && sm.arp_held == 0 && sm.arp_sel == 0, "F#3: a black key, nothing");
    ui_sm_arp_key(&sm, 1, FM1_LOW + 1u, 0, 0);
    CHECK(ui_sm_arp_key(&sm, 26, FM1_LOW + 26u, 1, 100) == 1 && sm.arp_held == 0x8000 && sm.arp_sel == 15, "G5 = step 16");
    ui_sm_arp_key(&sm, 26, FM1_LOW + 26u, 0, 0);
    CHECK(ui_sm_arp_key(&sm, 28, FM1_LOW + 28u, 1, 100) == 1 && sm.arp_held == 0 && sm.arp_sel == 15,
          "A5, above the 16 steps: taken, nothing");
    ui_sm_arp_key(&sm, 28, FM1_LOW + 28u, 0, 0);
    {   /* OCT+ held + step key n: SEQ LENGTH n, the step not selected; the LEDs 1..n, n lit */
        UiKeyLeds kl;
        uint8_t len0 = arp_state.sequence_length;
        CHECK(ui_sm_seq_key(&sm, 9, FM1_LOW + 9u, 1, UISEQ_MOD_OCT_UP) == 1 && arp_state.sequence_length == 6 &&
              sm.arp_sel == 15 && sm.arp_held == 0, "OCT+ + D4 (step 6): SEQ LENGTH 6, selection kept");
        ui_sm_seq_key(&sm, 9, FM1_LOW + 9u, 0, UISEQ_MOD_OCT_UP);
        ui_sm_key_leds(&sm, FM1_LOW, UISEQ_MOD_OCT_UP, &kl);
        CHECK(kl.lit == 1u << 9 && kl.dim == 0xD5u && kl.mid == 0, "OCT+ LEDs: steps 1..5 dim, 6 lit (%08x %08x)",
              (unsigned)kl.lit, (unsigned)kl.dim);
        CHECK(ui_sm_seq_key(&sm, 28, FM1_LOW + 28u, 1, UISEQ_MOD_OCT_UP) == 0 && arp_state.sequence_length == 6,
              "OCT+ + A5 (past 16): not taken");
        CHECK(ui_sm_seq_key(&sm, 9, FM1_LOW + 9u, 1, 0) == 0, "no OCT+: the step key goes to ui_sm_arp_key");
        arp_state.sequence_length = len0;
    }
    {   /* STEPS LEDs: 1..SEQ LENGTH dim (a rest dark), the selected step mid, held lit */
        UiKeyLeds kl;
        ui_sm_arp_key(&sm, 0, FM1_LOW, 1, 100);
        ui_sm_arp_key(&sm, 0, FM1_LOW, 0, 0);       /* step 1 selected */
        CHECK(ui_sm_key_leds(&sm, FM1_LOW, 0, &kl) && kl.lit == 0 && kl.mid == 1u && kl.dim == 0,
              "STEPS LEDs, empty pattern: step 1 (selected) mid, the rests dark (%08x %08x)", (unsigned)kl.mid, (unsigned)kl.dim);
        arp_state.steps[1].chord_root = 60; arp_state.steps[9].chord_root = ARP_CHORD_TIE;
        arp_state.sequence_length = 9;
        ui_sm_arp_key(&sm, 2, FM1_LOW + 2u, 1, 100);   /* hold step 2 */
        ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
        CHECK(kl.lit == 1u << 2 && kl.mid == 0 && kl.dim == 0,
              "step 2 held (and selected): lit; step 10's TIE past SEQ LENGTH dark (%08x %08x %08x)",
              (unsigned)kl.lit, (unsigned)kl.mid, (unsigned)kl.dim);
        ui_sm_arp_key(&sm, 2, FM1_LOW + 2u, 0, 0);
        ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
        CHECK(kl.mid == 1u << 2 && kl.lit == 0, "released: step 2 mid (selected)");
        sm.arp_rec = 1; sm.arp_rec_cursor = ARP_NSTEP + 3u;
        ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
        CHECK(kl.mid == 1u << 6 && kl.lit == 0, "stopped, REC armed: step 4 (B3), where the next chord goes, mid");
        sm.arp_rec = 0; sm.arp_rec_cursor = 0;
        arp_defaults(&arp_state);
    }
    /* hold a step, press a later one: the steps after the held one up to it tie */
    step(2, 57, 64, 0, ARPSTYLE_UP, 0);
    arp_state.steps[4].accent = 1;
    ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 1, 100);     /* step 3 held */
    ui_sm_arp_key(&sm, 9, FM1_LOW + 9u, 1, 100);     /* + step 6 */
    CHECK(arp_state.steps[2].chord_root == 57 && arp_state.steps[3].chord_root == ARP_CHORD_TIE &&
          arp_state.steps[4].chord_root == ARP_CHORD_TIE && arp_state.steps[5].chord_root == ARP_CHORD_TIE &&
          arp_state.steps[6].chord_root == ARP_CHORD_EMPTY && arp_state.steps[4].accent == 0 && sm.arp_sel == 2,
          "step 3 held + step 6: steps 4..6 tie, step 3 stays selected");
    ui_sm_arp_key(&sm, 9, FM1_LOW + 9u, 0, 0);
    ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 0, 0);
    ui_sm_arp_key(&sm, 18, FM1_LOW + 18u, 1, 100);   /* step 11 held */
    ui_sm_arp_key(&sm, 14, FM1_LOW + 14u, 1, 100);   /* + step 9, an earlier one: no tie */
    CHECK(arp_state.steps[9].chord_root == ARP_CHORD_EMPTY && arp_state.sequence_length == ARP_LEN_DEFAULT,
          "an earlier step pressed: no tie");
    ui_sm_arp_key(&sm, 14, FM1_LOW + 14u, 0, 0);
    ui_sm_arp_key(&sm, 18, FM1_LOW + 18u, 0, 0);
    ui_sm_arp_key(&sm, 18, FM1_LOW + 18u, 1, 100);   /* step 11 held .. */
    ui_sm_arp_key(&sm, 21, FM1_LOW + 21u, 1, 100);   /* + step 13: past SEQ LENGTH 8, which grows to take it */
    CHECK(arp_state.steps[11].chord_root == ARP_CHORD_TIE && arp_state.steps[12].chord_root == ARP_CHORD_TIE &&
          arp_state.sequence_length == 13, "a tie past SEQ LENGTH lengthens the pattern (%d)", arp_state.sequence_length);
    ui_sm_arp_key(&sm, 21, FM1_LOW + 21u, 0, 0);
    ui_sm_arp_key(&sm, 18, FM1_LOW + 18u, 0, 0);
    arp_defaults(&arp_state);

    /* ---- STEPS: the knobs and rows edit the selected step; CHORDS: the live dials ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
    {
        UiKnobInfo info;
        uint8_t *kb;
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 2, &info);
        CHECK(kb != NULL && info.kind == UIKNOB_LIST && info.n_enum == ARPSTYLE_COUNT && strcmp(info.label, "STYLE") == 0,
              "knob 3: STYLE list");
        *kb = ARPSTYLE_DOWN; ui_sm_knob_written(&sm, UIPAGE_ARP, 2);
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 3, &info);
        *kb = 13; ui_sm_knob_written(&sm, UIPAGE_ARP, 3);
        CHECK(strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 3), "5.07") == 0, "VARIATION text (the J-6's style 5, variation 07)");
        CHECK(arp_state.steps[0].style == ARPSTYLE_UP, "CHORDS, stopped: a knob turns the live dial only");
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 0, &info);
        CHECK(info.kind == UIKNOB_LIST && info.n_enum == ARPRATE_COUNT && strcmp(info.label, "RATE") == 0, "knob 1: RATE");
        *kb = 99; ui_sm_knob_written(&sm, UIPAGE_ARP, 0);
        CHECK(sm.arp_knob[0] == ARPRATE_COUNT - 1u && strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 0), "1/128") == 0,
              "RATE held to its list");
        *kb = ARP_RATE_DEFAULT; ui_sm_knob_written(&sm, UIPAGE_ARP, 0);
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);     /* STEPS, step 1 (a rest) selected */
        CHECK(sm.arp_view == UIARP_STEPS && sm.arp_sel == 0 && sm.arp_knob[2] == ARPSTYLE_DOWN,
              "STEPS on a rest: the knobs stay live");
        CHECK(strcmp(ui_sm_row_text(&sm, 0), "REST") == 0 && ui_sm_row_state(&sm, 1) == UIROW_GREYED &&
              ui_sm_row_state(&sm, 2) == UIROW_GREYED, "CHORD REST; ACCENT and CLEAR greyed on a rest");
        ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 1, 100);   /* step 3 */
        ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 0, 0);
        sm.s.current_param_focus = 0;
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
        CHECK(arp_state.steps[2].chord_root == ARP_CHORD_TIE && strcmp(ui_sm_row_text(&sm, 0), "TIE") == 0, "CHORD: REST -> TIE");
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
        CHECK(arp_state.steps[2].chord_root == 48 && strcmp(ui_sm_row_text(&sm, 0), "C3") == 0 &&
              arp_state.steps[2].style == ARPSTYLE_DOWN && arp_state.steps[2].variation == 13,
              "TIE -> C3 (no chord before it), the live dials captured with it (%d)", arp_state.steps[2].chord_root);
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 2);
        CHECK(arp_state.steps[2].chord_root == 50, "CHORD: +2 semitones");
        arp_state.steps[2].octave_range = 2;
        arp_state.steps[2].rate = ARPRATE_1_8;
        ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 1, 100);   /* reselect: its values on the knobs */
        ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 0, 0);
        CHECK(sm.arp_knob[0] == ARPRATE_1_8 && sm.arp_knob[1] == 2 && sm.arp_knob[2] == ARPSTYLE_DOWN &&
              strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 0), "1/8") == 0, "step 3 selected: its values on the knobs");
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 1, &info);
        *kb = 1; ui_sm_knob_written(&sm, UIPAGE_ARP, 1);
        CHECK(arp_state.steps[2].octave_range == 1 && arp_state.steps[0].octave_range == 0, "a knob edits the selected step");
        sm.s.current_param_focus = 1;                /* ACCENT */
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
        CHECK(arp_state.steps[2].accent == 1 && strcmp(ui_sm_row_text(&sm, 1), "ON") == 0, "ACCENT row: on");
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);     /* CHORDS: the live dials back */
        CHECK(sm.arp_view == UIARP_CHORDS && sm.arp_knob[0] == ARP_RATE_DEFAULT && sm.arp_knob[1] == 0 &&
              sm.arp_knob[3] == 13 && arp_state.steps[2].octave_range == 1, "back on CHORDS: the live dials, the step keeps its edit");
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);
        CHECK(sm.arp_sel == 2 && sm.arp_knob[1] == 1 && sm.s.current_param_focus == 1,
              "STEPS again: the same step on the knobs, the cursor where it was");
        ui_sm_goto(&sm, UIPAGE_HOME1);
        CHECK(sm.arp_knob[0] == ARP_RATE_DEFAULT && sm.arp_knob[1] == 0, "the page left from STEPS: the live dials back");
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);
        CHECK(sm.page == UIPAGE_ARP && sm.arp_view == UIARP_STEPS && sm.arp_knob[1] == 1, "ARP from another page: back on STEPS");
        sm.s.current_param_focus = 2;                /* CLEAR */
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
        CHECK(arp_state.steps[2].chord_root == ARP_CHORD_EMPTY && arp_state.steps[2].accent == 0 && sm.arp_knob[1] == 0,
              "CLEAR: a rest, the knobs live again");
        step(2, 57, 64, 0, ARPSTYLE_UP, 0);          /* a step's chord sounds while its key is held, stopped */
        k = count(&la, 0x90);
        ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 1, 90);
        run(4);
        ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 0, 0);
        run(4);
        n = carp_chord_notes(arp_state.current_genre_bank, 57, c);
        CHECK(n > 0 && count(&la, 0x90) == k + n && on_note(k) == c[0] && la.ev[nth(&la, 0x90, k)].d2 == 90 && all_off(&la),
              "STEPS, stopped: a step key sounds its chord while held (%d on)", count(&la, 0x90) - k);
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);
    }

    /* ---- a chord key held while the page changes is released (no hanging chord) ---- */
    ui_sm_arp_key(&sm, 0, FM1_LOW, 1, 100);
    run(2);
    ui_sm_goto(&sm, UIPAGE_SEQ);
    run(2);
    CHECK(sm.arp_chord_key == CARP_NOTE_NONE && all_off(&la) && la.stuck == 0, "leaving ARP releases the chord");

    /* ---- transposition while playing (8.2), and TRANSPOSE MAQ through the 1-frame mask ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);          /* BLOCK: every step strikes the chord from its bass */
    arp_state.sequence_length = 1;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    arp_state.transpose_maq_option = 1;
    play_on(UIPAGE_HOME1);                           /* both */
    run(QUARTER - 100u);
    ui_sm_arp_key(&sm, 9, FM1_LOW + 9u, 1, 100);     /* D4: C4 + 2 */
    run(4);
    ui_sm_arp_key(&sm, 9, FM1_LOW + 9u, 0, 0);
    CHECK(carp_transpose(&a) == 2, "playing: the chord key transposes by its distance from C4");
    { UiKeyLeds kl;
      ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
      CHECK(kl.mid == 1u << 9 && kl.lit == 0, "playing: the transposition's key mid, no step keys on CHORDS (%08x %08x)",
            (unsigned)kl.mid, (unsigned)kl.lit); }
    run(200u);
    { UiKeyLeds kl;                                   /* the notes sounding: dim, where they are on the keybed */
      uint32_t want = 0;
      for (i = 0; i < ARP_CHORD_NOTES; i++)
          if (a.on[i] != CARP_NOTE_NONE && a.on[i] >= FM1_LOW && a.on[i] < FM1_LOW + 32u)
              want |= 1u << (a.on[i] - FM1_LOW);
      ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
      CHECK(want != 0 && (kl.dim & ~(1u << 9)) == (want & ~(1u << 9)) && kl.mid == 1u << 9,
            "playing: the notes sounding dim on their keys, the transposition still mid (%08x %08x)",
            (unsigned)kl.dim, (unsigned)want); }
    k = -1;
    for (i = 0; i < la.n; i++)
        if ((la.ev[i].st & 0xF0u) == 0x90u && la.ev[i].frame >= QUARTER - 1u) { k = i; break; }
    CHECK(k >= 0 && la.ev[k].d1 == 50, "the next step plays C3 + 2, got %d", k >= 0 ? la.ev[k].d1 : -1);
    k = -1;
    for (i = 0; i < lm.n; i++)
        if ((lm.ev[i].st & 0xF0u) == 0x90u && lm.ev[i].frame >= QUARTER - 1u) { k = i; break; }
    CHECK(k >= 0 && lm.ev[k].d1 == 50, "the MAQ's C3 moves with it (TRANSPOSE MAQ), got %d", k >= 0 ? lm.ev[k].d1 : -1);
    play_on(UIPAGE_HOME1);                           /* stop */
    run(2);
    play_on(UIPAGE_HOME1);                           /* start again: as captured */
    run(2);
    CHECK(carp_transpose(&a) == 0 && all_off(&lm) && lm.stuck == 0, "a new start drops the transposition");

    /* ---- TRANSPOSE MAQ over an empty pattern: the MAQ follows the chord keys, stopped too, and a start keeps it ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
    arp_state.transpose_maq_option = 1;
    ui_sm_arp_key(&sm, 12, FM1_LOW + 12u, 1, 100);   /* stopped: F4, +5 */
    run(4);
    ui_sm_arp_key(&sm, 12, FM1_LOW + 12u, 0, 0);
    CHECK(carp_transpose(&a) == 5, "empty pattern, stopped: the chord key transposes the MAQ (%d)", carp_transpose(&a));
    play_on(UIPAGE_HOME1);
    run(QUARTER + 100u);
    k = nth(&lm, 0x90, 0);
    CHECK(carp_transpose(&a) == 5 && k >= 0 && lm.ev[k].d1 == 53, "a start keeps it: the MAQ's C3 plays F3, got %d",
          k >= 0 ? lm.ev[k].d1 : -1);
    ui_sm_arp_key(&sm, 7, FM1_LOW + 7u, 1, 100);     /* playing: C4, back to 0 */
    run(4);
    ui_sm_arp_key(&sm, 7, FM1_LOW + 7u, 0, 0);
    run(QUARTER);
    k = -1;
    for (i = 0; i < lm.n; i++)
        if ((lm.ev[i].st & 0xF0u) == 0x90u && lm.ev[i].frame >= QUARTER + 200u) { k = i; break; }
    CHECK(k >= 0 && lm.ev[k].d1 == 48, "playing: the MAQ follows the key, got %d", k >= 0 ? lm.ev[k].d1 : -1);
    play_on(UIPAGE_HOME1);
    run(2);
    arp_state.transpose_maq_option = 0;
    ui_sm_arp_key(&sm, 12, FM1_LOW + 12u, 1, 100);
    run(2);
    ui_sm_arp_key(&sm, 12, FM1_LOW + 12u, 0, 0);
    CHECK(carp_transpose(&a) == 0 && all_off(&lm) && lm.stuck == 0, "TRANSPOSE MAQ off, stopped: the key transposes nothing");

    /* ---- TRANSPOSE MAQ follows the chord roots, from the first sounding step, on the ARP step's own pulse ---- */
    {
        static const uint32_t at[5] = {0u, QUARTER, QUARTER + QUARTER / 4u, 2u * QUARTER, 3u * QUARTER};
        static const uint8_t want_on[5] = {48, 53, 53, 53, 45}, want_off[5] = {48, 48, 48, 48, 48};
        int o;
        for (o = 0; o < 2; o++) {
            reset();
            step(1, 48, 64, 0, ARPSTYLE_BLOCK, 0);  /* step 1 rests: the reference is step 2's C3 */
            step(2, 53, 64, 0, ARPSTYLE_BLOCK, 0);  /* F3: +5 */
            step(4, 45, 64, 0, ARPSTYLE_BLOCK, 0);  /* step 4 rests (keeps +5), step 5 A2: -3 */
            arp_state.sequence_length = 5;
            arp_state.transpose_maq_option = (uint8_t)(o == 0);
            play(1);
            run(4u * QUARTER + 10u);
            for (p = 0; p < 5; p++) {
                k = -1;
                for (i = 0; i < lm.n; i++)
                    if ((lm.ev[i].st & 0xF0u) == 0x90u && near(lm.ev[i].frame, at[p] + QUARTER)) { k = i; break; }
                CHECK(k >= 0 && lm.ev[k].d1 == (o == 0 ? want_on : want_off)[p],
                      "TRANSPOSE MAQ %s, MAQ note %d frames into step %d: got %d", o == 0 ? "on" : "off",
                      (int)(at[p] % QUARTER), (int)(at[p] / QUARTER) + 2, k >= 0 ? lm.ev[k].d1 : -1);
            }
            play(0);
            run(2);
            CHECK(all_off(&lm) && lm.stuck == 0, "no stuck MAQ notes with the root following");
        }
    }

    /* ---- LAYER BOTH in DUAL: both channels; under SINGL the row keeps BOTH (it plays L1) ---- */
    reset();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    arp_set_layer(&arp_state, ARPLAYER_BOTH);
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);
    arp_state.sequence_length = 1;
    play(1);
    run(10);
    CHECK(count(&la, 0x90) == 8 && (la.ev[0].st & 0x0Fu) == 0 && (la.ev[1].st & 0x0Fu) == 1, "BOTH: the chord on channels 1 and 2");
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    ui_sm_enforce(&sm);
    CHECK(ARP_LAYER(&arp_state) == ARPLAYER_BOTH, "SINGL: LAYER kept for the next DUAL");
    play(0);
    run(2);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes after a layer change");

    /* ---- a chord key held outside a capture arpeggiates live: the dials, TIME DIVISION, its own velocity ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
    arp_state.current_genre_bank = G_UTIL_MAJ;
    {
        uint8_t *kb;
        UiKnobInfo info;
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 3, &info);
        *kb = 1; ui_sm_knob_written(&sm, UIPAGE_ARP, 3);    /* RHYTHM 01B: o o _ _ */
        carp_chord_notes(G_UTIL_MAJ, FM1_LOW, c);
        ui_sm_arp_key(&sm, 0, FM1_LOW, 1, 80);              /* stopped */
        run(QUARTER * 2u - 10u);
        CHECK(count(&la, 0x90) == 4 && on_frame(0) == 0 && near(on_frame(1), CELL) && on_frame(2) >= QUARTER &&
              on_frame(2) <= QUARTER + 920u, "held, stopped: UP, two strikes a quarter from the key's frame (%d on, %u %u)",
              count(&la, 0x90), on_frame(1), on_frame(2));
        CHECK(on_note(0) == c[0] && on_note(1) == c[1] && on_note(2) == c[2] && on_note(3) == c[3] && la.ev[0].d2 == 80,
              "held: the key's own chord, upward, at its velocity: %u %u %u %u v%u", on_note(0), on_note(1), on_note(2),
              on_note(3), la.ev[0].d2);
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 2, &info);
        *kb = ARPSTYLE_STRUM_UP; ui_sm_knob_written(&sm, UIPAGE_ARP, 2);
        k = count(&la, 0x90);
        run(QUARTER / 2u - 2000u);                          /* the next step's two strikes: strums of the whole chord */
        CHECK(count(&la, 0x90) == k + 8 && on_note(k) == c[0] && on_note(k + 3) == c[3] && on_note(k + 4) == c[0] &&
              on_frame(k + 1) - on_frame(k) == CELL / 8u, "STRUM UP from the next strike (%d on, %u apart)",
              count(&la, 0x90) - k, on_frame(k + 1) - on_frame(k));
        CHECK(arp_state.steps[0].chord_root == ARP_CHORD_EMPTY, "a live hold captures nothing");
        ui_sm_arp_key(&sm, 0, FM1_LOW, 0, 0);
        run(2);
        k = count(&la, 0x90);
        run(QUARTER);
        CHECK(all_off(&la) && la.stuck == 0 && count(&la, 0x90) == k, "released: silent, nothing hangs");

        /* the transport plays an empty pattern: a held key arpeggiates on its grid; stopped while held, it plays on */
        ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
        run(QUARTER / 2u);
        k = count(&la, 0x90);
        ui_sm_arp_key(&sm, 2, FM1_LOW + 2u, 1, 100);
        run(2);
        CHECK(count(&la, 0x90) > k, "playing, empty pattern: the held key sounds at once");
        run(QUARTER * 2u);
        CHECK(count(&la, 0x90) >= k + 8 && carp_pos(&a) != CARP_POS_NONE, "and strums on the transport's steps (%d)",
              count(&la, 0x90) - k);
        ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);       /* stop, the key still held */
        run(2);
        k = count(&la, 0x90);
        run(QUARTER);
        CHECK(count(&la, 0x90) > k, "stopped while held: it plays on");
        ui_sm_arp_key(&sm, 2, FM1_LOW + 2u, 0, 0);
        run(QUARTER);
        CHECK(all_off(&la) && la.stuck == 0, "no stuck notes from a live hold");
    }

    /* ---- TIE: a one-note style runs on through the tied step, its note order unbroken ---- */
    reset();
    arp_state.current_genre_bank = G_UTIL_MAJ;
    carp_chord_notes(G_UTIL_MAJ, 48, c);
    step(0, 48, 64, 0, ARPSTYLE_UP, 1);              /* o o _ _ */
    arp_state.steps[1].chord_root = ARP_CHORD_TIE;
    arp_state.sequence_length = 2;
    play(1);
    run(2u * QUARTER - 10u);
    CHECK(count(&la, 0x90) == 4 && on_note(2) == c[2] && on_note(3) == c[3] && near(on_frame(2), QUARTER) &&
          near(on_frame(3), QUARTER + CELL), "TIE, UP: the arpeggio carries on into step 2 (%d on, %u %u)",
          count(&la, 0x90), on_note(2), on_note(3));
    play(0);
    run(2);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes after a tied arpeggio");

    /* ---- TIE: BLOCK holds its chord, struck once, until the step after the tie ---- */
    reset();
    arp_state.current_genre_bank = G_UTIL_MAJ;
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);           /* o _ _ _ at half gate: alone it would end within the first cell */
    arp_state.steps[1].chord_root = ARP_CHORD_TIE;
    arp_state.sequence_length = 3;                   /* step 3 rests */
    play(1);
    run(2u * QUARTER - 10u);
    CHECK(count(&la, 0x90) == 4 && count(&la, 0x80) == 0, "TIE, BLOCK: one strike, still sounding at the tie's end (%d on, %d off)",
          count(&la, 0x90), count(&la, 0x80));
    run(20u);
    k = nth(&la, 0x80, 0);
    CHECK(count(&la, 0x80) == 4 && k >= 0 && near(la.ev[k].frame, 2u * QUARTER), "released as step 3 begins (%u)",
          k >= 0 ? la.ev[k].frame : 0u);
    play(0);
    run(2);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes after a tied chord");

    /* ---- a TIE with nothing before it (step 1 at the start) rests ---- */
    reset();
    arp_state.steps[0].chord_root = ARP_CHORD_TIE;
    step(1, 48, 64, 0, ARPSTYLE_BLOCK, 0);
    arp_state.sequence_length = 2;
    play(1);
    run(QUARTER + 10u);
    CHECK(count(&la, 0x90) == 4 && near(on_frame(0), QUARTER), "a TIE on step 1 rests at the start (first on %u)", on_frame(0));
    play(0);
    run(2);

    /* ---- ACCENT: an accented step's first strike at ACCENT VEL, the rest at the base velocity ---- */
    reset();
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 1);           /* o o _ _ */
    arp_state.steps[0].accent = 1;
    arp_state.accent_velocity = 110;
    arp_state.sequence_length = 1;
    play(1);
    run(QUARTER - 10u);
    CHECK(count(&la, 0x90) == 8 && la.ev[nth(&la, 0x90, 0)].d2 == 110 && la.ev[nth(&la, 0x90, 3)].d2 == 110 &&
          la.ev[nth(&la, 0x90, 4)].d2 == perf_state.base_velocity, "ACCENT: first strike 110, second at the base velocity");
    play(0);
    run(2);

    /* ---- LATCH (ui_sm_arp_latch: ARP held + a key, or the LATCH row): the arpeggio outlives the key, survives a page
     * change, stops with LATCH off ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
#define KEY(k, d) ui_sm_arp_key(&sm, (k), (uint8_t)(FM1_LOW + (k)), (d), 100)
#define TAP(k) do { KEY(k, 1); run(2); KEY(k, 0); run(2); } while (0)
    sm.s.current_param_focus = 1;                    /* the LATCH row */
    CHECK(strcmp(ui_sm_row_label(&sm, 1), "LATCH") == 0, "CHORDS row 2: LATCH");
    ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
    CHECK(sm.arp_latch == 1 && strcmp(ui_sm_row_text(&sm, 1), "ON") == 0, "the LATCH row turns LATCH on");
    TAP(0);
    k = count(&la, 0x90);
    run(2u * QUARTER);                               /* (a live step is a quarter and a pulse) */
    CHECK(count(&la, 0x90) > k, "latched: the arpeggio plays on after the key");
    TAP(4);                                          /* another chord key takes over */
    carp_chord_notes(arp_state.current_genre_bank, FM1_LOW + 4u, c);
    run(QUARTER);
    CHECK(carp_shown(&a, cells) > 0 && cells[0] == c[0], "a new chord key replaces the latched chord");
    ui_sm_goto(&sm, UIPAGE_HOME1);
    k = count(&la, 0x90);
    run(2u * QUARTER);
    CHECK(count(&la, 0x90) > k, "latched: it plays on under another page");
    ui_sm_goto(&sm, UIPAGE_ARP);
    ui_sm_arp_latch(&sm, 0);                         /* LATCH off (ARP held + a key) */
    run(QUARTER);
    k = count(&la, 0x90);
    run(QUARTER);
    CHECK(sm.arp_latch == 0 && count(&la, 0x90) == k && all_off(&la) && la.stuck == 0, "LATCH off: silent, nothing hangs");

    /* ---- REC stopped: chords fill steps 1, 2, 3 .., SEQ LENGTH follows; a step chosen on STEPS takes the next ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    CHECK(sm.arp_rec == 1, "REC arms on the ARP page");
    TAP(0); TAP(5); TAP(7);
    CHECK(arp_state.steps[0].chord_root == FM1_LOW && arp_state.steps[1].chord_root == FM1_LOW + 5u &&
          arp_state.steps[2].chord_root == FM1_LOW + 7u && arp_state.steps[3].chord_root == ARP_CHORD_EMPTY &&
          arp_state.sequence_length == 3, "step entry: 3 chords, SEQ LENGTH 3 (%d)", arp_state.sequence_length);
    CHECK(carp_transpose(&a) == 0 && all_off(&la), "recording transposes nothing, every audition released");
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);         /* STEPS: step 10 chosen */
    CHECK(sm.arp_rec == 1, "REC stays armed across the views");
    TAP(16);
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);         /* CHORDS: the next two chords into steps 10 and 11 */
    TAP(2); TAP(9);
    CHECK(arp_state.steps[9].chord_root == FM1_LOW + 2u && arp_state.steps[10].chord_root == FM1_LOW + 9u &&
          arp_state.steps[2].chord_root == FM1_LOW + 7u && arp_state.sequence_length == 11,
          "from step 10: steps 10 and 11, SEQ LENGTH grows to 11 (%d)", arp_state.sequence_length);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    CHECK(sm.arp_rec == 0, "REC again disarms");

    /* ---- REC playing: chords land on the nearest step, knobs overdub the playing step ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
    for (i = 0; i < 4; i++)
        step((uint8_t)i, 48, 64, 0, ARPSTYLE_BLOCK, 0);
    arp_state.sequence_length = 4;
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    run(QUARTER / 4u);
    TAP(5);                                          /* early in step 1: step 1 */
    run(QUARTER / 2u);
    TAP(7);                                          /* late in step 1: step 2 */
    CHECK(arp_state.steps[0].chord_root == FM1_LOW + 5u && arp_state.steps[1].chord_root == FM1_LOW + 7u &&
          arp_state.sequence_length == 4 && carp_transpose(&a) == 0, "playing: quantized to the nearest step, no transposition");
    run(QUARTER + QUARTER / 4u);                     /* into step 3 */
    {
        UiKnobInfo info;
        uint8_t *kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 0, &info);
        *kb = ARPRATE_1_32; ui_sm_knob_written(&sm, UIPAGE_ARP, 0);
    }
    CHECK(carp_pos(&a) == 2 && arp_state.steps[2].rate == ARPRATE_1_32 && arp_state.steps[3].rate == ARP_RATE_DEFAULT,
          "playing + REC: the knob overdubs the playing step (pos %d)", carp_pos(&a));
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    run(4);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes after recording");
#undef TAP
#undef KEY

    /* ---- PLAY (2026-10-17): SEQ page the MAQ, ARP page the ARP, any other page both ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_SEQ);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    CHECK(maq_playing(&q) && !carp_playing(&a), "SEQ page: PLAY starts the MAQ alone");
    ui_sm_goto(&sm, UIPAGE_ARP);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    CHECK(maq_playing(&q) && carp_playing(&a), "ARP page: PLAY starts the ARP alone");
    ui_sm_goto(&sm, UIPAGE_SEQ);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    CHECK(!maq_playing(&q) && carp_playing(&a), "SEQ page: PLAY stops the MAQ, the ARP plays on");
    ui_sm_goto(&sm, UIPAGE_HOME1);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    CHECK(!maq_playing(&q) && !carp_playing(&a), "another page, one playing: PLAY stops it");
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    CHECK(maq_playing(&q) && carp_playing(&a), "another page, both stopped: PLAY starts both");
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    CHECK(!maq_playing(&q) && !carp_playing(&a), "another page, both playing: PLAY stops both");

    /* ---- one started while the other plays waits for its next bar (96 pulses from its first step) ---- */
    {
        uint32_t bar = 4u * QUARTER, t0, f0;
        reset();
        arp_state.current_genre_bank = G_UTIL_MAJ;
        step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);
        arp_state.sequence_length = 1;
        carp_set_playing(&a, 1);
        run(QUARTER + QUARTER / 3u);                 /* the ARP alone, a beat and a third in */
        t0 = now;
        maq_set_playing(&q, 1);
        run(bar);
        f0 = lm.n ? lm.ev[nth(&lm, 0x90, 0)].frame : 0u;
        CHECK(t0 < bar && f0 + 2u >= bar && f0 <= bar + 2u, "the MAQ started mid-bar plays its first step on the ARP's next bar (%u)", f0);
        carp_set_playing(&a, 0);
        run(QUARTER / 2u);
        k = count(&la, 0x90);
        carp_set_playing(&a, 1);
        run(f0 + bar - now - 10u);                   /* to just before the MAQ's next bar .. */
        CHECK(count(&la, 0x90) == k, "the ARP restarted mid-bar waits ..");
        run(20u);
        CHECK(count(&la, 0x90) == k + 4 && near(la.ev[nth(&la, 0x90, k)].frame, f0 + bar),
              ".. for the MAQ's next bar (%u, the MAQ's first step %u)", la.ev[nth(&la, 0x90, k)].frame, f0);
        play(0);
        run(2);
        CHECK(all_off(&la) && all_off(&lm) && la.stuck == 0 && lm.stuck == 0, "no stuck notes with two transports");
    }

    /* ---- TIMING (2026-10-21): every step's length, the pattern's; RATE: the rate its rhythm runs at, round and round ---- */
    reset();
    arp_state.current_genre_bank = G_UTIL_MAJ;
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);           /* o _ _ _ */
    step(1, 50, 64, 0, ARPSTYLE_BLOCK, 0);
    step(2, 52, 64, 0, ARPSTYLE_BLOCK, 0);
    arp_state.sequence_length = 3;
    arp_set_timing(&arp_state, ARPDIV_1_8);
    carp_set_playing(&a, 1);
    run(QUARTER - 10u);
    CHECK(count(&la, 0x90) == 8 && near(on_frame(4), QUARTER / 2u) && on_note(4) == 50,
          "TIMING 1/8: step 2 an eighth in (%u), one strike a step (RATE 1/16: half the rhythm fits)", on_frame(4));
    run(20u);
    CHECK(count(&la, 0x90) == 12 && near(on_frame(8), QUARTER) && on_note(8) == 52, "step 3 a quarter in (%u)", on_frame(8));
    play(0);
    run(2);
    reset();                                         /* TIMING 1/2, RATE 1/16: the rhythm twice a step */
    arp_state.current_genre_bank = G_UTIL_MAJ;
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);
    arp_state.sequence_length = 1;
    arp_set_timing(&arp_state, ARPDIV_1_2);
    carp_set_playing(&a, 1);
    run(2u * QUARTER - 10u);
    CHECK(count(&la, 0x90) == 8 && near(on_frame(4), QUARTER), "TIMING 1/2: o _ _ _ again a beat in (%u)", on_frame(4));
    play(0);
    run(2);
    reset();                                         /* TIMING 1/4, RATE 1/32: twice a step, cells a 32nd long */
    arp_state.current_genre_bank = G_UTIL_MAJ;
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);
    arp_state.steps[0].rate = ARPRATE_1_32;
    arp_state.sequence_length = 1;
    carp_set_playing(&a, 1);
    run(QUARTER - 10u);
    k = nth(&la, 0x80, 0);
    CHECK(count(&la, 0x90) == 8 && near(on_frame(4), QUARTER / 2u) && k >= 0 && near(la.ev[k].frame, (CELL / 2u) * 65u >> 7),
          "RATE 1/32: struck an eighth apart, the gate over a 32nd cell (%u, gate end %u)", on_frame(4),
          k >= 0 ? la.ev[k].frame : 0u);
    run(20u);
    CHECK(count(&la, 0x90) == 12 && near(on_frame(8), QUARTER), "the next step on the beat, not a cell early (%u)", on_frame(8));
    play(0);
    run(2);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes with TIMING and RATE");

    /* ---- HOLD: struck once, held to the step's end; the same chord on is never struck again ---- */
    reset();
    arp_state.current_genre_bank = G_UTIL_MAJ;
    step(0, 48, 20, 0, ARPSTYLE_HOLD, 0);            /* a short GATE: it does not apply */
    step(1, 48, 20, 0, ARPSTYLE_HOLD, 0);
    step(2, 53, 20, 0, ARPSTYLE_HOLD, 0);
    arp_state.sequence_length = 3;
    carp_set_playing(&a, 1);
    run(2u * QUARTER - 10u);
    CHECK(count(&la, 0x90) == 4 && count(&la, 0x80) == 0, "HOLD, the same chord twice: struck once, held (%d on, %d off)",
          count(&la, 0x90), count(&la, 0x80));
    run(20u);
    k = nth(&la, 0x80, 0);
    CHECK(count(&la, 0x90) == 8 && count(&la, 0x80) == 4 && k >= 0 && near(la.ev[k].frame, 2u * QUARTER),
          "a new chord: the held one off, the new one struck at the step");
    play(0);
    run(2);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes after HOLD");
    reset();                                         /* the rhythm is the strum (UP, DOWN), RATE its gap */
    arp_state.current_genre_bank = G_UTIL_MAJ;
    carp_chord_notes(G_UTIL_MAJ, 48, c);
    step(0, 48, 64, 0, ARPSTYLE_HOLD, 1);
    step(1, 53, 64, 0, ARPSTYLE_HOLD, 2);
    arp_state.steps[0].rate = ARPRATE_1_128;
    arp_state.steps[1].rate = ARPRATE_1_64;
    arp_state.sequence_length = 2;
    carp_set_playing(&a, 1);
    run(QUARTER + 6000u);
    {
        uint32_t gap = (235200u * 3u) >> 10;         /* 918.75 frames a pulse, q8; 1/128 = 3 quarter pulses */
        uint32_t gap2 = (235200u * 6u) >> 10;        /* 1/64 */
        uint8_t c53[ARP_CHORD_NOTES];
        carp_chord_notes(G_UTIL_MAJ, 53, c53);
        CHECK(on_note(0) == c[0] && on_note(3) == c[3] && on_frame(1) - on_frame(0) == gap && on_frame(3) == 3u * gap,
              "UP at 1/128: bass first, %u frames apart (%u)", gap, on_frame(1) - on_frame(0));
        CHECK(on_note(4) == c53[3] && on_note(7) == c53[0] && near(on_frame(4), QUARTER) &&
              on_frame(5) - on_frame(4) == gap2, "DOWN at 1/64: the top first, RATE apart (%u)", on_frame(5) - on_frame(4));
    }
    play(0);
    run(2);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes after a strummed HOLD");
    reset();                                         /* fast TIMING, a strum longer than the step, 2 octaves: one strum */
    arp_state.current_genre_bank = G_UTIL_MAJ;
    for (i = 0; i < 8; i++)
        step((uint8_t)i, 48, 64, 1, ARPSTYLE_HOLD, 1);
    arp_set_timing(&arp_state, ARPDIV_1_32T);
    carp_set_playing(&a, 1);
    run(4u * QUARTER);
    CHECK(count(&la, 0x90) == 4 && count(&la, 0x80) == 0 && on_frame(3) == 3u * ((235200u * 24u) >> 10),
          "HOLD at 1/32T, strummed at 1/16 over 2 octaves: struck once, held (%d on, %d off)", count(&la, 0x90),
          count(&la, 0x80));
    for (i = 0; i < 8; i++)                          /* another chord: the held one off, the new one an octave up */
        step((uint8_t)i, 50, 64, 1, ARPSTYLE_HOLD, 1);
    run(QUARTER);
    k = nth(&la, 0x90, 4);
    CHECK(count(&la, 0x80) == 4 && k >= 0 && la.ev[k].d1 == 50 + 12, "a new chord: struck an octave up (%u)",
          k >= 0 ? la.ev[k].d1 : 0u);
    play(0);
    run(2);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes after a strummed HOLD");
    {
        UiKnobInfo info;
        uint8_t *kb;
        reset();
        ui_sm_goto(&sm, UIPAGE_ARP);
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 3, &info);
        *kb = 14; ui_sm_knob_written(&sm, UIPAGE_ARP, 3);
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 2, &info);
        *kb = 0; ui_sm_knob_written(&sm, UIPAGE_ARP, 2);
        CHECK(strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 2), "HOLD") == 0 && sm.arp_knob[3] == 2u,
              "HOLD first in the STYLE list; RHYTHM 14 pulled into the strum's list");
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 3, &info);
        *kb = 9; ui_sm_knob_written(&sm, UIPAGE_ARP, 3);
        CHECK(strcmp(info.label, "STRUM") == 0 && info.n_enum == ARP_NSTRUM && sm.arp_knob[3] == 2u &&
              strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 3), "DOWN") == 0, "STYLE HOLD: knob 4 reads STRUM");
        *kb = 1; ui_sm_knob_written(&sm, UIPAGE_ARP, 3);
        CHECK(strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 3), "UP") == 0, "STRUM UP");
    }
    {                                                /* TIMING: a STEPS row (the 5th), in LAYER's byte */
        reset();
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);
        CHECK(strcmp(ui_sm_row_label(&sm, 4), "TIMING") == 0 && strcmp(ui_sm_row_text(&sm, 4), "1/4") == 0,
              "STEPS row 5: TIMING 1/4 (%s %s)", ui_sm_row_label(&sm, 4), ui_sm_row_text(&sm, 4));
        arp_set_layer(&arp_state, ARPLAYER_L2);
        arp_set_timing(&arp_state, ARPDIV_1_32T);
        CHECK(ARP_LAYER(&arp_state) == ARPLAYER_L2 && strcmp(ui_sm_row_text(&sm, 4), "1/32T") == 0 &&
              carp_div_ticks(ARP_TIMING(&arp_state)) == 2, "TIMING and LAYER share a byte, apart");
    }

    if (fails == 0)
        printf("all ARP checks passed\n");
    return fails;
}
