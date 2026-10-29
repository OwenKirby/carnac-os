#pragma once
/* SYNTH_COLD (2026-10-29): code that runs at start-up, on a load or once per event (a MIDI message, a note, a step),
 * never per sample or per control tick: built for size inside the -O2 DSP unit (build_fm1.py), to free flash for new
 * features. Clang's minsize (the FM1's and the web emulator's compilers); empty for the host's gcc. noinline too: an
 * -O2 caller (carp_frame, maq_frame) otherwise inlines its own copies, 3.3 KB more on 2026-10-29. */
#if defined(__clang__)
#define SYNTH_COLD __attribute__((minsize, noinline))
#else
#define SYNTH_COLD
#endif
