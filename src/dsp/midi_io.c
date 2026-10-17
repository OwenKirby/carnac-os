
#include "midi_io.h"
#include "preset.h"   // custom CC numbers, from the performance (GLOBAL, spec 4.6)

void midi_io_init(MidiInterfaceState *midi) {
    midi->active_note = -1;
    midi->last_velocity = 0;
    midi->mod_wheel_val = 0;
    midi->aftertouch_val = midi->custom_cc1_val = midi->custom_cc2_val = 0;
    midi->pitch_bend_val = 0;
}

MidiEventSignal midi_parse_three_bytes(MidiInterfaceState *midi, 
                                       uint8_t status_byte, uint8_t data1, uint8_t data2) {
    
    uint8_t message_type = status_byte & 0xF0;

    // The channel filter lives in synth_runtime (layer routing), so this only sees messages for a layer.
    switch (message_type) {
        case 0x90: // NOTE ON
            if (data2 > 0) {
                midi->active_note = data1;
                midi->last_velocity = data2;
                return MIDI_EVENT_NOTE_ON; // Notify coordinator a note was struck
            } else {
                goto handle_note_off;
            }
            break;

        case 0x80: // NOTE OFF
        handle_note_off:
            if (data1 == midi->active_note) {
                midi->active_note = -1;
                return MIDI_EVENT_NOTE_OFF; // Notify coordinator a note was released
            }
            break;

        case 0xB0: // CONTROL CHANGE
            if (data1 == 0x01) { // Mod Wheel
                midi->mod_wheel_val = data2;
                return MIDI_EVENT_CC_CHANGE; // Notify coordinator data changed
            }
            // Custom CC1 / CC2 follow the controller numbers set in GLOBAL (spec 4.6). 0 = unassigned.
            if (perf_state.cc1_num != 0 && data1 == perf_state.cc1_num) {
                midi->custom_cc1_val = data2;
                return MIDI_EVENT_CC_CHANGE;
            }
            if (perf_state.cc2_num != 0 && data1 == perf_state.cc2_num) {
                midi->custom_cc2_val = data2;
                return MIDI_EVENT_CC_CHANGE;
            }
            break;

        default:
            break;
    }

    if (message_type == 0xD0) { midi->aftertouch_val = data1; return MIDI_EVENT_CC_CHANGE; }
    if (message_type == 0xE0) { midi->pitch_bend_val = (int)data2 - 64; return MIDI_EVENT_CC_CHANGE; }
    return MIDI_EVENT_NONE;
}

