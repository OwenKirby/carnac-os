#!/bin/sh
# FM-1 stand-in build check (no JieLi toolchain needed): compiles Felucca's own firmware/src/felucca.c with every synth
# source #included where the glue will put them, for i686 freestanding with -DFELUCCA_ID, and lists anything the
# program calls but nothing defines (the FM-1 links no libc). See docs/dual-core-watchlist.md, "How it was checked".
#
#   src/host/fm1_unity_check.sh            from the repo root
#
# Passes when: no compile errors, and the only undefined functions are isr_alnk0 / isr_timer5 (hal/fm1_isr.S).
# x86 can't assemble pi32v2 inline asm, so this stops at LLVM IR: name collisions, types, missing functions and libc
# use are caught; code size and the link map are not.
#
# Environment: CLANG (default: the emsdk clang next to this repo), WORK (default: a temp dir; Felucca's generated
# headers are made there once with its tools/gen_*.py, which need Pillow and fontTools).
set -e
cd "$(dirname "$0")/../.."
# a Windows clang (emsdk) wants C:/... paths, not MSYS /c/...: convert when cygpath is there
winpath() { cygpath -m "$1" 2>/dev/null || printf '%s
' "$1"; }
ROOT=$(winpath "$(pwd)")
CLANG=${CLANG:-"$(pwd)/../emsdk/emsdk/upstream/bin/clang"}
WORK=${WORK:-"${TMPDIR:-/tmp}/fm1_unity_check"}
mkdir -p "$WORK/gen" "$WORK/overlay"
WORK=$(winpath "$WORK")
FW=felucca-firmware/firmware

# Felucca's generated headers (build.py generate()). Pillow without libraqm can't take OpenType features: run the font
# and icon tools with them dropped (glyph shapes don't matter for a compile check).
if [ ! -f "$WORK/gen/ui_fonts.h" ]; then
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
    T=felucca-firmware/tools
    python "$WORK/noraqm.py" "$T/gen_aa_font.py" "$WORK/gen/ui_fonts.h" --preset inter-tight >/dev/null
    python "$WORK/noraqm.py" "$T/gen_aa_icons.py" "$WORK/gen/ui_icons.h" >/dev/null
    python "$T/gen_aa_keycaps.py" "$WORK/gen/ui_keycaps.h" >/dev/null
    python "$T/gen_ui_palettes.py" "$WORK/gen/ui_palettes.h" >/dev/null
    python "$T/gen_tables.py" "$WORK/gen/felucca_tables.h" >/dev/null
    python "$T/gen_fm6_patches.py" "$WORK/gen/felucca_fm6.h" >/dev/null
    python "$T/gen_samples.py" "$WORK/gen/felucca_samples.h" >/dev/null
fi

# felucca.c with the synth before editor.c; editor.c with the wavetable store after editor_menu.c
SYN=""
for f in src/dsp/*.c; do
    case "$f" in */engine.c|*/test_wavetables.c|*/wavetables_image.c) continue ;; esac
    SYN="$SYN#include \"$ROOT/$f\"\n"
done
for f in src/UIStateMachine.c src/ui_seq_draw.c src/ui_arp_draw.c src/ui_page_draw.c; do SYN="$SYN#include \"$ROOT/$f\"\n"; done
awk -v syn="$SYN" '/^#include "editor.c"/ {printf "/* ---- synth ---- */\n%s", syn} {print}' "$FW/src/felucca.c" > "$WORK/overlay/felucca_synth.c"
awk -v p="$ROOT" '{print} /^#include "editor_menu.c"/ {print "#include \"" p "/src/hardware/wt_store_fm1.c\""}' \
    "$FW/src/editor.c" > "$WORK/overlay/editor.c"

FLAGS="--target=i686-unknown-none-elf -ffreestanding -fno-builtin -Os -Wall -Wno-unused-function -DFELUCCA_ID=\"FM-1\"
       -I$WORK/overlay -I$FW/hal -I$FW/src -I$WORK/gen -Isrc -Isrc/dsp -Isrc/hardware"
# shellcheck disable=SC2086
"$CLANG" $FLAGS -S -emit-llvm "$WORK/overlay/felucca_synth.c" -o "$WORK/felucca_synth.ll" 2>"$WORK/log.txt" || {
    grep -E "error" "$WORK/log.txt" | head -30; echo "fm1_unity_check: compile FAILED"; exit 1; }
grep -E "warning" "$WORK/log.txt" | grep -E "/src/(dsp|hardware|UI|ui_)" | head -20 || true
UNDEF=$(grep -E "^declare" "$WORK/felucca_synth.ll" | grep -v "@llvm\." | sed -E 's/.*@([A-Za-z0-9_]+)\(.*/\1/' |
        grep -vxE "isr_alnk0|isr_timer5" || true)
if [ -n "$UNDEF" ]; then
    echo "fm1_unity_check: called but defined nowhere (the FM-1 links no libc):"; echo "$UNDEF"; exit 1
fi
echo "fm1_unity_check: OK (felucca.c + synth: no errors, nothing undefined)"
