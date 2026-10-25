/* Host test for the MAQ 16/4 sequencer engine and its SEQ page controls (spec 7.1 - 7.5, 8.4, 6.3).
 * Drives midi_clock and maq_frame frame by frame at 120 BPM (918.75 frames per 24 PPQN tick) and records every MIDI
 * message the engine sends. Exit code 0 = pass. Test scaffolding only.
 *
 *   gcc -O1 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/test_maq_seq.c -o test_maq_seq */
#include <stdio.h>
#include <string.h>
#include "UIStateMachine.h"
#include "preset.h"
#include "dsp/midi_clock.h"
#include "dsp/maq_seq.h"
#include "dsp/seq_scale.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define STEP_FRAMES 5512u            /* 6 ticks at 120 BPM: 5512.5 frames */
#define NEV 4096

typedef struct { uint32_t frame; uint8_t st, d1, d2; } Ev;
static Ev ev[NEV];
static int nev;
static uint32_t now;
static int8_t sounding[16][128];     /* per channel: note-ons minus note-offs */
static int stuck_errors;

static void sink(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t kind = st & 0xF0u, ch = st & 0x0Fu;
    if (nev < NEV) { ev[nev].frame = now; ev[nev].st = st; ev[nev].d1 = d1; ev[nev].d2 = d2; nev++; }
    if (kind == 0x90u) { if (sounding[ch][d1] != 0) stuck_errors++; sounding[ch][d1]++; }
    if (kind == 0x80u) { if (sounding[ch][d1] != 1) stuck_errors++; sounding[ch][d1]--; }
}

static MaqSeq q;
static SequencerLane lanes[SEQ_NLANE];
static ArpState arp;
static UIStateMachine sm;

static void run(uint32_t frames)
{
    while (frames--) {
        midi_clock_frame(now);
        maq_frame(&q, now);
        now++;
    }
}

static void reset(void)
{
    preset_defaults();
    midi_clock_init();
    seq_defaults(lanes);
    nev = 0;
    now = 0;
    stuck_errors = 0;
    memset(sounding, 0, sizeof sounding);
    maq_init(&q, lanes, &perf_state, sink);
    memset(&arp, 0, sizeof arp);
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], lanes, &arp);
    ui_sm_attach_seq(&sm, &q);
}

static int count(uint8_t kind) { int i, n = 0; for (i = 0; i < nev; i++) n += (ev[i].st & 0xF0u) == kind; return n; }
static int nth(uint8_t kind, int k) { int i; for (i = 0; i < nev; i++) if ((ev[i].st & 0xF0u) == kind && k-- == 0) return i; return -1; }
static int all_off(void) { int c, n; for (c = 0; c < 16; c++) for (n = 0; n < 128; n++) if (sounding[c][n]) return 0; return 1; }

/* the playhead's step sequence over k steps of lane 0 */
static void positions(int k, uint8_t *out)
{
    int i;
    for (i = 0; i < k; i++) {
        run(1);                      /* the step starts on this frame's pulse */
        out[i] = maq_lane_pos(&q, 0);
        run(STEP_FRAMES);
    }
}

static int same(const uint8_t *a, const uint8_t *b, int n) { return memcmp(a, b, (size_t)n) == 0; }

