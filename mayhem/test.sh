#!/usr/bin/env bash
#
# mayhem/test.sh — RUNS the functional oracle that mayhem/build.sh built (mayhem/oracle/oracle.c).
# SkyEmu ships no tests of its own; the oracle boots hand-assembled GB / GBA / NDS programs on the
# emulated CPUs and checks the value they compute in emulated RAM, plus the cheat-code parser.
#
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

ORACLE="$SRC/mayhem/build-test/skyemu_oracle"
EXPECTED=5   # PASS/FAIL lines the oracle prints; fewer means it did not really run
[ -x "$ORACLE" ] || { echo "missing $ORACLE (mayhem/build.sh builds it)" >&2; emit_ctrf "skyemu-oracle" 0 1 0; exit 1; }

out="$("$ORACLE" "$SRC/mayhem/fuzz_gb/testsuite/oracle.gb" "$SRC/mayhem/fuzz_gba/testsuite/oracle.gba" "$SRC/mayhem/fuzz_nds/testsuite/oracle.nds")"
rc=$?
echo "$out"
passed=$(grep -c '^PASS ' <<<"$out")
failed=$(grep -c '^FAIL ' <<<"$out")
[ "$rc" -eq 0 ] || failed=$((failed + 1))
missing=$((EXPECTED - passed - failed))
[ "$missing" -le 0 ] || failed=$((failed + missing))

emit_ctrf "skyemu-oracle" "$passed" "$failed" 0
