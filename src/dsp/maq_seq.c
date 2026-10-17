/* MAQ 16/4 sequencer engine (see maq_seq.h). Every name here carries the maq_ prefix: on the FM1 this file is
 * #included into Felucca's unity build next to Felucca's own seq.c. Integer only, no division per frame (the
 * divisions run once per step), and every byte read from a lane is range-checked first, since the UI edits the lanes
 * from the main loop while this runs in the audio context and a slot may come from flash. */
#include "maq_seq.h"
#include "seq_scale.h"
#include "midi_clock.h"

enum { MAQ_DUE_NONE, MAQ_DUE_STRIKE, MAQ_DUE_RELEASE };

#define MAQ_CH_NONE 0xFFu

/* ------------------------------------------------------------ helpers --- */
static uint32_t maq_rand(MaqSeq *q)
{
    uint32_t x = q->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    q->rng = x ? x : 0x9E3779B9u;
    return q->rng;
}

/* 0..127 odds as 0 % .. 100 %: 127 always, 0 never */
static uint8_t maq_chance(MaqSeq *q, uint8_t value)
{
    if (value == 0u) return 0u;
    if (value >= VAL_MAX) return 1u;
    return (uint8_t)((maq_rand(q) & 0x7Fu) < value);
}

void maq_loop_window(const SequencerLane *lane, uint8_t *lo, uint8_t *hi)
{
    uint8_t a = lane->loop_start < SEQ_NSTEP ? lane->loop_start : (uint8_t)0u;
    uint8_t b = lane->loop_end < SEQ_NSTEP ? lane->loop_end : (uint8_t)(SEQ_NSTEP - 1u);
    *lo = a < b ? a : b;
    *hi = a < b ? b : a;
}

static uint8_t maq_event(const SequencerLane *lane)
{
    return lane->event_type < SEQEV_COUNT ? lane->event_type : (uint8_t)SEQEV_NOTE;
}

static uint8_t maq_mode(const SequencerLane *lane)
{
    return lane->playback_mode < SEQ_MODE_COUNT ? lane->playback_mode : (uint8_t)SEQ_MODE_FORWARD;
}

static uint8_t maq_prescale(const SequencerLane *lane)
{
    uint8_t p = lane->prescale;
    return p < SEQ_PRESCALE_MIN ? (uint8_t)SEQ_PRESCALE_MIN : p > SEQ_PRESCALE_MAX ? (uint8_t)SEQ_PRESCALE_MAX : p;
}

uint8_t maq_value_to_layer(uint8_t value)
{
    return (uint8_t)(((uint16_t)(value > VAL_MAX ? VAL_MAX : value) * 3u) >> 7);   /* 0..42 L1, 43..85 L2, 86..127 BOTH */
}

uint8_t maq_timing_ticks(uint8_t value)
{
    static const uint8_t ticks[4] = {4u, 8u, 16u, 32u};
    return value == 0u ? (uint8_t)0u : ticks[(value > VAL_MAX ? VAL_MAX : value) >> 5];
}

uint8_t maq_ratchet_count(uint8_t value) { return (uint8_t)(1u + ((value > VAL_MAX ? VAL_MAX : value) >> 5)); }

/* does lane m's target point at lane l? by m's event type: a lane bitmask, or a single lane / row index */
static uint8_t maq_targets(const SequencerLane *m, uint8_t ev, uint8_t l)
{
    if (ev == SEQEV_TRANSPOSE || ev == SEQEV_LAYER)
        return (uint8_t)(m->target_mask == l);
    return (uint8_t)((m->target_mask & SEQ_TGT_MASK & (1u << l)) != 0u);
}

