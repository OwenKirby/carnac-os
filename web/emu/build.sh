#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Carnac in the browser: src/host/carnac_web.c (the FM1's app, src/hardware/synth_panel.c, its UI and screens, on a
# simulated FM1 over X0X's gfx.c and storage.c) and the synth's DSP, compiled to WebAssembly with Emscripten.
#   web/emu/build.sh   ->  web/emu/carnac.wasm   (the page: web/emu/index.html + worklet.js, served as they are)
# Needs X0X's tree in x0x-firmware/ (its gfx.c, storage.c, plat.h and font). Emscripten: emcc on PATH, or the emsdk
# install at $EMSDK (default below), run through its bundled Python. SYNTH_VERSION=0.9.2 names the build on the splash.
set -e
cd "$(dirname "$0")/../.."

# (absolute, as Windows spells it: emcc keys its cache on the path, and a different spelling clears the cache)
EMSDK=${EMSDK:-"$( (cd .. && (pwd -W 2>/dev/null || pwd)) )/emsdk/emsdk"}
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

FW=x0x-firmware/firmware
[ -f "$FW/src/gfx.c" ] || { echo "build: no $FW (git clone https://github.com/charlesvestal/fm1-x0x.git x0x-firmware)"; exit 1; }

# X0X's screen font (gfx.c's FONT_S: the splash's and the crash screen's words; the pages draw their own runes)
GEN=build/emu-gen
mkdir -p "$GEN"
if [ ! -f "$GEN/felucca_font.h" ]; then
    PYTHON=$(command -v python3 >/dev/null 2>&1 && python3 -c "" 2>/dev/null && echo python3 || echo python)
    if ! "$PYTHON" x0x-firmware/tools/gen_font.py "$GEN/felucca_font.h" >/dev/null 2>&1; then
        # Pillow without libraqm can't take OpenType features: drop them (only tabular digits, "tnum", are lost), as
        # src/host/x0x_unity_check.sh does
        cat > "$GEN/noraqm.py" <<'EOF'
import runpy, sys
from PIL import ImageFont, ImageDraw
def strip(f):
    def g(*a, **k):
        for x in ("features", "language", "direction"): k.pop(x, None)
        return f(*a, **k)
    return g
F = ImageFont.FreeTypeFont
_gl = F.getlength
F.getlength = lambda self, text, mode="", direction=None, features=None, language=None: _gl(self, text, mode)
for n in ("getbbox", "getmask", "getmask2"): setattr(F, n, strip(getattr(F, n)))
ImageDraw.ImageDraw.text = strip(ImageDraw.ImageDraw.text)
tool = sys.argv[1]; sys.argv = sys.argv[1:]; sys.path.insert(0, tool.rsplit("/", 1)[0])
runpy.run_path(tool, run_name="__main__")
EOF
        "$PYTHON" "$GEN/noraqm.py" x0x-firmware/tools/gen_font.py "$GEN/felucca_font.h" >/dev/null
    fi
fi

SOURCES=""
for f in src/dsp/*.c; do
    case "$f" in
        src/dsp/engine.c|src/dsp/test_wavetables.c) continue ;;
    esac
    SOURCES="$SOURCES $f"
done

VER=$(printf %s "${SYNTH_VERSION:-WEB}" | tr a-z A-Z)
# shellcheck disable=SC2086
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    "-DSYNTH_VERSION=\"$VER\"" -Isrc -Isrc/dsp -Isrc/hardware -I$FW/src -I"$GEN" \
    $SOURCES src/host/pcm_images_host.c src/host/carnac_web.c \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o web/emu/carnac.wasm

echo "build: web/emu/carnac.wasm ($(wc -c < web/emu/carnac.wasm | tr -d ' ') B)"
