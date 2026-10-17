#!/bin/sh
# FM-1 stand-in build check on the X0X platform (no JieLi toolchain needed): compiles X0X's firmware/src/x0x.c with its
# app (the 909 / 808 / 303 groovebox: slots.c onwards) cut off and every synth source #included in its place, for i686
# freestanding, and lists anything the program calls but nothing defines (the FM-1 links no libc).
#
#   src/host/x0x_unity_check.sh            from the repo root
#
# X0X (x0x-firmware/, github.com/charlesvestal/fm1-x0x) is the platform the synth builds on: Felucca's HAL, USB, update
# loader and storage, plus a working second core and a safe mode. Its app layer is replaced by ours: the glue (boot,
# safe mode, audio ISR, main loop, adapted from X0X's app/main_fm1.c) goes in src/hardware/synth_main_fm1.c, included
# last when it exists. Without it, X0X's crash screen (fm1_fault) and audio_silence are missing, and nothing calls the
# synth yet, so the check only proves the sources compile together; with it, it checks the whole program.
# Passes when the compile is clean and nothing outside ALLOWED is missing.
#
# x86 can't assemble pi32v2 inline asm, so this stops at LLVM IR: name collisions, types, missing functions and libc
# use are caught; code size and the link map are not.
#
# Environment: CLANG (default: the emsdk clang next to this repo), WORK (default: a temp dir; X0X's generated font
# header is made there once with its tools/gen_font.py, which needs Pillow).
set -e
cd "$(dirname "$0")/../.."
winpath() { cygpath -m "$1" 2>/dev/null || printf '%s\n' "$1"; }
ROOT=$(winpath "$(pwd)")
CLANG=${CLANG:-"$(pwd)/../emsdk/emsdk/upstream/bin/clang"}
WORK=${WORK:-"${TMPDIR:-/tmp}/x0x_unity_check"}
mkdir -p "$WORK/gen" "$WORK/overlay"
WORK=$(winpath "$WORK")
FW=x0x-firmware/firmware
[ -f "$FW/src/x0x.c" ] || { echo "x0x_unity_check: no $FW (git clone https://github.com/charlesvestal/fm1-x0x.git x0x-firmware)"; exit 1; }

# Pillow without libraqm can't take OpenType features: run the font tool with them dropped (glyph shapes don't matter
# for a compile check)
if [ ! -f "$WORK/gen/felucca_font.h" ]; then
    cat > "$WORK/noraqm.py" <<'EOF'
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
    python "$WORK/noraqm.py" x0x-firmware/tools/gen_font.py "$WORK/gen/felucca_font.h" >/dev/null
fi

# x0x.c up to its app, then the synth: DSP, UI state machine, the canvas pieces X0X lacks, the screens
SYN=""
for f in src/dsp/*.c; do
    case "$f" in */engine.c|*/test_wavetables.c) continue ;; esac
    SYN="$SYN#include \"$ROOT/$f\"\n"
done
SYN="$SYN#include \"$ROOT/src/hardware/fm1_x0x_canvas.h\"\n"
for f in src/UIStateMachine.c src/ui_seq_draw.c src/ui_arp_draw.c src/ui_page_draw.c; do SYN="$SYN#include \"$ROOT/$f\"\n"; done
if [ -f src/hardware/synth_main_fm1.c ]; then SYN="$SYN#include \"$ROOT/src/hardware/synth_main_fm1.c\"\n"; fi
awk -v syn="$SYN" '/^#include "app\/slots.c"/ {printf "/* ---- synth (X0X app cut) ---- */\n%s", syn; done = 1} !done {print}' \
    "$FW/src/x0x.c" > "$WORK/overlay/synth_fm1.c"

FLAGS="--target=i686-unknown-none-elf -ffreestanding -fno-builtin -Os -Wall -Wno-unused-function
       -I$FW/src -I$FW/hal -I$WORK/gen -Isrc -Isrc/dsp -Isrc/hardware"
# shellcheck disable=SC2086
"$CLANG" $FLAGS -S -emit-llvm "$WORK/overlay/synth_fm1.c" -o "$WORK/synth_fm1.ll" 2>"$WORK/log.txt" || {
    grep -E "error" "$WORK/log.txt" | head -40; echo "x0x_unity_check: compile FAILED"; exit 1; }
grep -E "warning" "$WORK/log.txt" | grep -E "/src/(dsp|hardware|UI|ui_)" | head -20 || true

# the hardware's interrupt entries (hal/fm1_isr.S, fm1_vec.S, fm1_cpu1.S): assembly, not C
ALLOWED="isr_alnk0|isr_timer5|fm1_c1_entry"
UNDEF=$(grep -E "^declare" "$WORK/synth_fm1.ll" | grep -v "@llvm\." | sed -E 's/.*@([A-Za-z0-9_]+)\(.*/\1/' |
        grep -vxE "$ALLOWED" | sort || true)
if [ -n "$UNDEF" ]; then
    echo "x0x_unity_check: called but defined nowhere (glue still to write, or libc):"; echo "$UNDEF"; exit 1
fi
echo "x0x_unity_check: OK (X0X platform + synth: no errors, nothing undefined)"
