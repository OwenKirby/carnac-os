#pragma once
/* Where big buffers live on the FM1 (felucca-firmware/firmware/app.ld).
 *
 * The .data + .bss region is only 96 KiB. Large buffers go in .pool instead (0x54000 = 336 KiB at 0x01C20000,
 * NOLOAD, zero-initialised at boot like .bss; the Felucca build wants at least 8 KiB of it left spare).
 * Felucca's own engines do the same (fx.c, gfx.c, eng_grain.c...).
 *
 *   static int16_t big_line[N] SYNTH_POOL;
 *
 * Only the Felucca build (which always passes -DFELUCCA_ID) has a .pool output section. On the host and in the
 * browser emulator the macro is empty and the buffer stays in ordinary .bss. */
#if defined(FELUCCA_ID)
#define SYNTH_POOL __attribute__((section(".pool")))
#else
#define SYNTH_POOL
#endif
