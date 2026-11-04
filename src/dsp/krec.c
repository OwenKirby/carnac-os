/* The REC page's scratch-pad recorder (krec.h). */
#include "krec.h"
#include "hardware/synth_libc.h"      /* memset, memcpy (the FM1 links no libc: no memmove) */
#include "midi_clock.h"
#include "dsp_cold.h"
#if defined(__clang__)                          /* all of it for size: it runs on the clock's ticks and on notes,
                                                 * never per sample (entry points SYNTH_COLD: kept out of line) */
#pragma clang attribute push(__attribute__((minsize)), apply_to = function)
#endif

#define CH(n) ((uint8_t)((n)->chp & 0x0Fu))
#define PASS(n) ((uint8_t)((n)->chp >> 4))

/* ------------------------------------------------------------- clock --- */
/* the absolute tick at frame: the pulses since boot x KREC_SUB, plus the part of a pulse since the last one (a pulse
 * not yet seen by krec_frame counts from its start) */
static uint32_t now_abs(const Krec *k, uint32_t frame)
{
    uint32_t pc = midi_clock_pulse_count(), sub = 0;
    if (pc == k->pc && k->clocked && k->period_q8) {
        sub = (uint32_t)(((uint64_t)(frame - k->pulse_frame) * (KREC_SUB << 8)) / k->period_q8);
        if (sub > KREC_SUB - 1u) sub = KREC_SUB - 1u;
    }
    return pc * KREC_SUB + sub;
}

static uint16_t pos_of(const Krec *k, uint32_t abs)
{
    uint32_t d = abs - k->org;
    if (k->len == 0u)
        return (uint16_t)(d < KREC_MAX ? d : KREC_MAX - 1u);
    return (uint16_t)(d % k->len);
}

/* -------------------------------------------------------------- notes --- */
static void send_off(Krec *k, uint8_t note, uint8_t ch)
{
    k->out((uint8_t)(0x80u | ch), note, 0);
}

static void all_off(Krec *k)
{
    for (uint32_t i = 0; i < 16u; i++)
        if (k->act[i].live) {
            send_off(k, k->act[i].note, k->act[i].ch);
            k->act[i].live = 0;
        }
    k->nlive = 0;
}

static void play_note(Krec *k, const KrecNote *n, uint32_t abs)
{
    int32_t note = (int32_t)n->note + (int32_t)k->set.transpose - 64;
    uint32_t len = n->len, g = k->set.gate, i, slot = 0, best = 0xFFFFFFFFu;
    if (note < 0 || note > 127)
        return;
    len = g >= 64u ? len + ((len * (g - 64u)) >> 6) : (len * g) >> 6;
    if (len < 1u) len = 1u;
    if (k->len && len > (uint32_t)k->len * 2u) len = (uint32_t)k->len * 2u;
    for (i = 0; i < 16u; i++) {                      /* the same pitch sounding: off first; else a free slot, else the
                                                      * one ending soonest */
        if (k->act[i].live && k->act[i].note == (uint8_t)note && k->act[i].ch == CH(n)) {
            send_off(k, (uint8_t)note, CH(n));
            k->act[i].live = 0;
            k->nlive--;
        }
    }
    for (i = 0; i < 16u; i++) {
        uint32_t left = k->act[i].live ? k->act[i].off - abs : 0u;
        if (!k->act[i].live) { slot = i; break; }
        if (left < best) { best = left; slot = i; }
    }
    if (k->act[slot].live)
        send_off(k, k->act[slot].note, k->act[slot].ch);
    else
        k->nlive++;
    k->out((uint8_t)(0x90u | CH(n)), (uint8_t)note, n->vel ? n->vel : 1u);
    k->act[slot].live = 1;
    k->act[slot].note = (uint8_t)note;
    k->act[slot].ch = CH(n);
    k->act[slot].off = abs + len;
}

/* DENSITY: each note a fixed score 0..127, from a hash of its place and pitch, lowered for a loud note and on the
 * beat (the bar's first beat lowest): a setting always keeps the same notes, the strong ones longest */