/* the step's play-order successor in [lo, hi], from p (MAQ_POS_NONE or outside the window: the mode's first step) */
static uint8_t maq_advance(MaqSeq *q, MaqLaneRun *r, uint8_t mode, uint8_t p, uint8_t lo, uint8_t hi)
{
    int16_t n;
    if (p == MAQ_POS_NONE || p < lo || p > hi) {
        r->dir = mode == SEQ_MODE_BACKWARD ? (int8_t)-1 : (int8_t)1;
        if (mode == SEQ_MODE_RANDOM) return (uint8_t)(lo + maq_rand(q) % (uint32_t)(hi - lo + 1u));
        return mode == SEQ_MODE_BACKWARD ? hi : lo;
    }
    if (lo == hi) return lo;
    switch (mode) {
    case SEQ_MODE_BACKWARD:
        return p == lo ? hi : (uint8_t)(p - 1u);
    case SEQ_MODE_PENDULUM1:                         /* the ends play once: 1 2 3 4 3 2 1 2 .. */
        n = (int16_t)(p + r->dir);
        if (n > hi) { r->dir = -1; n = (int16_t)(p - 1); }
        else if (n < lo) { r->dir = 1; n = (int16_t)(p + 1); }
        return (uint8_t)n;
    case SEQ_MODE_PENDULUM2:                         /* the ends play twice: 1 2 3 4 4 3 2 1 1 2 .. */
        n = (int16_t)(p + r->dir);
        if (n > hi) { r->dir = -1; n = hi; }
        else if (n < lo) { r->dir = 1; n = lo; }
        return (uint8_t)n;
    case SEQ_MODE_RANDOM:
        return (uint8_t)(lo + maq_rand(q) % (uint32_t)(hi - lo + 1u));
    default:
        return p == hi ? lo : (uint8_t)(p + 1u);
    }
}

/* the MIDI channels of a layer target (SEQ_TGT_*): ch[1] = MAQ_CH_NONE unless BOTH on two channels. Without a second
 * layer (SINGL_SYNTH) everything plays on layer 1. */
static void maq_channels(const MaqSeq *q, uint8_t layer, uint8_t ch[2])
{
    uint8_t c1 = (uint8_t)(q->perf->midi_ch[0] & 0x0Fu), c2 = (uint8_t)(q->perf->midi_ch[1] & 0x0Fu);
    if (q->perf->perf_mode == PERF_SINGL_SYNTH || layer > SEQ_TGT_BOTH)
        layer = SEQ_TGT_L1;
    ch[0] = layer == SEQ_TGT_L2 ? c2 : c1;
    ch[1] = layer == SEQ_TGT_BOTH && c2 != c1 ? c2 : (uint8_t)MAQ_CH_NONE;
}

static void maq_send(const MaqSeq *q, const uint8_t ch[2], uint8_t kind, uint8_t d1, uint8_t d2)
{
    q->out((uint8_t)(kind | ch[0]), d1, d2);
    if (ch[1] != MAQ_CH_NONE)
        q->out((uint8_t)(kind | ch[1]), d1, d2);
}

/* 8.4 stuck-note shield: Note-Off every pitch in the lane's playback cache */
static void maq_release(MaqSeq *q, MaqLaneRun *r)
{
    uint8_t i;
    for (i = 0u; i < SEQ_NNOTE; i++)
        if (r->on[i] != SEQ_NOTE_NULL) {
            maq_send(q, r->on_ch, 0x80u, r->on[i], 0u);
            r->on[i] = SEQ_NOTE_NULL;
        }
}

static uint8_t maq_sounding(const MaqLaneRun *r)
{
    uint8_t i;
    for (i = 0u; i < SEQ_NNOTE; i++)
        if (r->on[i] != SEQ_NOTE_NULL)
            return 1u;
    return 0u;
}

static void maq_strike(MaqSeq *q, MaqLaneRun *r, uint8_t i)
{
    uint8_t note = r->notes[i];
    if (r->on[i] != SEQ_NOTE_NULL)                   /* (a note still held from this step's last burst) */
        maq_send(q, r->on_ch, 0x80u, r->on[i], 0u);
    r->on_ch[0] = r->ch[0];
    r->on_ch[1] = r->ch[1];
    maq_send(q, r->ch, 0x90u, note, r->vel);
    r->on[i] = note;
}

