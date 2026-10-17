#include "global.h"

GlobalSystemConfig global_sys;

static void global_factory_defaults(void)
{
    global_sys.midi_channel = 1;
    global_sys.sync_mode = SYNC_INTERNAL;
    global_sys.master_tune_cent = 0;
}

#if defined(__EMSCRIPTEN__) || defined(FELUCCA_ID)
/* The browser emulator has no desktop filesystem. Persisted browser settings can
   be added later through localStorage; boot must never touch WASI file APIs.
   The FM1 build (FELUCCA_ID) links no libc either: until the glue stores this record in Felucca's flash (spec 6.4,
   the globals area), it boots on the factory defaults and saving does nothing. */
void global_init(void) { global_factory_defaults(); }
void global_save_to_flash(void) { }
void global_load_from_flash(void) { global_factory_defaults(); }
#else
#include <stdio.h>
#include "flash_record.h"

void global_init(void) { global_load_from_flash(); }

/* sys_config.dat holds one flash record: the layout version byte, then GlobalSystemConfig (flash_record.h) */
void global_save_to_flash(void)
{
    uint8_t rec[FREC_SIZE_GLOBAL];
    FILE *f = fopen("sys_config.dat", "wb");
    frec_pack(FREC_GLOBAL, &global_sys, rec);
    if (f) {
        fwrite(rec, sizeof(rec), 1, f);
        fclose(f);
        printf("[GLOBAL] System configurations successfully committed to Non-Volatile Flash!\n");
    }
}

/* A missing file, a short one (the 4-byte files written before the version byte existed) or another layout version
 * all fall back to the factory defaults, which are then saved in the current layout. */
void global_load_from_flash(void)
{
    uint8_t rec[FREC_SIZE_GLOBAL];
    size_t got = 0;
    FILE *f = fopen("sys_config.dat", "rb");
    if (f) {
        got = fread(rec, 1, sizeof(rec), f);
        fclose(f);
    }
    if (got == sizeof(rec) && frec_unpack(FREC_GLOBAL, rec, &global_sys) == FREC_OK)
        return;
    global_factory_defaults();
    global_save_to_flash();
}
#endif
