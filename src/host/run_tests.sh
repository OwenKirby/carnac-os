#!/bin/sh
# Every host test of the synth, built and run: the unit tests, the render hashes, the PERF TEST's host run (normal,
# guard and the stage profile) and the X0X unity check. Run from the repo root before any FM1 build:
#
#   sh src/host/run_tests.sh            prints one line a test and "run_tests: N of N passed"
#
# Needs gcc and python (Pillow, for the unity check's font). OUT (default: a temp dir) holds the binaries.
cd "$(dirname "$0")/../.."
OUT=${OUT:-"${TMPDIR:-/tmp}/synth_tests"}
mkdir -p "$OUT"
DSP=$(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c)
I="-Isrc -Isrc/dsp -Isrc/hardware"
pass=0; total=0

run() {   # name, then the command; passes on exit status 0
    name=$1; shift
    total=$((total + 1))
    if "$@" >"$OUT/$name.log" 2>&1; then
        pass=$((pass + 1)); printf '  ok    %-22s %s\n' "$name" "$(tail -1 "$OUT/$name.log")"
    else
        printf '  FAIL  %-22s (see %s)\n' "$name" "$OUT/$name.log"; tail -5 "$OUT/$name.log" | sed 's/^/        /'
    fi
}
build() {   # name, sources...: a failed build is a failed test (and its old binary is removed, so nothing reruns it)
    name=$1; shift
    rm -f "$OUT/$name.exe"
    if gcc -O2 -w $I "$@" -lm -o "$OUT/$name.exe" >"$OUT/$name.build.log" 2>&1; then
        return 0
    fi
    total=$((total + 1))
    printf '  FAIL  %-22s (build: see %s)\n' "$name" "$OUT/$name.build.log"
    grep -m 3 error "$OUT/$name.build.log" | sed 's/^/        /'
    return 1
}

build render_hash $DSP src/host/test_render_hash.c && run render_hash "$OUT/render_hash.exe"
run render_hash_core2 "$OUT/render_hash.exe" --core2
run render_hash_core2_fail "$OUT/render_hash.exe" --core2-fail
build voice_arch $DSP src/host/test_voice_arch.c && run voice_arch "$OUT/voice_arch.exe"
build ui_arch $DSP src/UIStateMachine.c src/host/test_ui_arch.c && run ui_arch "$OUT/ui_arch.exe"
build maq_seq $DSP src/UIStateMachine.c src/host/test_maq_seq.c && run maq_seq "$OUT/maq_seq.exe"
build arp_seq $DSP src/UIStateMachine.c src/host/test_arp_seq.c && run arp_seq "$OUT/arp_seq.exe"
build seq_idle $DSP src/host/test_seq_idle.c && build seq_idle_ref -DSYNTH_SEQ_NO_IDLE $DSP src/host/test_seq_idle.c \
    && "$OUT/seq_idle_ref.exe" >"$OUT/seq_idle_ref.txt" \
    && run seq_idle sh -c "'$OUT/seq_idle.exe' | diff - '$OUT/seq_idle_ref.txt' && echo 'MAQ / ARP idle fast paths: bit-exact'"
build fx_hash src/dsp/fx.c src/host/test_fx_hash.c && run fx_hash "$OUT/fx_hash.exe"
build flash_record src/dsp/preset.c src/host/test_flash_record.c && run flash_record "$OUT/flash_record.exe"
if python src/hardware/wt_store_upload.py syx src/dsp/wavetables_image.bin "$OUT/upload.syx" >/dev/null 2>&1; then
    build wt_store src/host/test_wt_store.c src/dsp/wavetable_attach.c src/dsp/wavetables.c src/dsp/wavetables_image.c \
        && run wt_store "$OUT/wt_store.exe" "$OUT/upload.syx"
fi
build preset_store src/host/test_preset_store.c src/dsp/wavetables_image.c && run preset_store "$OUT/preset_store.exe"
build perf_test $DSP src/hardware/perf_test.c src/host/perf_test_host.c && run perf_test "$OUT/perf_test.exe"
run perf_test_guard "$OUT/perf_test.exe" --guard
build perf_test_prof -DSYNTH_PROF $DSP src/hardware/perf_test.c src/host/perf_test_host.c \
    && run perf_test_prof "$OUT/perf_test_prof.exe"
run unity_check sh src/host/x0x_unity_check.sh

echo "run_tests: $pass of $total passed"
[ "$pass" = "$total" ]