static void maq_stop_lane(MaqSeq *q, MaqLaneRun *r)
{
    maq_release(q, r);
    r->due_kind = MAQ_DUE_NONE;
    r->stepped = 0u;
}

/* -------------------------------------------------- pass A: advance --- */
/* the published value of a lane's current step: what its modifier targets read (8.4) */
static uint8_t maq_step_value(const SequencerLane *lane, uint8_t pos)
{
    uint8_t v = lane->steps[pos].value;
    return v > VAL_MAX ? (uint8_t)VAL_MAX : v;
}

/* is step p of lane l bypassed? its SKIP flag, or a PROB SKIP lane aimed at it (previous pulse's buffer) */
static uint8_t maq_skipped(MaqSeq *q, uint8_t l, uint8_t p)
{
    uint8_t m;
    if (q->lanes[l].steps[p].flags & SEQ_FLAG_SKIP)
        return 1u;
    for (m = 0u; m < SEQ_NLANE; m++)
        if ((q->xl_prev_valid & (1u << m)) && maq_event(&q->lanes[m]) == SEQEV_PROB_SKIP &&
            maq_targets(&q->lanes[m], SEQEV_PROB_SKIP, l) && maq_chance(q, q->xl_prev[m]))
            return 1u;
    return 0u;
}

static void maq_pass_a(MaqSeq *q, uint8_t l)
{
    MaqLaneRun *r = &q->run[l];
    const SequencerLane *lane = &q->lanes[l];
    uint8_t lo, hi, mode = maq_mode(lane), p = r->pos, t;
    maq_loop_window(lane, &lo, &hi);
    for (t = 0u; t < SEQ_NSTEP; t++) {               /* bounded: a window of all skipped steps stays put */
        p = maq_advance(q, r, mode, p, lo, hi);
        if (!maq_skipped(q, l, p))
            break;
    }
    r->pos = p;
    r->stepped = 1u;
    q->xl[l] = maq_step_value(lane, p);
    if (lane->steps[p].flags & SEQ_FLAG_MUTE)
        q->xl_valid &= (uint8_t)~(1u << l);
    else
        q->xl_valid |= (uint8_t)(1u << l);
}

/* ----------------------------------------------------- pass B: fire --- */
/* frames in `ticks` clock pulses at the current tempo (32 ticks at 40 BPM: ~2.4 s, well inside 32 bits in Q8) */
static uint32_t maq_frames(uint8_t ticks) { return ((uint32_t)ticks * midi_clock_period_q8()) >> 8; }

typedef struct {
    uint8_t vel, gate, ratchet, ticks, mute_odds, layer;   /* layer: SEQ_TGT_*, 0xFF = the lane's own */
    int8_t transpose;
} MaqMods;

/* what the other lanes' current steps (this pulse's buffer) do to lane l */
static void maq_mods(const MaqSeq *q, uint8_t l, MaqMods *md)
{
    uint8_t m, ev, v, bv = q->perf->base_velocity;
    md->vel = bv == 0u ? (uint8_t)1u : bv > VAL_MAX ? (uint8_t)VAL_MAX : bv;
    md->gate = MAQ_GATE_DEFAULT;
    md->ratchet = 1u;
    md->ticks = maq_prescale(&q->lanes[l]);
    md->mute_odds = 0u;
    md->layer = 0xFFu;
    md->transpose = 0;
    for (m = 0u; m < SEQ_NLANE; m++) {               /* a later lane wins over an earlier one aimed at the same lane */
        if (!(q->xl_valid & (1u << m)))
            continue;
        ev = maq_event(&q->lanes[m]);
        if (!maq_targets(&q->lanes[m], ev, l))
            continue;
        v = q->xl[m];
        switch (ev) {
        case SEQEV_DYNAMICS: md->vel = v ? v : (uint8_t)1u; break;
        case SEQEV_TRANSPOSE: md->transpose = (int8_t)((int16_t)v - (int16_t)BIPOLAR_CENTER); break;
        case SEQEV_TIMING: md->ticks = maq_timing_ticks(v); break;
        case SEQEV_GATE: md->gate = v; break;
        case SEQEV_RATCHET: md->ratchet = maq_ratchet_count(v); break;
        case SEQEV_PROB_MUTE: md->mute_odds = v; break;
        case SEQEV_LAYER: md->layer = maq_value_to_layer(v); break;
        default: break;
        }
    }
}