int main(void)
{
    int i, a, b;
    uint8_t pos[16];

    /* ---- default slot: 16 C3 notes per bar, every Note-On's predecessor released first (8.4) ---- */
    reset();
    maq_set_playing(&q, 1);
    run(STEP_FRAMES * 16u + 100u);
    CHECK(count(0x90) == 17, "16 steps + the first of the next bar, got %d note-ons", count(0x90));
    a = nth(0x90, 0); b = nth(0x90, 1);
    CHECK(a == 0 && ev[a].d1 == 48 && ev[a].d2 == 100, "first note C3 vel 100 (DYNAMICS lane), got %u/%u", ev[a].d1, ev[a].d2);
    CHECK(b >= 0 && (ev[b].frame - ev[a].frame == 5512u || ev[b].frame - ev[a].frame == 5513u),
          "1/16 at 120 BPM = 5512.5 frames, got %u", b >= 0 ? ev[b].frame - ev[a].frame : 0);
    a = nth(0x80, 0);
    CHECK(a >= 0 && ev[a].frame >= 2700u && ev[a].frame <= 2800u, "GATE 64: off at ~half the step, got frame %u",
          a >= 0 ? ev[a].frame : 0);
    CHECK(stuck_errors == 0, "no double note-on / orphan note-off (%d)", stuck_errors);
    maq_set_playing(&q, 0);
    run(2);
    CHECK(all_off(), "stop releases every note");

    /* ---- loop window and playback modes (7.1, MD:) ---- */
    reset();
    lanes[0].loop_start = 7; lanes[0].loop_end = 4;          /* set in either order */
    maq_set_playing(&q, 1);
    positions(6, pos);
    { const uint8_t want[] = {4, 5, 6, 7, 4, 5}; CHECK(same(pos, want, 6), "FOr 4..7: %u %u %u %u %u %u", pos[0], pos[1], pos[2], pos[3], pos[4], pos[5]); }
    reset(); lanes[0].loop_start = 4; lanes[0].loop_end = 7; lanes[0].playback_mode = SEQ_MODE_BACKWARD;
    maq_set_playing(&q, 1); positions(6, pos);
    { const uint8_t want[] = {7, 6, 5, 4, 7, 6}; CHECK(same(pos, want, 6), "bAC"); }
    reset(); lanes[0].loop_start = 4; lanes[0].loop_end = 7; lanes[0].playback_mode = SEQ_MODE_PENDULUM1;
    maq_set_playing(&q, 1); positions(8, pos);
    { const uint8_t want[] = {4, 5, 6, 7, 6, 5, 4, 5}; CHECK(same(pos, want, 8), "PE1: %u %u %u %u %u %u %u %u", pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pos[6], pos[7]); }
    reset(); lanes[0].loop_start = 4; lanes[0].loop_end = 7; lanes[0].playback_mode = SEQ_MODE_PENDULUM2;
    maq_set_playing(&q, 1); positions(10, pos);
    { const uint8_t want[] = {4, 5, 6, 7, 7, 6, 5, 4, 4, 5}; CHECK(same(pos, want, 10), "PE2"); }
    reset(); lanes[0].loop_start = 2; lanes[0].loop_end = 5; lanes[0].playback_mode = SEQ_MODE_RANDOM;
    maq_set_playing(&q, 1); positions(16, pos);
    for (i = 0; i < 16; i++) CHECK(pos[i] >= 2 && pos[i] <= 5, "rnd stays in the window (%u)", pos[i]);

    /* ---- SKIP flag, MUTE flag ---- */
    reset();
    lanes[0].steps[1].flags = SEQ_FLAG_SKIP;
    lanes[0].steps[2].flags = SEQ_FLAG_MUTE;
    maq_set_playing(&q, 1); positions(4, pos);
    { const uint8_t want[] = {0, 2, 3, 4}; CHECK(same(pos, want, 4), "SKIP bypasses step 2: %u %u %u %u", pos[0], pos[1], pos[2], pos[3]); }
    for (a = 0, i = 0; i < nev; i++) a += (ev[i].st & 0xF0u) == 0x90u && ev[i].frame < 4u * STEP_FRAMES;
    CHECK(a == 3, "the muted step is silent: %d note-ons over 4 steps", a);

    /* ---- TIE: the note holds through a tied step ---- */
    reset();
    lanes[0].steps[1].note_buffer[0] = SEQ_NOTE_TIE;
    for (i = 0; i < (int)SEQ_NSTEP; i++) lanes[1].steps[i].value = 127;   /* GATE full: legato into the tie */
    maq_set_playing(&q, 1);
    run(STEP_FRAMES * 2u - 50u);
    CHECK(count(0x90) == 1 && count(0x80) == 0, "C3 held through the tied step (%d on, %d off)", count(0x90), count(0x80));
    run(STEP_FRAMES);
    CHECK(count(0x90) == 2 && count(0x80) == 1, "released by step 3's shield (%d on, %d off)", count(0x90), count(0x80));

    /* ---- EV: changed under a held (legato) note: released on the lane's next step, never left hanging ---- */
    reset();
    for (i = 0; i < (int)SEQ_NSTEP; i++) lanes[1].steps[i].value = 127;
    maq_set_playing(&q, 1);
    run(100);
    lanes[0].event_type = SEQEV_MODWHEEL;
    run(STEP_FRAMES + 10u);
    CHECK(count(0x90) == 1 && count(0x80) == 1 && all_off(), "EV change releases the held note (%d on, %d off)",
          count(0x90), count(0x80));

    /* ---- chord + RATCHET: 2 bursts x 3 notes, strummed ---- */
    reset();
    lanes[0].steps[0].note_buffer[1] = 52; lanes[0].steps[0].note_buffer[2] = 55;
    lanes[3].steps[0].value = 40;                            /* RATCHET 32..63: 2 bursts */
    maq_set_playing(&q, 1);
    run(STEP_FRAMES - 10u);
    CHECK(count(0x90) == 6 && count(0x80) == 6, "2 bursts x 3 notes: %d on, %d off", count(0x90), count(0x80));
    a = nth(0x90, 1);
    CHECK(a >= 0 && ev[a].frame > 0u && ev[a].frame < STEP_FRAMES / 4u, "strummed: 2nd note a little after the 1st (%u)",
          a >= 0 ? ev[a].frame : 0);
    a = nth(0x90, 3);
    CHECK(a >= 0 && ev[a].frame >= STEP_FRAMES / 2u - 2u && ev[a].frame <= STEP_FRAMES / 2u + 2u, "burst 2 at half step (%u)",
          a >= 0 ? ev[a].frame : 0);
    CHECK(stuck_errors == 0, "ratchet: no stuck or doubled notes");

    /* ---- TRANSPOSE lane aimed at lane 1 (index 0), +12 ---- */
    reset();
    lanes[2].event_type = SEQEV_TRANSPOSE; lanes[2].target_mask = 0;
    for (i = 0; i < (int)SEQ_NSTEP; i++) lanes[2].steps[i].value = BIPOLAR_CENTER + 12;
    maq_set_playing(&q, 1); run(10);
    a = nth(0x90, 0);
    CHECK(a >= 0 && ev[a].d1 == 60 && ev[a].d2 == (uint8_t)perf_state.base_velocity,
          "C3 + 12 = C4 at the base velocity (no DYNAMICS lane): %u/%u", a >= 0 ? ev[a].d1 : 0, a >= 0 ? ev[a].d2 : 0);

    /* ---- TIMING lane: value 0 skips, 1..31 = 4 ticks ---- */
    reset();
    lanes[2].event_type = SEQEV_TIMING; lanes[2].target_mask = SEQ_TGT_LANE1; lanes[2].prescale = 6;
    for (i = 0; i < (int)SEQ_NSTEP; i++) lanes[2].steps[i].value = 10;    /* x4: 4 ticks */
    lanes[2].steps[1].value = 0;                                      /* the timing lane's step 2: skip */
    maq_set_playing(&q, 1);
    run(1); a = nth(0x90, 0);
    run(4u * 919u); b = nth(0x90, 1);
    CHECK(a == 0 && b > 0 && ev[b].frame >= 3670u && ev[b].frame <= 3680u, "4-tick step: next note at ~3675 (%u)", b > 0 ? ev[b].frame : 0);

    /* ---- 8.4 alignment: a GATE lane stepping with the note lane applies to the same step ---- */
    reset();
    lanes[1].steps[0].value = 15;  lanes[1].steps[1].value = 127;     /* step 1 short, step 2 full */
    maq_set_playing(&q, 1);
    run(STEP_FRAMES - 10u);
    a = nth(0x80, 0);
    CHECK(a >= 0 && ev[a].frame < 1000u, "step 1 uses GATE step 1 (short), off at %u", a >= 0 ? ev[a].frame : 0);

    /* ---- SINGL vs DUAL: BOTH plays both channels only with a second layer ---- */
    reset();
    lanes[0].target_mask = SEQ_TGT_BOTH;
    maq_set_playing(&q, 1); run(10);
    CHECK(count(0x90) == 1 && (ev[0].st & 0x0Fu) == 0, "SINGL: BOTH plays on layer 1's channel only");
    reset();
    perf_state.perf_mode = PERF_DUAL_SYNTH; lanes[0].target_mask = SEQ_TGT_BOTH;
    maq_set_playing(&q, 1); run(10);
    CHECK(count(0x90) == 2 && (ev[0].st & 0x0Fu) == 0 && (ev[1].st & 0x0Fu) == 1, "DUAL: BOTH plays channels 1 and 2");

    /* ---- CC lanes, PROG CHANGE only in DUAL (7.5) ---- */
    reset();
    lanes[1].event_type = SEQEV_MODWHEEL; lanes[1].target_mask = SEQ_TGT_L1;
    lanes[2].event_type = SEQEV_PROG_CHANGE; lanes[2].target_mask = SEQ_TGT_L1;
    lanes[1].steps[0].value = 99;
    maq_set_playing(&q, 1); run(10);
    CHECK(count(0xB0) == 1 && ev[nth(0xB0, 0)].d1 == 1 && ev[nth(0xB0, 0)].d2 == 99, "mod wheel CC1 = 99");
    CHECK(count(0xC0) == 0, "no Program Change outside DUAL_SYNTH");

    /* ---- step record (7.2): the playhead holds, retriggers on the clock, knobs per lane, keyboard capture ---- */
    reset();
    maq_set_playing(&q, 1);
    run(STEP_FRAMES * 3u + 10u);                    /* playhead on step 4 */
    ui_sm_goto(&sm, UIPAGE_SEQ);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    run(1);
    CHECK(ui_sm_seq_recording(&sm) && maq_rec_step(&q) == 3, "REC locks on the playhead's step 4 (%u)", maq_rec_step(&q));
    nev = 0;
    run(STEP_FRAMES * 4u);
    CHECK(maq_lane_pos(&q, 0) == 3 && count(0x90) == 4,
          "playing + REC: step 4 retriggers once per step (prescale) and the playhead holds (%u, %d note-ons in 4 steps)",
          maq_lane_pos(&q, 0), count(0x90));
    { int k, ok = 1;
      for (k = 0; k < count(0x90); k++)
          ok &= ev[nth(0x90, k)].d1 == ev[nth(0x90, 0)].d1;
      CHECK(ok, "the retriggers all play step 4's note"); }
    lanes[0].prescale = 12;                          /* 1/8: half as often */
    nev = 0;
    run(STEP_FRAMES * 4u);
    CHECK(count(0x90) == 2, "prescale 12: 2 retriggers in 4 sixteenths (%d)", count(0x90));
    lanes[0].prescale = 6;
    run(STEP_FRAMES * 2u);
    nev = 0;
    ui_sm_handle(&sm, UIEV_SELECT_ENC, 2);
    run(1);
    CHECK(maq_rec_step(&q) == 5 && count(0x90) == 1, "Select +2 -> step 6, auditioned (%u, %d)", maq_rec_step(&q), count(0x90));
    {
        UiKnobInfo info;
        uint8_t *p = ui_sm_knob(&sm, UIPAGE_SEQ, 0, 1, &info);
        CHECK(p == NULL && info.kind == UIKNOB_SEQ && strcmp(info.label, "LANE 2") == 0, "record: knob 2 = LANE 2 (%s)", info.label);
    }
    ui_sm_seq_knob_set(&sm, 1, 100);                 /* lane 2 (GATE) at step 6 */
    CHECK(lanes[1].steps[5].value == 100, "knob 2 writes lane 2 at the record step");
    CHECK(ui_sm_seq_key(&sm, 3, 60, 1, 0) == 0, "record: a key plays as normal");
    ui_sm_seq_key(&sm, 7, 64, 1, 0);
    ui_sm_seq_key(&sm, 10, 67, 1, 0);
    ui_sm_seq_key(&sm, 3, 60, 0, 0); ui_sm_seq_key(&sm, 7, 64, 0, 0); ui_sm_seq_key(&sm, 10, 67, 0, 0);
    { const SequencerStep *s = &lanes[0].steps[5];
      CHECK(s->note_buffer[0] == 60 && s->note_buffer[1] == 64 && s->note_buffer[2] == 67 && s->note_buffer[3] == SEQ_NOTE_NULL,
            "C-E-G chord captured into step 6"); }
    { UiKeyLeds kl;
      CHECK(ui_sm_key_leds(&sm, 48, UISEQ_MOD_OCT_DOWN, &kl) && kl.lit == (1u << 12 | 1u << 16 | 1u << 19) && !kl.mid && !kl.dim,
            "step record: the record step's C4 E4 G4 lit, OCT ignored (%08x)", (unsigned)kl.lit); }
    ui_sm_seq_key(&sm, 5, 62, 1, 0); ui_sm_seq_key(&sm, 5, 62, 0, 0);
    CHECK(lanes[0].steps[5].note_buffer[0] == 62 && lanes[0].steps[5].note_buffer[1] == SEQ_NOTE_NULL, "a new key after release starts a new chord");
    lanes[0].steps[5].note_buffer[1] = 66;
    ui_sm_seq_knob_set(&sm, 0, 64);                  /* knob 1 = lane 1: 62 -> 64, the chord moves by +2 */
    CHECK(lanes[0].steps[5].note_buffer[0] == 64 && lanes[0].steps[5].note_buffer[1] == 68, "knob transposes the chord");
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    run(STEP_FRAMES + 10u);
    CHECK(!ui_sm_seq_recording(&sm) && maq_lane_pos(&q, 0) == 6, "REC off: plays on from the record step (%u)", maq_lane_pos(&q, 0));

    /* ---- live play (7.1, 2026-10-21): the 16 white keys are the active lane's steps, SEQ picks the lane, the Select
     * Encoder walks the steps in groups of 4 and then the footer, OCT- / OCT+ set the loop ---- */
    /* the keyboard from C3 (key 0 = note 48): white keys C3 D3 E3 F3 G3 A3 B3 C4 D4 E4 F4 G4 A4 B4 C5 D5 = steps 1..16 */
    {
        static const uint8_t WK48[16] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26};
        UiKeyLeds kl;
        UiKnobInfo info;
        uint32_t all = 0, win = 0, w4;
        uint8_t p, n0, le, f, last;
        reset();
        ui_sm_goto(&sm, UIPAGE_SEQ);
        for (i = 0; i < 16; i++) all |= 1u << WK48[i];
        for (i = 0, w4 = 0; i < 4; i++) w4 |= 1u << WK48[i];
        CHECK(ui_sm_key_leds(&sm, 48, 0, &kl) && kl.lit == 0 && kl.mid == w4 && kl.dim == (all & ~w4),
              "key LEDs from C3, stopped: steps 1..4 (the knobs) mid, the other 12 dim (%08x %08x)", (unsigned)kl.mid,
              (unsigned)kl.dim);
        ui_sm_key_leds(&sm, 53, 0, &kl);
        CHECK(kl.mid == (1u << 0 | 1u << 2 | 1u << 4 | 1u << 6), "from F3 (FM1): F3 G3 A3 B3 mid");
        le = lanes[0].loop_end;
        lanes[0].steps[9].flags = SEQ_FLAG_MUTE; lanes[0].steps[10].flags = SEQ_FLAG_SKIP; lanes[0].loop_end = 13;
        ui_sm_key_leds(&sm, 48, 0, &kl);
        CHECK(kl.dim == (all & ~w4 & ~(1u << WK48[9] | 1u << WK48[10] | 1u << WK48[14] | 1u << WK48[15])),
              "a muted, a skipped and the out-of-loop steps dark (%08x)", (unsigned)kl.dim);
        lanes[0].steps[9].flags = 0; lanes[0].steps[10].flags = 0; lanes[0].loop_end = le;

        CHECK(ui_sm_seq_key(&sm, 4, 52, 1, 0) == 1 && sm.s.active_seq_lane == 0 && sm.seq_step0 == 2,
              "E3 (white key 3) -> steps 3..6 on the knobs (%u)", sm.seq_step0);
        ui_sm_knob(&sm, UIPAGE_SEQ, 0, 3, &info);
        CHECK(strcmp(info.label, "STEP 06") == 0, "live: knob 4 = STEP 06 (%s)", info.label);
        n0 = lanes[0].steps[2].note_buffer[0];
        ui_sm_seq_knob_turn(&sm, 0, 5);
        CHECK(lanes[0].steps[2].note_buffer[0] == n0 + 5, "knob 1 turns step 3's note + 5");
        for (i = 2, win = 0; i < 6; i++) win |= 1u << WK48[i];
        ui_sm_key_leds(&sm, 48, 0, &kl);
        CHECK(kl.mid == win && kl.dim == (all & ~win), "the LEDs follow: steps 3..6 mid");
        CHECK(ui_sm_seq_key(&sm, 1, 49, 1, 0) == 1 && sm.seq_step0 == 2, "C#3, a black key among the steps: taken, does nothing");
        CHECK(ui_sm_seq_key(&sm, 24, 72, 1, 0) == 1 && sm.seq_step0 == 12, "C5 (step 15): held to steps 13..16 (%u)", sm.seq_step0);
        CHECK(ui_sm_seq_key(&sm, 26, 74, 1, 0) == 1 && sm.seq_step0 == 12 && sm.s.active_seq_lane == 0,
              "D5 (step 16): 13..16, the lane stays");
        CHECK(ui_sm_seq_key(&sm, 26, 74, 0, 0) == 1, "a step key's release is taken too");
        CHECK(ui_sm_seq_key(&sm, 4, 52, 1, UISEQ_MOD_OCT_DOWN) == 1 && lanes[0].loop_start == 2, "OCT- + white key 3: loop start step 3");
        CHECK(ui_sm_seq_key(&sm, 23, 71, 1, UISEQ_MOD_OCT_UP) == 1 && lanes[0].loop_end == 13, "OCT+ + white key 14: loop end step 14");
        CHECK(sm.s.active_seq_lane == 0 && sm.seq_step0 == 12, "OCT+key moves neither the lane nor the knob window");

        /* key LEDs with OCT held: lane 1's steps, loop window 3..14 dim, the boundary lit */
        for (i = 2, win = 0; i <= 13; i++) win |= 1u << WK48[i];
        ui_sm_key_leds(&sm, 48, UISEQ_MOD_OCT_DOWN, &kl);
        CHECK(kl.lit == 1u << WK48[2] && kl.dim == (win & ~(1u << WK48[2])) && kl.mid == 0,
              "OCT- held, stopped: the loop start E3 lit, the window dim (%08x %08x)", (unsigned)kl.lit, (unsigned)kl.dim);
        ui_sm_key_leds(&sm, 48, UISEQ_MOD_OCT_UP, &kl);
        CHECK(kl.lit == 1u << WK48[13] && kl.dim == (win & ~(1u << WK48[13])), "OCT+ held, stopped: the loop end B4 lit");
        ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
        run(STEP_FRAMES * 3u + 10u);
        p = maq_lane_pos(&q, 0);
        ui_sm_key_leds(&sm, 48, UISEQ_MOD_OCT_DOWN, &kl);
        CHECK(p > 2 && p <= 13 && kl.lit == 1u << WK48[p] && kl.mid == 1u << WK48[2] &&
              kl.dim == (win & ~(1u << WK48[p] | 1u << WK48[2])),
              "OCT- held, playing: the playhead (step %u) lit, the loop start mid, the rest of the window dim", p + 1u);
        sm.seq_step0 = p > 1 ? (uint8_t)(p - 1u) : 0u;    /* the playhead inside the knob window */
        for (i = 0, w4 = 0; i < 4; i++) w4 |= 1u << WK48[sm.seq_step0 + i];
        ui_sm_key_leds(&sm, 48, 0, &kl);
        CHECK(kl.lit == 1u << WK48[p] && kl.mid == (w4 & ~(1u << WK48[p])) && kl.dim == (win & ~w4),
              "live, playing: the playhead lit over the knob window's mid, the loop's other steps dim (%08x %08x %08x)",
              (unsigned)kl.lit, (unsigned)kl.mid, (unsigned)kl.dim);
        ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
        run(2);
        ui_sm_goto(&sm, UIPAGE_HOME1);
        CHECK(!ui_sm_key_leds(&sm, 48, UISEQ_MOD_OCT_DOWN, &kl) && !kl.lit && !kl.mid && !kl.dim, "off the SEQ / ARP pages: nothing");
        ui_sm_goto(&sm, UIPAGE_SEQ);
        CHECK(ui_sm_seq_key(&sm, 27, 75, 1, 0) == 0 && ui_sm_seq_key(&sm, 28, 76, 1, 0) == 0, "keys above the 16th white key play");
        /* the FM1 keybed starts on F3 (key 0 = note 53): steps 1..16 are F3 .. G5 */
        CHECK(ui_sm_seq_key(&sm, 7, 60, 1, 0) == 1 && sm.seq_step0 == 4, "from F3: C4 (step 5) -> steps 5..8");
        CHECK(ui_sm_seq_key(&sm, 1, 54, 1, 0) == 1 && sm.seq_step0 == 4, "from F3: F#3 does nothing");

        /* the SEQ button: lane 1 from another page, the next lane on the page, the knob window kept */
        reset();
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_ARP);
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_SEQ);
        CHECK(sm.page == UIPAGE_SEQ && sm.s.active_seq_lane == 0 && sm.seq_step0 == 0 && sm.s.current_param_focus == 0,
              "SEQ from ARP: lane 1, steps 1..4, the cursor on STEPS");
        ui_sm_seq_key(&sm, 9, 57, 1, 0);
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_SEQ);
        CHECK(sm.s.active_seq_lane == 1 && sm.seq_step0 == 5, "SEQ again: lane 2, the same steps 6..9 (%u %u)",
              sm.s.active_seq_lane, sm.seq_step0);
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_SEQ);
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_SEQ);
        CHECK(sm.s.active_seq_lane == 3, "lane 4");
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_SEQ);
        CHECK(sm.s.active_seq_lane == 0 && sm.page == UIPAGE_SEQ, "and round to lane 1");
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_HOME);
        ui_sm_handle(&sm, UIEV_BUTTON, BTN_SEQ);
        CHECK(sm.s.active_seq_lane == 0 && sm.seq_step0 == 0, "back from HOME: lane 1, steps 1..4 again");

        /* the Select Encoder: STEPS 1..4 -> 5..8 -> 9..12 -> 13..16, the footer's fields, round to 1..4 */
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
        CHECK(sm.s.current_param_focus == 0 && sm.seq_step0 == 4, "Select +1: steps 5..8 (%u)", sm.seq_step0);
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 2);
        CHECK(sm.s.current_param_focus == 0 && sm.seq_step0 == 12, "Select +2: steps 13..16");
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
        CHECK(sm.s.current_param_focus == 1 && strcmp(ui_sm_row_label(&sm, 1), "PRESCALE") == 0 && sm.seq_step0 == 12,
              "past 13..16: the footer's first field, g (PRESCALE); the knobs stay on 13..16");
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
        CHECK(strcmp(ui_sm_row_label(&sm, sm.s.current_param_focus), "EVENT TYPE") == 0, "then the event");
        for (i = 0, last = 0; i < 10 && sm.s.current_param_focus != 0; i++) {
            last = sm.s.current_param_focus;
            ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
        }
        CHECK(sm.s.current_param_focus == 0 && sm.seq_step0 == 0 && strcmp(ui_sm_row_label(&sm, last), "PLAY MODE") == 0,
              "past PLAY MODE (the last field): round to steps 1..4");
        ui_sm_handle(&sm, UIEV_SELECT_ENC, -1);
        CHECK(sm.s.current_param_focus == last, "back from 1..4: PLAY MODE");
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
        CHECK(sm.s.current_param_focus == 0 && sm.seq_step0 == 0, "on again: 1..4");
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 4);
        ui_sm_handle(&sm, UIEV_SELECT_ENC, -1);
        CHECK(sm.s.current_param_focus == 0 && sm.seq_step0 == 12, "back from g: STEPS on 13..16");
        ui_sm_seq_key(&sm, 9, 57, 1, 0);                 /* A3: steps 6..9, between the groups */
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
        CHECK(sm.seq_step0 == 8, "from 6..9, Select +1 snaps on to 9..12 (%u)", sm.seq_step0);
        ui_sm_seq_key(&sm, 9, 57, 1, 0);
        ui_sm_handle(&sm, UIEV_SELECT_ENC, -1);
        CHECK(sm.seq_step0 == 4, "from 6..9, Select -1 snaps back to 5..8 (%u)", sm.seq_step0);
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 3);
        f = sm.s.current_param_focus;
        ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
        ui_sm_seq_key(&sm, 4, 52, 1, 0);
        CHECK(f == 1 && sm.s.current_param_focus == 0 && sm.seq_step0 == 2, "a step key from the footer: the cursor back on STEPS");
        ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
        CHECK(sm.seq_step0 == 2 && !sm.rowpop, "the Algorithm Encoder on STEPS: nothing, no popup");
    }

    /* ---- TARGET (7.4, 7.5): 4 slots for a lane target, filtered, hidden with one option ---- */
    { char b[13];
      uint8_t on, ok;
      reset();
      ui_sm_goto(&sm, UIPAGE_SEQ);
      lanes[2].event_type = SEQEV_DYNAMICS; lanes[2].target_mask = SEQ_TGT_LANE2 | SEQ_TGT_LANE4;
      CHECK(strcmp(ui_sm_seq_target_text(&sm, 2, b), "[ ][X][ ][X]") == 0, "lane bitmask: %s", b);
      CHECK(ui_sm_seq_target_slots(&sm, 2, &on, &ok) == 1 && on == 0x0Au && ok == 0x0Fu && ui_sm_seq_target_count(&sm, 2) == 16,
            "lane bitmask: slots on %02X ok %02X", on, ok);
      lanes[2].event_type = SEQEV_TRANSPOSE; lanes[2].target_mask = 3;   /* lane 4 is RATCHET: not a NOTE lane */
      CHECK(strcmp(ui_sm_seq_target_text(&sm, 2, b), "[ ][-][-][X]") == 0, "TRANSPOSE offers NOTE lanes only: %s", b);
      CHECK(ui_sm_seq_target_count(&sm, 2) == 1, "TRANSPOSE: one NOTE lane, one option");
      ui_sm_enforce(&sm);
      CHECK(lanes[2].target_mask == 0, "one option left: the hidden TARGET is pointed at it (%u)", lanes[2].target_mask);
      sm.s.active_seq_lane = 2;
      CHECK(ui_sm_row_state(&sm, 3) == UIROW_HIDDEN, "TARGET hidden with one option");
      sm.s.current_param_focus = 2;                  /* EV */
      ui_sm_handle(&sm, UIEV_SELECT_ENC, 1);
      CHECK(sm.s.current_param_focus == 6, "the cursor skips the hidden TARGET, SCALE, ROOT (EV -> MD): %u",
            sm.s.current_param_focus);
      lanes[3].event_type = SEQEV_NOTE;                /* a second NOTE lane: two options, TARGET back */
      ui_sm_enforce(&sm);
      CHECK(ui_sm_row_state(&sm, 3) == UIROW_ACTIVE && ui_sm_seq_target_count(&sm, 2) == 2, "two NOTE lanes: TARGET shown");
      ui_sm_handle(&sm, UIEV_SELECT_ENC, -1);
      CHECK(sm.s.current_param_focus == 3, "the cursor reaches TARGET again");
      ui_sm_handle(&sm, UIEV_ALGO_ENC, 1);
      CHECK(lanes[2].target_mask == 3, "the Algorithm Encoder steps over lanes 2 and 3 (GATE, itself) to lane 4 (%u)", lanes[2].target_mask);
      CHECK(strcmp(ui_sm_row_text(&sm, 3), "[ ][-][-][X]") == 0, "TARGET row text: %s", ui_sm_row_text(&sm, 3));
      lanes[2].event_type = SEQEV_LAYER; lanes[2].target_mask = 1;       /* lane 2 is GATE: plays on no layer */
      CHECK(strcmp(ui_sm_seq_target_text(&sm, 2, b), "[ ][X][-][ ]") == 0, "LAYER offers the layer lanes: %s", b);
      /* a layer target: text, and under SINGL one option (L1), so hidden */
      CHECK(strcmp(ui_sm_seq_target_text(&sm, 0, b), "[L1]") == 0 && ui_sm_seq_target_slots(&sm, 0, &on, &ok) == 0,
            "NOTE lane: [L1] as text");
      sm.s.active_seq_lane = 0;
      ui_sm_enforce(&sm);
      CHECK(ui_sm_row_state(&sm, 3) == UIROW_HIDDEN, "SINGL: a NOTE lane's TARGET is hidden");
      perf_state.perf_mode = PERF_DUAL_SYNTH;
      CHECK(ui_sm_row_state(&sm, 3) == UIROW_ACTIVE && ui_sm_seq_target_count(&sm, 0) == 3, "DUAL: L1 / L2 / BOTH");
      CHECK(strcmp(ui_sm_row_label(&sm, 3), "TARGET") == 0, "the row reads TARGET");
      CHECK(strcmp(ui_sm_seq_ev_name(SEQEV_NOTE), "Note") == 0 && strcmp(ui_sm_seq_mode_name(SEQ_MODE_FORWARD), "FOr") == 0, "EV / MD names");
      CHECK(strcmp(ui_sm_note_name(48, b), "C3") == 0 && strcmp(ui_sm_note_name(1, b), "C#-1") == 0 &&
            strcmp(ui_sm_note_name(127, b), "TIE") == 0, "note names"); }

    /* ---- 6.3: navigation never stops the clock; PLAY works from any page; REC only on SEQ ---- */
    reset();
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    run(STEP_FRAMES);
    for (i = 0; i < BTN_COUNT; i++) { ui_sm_handle(&sm, UIEV_BUTTON, (int8_t)i); ui_sm_handle(&sm, UIEV_SELECT_ENC, 1); run(500); }
    ui_sm_handle(&sm, UIEV_BUTTON, BTN_HOME);
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
    run(STEP_FRAMES * 2u);
    CHECK(maq_playing(&q) && !maq_recording(&q), "still playing after every section button; REC ignored off the SEQ page");
    CHECK(count(0x90) >= 3, "steps kept firing during navigation (%d)", count(0x90));
    ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
    run(2);
    CHECK(!maq_playing(&q) && all_off() && stuck_errors == 0, "PLAY again stops, everything released");

    /* ---- MIDI clock: Start plays from the top, Stop stops ---- */
    reset();
    perf_state.clock_source = CLKSRC_MIDI;
    run(10);
    midi_clock_realtime(0xFA);
    /* the last wait stops short of where the clock's lock would play pulse 49 ahead of the sender */
    for (i = 0; i < 24 * 2; i++) { midi_clock_realtime(0xF8); run(i < 24 * 2 - 1 ? 919 : 900); }
    CHECK(maq_playing(&q) && count(0x90) == 8, "MIDI Start + 2 beats of clock: 8 sixteenths (%d)", count(0x90));
    midi_clock_realtime(0xFC);
    run(2);
    CHECK(!maq_playing(&q) && all_off(), "MIDI Stop stops and releases");

    /* ---- SCALE (2026-10-16): a NOTE lane held to a scale, as it is entered and as it plays ---- */
    { uint16_t cmaj = seq_scale_mask(SEQSCALE_MAJOR, 0, 0), dmaj = seq_scale_mask(SEQSCALE_MAJOR, 2, 0);
      CHECK(cmaj == 0xAB5u && dmaj == 0xAD6u, "major masks: C %03X, D %03X", cmaj, dmaj);
      CHECK(seq_scale_snap(61, cmaj) == 60 && seq_scale_snap(61, dmaj) == 61 && seq_scale_snap(66, cmaj) == 65,
            "snap: C#4 to C4 in C (a tie: down), kept in D; F#4 to F4");
      CHECK(seq_scale_snap(SEQ_NOTE_TIE, cmaj) == SEQ_NOTE_TIE && seq_scale_snap(61, 0) == 61, "snap: TIE and no scale pass");
      CHECK(seq_scale_step(60, 1, cmaj, 126) == 62 && seq_scale_step(64, 1, cmaj, 126) == 65 &&
            seq_scale_step(60, -1, cmaj, 126) == 59 && seq_scale_step(60, 7, cmaj, 126) == 72 &&
            seq_scale_step(61, 1, cmaj, 126) == 62 && seq_scale_step(124, 3, cmaj, 126) == 125,
            "step: degrees of C major; from outside to the neighbour; E9 up 3: F9, the top (126 is F#)");
      CHECK(seq_scale_mask(SEQSCALE_GENRE, 7, 5) == seq_scale_mask(SEQSCALE_BLUES, 0, 0) &&
            seq_scale_mask(SEQSCALE_GENRE, 7, 0) == cmaj && seq_scale_mask(SEQSCALE_OFF, 3, 0) == 0u,
            "GENRE: Blues -> C blues, Pop_1 -> C major (the lane's ROOT unused); OFF: none"); }
    { int k;
      reset();
      for (k = 0; k < (int)SEQ_NSTEP; k++) lanes[0].steps[k].note_buffer[0] = 61;   /* C#4, stored */
      lanes[0].scale = SEQSCALE_MAJOR; lanes[0].scale_root = 0;
      maq_set_playing(&q, 1);
      run(2);
      CHECK(count(0x90) == 1 && ev[nth(0x90, 0)].d1 == 60, "plays C#4 as C4 in C major (%d)", ev[nth(0x90, 0)].d1);
      CHECK(lanes[0].steps[0].note_buffer[0] == 61, "the stored note is kept");
      lanes[0].scale = SEQSCALE_OFF;
      run(STEP_FRAMES + 4u);                         /* (a step is 5512.5 frames: past the next one) */
      CHECK(ev[nth(0x90, count(0x90) - 1)].d1 == 61, "SCALE OFF: C#4 again");
      for (k = 0; k < (int)SEQ_NSTEP; k++) lanes[0].steps[k].note_buffer[0] = 62;   /* D4 */
      lanes[0].scale = SEQSCALE_GENRE;
      maq_set_genre(&q, 5);                          /* Blues: C Eb F F# G Bb */
      run(STEP_FRAMES + 4u);
      CHECK(ev[nth(0x90, count(0x90) - 1)].d1 == 63, "GENRE Blues: D4 plays Eb4 (%d)", ev[nth(0x90, count(0x90) - 1)].d1);
      maq_set_genre(&q, 0);                          /* Pop_1: C major */
      run(STEP_FRAMES + 4u);
      CHECK(ev[nth(0x90, count(0x90) - 1)].d1 == 62, "GENRE Pop_1: D4 as it is (%d)", ev[nth(0x90, count(0x90) - 1)].d1);
      maq_set_playing(&q, 0);
      run(2); }
    /* the scale after the transposition (2026-10-16): C E G in C major over the ARP's chord roots of the key plays
     * each chord of the key (the QY-10 way: written in C, re-mapped), not a chromatic shift; SCALE OFF: chromatic */
    { static const int8_t tr[7] = {0, 2, 4, 5, 7, 9, 11};
      static const uint8_t want[7][3] = {{60, 64, 67}, {62, 65, 69}, {64, 67, 71}, {65, 69, 72}, {67, 71, 74},
                                         {69, 72, 76}, {71, 74, 77}};
      int k, c, ok;
      reset();
      for (k = 0; k < (int)SEQ_NSTEP; k++) {
          lanes[0].steps[k].note_buffer[0] = 60; lanes[0].steps[k].note_buffer[1] = 64;
          lanes[0].steps[k].note_buffer[2] = 67;
      }
      lanes[0].scale = SEQSCALE_GENRE;
      maq_set_genre(&q, 0);                          /* Pop_1: C major */
      maq_set_playing(&q, 1);
      run(2);
      for (c = 0; c < 7; c++) {
          maq_set_ext_transpose(&q, tr[c]);
          nev = 0;
          run(STEP_FRAMES + 4u);
          ok = count(0x90) == 3;
          for (k = 0; k < 3 && ok; k++)
              ok = ev[nth(0x90, k)].d1 == want[c][k];
          CHECK(ok, "C E G over the chord root +%d plays %u %u %u (got %d notes: %d %d %d)", tr[c], want[c][0],
                want[c][1], want[c][2], count(0x90), count(0x90) > 0 ? ev[nth(0x90, 0)].d1 : -1,
                count(0x90) > 1 ? ev[nth(0x90, 1)].d1 : -1, count(0x90) > 2 ? ev[nth(0x90, 2)].d1 : -1);
      }
      lanes[0].scale = SEQSCALE_OFF;
      maq_set_ext_transpose(&q, 9);
      nev = 0;
      run(STEP_FRAMES + 4u);
      CHECK(count(0x90) == 3 && ev[nth(0x90, 0)].d1 == 69 && ev[nth(0x90, 1)].d1 == 73 && ev[nth(0x90, 2)].d1 == 76,
            "SCALE OFF: the plain transposition, A C# E");
      maq_set_playing(&q, 0);
      run(2); }
    reset();
    ui_sm_goto(&sm, UIPAGE_SEQ);
    CHECK(ui_sm_row_state(&sm, 4) == UIROW_ACTIVE && ui_sm_row_state(&sm, 5) == UIROW_HIDDEN,
          "NOTE lane: SCALE shown, ROOT hidden under OFF");
    lanes[0].scale = SEQSCALE_GENRE;
    CHECK(ui_sm_row_state(&sm, 5) == UIROW_HIDDEN, "ROOT hidden under GENRE (the genre's own)");
    lanes[0].scale = SEQSCALE_MAJOR;
    CHECK(ui_sm_row_state(&sm, 5) == UIROW_ACTIVE, "ROOT shown with MAJOR");
    sm.s.active_seq_lane = 1;                        /* GATE */
    CHECK(ui_sm_row_state(&sm, 4) == UIROW_HIDDEN && ui_sm_row_state(&sm, 5) == UIROW_HIDDEN, "GATE lane: no SCALE, ROOT");
    sm.s.active_seq_lane = 0;
    lanes[0].steps[0].note_buffer[0] = 60;
    ui_sm_seq_knob_turn(&sm, 0, 1);
    CHECK(lanes[0].steps[0].note_buffer[0] == 62, "a detent is a degree: C4 -> D4 (%u)", lanes[0].steps[0].note_buffer[0]);
    ui_sm_seq_knob_turn(&sm, 0, 2);
    CHECK(lanes[0].steps[0].note_buffer[0] == 65, "two more: E4, F4 (%u)", lanes[0].steps[0].note_buffer[0]);
    ui_sm_seq_knob_set(&sm, 0, 66);
    CHECK(lanes[0].steps[0].note_buffer[0] == 65, "no note outside the scale: F#4 in -> F4");
    lanes[0].steps[0].note_buffer[0] = 61;
    CHECK(ui_sm_seq_knob_get(&sm, 0) == 60, "a stored C#4 shows as the C4 it plays");
    lanes[0].steps[0].note_buffer[0] = 125;            /* F9: C major's top note (126 is F#) */
    ui_sm_seq_knob_turn(&sm, 0, 1);
    CHECK(lanes[0].steps[0].note_buffer[0] == SEQ_NOTE_TIE, "past the top note: TIE (%u)", lanes[0].steps[0].note_buffer[0]);
    ui_sm_seq_knob_turn(&sm, 0, -1);
    CHECK(lanes[0].steps[0].note_buffer[0] == 125, "back from TIE: the top note (%u)", lanes[0].steps[0].note_buffer[0]);

    printf(fails ? "%d check(s) failed\n" : "all MAQ checks passed\n", fails);
    return fails != 0;
}
