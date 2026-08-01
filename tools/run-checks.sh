#!/usr/bin/env bash
#
# run-checks.sh -- build and run the test suite and the sanitizers, persisting
# all output to a deterministic file under /tmp (override with LOG_DIR) so it
# can be read back later WITHOUT rebuilding and rerunning everything.
#
# Usage:
#   tools/run-checks.sh tests   # plain build + ctest        -> /tmp/<app>-tests.log
#   tools/run-checks.sh asan    # AddressSanitizer + ctest   -> /tmp/<app>-asan.log
#   tools/run-checks.sh ubsan   # UndefinedBehavior + ctest  -> /tmp/<app>-ubsan.log
#   tools/run-checks.sh tsan    # ThreadSanitizer + ctest    -> /tmp/<app>-tsan.log
#   tools/run-checks.sh all     # tests, asan, ubsan, tsan in sequence
#
# The full console output (build + test) is tee'd to the log; the exit status is
# the real status of the underlying command (via PIPESTATUS), so callers still
# see pass/fail while the file keeps the detail.
#
# IMPORTANT for agents: after a run, READ the log instead of rerunning. Each log
# starts with a header naming the commit it came from. The sanitizers are slow
# -- run them once, at the end of a change, not per edit.
#
# Extra CMake arguments (for example a hand-pointed SQLite) can be passed once
# via CMAKE_EXTRA_ARGS and apply to every configure this script performs.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Derive the app name from CMakeLists so this script is identical in both repos.
APP="$(sed -n 's/^project(\([A-Za-z0-9_-]*\).*/\1/p' CMakeLists.txt | head -1)"
APP="${APP:-app}"

JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || echo 8)}"
LOG_DIR="${LOG_DIR:-/tmp}"
mkdir -p "$LOG_DIR"

read -r -a EXTRA_CMAKE_ARGS <<< "${CMAKE_EXTRA_ARGS:-}"

# Run a command, tee combined stdout+stderr to $1, return the command's real
# exit status (not tee's).
run_logged() {
  local log="$1"; shift
  {
    echo "=== run-checks.sh: $* ==="
    echo "=== date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ') ==="
    echo "=== commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown) ==="
    echo "=== branch: $(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown) ==="
    echo
    "$@"
  } 2>&1 | tee "$log"
  return "${PIPESTATUS[0]}"
}

check_tests() {
  local log="${LOG_DIR}/${APP}-tests.log"
  run_logged "$log" bash -c '
    set -e
    cmake -S . -B build "$@"
    cmake --build build -j'"$JOBS"'
    ctest --test-dir build --output-on-failure
  ' _ "${EXTRA_CMAKE_ARGS[@]}"
  local rc=$?
  echo "run-checks: tests finished (exit $rc); log at $log"
  return $rc
}

# $1 = asan | ubsan | tsan
check_sanitizer() {
  local san="$1"
  local build_dir="build-${san}"
  local log="${LOG_DIR}/${APP}-${san}.log"
  local flags

  case "$san" in
    asan)  flags="-fsanitize=address -fno-omit-frame-pointer -g -O1" ;;
    ubsan) flags="-fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1" ;;
    tsan)  flags="-fsanitize=thread -fno-omit-frame-pointer -g -O1" ;;
    *)     echo "run-checks: unknown sanitizer '$san'" >&2; return 2 ;;
  esac

  # Route the sanitizer runtime's own diagnostics into LOG_DIR too. The runtime
  # writes its report to log_path, NOT to stderr, so without this the tee'd log
  # records the failure but not the stack that explains it.
  export ASAN_OPTIONS="halt_on_error=1:log_path=${LOG_DIR}/${APP}-asan-rt"
  export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:log_path=${LOG_DIR}/${APP}-ubsan-rt"
  export TSAN_OPTIONS="halt_on_error=1:log_path=${LOG_DIR}/${APP}-tsan-rt"

  # TSAN aborts at startup ("unexpected memory mapping") when the kernel's ASLR
  # entropy exceeds its shadow-mapping assumptions. The usual advice is
  # `sudo sysctl vm.mmap_rnd_bits=28`, which needs root and changes the setting
  # for every process on the machine. `setarch -R` gets the same result for one
  # child process via personality(ADDR_NO_RANDOMIZE) and needs no privileges, so
  # prefer it and fall back to printing the sysctl advice.
  local test_prefix=""
  if [[ "$san" == "tsan" ]]; then
    if command -v setarch >/dev/null 2>&1 && setarch -R true >/dev/null 2>&1; then
      test_prefix="setarch -R "
      echo "run-checks: TSAN running under 'setarch -R' (per-process ASLR off; no sudo needed)." >&2
    else
      echo "run-checks: 'setarch -R' unavailable here; if TSAN aborts at startup run" >&2
      echo "            'sudo sysctl vm.mmap_rnd_bits=28'." >&2
    fi
  fi

  run_logged "$log" bash -c '
    set -e
    cmake -S . -B '"$build_dir"' \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="'"$flags"'" \
      -DCMAKE_C_FLAGS="'"$flags"'" \
      -DCMAKE_EXE_LINKER_FLAGS="'"$flags"'" "$@"
    cmake --build '"$build_dir"' -j'"$JOBS"'
    '"$test_prefix"'ctest --test-dir '"$build_dir"' --output-on-failure
  ' _ "${EXTRA_CMAKE_ARGS[@]}"
  local rc=$?

  # Fold every per-pid runtime report into the main log so one file holds the
  # whole failure, then clean the scratch files up.
  shopt -s nullglob
  local rt_files=("${LOG_DIR}"/"${APP}"-"${san}"-rt.*)
  if (( ${#rt_files[@]} )); then
    {
      echo
      echo "=== ${san} sanitizer runtime reports (${#rt_files[@]} file(s)) ==="
      cat "${rt_files[@]}"
    } >> "$log"
    rm -f "${rt_files[@]}"
  fi
  shopt -u nullglob

  echo "run-checks: ${san} finished (exit $rc); log at $log"
  return $rc
}

TARGET="${1:-tests}"
case "$TARGET" in
  tests) check_tests ;;
  asan|ubsan|tsan) check_sanitizer "$TARGET" ;;
  all)
    rc=0
    check_tests || rc=1
    for san in asan ubsan tsan; do
      check_sanitizer "$san" || rc=1
    done
    echo "run-checks: all finished (exit $rc)"
    exit $rc
    ;;
  *)
    echo "usage: run-checks.sh [tests|asan|ubsan|tsan|all]" >&2
    exit 2
    ;;
esac
