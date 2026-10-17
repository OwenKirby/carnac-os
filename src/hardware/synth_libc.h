#pragma once
/* The C library the synth uses: memset, memcpy and memcmp, nothing else.
 *
 * The FM1 build links no libc (felucca-firmware/tools/build.py links crt0 + vectors + felucca.o only), and its
 * toolchain may ship no <string.h>. Felucca's firmware/src/libc.c defines these three, with an `unsigned` length, in
 * the same translation unit, so the FM1 build declares them here with the same signatures. The host and the browser
 * emulator use the real <string.h>. Anything else (abs, rand, printf...) must not be called from src/ outside
 * src/host. */
#if defined(FELUCCA_ID)
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#else
#include <string.h>
#endif