SYNTH_COLD uint8_t krec_note_kept(const Krec *k, const KrecNote *n)
{
    uint32_t h, s;
    if (k->set.density >= 127u)
        return 1;
    h = ((uint32_t)n->t * 2654435761u) ^ ((uint32_t)n->note * 40503u);
    h ^= h >> 15;
    s = (h & 127u) * (200u - n->vel) / 200u;
    if (n->t % KREC_BAR == 0u) s >>= 2;
    else if (n->t % KREC_TPB == 0u) s >>= 1;
    return (uint8_t)(s < k->set.density);
}

static uint16_t insert_at(const Krec *k, uint16_t t)
{
    uint16_t i = k->nn;
    while (i > 0u && k->n[i - 1u].t > t) i--;
    return i;
}

/* a note played while recording: into the loop at the playhead (its start on the QUANTIZE grid) */
static void record_on(Krec *k, uint8_t ch, uint8_t note, uint8_t vel, uint32_t abs)
{
    static const uint8_t Q[KQ_COUNT] = {1, KREC_TPB / 2u, KREC_TPB / 4u, KREC_TPB / 6u};
    uint32_t q = Q[k->set.quantize < KQ_COUNT ? k->set.quantize : 0u], t = pos_of(k, abs), i, j, m;
    KrecNote *n;
    if (k->nn >= KREC_NOTES) {
        k->full = 1;
        return;
    }
    if (q > 1u) {
        t = (t + q / 2u) / q * q;
        if (k->len && t >= k->len) t -= k->len;
        if (!k->len && t >= KREC_MAX) t = KREC_MAX - 1u;
    }
    for (j = 0; j < 16u && k->open[j].used; j++) {}
    if (j == 16u) return;                            /* 16 keys already held: this one goes unrecorded */
    i = insert_at(k, (uint16_t)t);
    for (m = k->nn; m > i; m--)                     /* (no memmove on the FM1) */
        k->n[m] = k->n[m - 1u];
    k->nn++;
    n = &k->n[i];
    n->fresh = 0;
    if (k->len) {                                    /* overdub: the playhead keeps its place among the notes */
        uint32_t pd = pos_of(k, k->done), ahead = (t + k->len - pd) % k->len;
        if (k->cursor > i || (k->cursor == i && ahead == 0u) || (k->cursor == i && ahead >= k->len / 2u))
            k->cursor++;
        n->fresh = (uint8_t)(ahead != 0u && ahead < k->len / 2u);   /* the playhead reaches it soon: it sounded live */
    }
    n->t = (uint16_t)t;
    n->len = KREC_LEN_OPEN;
    n->note = note;
    n->vel = vel;
    n->chp = (uint8_t)((ch & 0x0Fu) | k->pass << 4);
    k->open[j].used = 1;
    k->open[j].note = note;
    k->open[j].ch = ch;
    k->open[j].on = abs;
    k->pass_mask |= (uint16_t)(1u << k->pass);
}

static void record_off(Krec *k, uint8_t ch, uint8_t note, uint32_t abs)
{
    for (uint32_t j = 0; j < 16u; j++) {
        uint32_t len, i;
        if (!k->open[j].used || k->open[j].note != note || k->open[j].ch != ch) continue;
        k->open[j].used = 0;
        len = abs - k->open[j].on;
        if (len < 1u) len = 1u;
        if (k->len && len >= k->len) len = k->len - 1u;
        if (len > KREC_MAX - 1u) len = KREC_MAX - 1u;
        for (i = k->nn; i-- > 0u;)                   /* the newest open note of that pitch */
            if (k->n[i].len == KREC_LEN_OPEN && k->n[i].note == note && CH(&k->n[i]) == ch) {
                k->n[i].len = (uint16_t)len;
                break;
            }
        return;
    }
}

/* every note still held while recording: its length up to now */
static void close_open(Krec *k, uint32_t abs)
{
    for (uint32_t j = 0; j < 16u; j++)
        if (k->open[j].used)
            record_off(k, k->open[j].ch, k->open[j].note, abs);
}

/* -------------------------------------------------------------- lanes --- */
static const uint16_t LANE_N[2] = {KREC_WHEEL_N, KREC_BEND_N};
static const uint8_t LANE_T[2] = {KREC_WHEEL_T, KREC_BEND_T};
static uint8_t *lane_v(Krec *k, uint32_t l) { return l ? k->bend : k->wheel; }
static uint8_t *lane_u(Krec *k, uint32_t l) { return l ? k->bend_undo : k->wheel_undo; }

