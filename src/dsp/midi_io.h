
#ifndef MIDI_IO_H
#define MIDI_IO_H

#include <stdint.h>

// Structural event flags passed out to the engine coordinator
typedef enum {
    MIDI_EVENT_NONE,
    MIDI_EVENT_NOTE_ON,
    MIDI_EVENT_NOTE_OFF,
    MIDI_EVENT_CC_CHANGE
} MidiEventSignal;

typedef struct {
    int active_note;       // Currently held note number (-1 if none)
    int last_velocity;     // Most recent key strike velocity
    int pitch_bend_val;    // -64..+63, from the MSB of the 14-bit wheel (spring-centred, spec 5.1)
    int mod_wheel_val;     // CC #1
    int aftertouch_val;
    int custom_cc1_val;
    int custom_cc2_val;
} MidiInterfaceState;

void midi_io_init(MidiInterfaceState *midi);

// FIXED TYPO: Changed MideEventSignal to MidiEventSignal to perfectly match the enum definition!
MidiEventSignal midi_parse_three_bytes(MidiInterfaceState *midi, 
                                       uint8_t status_byte, uint8_t data1, uint8_t data2);

#endif

