#!/usr/bin/env bash
#
# session-compare.sh -- run two builds of the app through the same real session,
# headlessly, and report what changed: the pixels, the counters, the scope
# timings and the frame trace.
#
# This exists because the perf harness cannot see the font path (see
# docs/performance.md, "the harness cannot see the font path"): every budget in
# tools/PerfMain.cpp measures against a fixed-advance stub, so the largest cost
# in the app -- glyph shaping -- and everything above the document layout are
# invisible to it. A real session IS the instrument for those, and this makes
# running one a command rather than a recipe.
#
# Usage:
#   tools/session-compare.sh <baseline-ref> [--library DIR] [--select TITLE]
#                            [--panes live,reading,split] [--rounds 3]
#
#   tools/session-compare.sh main
#   tools/session-compare.sh HEAD~3 --library ~/notes --select "Some Note"
#
# The baseline is built in a git worktree, Release, alongside the working tree's
# own Release build. Both are then run **alternated** -- baseline, working,
# baseline, working -- because the run-to-run spread on a real renderer is wide
# enough that two consecutive runs of the same build differ more than a real
# regression does. Interleaving is what makes the comparison mean anything.
#
# Two things come out of it:
#
#   * `cmp` of the screenshots. A layout or paint optimisation is only safe if
#     it cannot be observed, and this is the cheapest proof there is. Both runs
#     use the SAME library path, because the root folder's name is drawn in the
#     sidebar and two copies under different names differ legitimately.
#   * the counter delta. Counters are deterministic -- the same workload gives
#     byte-identical values every run -- so any difference is a real change in
#     what the code did, not noise. The timings are the noisy half and are
#     printed per round rather than averaged, so the spread is visible.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BASE_REF="${1:-main}"
shift || true

LIBRARY=""
SELECT=""
PANES="live,reading,split"
ROUNDS=3
SIZE="1600x1000"
PANELS="sidebar,right"
DISPLAY_NUM="${SESSION_COMPARE_DISPLAY:-:97}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --library) LIBRARY="$2"; shift 2 ;;
    --select)  SELECT="$2";  shift 2 ;;
    --panes)   PANES="$2";   shift 2 ;;
    --rounds)  ROUNDS="$2";  shift 2 ;;
    --size)    SIZE="$2";    shift 2 ;;
    --panels)  PANELS="$2";  shift 2 ;;
    *) echo "session-compare: unknown argument $1" >&2; exit 2 ;;
  esac
done

WORK="${SESSION_COMPARE_DIR:-$(mktemp -d)}"
mkdir -p "$WORK"
echo "session-compare: baseline=$BASE_REF work=$WORK"

command -v Xvfb >/dev/null || { echo "session-compare: Xvfb not installed" >&2; exit 2; }
pgrep -f "Xvfb $DISPLAY_NUM" >/dev/null || {
  Xvfb "$DISPLAY_NUM" -screen 0 "${SIZE/x/x}x24" >/dev/null 2>&1 &
  sleep 2
}

# --- the two builds ---------------------------------------------------------
# The baseline goes in a worktree so the working tree is never touched. The
# md4c submodule is copied rather than re-fetched: a worktree does not get one.
BASE_TREE="$WORK/baseline"
if [[ ! -d "$BASE_TREE" ]]; then
  git worktree add --detach "$BASE_TREE" "$BASE_REF" >/dev/null || exit 1
  rm -rf "$BASE_TREE/third_party/md4c"
  cp -r third_party/md4c "$BASE_TREE/third_party/md4c"
fi

build() {
  local src="$1" dir="$2"
  cmake -S "$src" -B "$dir" -DCMAKE_BUILD_TYPE=Release >/dev/null || return 1
  cmake --build "$dir" -j"$(nproc 2>/dev/null || echo 8)" --target micronotes >/dev/null || return 1
}
echo "session-compare: building baseline..."
build "$BASE_TREE" "$BASE_TREE/build-session" || exit 1
echo "session-compare: building working tree..."
build "$REPO_ROOT" "$REPO_ROOT/build-release" || exit 1

