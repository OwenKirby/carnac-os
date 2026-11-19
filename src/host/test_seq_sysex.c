/* Host test: the web editor's SEQ tab over SysEx (2026-11-19, synth_panel.c "SEQ OUT", seq_wire.h), on the browser
 * build of the whole app (carnac_web.c: the panel, the preset store on a simulated flash, the render): the whole
 * sequence on request, a step and a lane written (as sent: no echo; clamped: the corrected lane comes back), the
 * panel's edits going out (a step alone, a footer as the lane), the name both ways, SAVE into a slot (answered once in
 * flash, the slot list shows it), LOAD, PLAY and the playhead, a lane of an unknown EVENT refused. Exit code 0 = pass.
 *   built by src/host/run_tests.sh (needs X0X's tree in x0x-firmware/ and build/emu-gen/felucca_font.h) */
#include "carnac_web.c"
#include <stdio.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* the messages the app sent, split (cmd, args) */
#define NMSG 64
static uint8_t m_cmd[NMSG], m_arg[NMSG][200];
static uint32_t m_len[NMSG], m_n, acks, ack_last;

static void run(uint32_t ms)
{
    while (ms--)
        web_render(48);
}
static void collect(void)
{
    uint32_t i = 0, n = web_sysex_out_len();
    const uint8_t *b = web_sysex_out();
    m_n = 0;
    while (i < n) {
        uint32_t j = i + 1;
        while (j < n && b[j] != 0xF7u)
            j++;
        if (j - i >= 6u && b[i + 1] == 0x7Du && b[i + 4] == ED_SEQ_ACK) {   /* (counted apart) */
            acks++;
            ack_last = b[i + 5];
        } else if (m_n < NMSG && j - i >= 5u && b[i + 1] == 0x7Du) {
            m_cmd[m_n] = b[i + 4];
            m_len[m_n] = j - i - 5u;
            memcpy(m_arg[m_n], b + i + 5, m_len[m_n] < 200u ? m_len[m_n] : 200u);
            m_n++;
        }
        i = j + 1;
    }
    web_sysex_out_take();
}
static void collect_acks_only(void)                  /* the ACKs the command just sent (handled at once) */
{
    uint32_t n = web_sysex_out_len();
    uint8_t *b = web_sysex_out();
    if (n >= 7u && b[n - 7] == 0xF0u && b[n - 3] == ED_SEQ_ACK) {
        acks++;
        ack_last = b[n - 2];
        web_sx_n = n - 7u;
    }
}
static uint32_t count(uint8_t cmd)
{
    uint32_t i, c = 0;
    for (i = 0; i < m_n; i++)
        c += m_cmd[i] == cmd;
    return c;
}
static int find(uint8_t cmd, int a0)                 /* the last message of cmd (whose first arg is a0, unless -1) */
{
    int i, f = -1;
    for (i = 0; i < (int)m_n; i++)
        if (m_cmd[i] == cmd && (a0 < 0 || m_arg[i][0] == a0))
            f = i;
    return f;
}
static void send(uint8_t cmd, const uint8_t *a, uint32_t n)
{
    uint8_t *b = web_sysex_in_buf();
    uint32_t before = acks;
    b[0] = 0x7Du; b[1] = 0x46u; b[2] = 0x4Cu; b[3] = cmd;
    memcpy(b + 4, a, n);
    web_sysex_in(4u + n);
    if (cmd != ED_SEQ_SLOTS)                         /* (SLOTS: its answer stays for the caller's collect) */
        collect_acks_only();
    CHECK(cmd == ED_SEQ_SLOTS || (acks == before + 1u && ack_last == cmd), "command %u: one ACK naming it", cmd);
}