/* the step's distinct pitches, transposed, clamped, then held to the lane's scale (seq_scale.h; scale 0: none); 0 when
 * it holds none. The scale stays in its key and a transposition (a TRANSPOSE lane, the ARP's chord root) moves the
 * notes within it (2026-10-16, after the QY-10's re-harmonization: patterns written in C, re-mapped to the chord): C E G
 * in C major over the ARP's Am plays A C E, over Dm D F A, over B dim B D F (a tie snaps down), where a plain
 * transposition played A C# E. Without a scale it is the plain transposition, as before. */
static uint8_t maq_collect(const SequencerStep *s, uint16_t scale, int8_t transpose, uint8_t out[SEQ_NNOTE])
{
    uint8_t i, j, n = 0u;
    int16_t v;
    for (i = 0u; i < SEQ_NNOTE; i++) {
        if (s->note_buffer[i] > VAL_MAX || (i == 0u && s->note_buffer[0] == SEQ_NOTE_TIE))
            continue;                                /* NULL slot (0xFF), or slot 1's TIE marker */
        v = (int16_t)(s->note_buffer[i] + transpose);
        v = v < 0 ? (int16_t)0 : v > (int16_t)VAL_MAX ? (int16_t)VAL_MAX : v;
        v = (int16_t)seq_scale_snap((uint8_t)v, scale);
        for (j = 0u; j < n && out[j] != (uint8_t)v; j++)
            ;
        if (j == n)
            out[n++] = (uint8_t)v;
    }
    return n;
}

/* NOTE lane: shield, then schedule the step's bursts (ratchet) and strums; the first strike is now */
static void maq_fire_notes(MaqSeq *q, uint8_t l, const MaqMods *md, uint32_t len, uint8_t audition)
{
    MaqLaneRun *r = &q->run[l];
    const SequencerLane *lane = &q->lanes[l];
    const SequencerStep *s = &lane->steps[r->pos];
    uint32_t min_gate;
    int16_t t;
    if ((s->flags & SEQ_FLAG_TIE) || s->note_buffer[0] == SEQ_NOTE_TIE) {
        r->due_kind = MAQ_DUE_NONE;                  /* tie: what sounds keeps sounding (nothing: a pause) */
        return;
    }
    maq_release(q, r);                               /* 8.4: Note-Offs before this step's Note-Ons */
    r->due_kind = MAQ_DUE_NONE;
    if ((s->flags & SEQ_FLAG_MUTE) || (!audition && maq_chance(q, md->mute_odds)))
        return;
    t = (int16_t)(md->transpose + q->ext_transpose);  /* a TRANSPOSE lane plus the ARP's chord key (8.2) */
    r->n_notes = maq_collect(s, seq_lane_scale_mask(lane, q->genre), (int8_t)(t < -127 ? -127 : t > 127 ? 127 : t),
                             r->notes);
    if (r->n_notes == 0u)
        return;
    r->vel = md->vel;
    maq_channels(q, md->layer != 0xFFu ? md->layer : (uint8_t)(lane->target_mask <= SEQ_TGT_BOTH ? lane->target_mask : 0u),
                 r->ch);
    r->burst_frames = len / md->ratchet;
    if (r->burst_frames == 0u) r->burst_frames = 1u;
    r->strum_frames = md->ratchet > 1u && r->n_notes > 1u ? r->burst_frames / (2u * r->n_notes) : 0u;
    r->gate_frames = (r->burst_frames * ((uint32_t)md->gate + 1u)) >> 7;
    min_gate = r->strum_frames * (uint32_t)(r->n_notes - 1u) + 1u;   /* every strummed note sounds a frame at least */
    if (r->gate_frames < min_gate) r->gate_frames = min_gate;
    if (md->ratchet > 1u && r->gate_frames >= r->burst_frames)
        r->gate_frames = r->burst_frames > 1u ? r->burst_frames - 1u : 1u;   /* a gap before the next burst restrikes */
    if (md->ratchet == 1u && md->gate >= VAL_MAX)
        r->gate_frames = 0u;                         /* full gate: legato, held until the next step's shield */
    r->bursts_left = (uint8_t)(md->ratchet - 1u);
    r->burst_start = q->frame;
    r->strike = 0u;
    r->due_kind = MAQ_DUE_STRIKE;
    r->due = q->frame;
}