BASE_BIN="$BASE_TREE/build-session/bin/micronotes"
WORK_BIN="$REPO_ROOT/build-release/bin/micronotes"

# --- the library ------------------------------------------------------------
# Generated if none was named, so the script has something to open with no
# arguments at all. One note big enough that the walks show up.
if [[ -z "$LIBRARY" ]]; then
  LIBRARY="$WORK/lib"
  mkdir -p "$LIBRARY"
  if [[ ! -f "$LIBRARY/Session.md" ]]; then
    {
      printf -- '---\nid: session-note\ntitle: Session Note\n---\n\n'
      for i in $(seq 1 700); do
        printf '## Section %s\n\nA paragraph with **strong** and `code` and a [link](https://example.com) in it, long enough to wrap.\n\n- bullet %s\n- another bullet\n\n' "$i" "$i"
      done
    } > "$LIBRARY/Session.md"
  fi
  SELECT="${SELECT:-Session Note}"
fi

# --- one session ------------------------------------------------------------
# The index is dropped before every run so both builds do the same amount of
# work: a warm index and a cold one differ by a thousand file reads.
session() {
  local bin="$1" pane="$2" shot="$3" log="$4"
  rm -rf "$LIBRARY/.micronotes"
  local args=(--library "$LIBRARY" --size "$SIZE" --pane "$pane" --panels "$PANELS"
              --screenshot "$shot")
  [[ -n "$SELECT" ]] && args+=(--select "$SELECT")
  DISPLAY="$DISPLAY_NUM" MICROCORE_PERF_COUNTERS=1 MICROCORE_PERF_SUMMARY=1 \
    MICRONOTES_TRACE_FRAMES=1 "$bin" "${args[@]}" >"$log" 2>&1
}

status=0
IFS=',' read -r -a PANE_LIST <<< "$PANES"
for pane in "${PANE_LIST[@]}"; do
  echo
  echo "=== pane: $pane ==============================================="
  for round in $(seq 1 "$ROUNDS"); do
    session "$BASE_BIN" "$pane" "$WORK/base-$pane-$round.png" "$WORK/base-$pane-$round.log"
    session "$WORK_BIN" "$pane" "$WORK/work-$pane-$round.png" "$WORK/work-$pane-$round.log"
    printf 'round %s  ' "$round"
    for side in base work; do
      # shell.content is the frame's own cost; the frame trace is what the user
      # would have felt.
      printf '%s: %s  ' "$side" \
        "$(grep -E '^\[perf\].*shell\.content' "$WORK/$side-$pane-$round.log" \
             | head -1 | awk '{printf "self %sms", $2}')"
    done
    printf '\n'
  done

  if cmp -s "$WORK/base-$pane-1.png" "$WORK/work-$pane-1.png"; then
    echo "pixels: IDENTICAL"
  else
    echo "pixels: DIFFERENT -- $WORK/base-$pane-1.png vs $WORK/work-$pane-1.png"
    status=1
  fi

  # Counters are deterministic, so a diff of them is signal and not spread.
  counters() { grep -E '^[a-z_]+\.[a-z_]+ +[0-9]+$' "$1" | sort; }
  counters "$WORK/base-$pane-1.log" > "$WORK/base-$pane.counters"
  counters "$WORK/work-$pane-1.log" > "$WORK/work-$pane.counters"
  if diff -u "$WORK/base-$pane.counters" "$WORK/work-$pane.counters" > "$WORK/$pane.counters.diff"; then
    echo "counters: unchanged"
  else
    echo "counters:"
    sed -n '3,$p' "$WORK/$pane.counters.diff" | sed 's/^/  /'
  fi
done

echo
echo "session-compare: logs and screenshots under $WORK"
echo "session-compare: pixels $([[ $status -eq 0 ]] && echo unchanged || echo CHANGED)"
exit "$status"