int main(void)
{
    uint8_t a[200], l, s;
    SequencerLane lane;
    int k;
    web_nor_erase();
    web_boot();
    run(3000);                                       /* (past the splash) */
    collect();
    CHECK(m_n == 0, "nothing goes out before the editor asks (CC OUT off): %u messages", m_n);

    /* the whole sequence on request */
    send(ED_SEQ_REQ, a, 0);
    run(40);
    collect();
    CHECK(count(ED_SEQ_LANE) == 4 && count(ED_SEQ_NAME) == 1 && count(ED_SEQ_POS) == 1,
          "REQ: 4 lanes, a name, the playhead (got %u %u %u)", count(ED_SEQ_LANE), count(ED_SEQ_NAME), count(ED_SEQ_POS));
    for (l = 0; l < SEQ_NLANE; l++) {
        k = find(ED_SEQ_LANE, l);
        CHECK(k >= 0 && m_len[k] == 1u + SEQW_LANE_N, "lane %u sent whole", l);
        if (k >= 0) {
            memset(&lane, 0, sizeof lane);
            CHECK(!seqw_lane_unpack(&lane, m_arg[k] + 1) && !memcmp(&lane, &seq_lanes[l], sizeof lane),
                  "lane %u reads back as seq_lanes", l);
        }
    }
    k = find(ED_SEQ_POS, -1);
    CHECK(k >= 0 && m_arg[k][0] == 0 && m_arg[k][1] == 127, "stopped: no playhead");

    /* a step from the editor: taken, not echoed */
    a[0] = 1; a[1] = 6;
    a[2] = 60; a[3] = 64; a[4] = 0; a[5] = 0; a[6] = 99; a[7] = SEQ_FLAG_TIE; a[8] = 0x0C;
    send(ED_SEQ_STEP, a, 9);
    CHECK(seq_lanes[1].steps[6].note_buffer[0] == 60 && seq_lanes[1].steps[6].note_buffer[1] == 64 &&
          seq_lanes[1].steps[6].note_buffer[2] == SEQ_NOTE_NULL && seq_lanes[1].steps[6].note_buffer[3] == SEQ_NOTE_NULL &&
          seq_lanes[1].steps[6].value == 99 && seq_lanes[1].steps[6].flags == SEQ_FLAG_TIE, "STEP written");
    run(20);
    collect();
    CHECK(m_n == 0, "STEP as sent: no echo (%u messages)", m_n);

    /* a lane from the editor, PRESCALE out of range: clamped, and the corrected lane comes back */
    seqw_lane_pack(&seq_lanes[2], a + 1);
    a[0] = 2;
    a[1 + 0] = SEQEV_NOTE; a[1 + 1] = SEQ_TGT_L2; a[1 + 3] = 99;
    a[1 + SEQW_FOOT_N + 3 * SEQW_STEP_N] = 72;       /* step 4's first note */
    a[1 + SEQW_FOOT_N + 3 * SEQW_STEP_N + 6] &= 0x0Eu;
    send(ED_SEQ_LANE, a, 1 + SEQW_LANE_N);
    CHECK(seq_lanes[2].prescale == SEQ_PRESCALE_MAX && seq_lanes[2].target_mask == SEQ_TGT_L2 &&
          seq_lanes[2].steps[3].note_buffer[0] == 72, "LANE written, PRESCALE clamped to 32");
    run(20);
    collect();
    k = find(ED_SEQ_LANE, 2);
    CHECK(k >= 0 && m_arg[k][1 + 3] == SEQ_PRESCALE_MAX, "the clamped lane goes back");

    /* an unknown EVENT: refused */
    lane = seq_lanes[3];
    seqw_lane_pack(&seq_lanes[3], a + 1);
    a[0] = 3; a[1] = SEQEV_COUNT; a[1 + SEQW_FOOT_N] = 5;
    send(ED_SEQ_LANE, a, 1 + SEQW_LANE_N);
    CHECK(!memcmp(&lane, &seq_lanes[3], sizeof lane), "a lane of an unknown EVENT changes nothing");

    /* the panel's edits go out: a step alone, a footer as the whole lane */
    seq_lanes[0].steps[9].value = 33;
    run(20);
    collect();
    k = find(ED_SEQ_STEP, 0);
    CHECK(m_n == 1 && k >= 0 && m_arg[k][1] == 9 && m_arg[k][2 + 4] == 33, "panel step edit -> STEP (%u msgs)", m_n);
    seq_lanes[3].playback_mode = SEQ_MODE_PENDULUM1;
    run(20);
    collect();
    k = find(ED_SEQ_LANE, 3);
    CHECK(m_n == 1 && k >= 0 && m_arg[k][1 + 2] == SEQ_MODE_PENDULUM1, "panel footer edit -> LANE");
    for (s = 0; s < 5; s++)
        seq_lanes[1].steps[s].value = (uint8_t)(10 + s);
    run(20);
    collect();
    CHECK(count(ED_SEQ_LANE) == 1 && count(ED_SEQ_STEP) == 0, "five steps changed -> one LANE");

    /* the name both ways */
    for (s = 0; s < NAME_LEN; s++)
        a[s] = s < 4 ? (uint8_t)(1 + s) : 0;         /* "ABCD" */
    send(ED_SEQ_NAME, a, NAME_LEN);
    CHECK(sm.seq_name[0] == 1 && sm.seq_name[3] == 4 && sm.seq_name[4] == 0, "NAME written");
    run(10);
    collect();
    CHECK(count(ED_SEQ_NAME) == 0, "NAME as sent: no echo");
    sm.seq_name[0] = 26;                             /* (Z, as RENAME would) */
    run(10);
    collect();
    k = find(ED_SEQ_NAME, -1);
    CHECK(k >= 0 && m_arg[k][0] == 26, "a panel rename goes out");

    /* SAVE into slot 20 under a name: answered once in flash; the slot list shows it */
    uint8_t tgt0 = sm.save_target, slot0 = sm.save_slot;
    a[0] = 20;
    for (s = 0; s < NAME_LEN; s++)
        a[1 + s] = s < 3 ? (uint8_t)(19 + s) : 0;    /* "STU" */
    send(ED_SEQ_SAVE, a, 1 + NAME_LEN);
    run(50);
    collect();
    k = find(ED_SEQ_SAVE, 20);
    CHECK(k >= 0 && m_arg[k][1] == 0, "SAVE answered: saved");
    CHECK(!memcmp(syn_seq_bank[20], seq_lanes, sizeof seq_lanes), "slot 20 holds the live sequence");
    CHECK(sm.save_target == tgt0 && sm.save_slot == slot0, "the SAVE page's TARGET and slot are left alone");
    a[0] = 16;
    send(ED_SEQ_SLOTS, a, 1);
    collect();
    k = find(ED_SEQ_SLOTS, 16);
    CHECK(k >= 0 && m_arg[k][1] == 8 && m_arg[k][2 + 4 * 17] == 1 && m_arg[k][2 + 4 * 17 + 1] == 19 &&
          m_arg[k][2 + 4 * 17 + 3] == 21, "SLOTS: slot 20 used, named STU");

    /* LOAD slot 0 (a factory sequence): it comes back as lanes */
    {
        uint8_t used0 = syn_slot_used(UISAVE_SEQ_SETTINGS, 0);
        a[0] = 0;
        send(ED_SEQ_LOAD, a, 1);
        run(40);
        collect();
        if (used0) {
            CHECK(!memcmp(syn_seq_bank[0], seq_lanes, sizeof seq_lanes), "LOAD: slot 0 is live");
            CHECK(count(ED_SEQ_LANE) >= 1, "LOAD: the new lanes go out (%u)", count(ED_SEQ_LANE));
        } else {
            printf("(slot 0 empty: LOAD not checked)\n");
        }
    }

    /* PLAY and the playhead */
    a[0] = 1;
    send(ED_SEQ_PLAY, a, 1);
    run(1500);
    collect();
    k = find(ED_SEQ_POS, -1);
    CHECK(count(ED_SEQ_POS) >= 3 && k >= 0 && (m_arg[k][0] & 1u), "PLAY: the playhead moves (%u)", count(ED_SEQ_POS));
    a[0] = 0;
    send(ED_SEQ_PLAY, a, 1);
    run(100);
    collect();
    k = find(ED_SEQ_POS, -1);
    CHECK(k >= 0 && !(m_arg[k][0] & 1u), "STOP: shown stopped");

    printf("seq_sysex: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