static void lane_snapshot(Krec *k, uint32_t l)
{
    if (k->undo_pass[l] != k->pass) {
        memcpy(lane_u(k, l), lane_v(k, l), LANE_N[l]);
        k->undo_pass[l] = k->pass;
    }
    k->pass_mask |= (uint16_t)(1u << k->pass);
}

static uint8_t lane_at(const uint8_t *lane, uint32_t n, uint32_t step, uint32_t pos, uint32_t len)
{
    uint32_t i = pos / step, f = pos % step, j;
    uint8_t a = lane[i], b;
    if (a == KREC_LANE_NONE)
        return KREC_LANE_NONE;
    j = i + 1u;
    if (j * step >= (len ? len : KREC_MAX) || j >= n) j = 0;
    b = lane[j];
    if (b == KREC_LANE_NONE || f == 0u)
        return a;
    return (uint8_t)((int32_t)a + (((int32_t)b - (int32_t)a) * (int32_t)f) / (int32_t)step);
}

SYNTH_COLD uint8_t krec_wheel_now(const Krec *k)
{
    if (!k->playing || k->hold[0] || k->touch[0] || k->state < KREC_PLAY)
        return KREC_LANE_NONE;
    return k->lout[0];
}

static void new_pass(Krec *k);

/* the first pass closes at d ticks: the loop plays on and overdub goes on (looper style) */
static void close_loop(Krec *k, uint32_t abs, uint32_t d)
{
    uint32_t i, j;
    uint16_t pos, c = 0;
    close_open(k, abs);
    k->len = (uint16_t)d;
    for (i = 0; i < k->nn; i++) {                    /* a note past a rounded-down end wraps to the top; all play */
        if (k->n[i].t >= k->len) k->n[i].t = (uint16_t)(k->n[i].t % k->len);
        k->n[i].fresh = 0;
    }
    for (i = 1; i < k->nn; i++) {                    /* (in order again) */
        KrecNote x = k->n[i];
        for (j = i; j > 0u && k->n[j - 1u].t > x.t; j--) k->n[j] = k->n[j - 1u];
        k->n[j] = x;
    }
    k->state = KREC_ODUB;
    k->playing = 1;
    new_pass(k);
    k->touch[0] = k->touch[1] = 0;
    pos = pos_of(k, abs);
    while (c < k->nn && k->n[c].t <= pos) c++;
    k->cursor = c;
    k->done = abs;
}

/* ------------------------------------------------------------ the tick --- */
static void tick(Krec *k, uint32_t abs)
{
    uint32_t i;
    uint16_t pos = pos_of(k, abs);
    uint8_t rec = (uint8_t)(k->state == KREC_REC || k->state == KREC_ODUB);
    for (i = 0; k->nlive && i < 16u; i++)            /* note-offs due */
        if (k->act[i].live && (int32_t)(abs - k->act[i].off) >= 0) {
            send_off(k, k->act[i].note, k->act[i].ch);
            k->act[i].live = 0;
            k->nlive--;
        }
    if (k->state == KREC_REC && abs - k->org >= KREC_MAX) {   /* the first pass at 16 bars: it closes itself */
        close_loop(k, abs, KREC_MAX);
        pos = pos_of(k, abs);
    }
    if (k->len && pos == 0u) {                       /* the top: the lanes' takeovers end, notes from the first */
        k->cursor = 0;
        k->hold[0] = k->hold[1] = 0;
        k->touch[0] = k->touch[1] = 0;
    }
    for (i = 0; i < 2u; i++)                         /* a lane being written: its point here */
        if (rec && k->touch[i] && pos % LANE_T[i] == 0u)
            lane_v(k, i)[pos / LANE_T[i]] = k->live[i];
    if (!k->len || !k->playing)
        return;
    while (k->cursor < k->nn && k->n[k->cursor].t <= pos) {   /* the notes starting here */
        KrecNote *n = &k->n[k->cursor++];
        if (n->t != pos) continue;                   /* (passed while the loop was stopped or skipped) */
        if (n->fresh) { n->fresh = 0; continue; }    /* it sounded live as it was recorded */
        if (n->len == KREC_LEN_OPEN || !krec_note_kept(k, n)) continue;
        play_note(k, n, abs);
    }
    for (i = 0; i < 2u; i++) {                       /* the lanes: their value here, sent when it moves */
        uint8_t v;
        if (k->hold[i] || k->touch[i]) continue;
        v = lane_at(lane_v(k, i), LANE_N[i], LANE_T[i], pos, k->len);
        if (v != KREC_LANE_NONE && v != k->lout[i]) {
            if (i) k->out((uint8_t)(0xE0u | k->out_ch), 0, v);
            else k->out((uint8_t)(0xB0u | k->out_ch), 1, v);
            k->lout[i] = v;
        }
    }
}

