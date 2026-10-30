#!/usr/bin/env bash
#
# mayhem/build.sh — builds SkyEmu's libretro core API (src/libretro.c + shared.c + localization.c, no GUI,
# no network) with the fuzzing sanitizers, links it into the libFuzzer harnesses and their -standalone
# reproducers, and builds the functional oracle (mayhem/oracle/oracle.c) with the project's NORMAL flags.
# Runs in /mayhem as `mayhem`; re-runnable and offline (nothing is fetched).
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# `=` (no colon) on SANITIZER_FLAGS so an explicit empty value builds with no sanitizers.
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
FUZZ_DIR="$SRC/mayhem/build-fuzz"
TEST_DIR="$SRC/mayhem/build-test"
mkdir -p "$FUZZ_DIR" "$TEST_DIR"

# libretro.c reports GIT_COMMIT_HASH as the core version string; the value is irrelevant here.
DEFS=(-DGIT_COMMIT_HASH='"mayhem"')
SRCS=(libretro shared localization)

# 1) The project's core API, sanitized, with DWARF 3. -O1: emulation under ASan at -O0 is far too slow to fuzz.
# Three UBSan checks are relaxed because upstream trips them on every ordinary ROM, including the seeds, which
# would make each run abort at once: shift-base (`0xff<<24` into a signed int, gba.h:1692, benign), alignment
# (a uint64_t* view of a byte buffer, gba.h:3776, fine on x86) and object-size (nds_rom_db_compare_func,
# nds.h:3043, casts bsearch's 4-byte key to the wider entry struct and only reads its first member).
# ASan and every other UBSan check stay on and halting.
RELAX=(-fno-sanitize=shift-base,alignment,object-size)
# The project code carries libFuzzer's coverage instrumentation (the link step only adds the engine).
COV=(-fsanitize=fuzzer-no-link)
pids=()
for s in "${SRCS[@]}"; do
  $CC $SANITIZER_FLAGS "${RELAX[@]}" "${COV[@]}" $DEBUG_FLAGS -O1 "${DEFS[@]}" -I"$SRC/src" -c "$SRC/src/$s.c" -o "$FUZZ_DIR/$s.o" &
  pids+=($!)
done
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$FUZZ_DIR/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_DIR/standalone_main.o"
for p in "${pids[@]}"; do wait "$p"; done
OBJS=("$FUZZ_DIR/libretro.o" "$FUZZ_DIR/shared.o" "$FUZZ_DIR/localization.o" "$FUZZ_DIR/lsan_off.o")

# 2) Harnesses: fuzzer binary + standalone reproducer. fuzz_<core>: ROM image fed to the matching core.
build_harness() {  # <name> <source> [extra cc args...]
  local name="$1" src="$2"; shift 2
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 "$@" $LIB_FUZZING_ENGINE "$src" "${OBJS[@]}" -lm -o "$SRC/$name"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 "$@" "$FUZZ_DIR/standalone_main.o" "$src" "${OBJS[@]}" -lm -o "$SRC/$name-standalone"
}
build_harness fuzz_gb  "$SRC/mayhem/fuzz_libretro.c" -DFUZZ_CORE='"Game Boy"'        -DFUZZ_EXT='".gb"'  -DFUZZ_MIN_SIZE=336   -DFUZZ_PAD_SIZE=32768 -DFUZZ_FRAMES=3
build_harness fuzz_gba "$SRC/mayhem/fuzz_libretro.c" -DFUZZ_CORE='"Game Boy Advance"' -DFUZZ_EXT='".gba"' -DFUZZ_MIN_SIZE=192   -DFUZZ_FRAMES=4
build_harness fuzz_nds "$SRC/mayhem/fuzz_libretro.c" -DFUZZ_CORE='"Nintendo DS"'     -DFUZZ_EXT='".nds"' -DFUZZ_MIN_SIZE=1024  -DFUZZ_FRAMES=1
build_harness fuzz_cheat "$SRC/mayhem/fuzz_cheat.c"

# 3) The functional oracle, built with the project's NORMAL flags (no sanitizers), run by mayhem/test.sh.
$CC -O1 $COVERAGE_FLAGS "${DEFS[@]}" -I"$SRC/src" \
  "$SRC/mayhem/oracle/oracle.c" "$SRC/src/shared.c" "$SRC/src/localization.c" -lm -o "$TEST_DIR/skyemu_oracle"
