/* SPDX-License-Identifier: GPL-3.0-only */
/* Overload shedding policy (the FM1 glue's shed_update calls shed_step once a half; host-tested by test_shed_policy.c).
 *
 * n voices are taken off each layer's limit (voice_shed; the voice left out fades in a few ms). Rules, 2026-11-09:
 *  - trouble = a late half, or SHED_ON_PM (97 %) two halves in a row. (90 % shed patches that were never going to
 *    glitch: peaks of 85..93 % on a chord at 80 % average, with no late half.)
 *  - A shed is JUDGED SHED_JUDGE halves later: if the smoothed load fell by less than SHED_RELIEF points the voices were
 *    not the cost (effects, MFX, the sequencer): the voice is given back at once and shedding is held off for SHED_HOLD
 *    halves. Taking a chord down to one voice a layer for no gain was the worst case on the device (80..84 % at 3 voices
 *    with 2 shed).
 *  - A voice comes back after `need` calm halves, when the load it would come back to is safe: the smoothed load plus
 *    the relief the shed bought (measured at the judging) is under SHED_BACK_PCT. A steady overload therefore keeps its
 *    shed voices (no shed / give back / overload cycle, each turn of which is a glitch) and they return when the player
 *    lets go of notes. A shed within SHED_REBOUND halves of a give-back doubles `need` (to SHED_MAXNEED); a long quiet
 *    stretch resets it.
 *  - With nothing sounding for SHED_CALM halves the limit is lifted whole.
 *  - Tails first (2026-11-11, the user: a stolen voice beats a shed one): while a released note still rings, trouble
 *    cuts the quietest tail (shed_step returns 1; the render chokes it) and the limit stays. Only with every voice
 *    held is the limit cut. A tail cut is judged like a shed: one that bought nothing holds cutting off too. */
#ifndef SHED_POLICY_H
#define SHED_POLICY_H
#include <stdint.h>

#define SHED_ON_PM 970u                             /* per mille of a half's time */
#define SHED_BACK_PCT 88u                           /* the load (smoothed, plus the relief a give-back undoes) a voice may come back to */
#define SHED_GAP 4u                                 /* halves between two sheds (~46 ms) */
#define SHED_CALM 43u                               /* halves of calm before a voice comes back (~0.5 s) */
#define SHED_MAXNEED 344u                           /* ~4 s */
#define SHED_REBOUND 172u                           /* a shed this soon after a give-back doubles the calm needed */
#define SHED_JUDGE 32u                              /* halves until a shed is judged (the load is smoothed over ~16) */
#define SHED_RELIEF 3u                              /* points the load must have fallen by */
#define SHED_HOLD 345u                              /* halves without shedding after a futile one (~4 s) */

typedef struct {
    uint32_t count;                                 /* voices shed since boot (the debug read) */
    uint16_t wait, calm, need, since, judge, hold, hot, idle;
    uint8_t pre;                                    /* the smoothed load when the last shed was made */
    uint8_t relief;                                 /* points that shed bought, as judged */
    uint8_t n;                                      /* voices off each layer */
    uint8_t tcut;                                   /* the shed being judged was a tail cut */
    uint32_t tail_count;                            /* tails cut since boot (the debug read) */
} ShedState;

/* once a half; tails: released voices still ringing. Returns 1 when the quietest tail is to be cut now. */
static inline uint8_t shed_step(ShedState *s, uint32_t pm, uint8_t late, uint8_t sounding, uint32_t cpu_pct,
                                uint8_t pool, uint8_t tails)
{
    uint8_t trouble;
    if (!s->need)
        s->need = SHED_CALM;
    if (s->wait) s->wait--;
    if (s->hold) s->hold--;
    if (s->since < 0xFFFFu) s->since++;
    s->hot = pm >= SHED_ON_PM ? (uint16_t)(s->hot + 1u) : 0u;
    s->idle = sounding ? 0u : (uint16_t)(s->idle + (s->idle < 0xFFFFu));
    if (s->since > 4u * SHED_MAXNEED)
        s->need = SHED_CALM;
    if (s->n && s->idle >= SHED_CALM) {             /* nothing sounds: nothing to protect */
        s->n = 0;
        s->calm = s->judge = s->tcut = 0;
        return 0;
    }
    if (s->judge && --s->judge == 0 && (s->n || s->tcut)) {
        uint32_t r = s->pre > cpu_pct ? s->pre - cpu_pct : 0u;
        if (r < SHED_RELIEF) {                      /* no relief: the voices were not it */
            if (!s->tcut)
                s->n--;
            s->hold = SHED_HOLD;
            s->calm = s->tcut = 0;
            return 0;
        }
        if (!s->tcut)
            s->relief = (uint8_t)r;
        s->tcut = 0;
    }
    trouble = (uint8_t)(late || s->hot >= 2u);
    if (trouble) {
        s->calm = 0;
        if (tails) {                                /* a tail first: it was going to be stolen anyway */
            if (s->hold || s->wait || (s->judge && s->tcut && !late))
                return 0;                           /* (judging one: another only for a late half) */
            if (!(s->judge && s->tcut))
                s->pre = (uint8_t)(cpu_pct > 255u ? 255u : cpu_pct);
            s->judge = SHED_JUDGE;
            s->tcut = 1;
            s->wait = SHED_GAP;
            s->tail_count++;
            return 1;
        }
        if (!s->hold && !s->wait && s->n + 1u < pool) {
            if (s->since < SHED_REBOUND && s->need < SHED_MAXNEED)
                s->need = (uint16_t)(s->need * 2u > SHED_MAXNEED ? SHED_MAXNEED : s->need * 2u);
            s->n++;
            s->pre = (uint8_t)(cpu_pct > 255u ? 255u : cpu_pct);
            s->judge = SHED_JUDGE;
            s->tcut = 0;
            s->wait = SHED_GAP;
            s->count++;
        }
    } else if (s->n && !s->judge && cpu_pct + s->relief < SHED_BACK_PCT) {
        if (++s->calm >= s->need) {
            s->n--;
            s->calm = 0;
            s->since = 0;
        }
    } else {
        s->calm = 0;
    }
    return 0;
}
#endif