/* a controller lane: one message per step on its layer's channels (muted steps send nothing) */
static void maq_fire_cc(MaqSeq *q, uint8_t l, uint8_t ev, const MaqMods *md)
{
    const SequencerLane *lane = &q->lanes[l];
    const SequencerStep *s = &lane->steps[q->run[l].pos];
    uint8_t ch[2], v = s->value > VAL_MAX ? (uint8_t)VAL_MAX : s->value, cc;
    if ((s->flags & SEQ_FLAG_MUTE) || maq_chance(q, md->mute_odds))
        return;
    maq_channels(q, md->layer != 0xFFu ? md->layer : (uint8_t)(lane->target_mask <= SEQ_TGT_BOTH ? lane->target_mask : 0u),
                 ch);
    switch (ev) {
    case SEQEV_MODWHEEL: maq_send(q, ch, 0xB0u, 1u, v); break;
    case SEQEV_AFTERTOUCH: maq_send(q, ch, 0xD0u, v, 0u); break;
    case SEQEV_CUSTOMCC1: case SEQEV_CUSTOMCC2:
        cc = ev == SEQEV_CUSTOMCC1 ? q->perf->cc1_num : q->perf->cc2_num;
        if (cc != 0u && cc <= VAL_MAX)               /* 0 = unassigned (GLOBAL, 4.6) */
            maq_send(q, ch, 0xB0u, cc, v);
        break;
    case SEQEV_PROG_CHANGE:
        if (seq_event_available(SEQEV_PROG_CHANGE, q->perf->perf_mode))   /* DUAL_SYNTH only (7.5) */
            maq_send(q, ch, 0xC0u, v, 0u);
        break;
    default: break;
    }
}

static void maq_pass_b(MaqSeq *q, uint8_t l)
{
    MaqLaneRun *r = &q->run[l];
    MaqMods md;
    uint8_t ev = maq_event(&q->lanes[l]);
    r->stepped = 0u;
    if (ev != SEQEV_NOTE && maq_sounding(r)) {
        maq_release(q, r);                           /* EV: changed under a sounding note: it must not hang */
        r->due_kind = MAQ_DUE_NONE;
    }
    maq_mods(q, l, &md);
    if (md.ticks == 0u) {                            /* TIMING 0: the step is skipped (one tick, silent) */
        r->ticks_left = 1u;
        if (ev == SEQEV_NOTE) {
            maq_release(q, r);
            r->due_kind = MAQ_DUE_NONE;
        }
        return;
    }
    r->ticks_left = md.ticks;
    if (ev == SEQEV_NOTE)
        maq_fire_notes(q, l, &md, maq_frames(md.ticks), 0u);
    else
        maq_fire_cc(q, l, ev, &md);                  /* (modifier lanes only publish: pass A did it) */
}