static uint8_t active(const Krec *k)
{
    return (uint8_t)(k->state == KREC_REC || (k->playing && k->len) || k->nlive);
}

SYNTH_COLD uint8_t krec_frame(Krec *k, uint32_t frame)
{
    uint32_t pc = midi_clock_pulse_count(), abs;
    if (!k->clocked || pc != k->pc) {
        k->pc = pc;
        k->pulse_frame = frame;
        k->period_q8 = midi_clock_period_q8();
        k->clocked = 1;
    }
    if (!active(k))                                  /* (idle: nothing more, every sequencer frame; a start sets done) */
        return 1;
    abs = now_abs(k, frame);
    if ((int32_t)(abs - k->done) > (int32_t)(4u * KREC_SUB))   /* a long gap (the clock jumped): no burst of notes */
        k->done = abs - 1u;
    while ((int32_t)(abs - k->done) > 0)
        tick(k, ++k->done);
    return 0;
}

SYNTH_COLD uint8_t krec_next_due(const Krec *k, uint32_t *due)
{
    uint32_t sub;
    if (!active(k) || !k->clocked || !k->period_q8)
        return 0;
    sub = k->done - k->pc * KREC_SUB + 1u;           /* the next tick inside this pulse */
    if (sub >= KREC_SUB)
        return 0;                                    /* the next pulse wakes it */
    *due = k->pulse_frame + (uint32_t)(((uint64_t)sub * k->period_q8 + (KREC_SUB << 8) - 1u) / (KREC_SUB << 8));
    return 1;
}

SYNTH_COLD void krec_set_sync(Krec *k, uint8_t running, uint32_t origin_pulse)
{
    k->sync_run = running;
    k->sync_origin = origin_pulse;
}

/* the loop's top for a start at abs: the sequencers' bar it falls in when they run, else abs itself */
static uint32_t start_org(const Krec *k, uint32_t abs)
{
    uint32_t o;
    if (!k->sync_run)
        return abs;
    o = k->sync_origin * KREC_SUB;
    return o + (abs - o) / KREC_BAR * KREC_BAR;
}

/* ------------------------------------------------------------- input --- */
SYNTH_COLD void krec_input(Krec *k, uint8_t status, uint8_t data1, uint8_t data2, uint32_t frame)
{
    uint8_t kind = status & 0xF0u, ch = status & 0x0Fu;
    uint8_t rec = (uint8_t)(k->state == KREC_REC || k->state == KREC_ODUB);
    uint32_t abs;
    if (k->state == KREC_EMPTY || (kind != 0x90u && kind != 0x80u && kind != 0xB0u && kind != 0xE0u))
        return;
    abs = now_abs(k, frame);
    if (kind == 0x90u && data2) {
        if (k->state == KREC_ARMED) {                /* the first note starts the loop */
            k->org = start_org(k, abs);
            k->done = abs;
            k->len = 0;
            k->pass = 0;
            k->state = KREC_REC;
            k->out_ch = ch;
            rec = 1;
        }
        if (rec)
            record_on(k, ch, data1, data2, abs);
    } else if (kind == 0x80u || kind == 0x90u) {
        if (rec)
            record_off(k, ch, data1, abs);
    } else if ((kind == 0xB0u && data1 == 1u) || kind == 0xE0u) {   /* the mod wheel, the pitch bend (7 bits, as
                                                                     * the engine reads it) */
        uint32_t l = kind == 0xE0u;
        k->live[l] = data2;
        if (rec) {
            if (!k->touch[l]) lane_snapshot(k, l);
            k->touch[l] = 1;
            lane_v(k, l)[pos_of(k, abs) / LANE_T[l]] = data2;
        } else if (k->playing) {
            k->hold[l] = 1;
        }
        k->lout[l] = data2;
    }
}

