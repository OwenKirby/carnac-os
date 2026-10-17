#pragma once
/* Shared clamps. One definition each: on the FM1 every src/ file lands in one translation unit (Felucca's unity
 * build), where per-file copies of these collide. int32 only: synth_runtime.c's int64 mix sums use clamp_q15_64. */
#include <stdint.h>

static inline int32_t clamp_q15(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* v / 3, exactly (truncated toward zero, as C's /, for every int32: checked against v / 3 over all 2^32 values),
 * by the signed magic number 0x55555556: one 32 x 32 -> 64 multiply (~15 cycles on the FM1) for a hardware divide
 * (~24, and the compiler does not turn a constant /3 into this on its own). The soft clippers' x^3/3. */
static inline int32_t div3_s32(int32_t v)
{
    int32_t q = (int32_t)(((int64_t)v * 1431655766LL) >> 32);
    return q + (int32_t)((uint32_t)v >> 31);
}