/* ------------------------------------------------------ scheduling --- */
static void maq_due(MaqSeq *q, MaqLaneRun *r)
{
    uint8_t guard = 2u * SEQ_NNOTE + 2u;
    while (r->due_kind != MAQ_DUE_NONE && (int32_t)(q->frame - r->due) >= 0 && guard--) {
        if (r->due_kind == MAQ_DUE_STRIKE) {
            do
                maq_strike(q, r, r->strike++);
            while (r->strike < r->n_notes && r->strum_frames == 0u);
            if (r->strike < r->n_notes) {
                r->due = r->burst_start + r->strum_frames * r->strike;
            } else if (r->gate_frames == 0u) {
                r->due_kind = MAQ_DUE_NONE;          /* legato: the next step releases it */
            } else {
                r->due_kind = MAQ_DUE_RELEASE;
                r->due = r->burst_start + r->gate_frames;
            }
        } else {
            maq_release(q, r);
            if (r->bursts_left) {
                r->bursts_left--;
                r->burst_start += r->burst_frames;
                r->strike = 0u;
                r->due_kind = MAQ_DUE_STRIKE;
                r->due = r->burst_start;
            } else {
                r->due_kind = MAQ_DUE_NONE;
            }
        }
    }
}

static void maq_pulse(MaqSeq *q)
{
    uint8_t l;
    for (l = 0u; l < SEQ_NLANE; l++)                 /* 8.4: pass A's reads see only the previous pulse */
        q->xl_prev[l] = q->xl[l];
    q->xl_prev_valid = q->xl_valid;
    for (l = 0u; l < SEQ_NLANE; l++) {
        MaqLaneRun *r = &q->run[l];
        if (r->ticks_left > 1u) {
            r->ticks_left--;
            continue;
        }
        maq_pass_a(q, l);
    }
    for (l = 0u; l < SEQ_NLANE; l++)
        if (q->run[l].stepped)
            maq_pass_b(q, l);
}

/* every lane to its first step on the next pulse */
static void maq_rewind(MaqSeq *q)
{
    uint8_t l;
    for (l = 0u; l < SEQ_NLANE; l++) {
        maq_stop_lane(q, &q->run[l]);
        q->run[l].pos = MAQ_POS_NONE;
        q->run[l].ticks_left = 1u;
    }
    q->xl_valid = q->xl_prev_valid = 0u;
}

/* lane l publishes the record step's value (as pass A would on reaching it) */
static void maq_rec_publish(MaqSeq *q, uint8_t l)
{
    q->xl[l] = maq_step_value(&q->lanes[l], q->rec_step);
    if (q->lanes[l].steps[q->rec_step].flags & SEQ_FLAG_MUTE)
        q->xl_valid &= (uint8_t)~(1u << l);
    else
        q->xl_valid |= (uint8_t)(1u << l);
}

/* 7.2: the record step's strike on lane l (NOTE lanes sound; every lane waits its step length before the next) */
static void maq_rec_fire(MaqSeq *q, uint8_t l, uint8_t preview)
{
    MaqMods md;
    maq_mods(q, l, &md);
    if (md.ticks == 0u)
        md.ticks = maq_prescale(&q->lanes[l]);
    q->run[l].ticks_left = md.ticks;
    if (maq_event(&q->lanes[l]) != SEQEV_NOTE)
        return;
    if (preview)
        md.ratchet = 1u;                             /* a preview: one strike, the step's own length */
    maq_fire_notes(q, l, &md, maq_frames(md.ticks), 1u);
}

/* 7.2: lock every lane to the record step and sound it (NOTE lanes, with the modifier lanes' values at that step) */
static void maq_audition(MaqSeq *q)
{
    uint8_t l;
    for (l = 0u; l < SEQ_NLANE; l++) {
        maq_stop_lane(q, &q->run[l]);
        q->run[l].pos = q->rec_step;
        maq_rec_publish(q, l);
    }
    for (l = 0u; l < SEQ_NLANE; l++)
        maq_rec_fire(q, l, 1u);
}

/* 7.2 REC while playing: the playhead stays on the record step, which each lane retriggers on its own clock
 * (prescale, or a TIMING lane's length) with what the step holds now, so a chord just recorded plays on the next */
