/* Host test: the overload shedding policy (src/dsp/shed_policy.h) against load models: what it must shed, what it must
 * leave alone, that it settles and recovers. A half is ~11.6 ms; the device smooths the load over ~16 halves.
 * Exit code 0 = pass.
 * Build: gcc -O1 -Wall -Wextra -Isrc/dsp src/host/test_shed_policy.c -o test_shed_policy */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shed_policy.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t rng = 12345u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

typedef struct {
    int floor_pct, per_voice, want;                 /* want: voices the player holds per layer (2 layers) */
    int jitter;                                     /* +- pct per half */
    int late_at;                                    /* a half at or over this pct is late */
} Model;

typedef struct { int sheds, max_n, final_n, lates, halves; } Out;

/* run `halves` halves of the model; the limit per layer is want, less the shed voices, never under 2 */
static Out run(ShedState *s, const Model *m, int halves, int sounding)
{
    Out o = {0, 0, 0, 0, 0};
    uint32_t smooth_q8 = 0;
    for (int h = 0; h < halves; h++) {
        int per_layer = m->want;
        if (s->n && per_layer > 2) per_layer = per_layer > s->n + 2 ? per_layer - s->n : 2;
        int voices = sounding ? 2 * per_layer : 0;
        int load = m->floor_pct + m->per_voice * voices + (int)(rnd() % (2u * m->jitter + 1u)) - m->jitter;
        if (load < 0) load = 0;
        uint8_t late = load >= m->late_at;
        uint32_t pm = (uint32_t)load * 10u;
        smooth_q8 = (smooth_q8 * 15u + (uint32_t)load * 256u) / 16u;
        uint32_t before = s->count;
        shed_step(s, pm, late, (uint8_t)sounding, smooth_q8 / 256u, 8);
        o.sheds += (int)(s->count - before);
        o.lates += late;
        if (s->n > o.max_n) o.max_n = s->n;
        o.halves++;
    }
    o.final_n = s->n;
    return o;
}

int main(void)
{
    ShedState s;
    Out o;
    /* 1. a chord at 80 % average with peaks near 90 and no late half: leave it alone (the device's own 80/85-90 case) */
    memset(&s, 0, sizeof s);
    o = run(&s, &(Model){31, 12, 2, 6, 100}, 3000, 1);
    CHECK(o.sheds == 0, "an 80 %% chord with 90 %% peaks was shed %d times", o.sheds);

    /* 2. voices are the cost: 6 x 14 on a 15 floor is 99 %: it sheds, settles, and does not pump forever */
    memset(&s, 0, sizeof s);
    o = run(&s, &(Model){15, 14, 3, 4, 100}, 8000, 1);
    CHECK(o.sheds >= 1, "an overload by voices was never shed");
    CHECK(o.sheds <= 12, "it pumped: %d sheds in ~90 s", o.sheds);
    CHECK(o.final_n >= 1, "it gave everything back into an overload");

    /* 3. the floor is the cost (effects at 90 %), voices add ~2: the shed buys nothing, so it is given back and held */
    memset(&s, 0, sizeof s);
    o = run(&s, &(Model){90, 1, 3, 8, 100}, 4000, 1);
    CHECK(o.sheds <= 20, "a floor overload kept shedding: %d", o.sheds);
    CHECK(o.final_n == 0 || s.hold > 0, "a futile shed was left standing (n %d)", s.n);

    /* 4. recovery: after the overload ends the voices come back, and nothing sounding lifts the limit */
    memset(&s, 0, sizeof s);
    run(&s, &(Model){15, 14, 3, 4, 100}, 800, 1);
    CHECK(s.n > 0, "setup: nothing was shed");
    o = run(&s, &(Model){15, 6, 3, 4, 100}, 2500, 1);
    CHECK(o.final_n == 0, "the voices did not come back when the load fell (n %d)", o.final_n);
    run(&s, &(Model){15, 14, 3, 4, 100}, 300, 1);
    o = run(&s, &(Model){15, 14, 3, 4, 100}, 200, 0);
    CHECK(o.final_n == 0, "silence did not lift the limit");

    /* 5. a late half sheds even when the average is low */
    memset(&s, 0, sizeof s);
    for (int i = 0; i < 20; i++) shed_step(&s, 500, 0, 1, 50, 8);
    shed_step(&s, 700, 1, 1, 50, 8);
    CHECK(s.n == 1, "a late half did not shed");

    /* 6. an edge patch: 4 voices a layer are 78 %, 5 are 95 %: it settles on 4 (n 1), not a limit cycle */
    memset(&s, 0, sizeof s);
    o = run(&s, &(Model){14, 8, 5, 3, 100}, 10000, 1);
    CHECK(o.sheds <= 10, "the edge patch pumped: %d sheds in ~2 min", o.sheds);

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("shed policy: leaves a 90%% chord alone, sheds an overload, gives a futile shed back, recovers, settles\n");
    return 0;
}
