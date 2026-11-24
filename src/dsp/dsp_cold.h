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

/* SYNTH_FAST (2026-11-24): code run per voice per control tick, from RAM instead of the XIP flash. On the FM1 the voice
 * control tick, run cold (its code fetched again after the voices' render evicts it from the I-cache), cost 40..55 %
 * more than warm (the same tick run twice: FACT 0 209 vs 91 ticks a sample). build_fm1.py --fast-ram puts .c1_text at
 * the top of the pool (FAST), within a call's reach of the flash both ways; elsewhere (the host, a build without it)
 * the functions stay where they were. */
#if defined(SYNTH_FAST_RAM)
#define SYNTH_FAST __attribute__((section(".c1_text.fast")))
#else
#define SYNTH_FAST
#endif