static void maq_rec_pulse(MaqSeq *q)
{
    uint8_t l, due = 0u;
    for (l = 0u; l < SEQ_NLANE; l++) {
        if (q->run[l].ticks_left > 1u) {
            q->run[l].ticks_left--;
            continue;
        }
        due |= (uint8_t)(1u << l);
        maq_rec_publish(q, l);
    }
    for (l = 0u; l < SEQ_NLANE; l++)
        if (due & (1u << l))
            maq_rec_fire(q, l, 0u);
}

/* the requests and the MIDI transport, applied at the top of a frame */
static void maq_transport(MaqSeq *q)
{
    uint8_t l, rec = q->req_rec ? 1u : 0u, step = q->req_rec_step;
    if (midi_clock_is_external()) {                  /* CLOCK SOURCE MIDI: Start plays from the top, Stop stops */
        uint32_t starts = midi_clock_start_count();
        uint8_t ext = midi_clock_ext_running();
        if (starts != q->start_seen) {
            q->start_seen = starts;
            maq_rewind(q);
            q->req_play = 1u;
        } else if (q->ext_running_seen && !ext) {
            q->req_play = 0u;
        }
        q->ext_running_seen = ext;
    } else {
        q->start_seen = midi_clock_start_count();
        q->ext_running_seen = 0u;
    }
    if (step >= SEQ_NSTEP)
        step = 0u;
    if (rec != q->rec) {
        q->rec = rec;
        if (rec) {                                   /* 7.2: free running halts at the record step */
            q->rec_step = step;
            maq_audition(q);
        } else if (q->playing) {                     /* back to free running, on from the record step */
            for (l = 0u; l < SEQ_NLANE; l++)
                maq_stop_lane(q, &q->run[l]);
        } else {
            maq_rewind(q);
        }
    } else if (rec && step != q->rec_step) {
        q->rec_step = step;
        maq_audition(q);
    }
    if ((q->req_play ? 1u : 0u) != q->playing) {
        q->playing = q->req_play ? 1u : 0u;
        if (!q->rec)
            maq_rewind(q);                           /* start: from the top; stop: every note off */
        q->rng ^= q->frame * 0x9E3779B9u;            /* random modes differ from run to run */
        if (q->rng == 0u) q->rng = 0x9E3779B9u;
    }
}

/* --------------------------------------------------------------- API --- */
void maq_init(MaqSeq *q, SequencerLane *lanes, const MasterPerformancePreset *perf, MaqMidiOut out)
{
    uint8_t l, i;
    q->lanes = lanes;
    q->perf = perf;
    q->out = out;
    for (l = 0u; l < SEQ_NLANE; l++) {
        MaqLaneRun *r = &q->run[l];
        r->pos = MAQ_POS_NONE;
        r->dir = 1;
        r->ticks_left = 1u;
        r->stepped = 0u;
        for (i = 0u; i < SEQ_NNOTE; i++) r->on[i] = SEQ_NOTE_NULL;
        r->on_ch[0] = r->on_ch[1] = MAQ_CH_NONE;
        r->due_kind = MAQ_DUE_NONE;
        r->due = 0u;
        q->xl[l] = q->xl_prev[l] = 0u;
    }
    q->xl_valid = q->xl_prev_valid = 0u;
    q->req_play = 0u;
    q->req_rec = 0u;
    q->req_rec_step = 0u;
    q->ext_transpose = 0;
    q->genre = 0;
    q->playing = q->rec = q->rec_step = 0u;
    q->ext_running_seen = midi_clock_ext_running();
    q->pulse_seen = midi_clock_pulse_count();
    q->start_seen = midi_clock_start_count();
    q->frame = 0u;
    q->rng = 0x2545F491u;
}

/* A stopped MAQ with nothing scheduled and no request: what maq_transport and the due loop would do on this frame,
 * without them (most frames; the FM1 pays ~8 cycles a load and ~45 ticks a sample went here, 2026-10-15 profile).
 * 0: the full frame must run. Bit-exact: the same writes, and the cases this skips change nothing. */
