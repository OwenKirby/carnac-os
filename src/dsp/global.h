
#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdint.h>

typedef enum { SYNC_INTERNAL, SYNC_EXTERNAL } SyncSource;

// Packed structure ensuring byte alignment matches what is written directly to Flash memory
typedef struct __attribute__((packed)) {
    uint8_t midi_channel;     // 1 to 16 (Default: 1)
    uint8_t sync_mode;        // 0 = SYNC_INTERNAL, 1 = SYNC_EXTERNAL
    uint8_t master_tune_cent; // Fine tuning offset (-50 to +50 cents)
    uint8_t pad_byte;         // Aligns our struct cleanly to a 4-byte hardware boundary
} GlobalSystemConfig;

// Expose a single active runtime instance of your settings globally to the engine
extern GlobalSystemConfig global_sys;

void global_init(void);
void global_save_to_flash(void);
void global_load_from_flash(void);

#endif