/* ------------------------------------------------------------- the UI --- */
SYNTH_COLD void krec_init(Krec *k, KrecOut out)
{
    memset(k, 0, sizeof *k);
    k->out = out;
    memset(k->wheel, KREC_LANE_NONE, sizeof k->wheel);
    memset(k->bend, KREC_LANE_NONE, sizeof k->bend);
    k->undo_pass[0] = k->undo_pass[1] = 0xFFu;
    k->lout[0] = k->lout[1] = 0xFFu;
    k->live[1] = 64;
    k->set.transpose = 64;
    k->set.quantize = KQ_OFF;
    k->set.gate = 64;
    k->set.density = 127;
}

/* a new overdub: a new pass tag (the oldest merged into pass 0 when all 16 are taken) */
static void new_pass(Krec *k)
{
    uint32_t i;
    if (!(k->pass_mask & (1u << k->pass)))
        return;                                      /* the last one recorded nothing: reuse it */
    if (k->pass + 1u >= KREC_PASSES) {
        for (i = 0; i < k->nn; i++)
            if (PASS(&k->n[i]) >= 1u)
                k->n[i].chp = (uint8_t)((k->n[i].chp & 0x0Fu) | (PASS(&k->n[i]) - 1u) << 4);
        k->pass_mask = (uint16_t)((k->pass_mask >> 1) | (k->pass_mask & 1u));
        for (i = 1; i + 1u < KREC_PASSES; i++)
            k->pass_len[i] = k->pass_len[i + 1u];
        k->pass_len[KREC_PASSES - 1u] = 0;
        for (i = 0; i < 2u; i++)
            if (k->undo_pass[i] != 0xFFu) k->undo_pass[i] = k->undo_pass[i] ? (uint8_t)(k->undo_pass[i] - 1u) : 0xFFu;
        k->pass = KREC_PASSES - 1u;
    } else {
        k->pass++;
    }
    k->pass_len[k->pass] = 0;
}

SYNTH_COLD void krec_arm(Krec *k)
{
    if (k->state == KREC_EMPTY)
        k->state = KREC_ARMED;
}

SYNTH_COLD void krec_tap(Krec *k, uint32_t frame)
{
    uint32_t abs = now_abs(k, frame), d;
    switch (k->state) {
    case KREC_EMPTY: k->state = KREC_ARMED; break;
    case KREC_ARMED: k->state = KREC_EMPTY; break;
    case KREC_REC:                                   /* the loop closes on the nearest beat (at least one) */
        d = abs - k->org;
        d = (d + KREC_TPB / 2u) / KREC_TPB * KREC_TPB;
        if (d < KREC_TPB) d = KREC_TPB;
        if (d > KREC_MAX) d = KREC_MAX;
        close_loop(k, abs, d);
        break;
    case KREC_ODUB:
        close_open(k, abs);
        k->touch[0] = k->touch[1] = 0;
        k->state = KREC_PLAY;
        break;
    case KREC_PLAY:
    default:
        new_pass(k);
        k->state = KREC_ODUB;
        if (!k->playing)
            krec_set_playing(k, 1, frame);
        break;
    }
}

SYNTH_COLD void krec_set_playing(Krec *k, uint8_t on, uint32_t frame)
{
    uint32_t abs = now_abs(k, frame);
    if (!k->len)
        return;
    if (!on) {
        close_open(k, abs);
        all_off(k);
        k->playing = 0;
        if (k->state == KREC_ODUB) k->state = KREC_PLAY;
        return;
    }
    if (k->playing)
        return;
    k->org = start_org(k, abs);                      /* from the top (on the sequencers' bar when they run) */
    k->done = abs - 1u;
    k->cursor = 0;
    {
        uint16_t pos = pos_of(k, abs);
        while (k->cursor < k->nn && k->n[k->cursor].t < pos) k->cursor++;
    }
    k->playing = 1;
    k->hold[0] = k->hold[1] = 0;
}

