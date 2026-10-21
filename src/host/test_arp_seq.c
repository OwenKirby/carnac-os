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
#define G_UTIL_MAJ 16u               /* arp_genre_names: Utility_Maj */
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
    s->chord_root = root; s->rate = ARPDIV_1_4; s->octave_range = oct; s->style = style; s->variation = var;
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

    /* ---- the chord bank (8.3): 4 ascending voices on the key, spec rows kept ---- */
    for (g = 0; g < (int)ARP_GENRE_COUNT; g++)
        for (p = 0; p < 12; p++) {
            const uint8_t *iv = arp_chord_iv[g][p];
            CHECK(iv[0] == 0 && iv[1] > 0 && iv[2] > iv[1] && iv[3] > iv[2] && iv[3] <= 40,
                  "%s pc %d: %u %u %u %u", arp_genre_names[g], p, iv[0], iv[1], iv[2], iv[3]);
        }
    CHECK(strcmp(arp_genre_names[0], "Pop_1") == 0 && strcmp(arp_genre_names[ARP_GENRE_COUNT - 2u], "Modern_2") == 0 &&
          strcmp(arp_genre_names[G_UTIL_MAJ], "Utility_Maj") == 0, "8.2 genre order");
    CHECK(strcmp(arp_genre_names[ARP_GENRE_PHRYGIAN], "Phrygian") == 0 && (arp_genre_scale[ARP_GENRE_PHRYGIAN] & 15u) ==
          SEQSCALE_PHRYGIAN, "Phrygian after the spec's list, on the Phrygian scale");
    { ArpState d; arp_defaults(&d);
      CHECK(d.current_genre_bank == ARP_GENRE_PHRYGIAN && d.steps[3].rate == ARPDIV_1_4 && d.gate_length == ARP_GATE_DEFAULT,
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
    CHECK(carp_div_ticks(ARPDIV_2) == 192 && carp_div_ticks(ARPDIV_1_4) == 24 && carp_div_ticks(ARPDIV_1_4T) == 16 &&
          carp_div_ticks(ARPDIV_1_32) == 3, "TIME DIVISION in pulses");

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
    step(0, 48, 64, 0, ARPSTYLE_UP, 19);             /* 10T: o o _ (triplet) */
    arp_state.sequence_length = 1;
    arp_state.current_genre_bank = G_UTIL_MAJ;
    play(1);
    run(QUARTER - 10u);
    CHECK(count(&la, 0x90) == 2 && near(on_frame(1), QUARTER / 3u), "triplet cells: a third of the step, %u", on_frame(1));
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

    /* ---- keybed split (8.1) on the FM1's 27 keys, F3 .. G5 ---- */
    reset();
    CHECK(ui_sm_arp_key(&sm, 0, FM1_LOW, 1, 100) == 0, "not on the ARP page: plays");
    ui_sm_goto(&sm, UIPAGE_ARP);
    CHECK(ui_sm_arp_key(&sm, 12, FM1_LOW + 12u, 1, 100) == 1 && sm.arp_chord_key == FM1_LOW + 12u, "key 13 (F4) is a chord key");
    ui_sm_arp_key(&sm, 12, FM1_LOW + 12u, 0, 0);
    CHECK(ui_sm_arp_key(&sm, 13, FM1_LOW + 13u, 1, 100) == 1 && sm.arp_held == 0, "F#4: a black key in the step zone, nothing");
    CHECK(ui_sm_arp_key(&sm, 14, FM1_LOW + 14u, 1, 100) == 1 && sm.arp_held == 0x01, "G4 = step 1");
    ui_sm_arp_key(&sm, 14, FM1_LOW + 14u, 0, 0);
    CHECK(ui_sm_arp_key(&sm, 26, FM1_LOW + 26u, 1, 100) == 1 && sm.arp_held == 0x80, "G5 = step 8");
    ui_sm_arp_key(&sm, 26, FM1_LOW + 26u, 0, 0);
    CHECK(sm.arp_held == 0 && ui_sm_arp_key(&sm, 28, FM1_LOW + 28u, 1, 100) == 0, "above the 8 step keys: plays");
    {   /* key LEDs from F3: step keys G4 A4 B4 C5 D5 E5 F5 G5 = keys 14 16 18 19 21 23 24 26; functions G#4 LAT 15,
         * A#4 TIE 17, C#5 CLR 20, D#5 CPY 22, F#5 ACC 25 */
        const uint32_t FN = 1u << 15 | 1u << 17 | 1u << 20 | 1u << 22 | 1u << 25;
        UiKeyLeds kl;
        CHECK(ui_sm_key_leds(&sm, FM1_LOW, 0, &kl) && kl.lit == 0 && kl.mid == 0 && kl.dim == FN,
              "ARP LEDs, empty pattern: the steps dark (rests), the function keys dim (%08x)", (unsigned)kl.dim);
        arp_state.steps[1].chord_root = 60; arp_state.steps[6].chord_root = ARP_CHORD_TIE;
        arp_state.sequence_length = 6;
        ui_sm_arp_key(&sm, 16, FM1_LOW + 16u, 1, 100);                  /* hold step 2 */
        ui_sm_arp_key(&sm, 15, FM1_LOW + 15u, 1, 100);                  /* LATCH on */
        ui_sm_arp_key(&sm, 15, FM1_LOW + 15u, 0, 0);
        ui_sm_arp_key(&sm, 25, FM1_LOW + 25u, 1, 100);                  /* ACC held (accents step 2) */
        ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
        CHECK(kl.lit == (1u << 16 | 1u << 15 | 1u << 25) && kl.dim == (FN & ~(1u << 15 | 1u << 25)) && kl.mid == 0,
              "held step 2, LATCH on, ACC held: lit; step 7's TIE past SEQ LENGTH dark (%08x %08x)", (unsigned)kl.lit, (unsigned)kl.dim);
        ui_sm_arp_key(&sm, 25, FM1_LOW + 25u, 0, 0);
        ui_sm_arp_key(&sm, 16, FM1_LOW + 16u, 0, 0);
        ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
        CHECK(kl.dim == ((FN & ~(1u << 15)) | 1u << 16) && kl.lit == 1u << 15, "released: step 2 (a chord) dim, LATCH still lit");
        ui_sm_arp_key(&sm, 15, FM1_LOW + 15u, 1, 100);                  /* LATCH off */
        ui_sm_arp_key(&sm, 15, FM1_LOW + 15u, 0, 0);
        sm.arp_rec = 1; sm.arp_rec_cursor = 3;
        ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
        CHECK(kl.mid == 1u << 19 && kl.lit == 0, "stopped, REC armed: step 4 (C5), where the next chord goes, mid");
        sm.arp_rec = 0; sm.arp_rec_cursor = 0;
        arp_defaults(&arp_state);
    }

    /* ---- capture: hold a step key, strike a chord key; knobs into held steps ---- */
    {
        UiKnobInfo info;
        uint8_t *kb;
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 2, &info);
        CHECK(kb != NULL && info.kind == UIKNOB_LIST && info.n_enum == ARPSTYLE_COUNT && strcmp(info.label, "STYLE") == 0,
              "knob 3: STYLE list");
        *kb = ARPSTYLE_DOWN; ui_sm_knob_written(&sm, UIPAGE_ARP, 2);
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 3, &info);
        *kb = 13; ui_sm_knob_written(&sm, UIPAGE_ARP, 3);
        CHECK(strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 3), "07T") == 0, "RHYTHM text");
        ui_sm_arp_key(&sm, 18, FM1_LOW + 18u, 1, 100);                 /* B4 = step 3 */
        ui_sm_arp_key(&sm, 21, FM1_LOW + 21u, 1, 100);                 /* D5 = step 5 */
        ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 1, 90);                    /* A3 */
        run(4);                      /* (a press and release between two frames would merge into the release) */
        ui_sm_arp_key(&sm, 4, FM1_LOW + 4u, 0, 0);
        CHECK(arp_state.steps[2].chord_root == FM1_LOW + 4u && arp_state.steps[4].chord_root == FM1_LOW + 4u &&
              arp_state.steps[2].style == ARPSTYLE_DOWN && arp_state.steps[4].variation == 13 &&
              arp_state.steps[2].rate == ARP_RATE_DEFAULT && arp_state.steps[0].chord_root == ARP_CHORD_EMPTY,
              "A3 + the dials captured into steps 3 and 5 only");
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 0, &info);
        CHECK(info.kind == UIKNOB_LIST && info.n_enum == ARPDIV_COUNT && strcmp(info.label, "RATE") == 0, "knob 1: RATE");
        *kb = ARPDIV_1_8; ui_sm_knob_written(&sm, UIPAGE_ARP, 0);
        CHECK(strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 0), "1/8") == 0 && arp_state.steps[2].rate == ARPDIV_1_8 &&
              arp_state.steps[4].rate == ARPDIV_1_8 && arp_state.steps[3].rate == ARP_RATE_DEFAULT,
              "a knob turned with step keys held edits those steps");
        ui_sm_arp_key(&sm, 18, FM1_LOW + 18u, 0, 0);
        ui_sm_arp_key(&sm, 21, FM1_LOW + 21u, 0, 0);
        *kb = ARPDIV_1_16; ui_sm_knob_written(&sm, UIPAGE_ARP, 0);
        CHECK(arp_state.steps[2].rate == ARPDIV_1_8, "released: the knob edits only the live value");
        *kb = 99; ui_sm_knob_written(&sm, UIPAGE_ARP, 0);
        CHECK(sm.arp_knob[0] == ARPDIV_COUNT - 1u, "RATE held to its list");
        *kb = ARPDIV_1_4; ui_sm_knob_written(&sm, UIPAGE_ARP, 0);
        run(4);
        carp_chord_notes(arp_state.current_genre_bank, FM1_LOW + 4u, c);
        CHECK(count(&la, 0x90) == 4 && count(&la, 0x80) == 4 && on_note(0) == c[0] && la.ev[0].d2 == 90 && all_off(&la),
              "the chord key sounded its chord while held (%d on, %d off)", count(&la, 0x90), count(&la, 0x80));
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
    ui_sm_arp_key(&sm, 2, FM1_LOW + 2u, 1, 100);     /* the zone's third key: +2 */
    run(4);
    ui_sm_arp_key(&sm, 2, FM1_LOW + 2u, 0, 0);
    CHECK(carp_transpose(&a) == 2, "playing: the chord key transposes by its place in the zone");
    { UiKeyLeds kl;
      ui_sm_key_leds(&sm, FM1_LOW, 0, &kl);
      CHECK(kl.mid == 1u << 2 && kl.lit == 1u << 14, "playing: the transposition's chord key mid, step 1's key lit (%08x %08x)",
            (unsigned)kl.mid, (unsigned)kl.lit); }
    run(200u);
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
    ui_sm_arp_key(&sm, 5, FM1_LOW + 5u, 1, 100);     /* stopped: +5 */
    run(4);
    ui_sm_arp_key(&sm, 5, FM1_LOW + 5u, 0, 0);
    CHECK(carp_transpose(&a) == 5, "empty pattern, stopped: the chord key transposes the MAQ (%d)", carp_transpose(&a));
    play_on(UIPAGE_HOME1);
    run(QUARTER + 100u);
    k = nth(&lm, 0x90, 0);
    CHECK(carp_transpose(&a) == 5 && k >= 0 && lm.ev[k].d1 == 53, "a start keeps it: the MAQ's C3 plays F3, got %d",
          k >= 0 ? lm.ev[k].d1 : -1);
    ui_sm_arp_key(&sm, 0, FM1_LOW, 1, 100);         /* playing: back to 0 */
    run(4);
    ui_sm_arp_key(&sm, 0, FM1_LOW, 0, 0);
    run(QUARTER);
    k = -1;
    for (i = 0; i < lm.n; i++)
        if ((lm.ev[i].st & 0xF0u) == 0x90u && lm.ev[i].frame >= QUARTER + 200u) { k = i; break; }
    CHECK(k >= 0 && lm.ev[k].d1 == 48, "playing: the MAQ follows the key, got %d", k >= 0 ? lm.ev[k].d1 : -1);
    play_on(UIPAGE_HOME1);
    run(2);
    arp_state.transpose_maq_option = 0;
    ui_sm_arp_key(&sm, 5, FM1_LOW + 5u, 1, 100);
    run(2);
    ui_sm_arp_key(&sm, 5, FM1_LOW + 5u, 0, 0);
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

    /* ---- LAYER BOTH in DUAL: both channels; under SINGL the row falls back to L1 ---- */
    reset();
    perf_state.perf_mode = PERF_DUAL_SYNTH;
    arp_state.target_layer = ARPLAYER_BOTH;
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);
    arp_state.sequence_length = 1;
    play(1);
    run(10);
    CHECK(count(&la, 0x90) == 8 && (la.ev[0].st & 0x0Fu) == 0 && (la.ev[1].st & 0x0Fu) == 1, "BOTH: the chord on channels 1 and 2");
    perf_state.perf_mode = PERF_SINGL_SYNTH;
    ui_sm_enforce(&sm);
    CHECK(arp_state.target_layer == ARPLAYER_L1, "SINGL: LAYER back to L1");
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

    /* ---- function keys (the black keys among the step keys): CLEAR, COPY, TIE, ACCENT ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
#define KEY(k, d) ui_sm_arp_key(&sm, (k), (uint8_t)(FM1_LOW + (k)), (d), 100)
#define TAP(k) do { KEY(k, 1); run(2); KEY(k, 0); run(2); } while (0)
    KEY(14, 1); TAP(0); KEY(14, 0);                  /* step 1 <- F3 */
    KEY(16, 1); TAP(4); KEY(16, 0);                  /* step 2 <- A3 */
    CHECK(arp_state.steps[0].chord_root == FM1_LOW && arp_state.steps[1].chord_root == FM1_LOW + 4u, "captures for the key tests");
    KEY(20, 1); TAP(14); KEY(20, 0);                 /* CLEAR held, tap step 1 */
    CHECK(arp_state.steps[0].chord_root == ARP_CHORD_EMPTY && sm.arp_held == 0, "CLEAR + step 1: a rest (and not held)");
    KEY(22, 1); TAP(16); TAP(21); TAP(23); KEY(22, 0);   /* COPY: step 2 -> steps 5 and 6 */
    CHECK(arp_state.steps[4].chord_root == FM1_LOW + 4u && arp_state.steps[5].chord_root == FM1_LOW + 4u &&
          arp_state.steps[2].chord_root == ARP_CHORD_EMPTY && sm.arp_copy_src == CARP_POS_NONE, "COPY: step 2 into 5 and 6");
    KEY(18, 1); TAP(17); KEY(18, 0);                 /* step 3 held, tap TIE */
    CHECK(arp_state.steps[2].chord_root == ARP_CHORD_TIE, "step 3 held + TIE: a tie");
    KEY(25, 1); TAP(16); KEY(25, 0);
    CHECK(arp_state.steps[1].accent == 1, "ACCENT + step 2: accented");
    KEY(16, 1); TAP(25); KEY(16, 0);
    CHECK(arp_state.steps[1].accent == 0, "step 2 held + ACCENT: toggled off again");
    CHECK(KEY(13, 1) == 1 && sm.arp_fn == 0 && sm.arp_latch == 0, "F#4 (below step 1) is no function key");
    KEY(13, 0);

    /* ---- LATCH: the arpeggio outlives the key, survives a page change, stops with LATCH off ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
    TAP(15);                                         /* G#4: LATCH on */
    CHECK(sm.arp_latch == 1, "G#4 toggles LATCH");
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
    TAP(15);                                         /* LATCH off */
    run(QUARTER);
    k = count(&la, 0x90);
    run(QUARTER);
    CHECK(sm.arp_latch == 0 && count(&la, 0x90) == k && all_off(&la) && la.stuck == 0, "LATCH off: silent, nothing hangs");

    /* ---- REC stopped: step entry fills steps 1, 2, 3 .., SEQ LENGTH follows; TIE / CLEAR / ACCENT alone record ---- */
    reset();
    ui_sm_goto(&sm, UIPAGE_ARP);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    CHECK(sm.arp_rec == 1, "REC arms on the ARP page");
    TAP(0); TAP(5); TAP(7);
    TAP(17);                                         /* TIE */
    TAP(20);                                         /* CLEAR */
    TAP(25);                                         /* ACCENT: the step just recorded */
    CHECK(arp_state.steps[0].chord_root == FM1_LOW && arp_state.steps[1].chord_root == FM1_LOW + 5u &&
          arp_state.steps[2].chord_root == FM1_LOW + 7u && arp_state.steps[3].chord_root == ARP_CHORD_TIE &&
          arp_state.steps[4].chord_root == ARP_CHORD_EMPTY && arp_state.steps[4].accent == 1 &&
          arp_state.sequence_length == 5, "step entry: 3 chords, a tie, a rest; SEQ LENGTH 5 (%d)", arp_state.sequence_length);
    CHECK(carp_transpose(&a) == 0 && all_off(&la), "recording transposes nothing, every audition released");
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
        *kb = ARPDIV_1_16; ui_sm_knob_written(&sm, UIPAGE_ARP, 0);
    }
    CHECK(carp_pos(&a) == 2 && arp_state.steps[2].rate == ARPDIV_1_16 && arp_state.steps[3].rate == ARPDIV_1_4,
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

    /* ---- each step its own RATE ---- */
    reset();
    arp_state.current_genre_bank = G_UTIL_MAJ;
    step(0, 48, 64, 0, ARPSTYLE_BLOCK, 0);
    step(1, 50, 64, 0, ARPSTYLE_BLOCK, 0);
    step(2, 52, 64, 0, ARPSTYLE_BLOCK, 0);
    arp_state.steps[0].rate = ARPDIV_1_8;
    arp_state.steps[1].rate = ARPDIV_1_2;
    arp_state.sequence_length = 3;
    carp_set_playing(&a, 1);
    run(QUARTER / 2u + 2u * QUARTER - 10u);
    CHECK(count(&la, 0x90) == 8 && near(on_frame(4), QUARTER / 2u) && on_note(4) == 50,
          "1/8 then 1/2: step 2 at an eighth (%u)", on_frame(4));
    run(20u);
    CHECK(count(&la, 0x90) == 12 && near(on_frame(8), QUARTER / 2u + 2u * QUARTER) && on_note(8) == 52,
          "step 3 after the half (%u)", on_frame(8));
    k = nth(&la, 0x80, 4);
    CHECK(k >= 0 && near(la.ev[k].frame, QUARTER / 2u + ((2u * QUARTER / 4u) * 65u >> 7)),
          "a 1/2 step's cells are an eighth long (gate end %u)", k >= 0 ? la.ev[k].frame : 0u);
    play(0);
    run(2);

    /* ---- HOLD: struck once, held to the step's end; the same chord on is never struck again ---- */
    reset();
    arp_state.current_genre_bank = G_UTIL_MAJ;
    step(0, 48, 20, 0, ARPSTYLE_HOLD, 3);            /* a short GATE and o o o o's rhythm: neither applies */
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
    reset();                                         /* the rhythm is the strum: UP 2, DN 2 */
    arp_state.current_genre_bank = G_UTIL_MAJ;
    carp_chord_notes(G_UTIL_MAJ, 48, c);
    step(0, 48, 64, 0, ARPSTYLE_HOLD, 2);
    step(1, 53, 64, 0, ARPSTYLE_HOLD, 14);
    arp_state.sequence_length = 2;
    carp_set_playing(&a, 1);
    run(QUARTER + 2000u);
    {
        uint32_t gap = (235200u * 2u) >> 10;         /* 918.75 frames a pulse, q8; 2 quarter pulses */
        uint8_t c53[ARP_CHORD_NOTES];
        carp_chord_notes(G_UTIL_MAJ, 53, c53);
        CHECK(on_note(0) == c[0] && on_note(3) == c[3] && on_frame(1) - on_frame(0) == gap && on_frame(3) == 3u * gap,
              "UP 2: bass first, %u frames apart (%u)", gap, on_frame(1) - on_frame(0));
        CHECK(on_note(4) == c53[3] && on_note(7) == c53[0] && near(on_frame(4), QUARTER), "DN 2: the top first");
    }
    play(0);
    run(2);
    CHECK(all_off(&la) && la.stuck == 0, "no stuck notes after a strummed HOLD");
    {
        UiKnobInfo info;
        uint8_t *kb;
        reset();
        ui_sm_goto(&sm, UIPAGE_ARP);
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 2, &info);
        *kb = ARPSTYLE_HOLD; ui_sm_knob_written(&sm, UIPAGE_ARP, 2);
        kb = ui_sm_knob(&sm, UIPAGE_ARP, 0, 3, &info);
        *kb = 14; ui_sm_knob_written(&sm, UIPAGE_ARP, 3);
        CHECK(strcmp(info.label, "STRUM") == 0 && strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 2), "HOLD") == 0 &&
              strcmp(ui_sm_knob_text(&sm, UIPAGE_ARP, 0, 3), "DN 2") == 0, "STYLE HOLD: knob 4 reads STRUM");
    }

    if (fails == 0)
        printf("all ARP checks passed\n");
    return fails;
}
