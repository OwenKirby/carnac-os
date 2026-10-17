#!/bin/sh
# Synth core in the browser: src/dsp/*.c (minus engine.c, the old shim, and test_wavetables.c) plus
# src/host/web_entry.c and src/host/web_seq_screen.c (the firmware's screen renderers, src/ui_page_draw.c with
# src/ui_seq_draw.c and src/ui_arp_draw.c, on a stand-in canvas), compiled to WebAssembly with Emscripten.
#   web/emu/build.sh   ->  web/emu/synth.js, web/emu/synth.wasm
# Emscripten: emcc on PATH, or the emsdk install at $EMSDK (default below), run through its bundled Python.
# EMSDK=/path/to/emsdk web/emu/build.sh overrides the install location.
set -e
cd "$(dirname "$0")/../.."

EMSDK=${EMSDK:-"$(pwd)/../emsdk/emsdk"}
if command -v emcc >/dev/null 2>&1; then
    emcc() { command emcc "$@"; }
elif [ -f "$EMSDK/upstream/emscripten/emcc.py" ]; then
    PY=$(ls "$EMSDK"/python/*/python.exe 2>/dev/null | head -n 1)
    [ -n "$PY" ] || PY=python3
    NODE_DIR=$(ls -d "$EMSDK"/node/*/ 2>/dev/null | head -n 1)
    [ -n "$NODE_DIR" ] && PATH="$NODE_DIR:$PATH"
    export PATH EM_CONFIG="$EMSDK/.emscripten" EMSDK
    emcc() { "$PY" "$EMSDK/upstream/emscripten/emcc.py" "$@"; }
else
    echo "build: no emcc on PATH and no emsdk at \$EMSDK ($EMSDK)"
    exit 1
fi

SOURCES=""
for f in src/dsp/*.c; do
    case "$f" in
        src/dsp/engine.c|src/dsp/test_wavetables.c) continue ;;
    esac
    SOURCES="$SOURCES $f"
done

EXPORTS="_web_init,_web_midi,_web_render,_web_out_ptr,_web_max_frames,_web_sample_rate,_web_knob_get,_web_knob_set,_web_knob_label,_web_knob_kind,_web_knob_text,_web_page_visible,_web_layer_arch,_web_arch_name,_web_layer_voices,_web_perf_mode,_web_ui_page,_web_ui_layer,_web_ui_select,_web_ui_algo,_web_ui_preset,_web_ui_preset_target,_web_ui_preset_slot,_web_ui_count,_web_ui_cursor,_web_ui_active,_web_ui_state,_web_ui_value,_web_ui_label,_web_ui_text,_web_ui_transport,_web_ui_button,_web_ui_wpage,_web_ui_uipage,_web_ui_focus_layer,_web_ui_save_armed,_web_seq_key,_web_arp_key,_web_arp_pos,_web_arp_held,_web_key_leds,_web_screen_draw,_web_seq_playing,_web_seq_recording,_web_seq_draw,_web_seq_palette,_web_seq_fb,_web_seq_blits,_web_seq_text_pos,_web_seq_text_col,_web_seq_text_face,_web_seq_text_str,_web_seq_text_len,_web_seq_knob_text,_malloc,_free"

mkdir -p web/emu
# shellcheck disable=SC2086
emcc -O3 -Wall -Wextra -Isrc -Isrc/dsp -Isrc/hardware \
    $SOURCES src/UIStateMachine.c src/host/web_entry.c src/host/web_seq_screen.c \
    -sWASM=1 -sSTANDALONE_WASM=1 -sENVIRONMENT=web -sFILESYSTEM=0 \
    -sEXPORTED_FUNCTIONS="$EXPORTS" \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
    --no-entry \
    -o web/emu/synth.js

echo "build: web/emu/synth.js + synth.wasm ($(wc -c < web/emu/synth.wasm | tr -d ' ') B wasm)"