SYNTH_COLD uint8_t krec_undo(Krec *k)
{
    int32_t p;
    uint32_t i, o = 0;
    if (k->state == KREC_REC) {
        krec_clear(k);                               /* the first pass going: nothing left */
        k->state = KREC_ARMED;
        return 1;
    }
    for (p = KREC_PASSES - 1; p >= 0 && !(k->pass_mask & (1u << p)); p--) {}
    if (p < 0)
        return 0;
    if (p == 0) {                                    /* only the first pass: all of it */
        krec_clear(k);
        k->state = KREC_ARMED;
        return 1;
    }
    all_off(k);
    for (i = 0; i < 16u; i++) k->open[i].used = 0;
    for (i = 0; i < k->nn; i++)                      /* its notes */
        if (PASS(&k->n[i]) != (uint8_t)p) k->n[o++] = k->n[i];
    k->nn = (uint16_t)o;
    for (i = 0; i < 2u; i++)                         /* its lane writes */
        if (k->undo_pass[i] == (uint8_t)p) {
            memcpy(lane_v(k, i), lane_u(k, i), LANE_N[i]);
            k->undo_pass[i] = 0xFFu;
        }
    if (k->pass_len[p]) {                            /* a DOUBLE: the length before it */
        k->len = k->pass_len[p];
        k->pass_len[p] = 0;
    }
    k->pass_mask &= (uint16_t)~(1u << p);
    k->pass = (uint8_t)p;                            /* (its tag free again) */
    k->touch[0] = k->touch[1] = 0;
    if (k->state == KREC_ODUB) k->state = KREC_PLAY; /* overdub off */
    k->cursor = 0;
    k->full = 0;
    {
        uint16_t pos = k->len ? pos_of(k, k->done) : 0u;
        while (k->cursor < k->nn && k->n[k->cursor].t <= pos) k->cursor++;
    }
    return 1;
}

SYNTH_COLD uint8_t krec_can_double(const Krec *k)
{
    return (uint8_t)(k->len && k->state >= KREC_PLAY && (uint32_t)k->len * 2u <= KREC_MAX &&
                     (uint32_t)k->nn * 2u <= KREC_NOTES);
}

SYNTH_COLD uint8_t krec_double(Krec *k)
{
    uint32_t i, n = k->nn, L = k->len, w, b;
    if (!krec_can_double(k))
        return 0;
    close_open(k, k->done);
    new_pass(k);
    lane_snapshot(k, 0);
    lane_snapshot(k, 1);
    for (i = 0; i < n; i++) {                        /* the copy, a pass of its own, after the originals (in order) */
        k->n[n + i] = k->n[i];
        k->n[n + i].t = (uint16_t)(k->n[i].t + L);
        k->n[n + i].chp = (uint8_t)((k->n[i].chp & 0x0Fu) | k->pass << 4);
        k->n[n + i].fresh = 0;
    }
    k->nn = (uint16_t)(n * 2u);
    w = L / KREC_WHEEL_T;
    b = L / KREC_BEND_T;
    memcpy(&k->wheel[w], k->wheel, w);
    memcpy(&k->bend[b], k->bend, b);
    k->pass_len[k->pass] = (uint16_t)L;
    k->pass_mask |= (uint16_t)(1u << k->pass);
    k->len = (uint16_t)(L * 2u);
    {
        uint16_t pos = pos_of(k, k->done);           /* the playhead stays in the first half */
        k->cursor = 0;
        while (k->cursor < k->nn && k->n[k->cursor].t <= pos) k->cursor++;
    }
    if (k->state == KREC_ODUB)                       /* overdub goes on, into a pass after the copy */
        new_pass(k);
    return 1;
}

SYNTH_COLD void krec_clear(Krec *k)
{
    KrecOut out = k->out;
    KrecSettings s = k->set;
    uint32_t pc = k->pc, pf = k->pulse_frame, per = k->period_q8, done = k->done;
    uint8_t clocked = k->clocked, sr = k->sync_run;
    uint32_t so = k->sync_origin;
    all_off(k);
    krec_init(k, out);
    k->set = s;
    k->pc = pc; k->pulse_frame = pf; k->period_q8 = per; k->done = done; k->clocked = clocked;
    k->sync_run = sr; k->sync_origin = so;
}

SYNTH_COLD uint16_t krec_pos(const Krec *k, uint32_t frame)
{
    if (k->state < KREC_REC)
        return 0;
    if (k->state != KREC_REC && !k->playing)
        return 0;
    return pos_of(k, now_abs(k, frame));
}

#if defined(__clang__)
#pragma clang attribute pop
#endif
