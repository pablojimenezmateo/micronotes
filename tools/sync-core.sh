#!/usr/bin/env bash
#
# src/core/ is vendored: micronotes and microagenda each hold a byte-identical
# copy so both build standalone with no submodule or monorepo ceremony. The cost
# of that choice is silent drift -- a fix applied to one copy and forgotten in
# the other. This script is what makes the drift loud.
#
#   sync-core.sh --check                 verify src/core matches CORE.sha256
#   sync-core.sh --write-manifest        regenerate CORE.sha256 from src/core
#   sync-core.sh --push <peer-repo>      copy src/core to the peer, remanifest both
#
# micronotes is canonical. Make core changes there, then --push. The --check
# mode runs as a ctest test in both repos, so editing a vendored copy without
# syncing fails the build rather than diverging quietly.
#
# Everything under src/core must be app-agnostic. The single permitted
# difference is src/core/AppIdentity.h, which reads the MICROCORE_APP_NAME macro
# the host CMakeLists defines -- the file itself is identical in both repos.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_DIR="$REPO_ROOT/src/core"
MANIFEST="$CORE_DIR/CORE.sha256"

if [[ ! -d "$CORE_DIR" ]]; then
  echo "sync-core: no src/core in $REPO_ROOT" >&2
  exit 2
fi

# Hash every tracked source under src/core, excluding the manifest itself.
# Paths are relative to src/core and sorted so the manifest is reproducible
# regardless of filesystem order.
compute_manifest() {
  local dir="$1"
  ( cd "$dir" && find . -type f ! -name 'CORE.sha256' -printf '%P\n' \
      | LC_ALL=C sort \
      | xargs -r sha256sum )
}

write_manifest() {
  local dir="$1"
  compute_manifest "$dir" > "$dir/CORE.sha256"
}

check_manifest() {
  local dir="$1"
  local label="$2"
  if [[ ! -f "$dir/CORE.sha256" ]]; then
    echo "sync-core: $label has no src/core/CORE.sha256; run tools/sync-core.sh --write-manifest" >&2
    return 1
  fi
  local actual expected
  actual="$(compute_manifest "$dir")"
  expected="$(cat "$dir/CORE.sha256")"
  if [[ "$actual" != "$expected" ]]; then
    echo "sync-core: $label src/core has drifted from its manifest." >&2
    echo >&2
    diff <(printf '%s\n' "$expected") <(printf '%s\n' "$actual") \
      | sed 's/^/  /' >&2 || true
    echo >&2
    echo "  The shared core is vendored into both micronotes and microagenda." >&2
    echo "  Make the change in micronotes (canonical), then:" >&2
    echo "    micronotes/tools/sync-core.sh --push ../microagenda" >&2
    return 1
  fi
  echo "sync-core: $label src/core matches its manifest ($(wc -l < "$dir/CORE.sha256") files)"
  return 0
}

MODE="${1:---check}"

case "$MODE" in
  --check)
    check_manifest "$CORE_DIR" "$(basename "$REPO_ROOT")"
    ;;

  --write-manifest)
    write_manifest "$CORE_DIR"
    echo "sync-core: wrote $MANIFEST ($(wc -l < "$MANIFEST") files)"
    ;;

  --push)
    PEER="${2:-}"
    if [[ -z "$PEER" ]]; then
      echo "sync-core: --push needs a peer repository path" >&2
      exit 2
    fi
    PEER_ROOT="$(cd "$PEER" && pwd)"
    PEER_CORE="$PEER_ROOT/src/core"
    if [[ ! -d "$PEER_ROOT/src" ]]; then
      echo "sync-core: $PEER_ROOT does not look like a repo (no src/)" >&2
      exit 2
    fi
    mkdir -p "$PEER_CORE"
    # --delete so a file removed from the canonical core is removed downstream
    # too; without it a deleted file lingers and still compiles in the peer.
    rsync -a --delete --exclude 'CORE.sha256' "$CORE_DIR/" "$PEER_CORE/"
    write_manifest "$CORE_DIR"
    cp "$MANIFEST" "$PEER_CORE/CORE.sha256"
    echo "sync-core: pushed $(wc -l < "$MANIFEST") files to $PEER_CORE"
    echo "sync-core: commit both repositories"
    ;;

  *)
    echo "usage: sync-core.sh [--check | --write-manifest | --push <peer-repo>]" >&2
    exit 2
    ;;
esac