static uint8_t maq_idle_frame(MaqSeq *q)
{
    uint8_t l;
    if (q->playing | q->rec | q->req_play | q->req_rec)
        return 0u;
    for (l = 0u; l < SEQ_NLANE; l++)
        if (q->run[l].due_kind != MAQ_DUE_NONE)
            return 0u;
    if (midi_clock_is_external()) {
        if (midi_clock_start_count() != q->start_seen)
            return 0u;                               /* MIDI Start: rewind and play */
        q->ext_running_seen = midi_clock_ext_running();   /* (a Stop would clear req_play, already 0) */
    } else {
        q->start_seen = midi_clock_start_count();
        q->ext_running_seen = 0u;
    }
    return 1u;
}

void maq_frame(MaqSeq *q, uint32_t frame)
{
    uint32_t pulses = midi_clock_pulse_count(), n = pulses - q->pulse_seen;
    uint8_t l;
    q->frame = frame;
    q->pulse_seen = pulses;
#if !defined(SYNTH_SEQ_NO_IDLE)                     /* test_seq_idle.c builds it both ways */
    if (maq_idle_frame(q))
        return;
#endif
    maq_transport(q);
    if (q->playing) {
        if (n > MAQ_PULSES_PER_FRAME_MAX)
            n = MAQ_PULSES_PER_FRAME_MAX;
        while (n--) {
            if (q->rec)
                maq_rec_pulse(q);
            else
                maq_pulse(q);
        }
    }
    for (l = 0u; l < SEQ_NLANE; l++)
        if (q->run[l].due_kind != MAQ_DUE_NONE)
            maq_due(q, &q->run[l]);
}

void maq_set_playing(MaqSeq *q, uint8_t on) { q->req_play = on ? 1u : 0u; }
void maq_set_record(MaqSeq *q, uint8_t on) { q->req_rec = on ? 1u : 0u; }
void maq_set_rec_step(MaqSeq *q, uint8_t step) { q->req_rec_step = step < SEQ_NSTEP ? step : (uint8_t)0u; }

uint8_t maq_rec_nav(MaqSeq *q, uint8_t lane, int8_t detents)
{
    const SequencerLane *ln = &q->lanes[lane < SEQ_NLANE ? lane : 0u];
    uint8_t lo, hi, p = q->req_rec_step < SEQ_NSTEP ? q->req_rec_step : (uint8_t)0u;
    int16_t n = detents;
    int8_t dir = maq_mode(ln) == SEQ_MODE_BACKWARD ? (int8_t)-1 : (int8_t)1;   /* clockwise = the way it plays */
    maq_loop_window(ln, &lo, &hi);
    if (n < 0) { n = (int16_t)-n; dir = (int8_t)-dir; }
    if (n > 0 && (p < lo || p > hi)) {               /* outside the window: the first detent lands on its near end */
        p = dir > 0 ? lo : hi;
        n--;
    }
    while (n-- > 0)
        p = dir > 0 ? (p >= hi ? lo : (uint8_t)(p + 1u)) : (p <= lo ? hi : (uint8_t)(p - 1u));
    q->req_rec_step = p;
    return p;
}

uint8_t maq_playing(const MaqSeq *q) { return q->req_play ? 1u : 0u; }
uint8_t maq_recording(const MaqSeq *q) { return q->req_rec ? 1u : 0u; }
uint8_t maq_rec_step(const MaqSeq *q) { return q->req_rec_step < SEQ_NSTEP ? q->req_rec_step : (uint8_t)0u; }
uint8_t maq_lane_pos(const MaqSeq *q, uint8_t lane) { return lane < SEQ_NLANE ? q->run[lane].pos : (uint8_t)MAQ_POS_NONE; }
uint8_t maq_free_running(const MaqSeq *q) { return (uint8_t)(q->playing && !q->rec); }
void maq_set_ext_transpose(MaqSeq *q, int8_t semitones) { q->ext_transpose = semitones; }
void maq_set_genre(MaqSeq *q, uint8_t genre) { q->genre = genre; }
