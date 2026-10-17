/* SPDX-License-Identifier: GPL-3.0-only */
/* The 64-bit divide helpers the compiler calls for an int64_t / or %: the FM1 links no compiler runtime (no libgcc,
 * no compiler-rt), and X0X's libc.c has none. Only the screens divide 64-bit values (the filter curve's complex gain,
 * ui_page_draw.c pu_ladder), a few hundred times per redraw, so a plain shift-and-subtract is enough. Nothing in the
 * audio path may divide 64-bit values.
 *
 * A unity-build fragment for the FM1 build only (src/hardware/build_fm1.py); the host links its own runtime. No
 * 64-bit / or % in here: the compiler would turn it into a call to these. */
#include <stdint.h>

uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    int i;
    if (d == 0) {                   /* as the 32-bit divide does with its trap off (fm1_irq_init): a wrong value */
        if (rem) *rem = n;
        return ~(uint64_t)0;
    }
    if ((d >> 32) == 0 && (n >> 32) == 0) {
        uint32_t a = (uint32_t)n, b = (uint32_t)d;
        if (rem) *rem = a % b;
        return a / b;
    }
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t)1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

uint64_t __udivdi3(uint64_t n, uint64_t d) { return __udivmoddi4(n, d, 0); }

uint64_t __umoddi3(uint64_t n, uint64_t d)
{
    uint64_t r;
    (void)__udivmoddi4(n, d, &r);
    return r;
}

int64_t __divdi3(int64_t n, int64_t d)
{
    uint64_t q = __udivmoddi4(n < 0 ? 0u - (uint64_t)n : (uint64_t)n, d < 0 ? 0u - (uint64_t)d : (uint64_t)d, 0);
    return (n < 0) != (d < 0) ? (int64_t)(0u - q) : (int64_t)q;
}

int64_t __moddi3(int64_t n, int64_t d)
{
    uint64_t r;
    (void)__udivmoddi4(n < 0 ? 0u - (uint64_t)n : (uint64_t)n, d < 0 ? 0u - (uint64_t)d : (uint64_t)d, &r);
    return n < 0 ? (int64_t)(0u - r) : (int64_t)r;
}
