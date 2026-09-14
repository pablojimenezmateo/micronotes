#!/usr/bin/env bash
#
# check-doc-versions.sh -- assert every public-facing document states the same
# version as CMakeLists.txt (the single source of truth). Fails loudly, listing
# every drift, so what the project SAYS it is cannot fall out of step with what
# it IS.
#
# Canonical version:  project(micronotes VERSION X.Y.Z ...) in CMakeLists.txt.
# All of these must agree with it:
#   - README.md      every  Tagged `vX.Y.Z`  and  micronotes_X.Y.Z_amd64.deb(.sha256)
#   - CHANGELOG.md   the NEWEST  ## [X.Y.Z]  entry only (history is left alone)
#
# Internal docs (docs/tech-debt.md, docs/performance.md, ...) intentionally lag
# the released version and are deliberately NOT checked.
#
# Exit status:  0 = consistent, 1 = drift found, 2 = cannot read canonical version.
#
# Wired into tools/release.sh (a hard gate, run after the bump AND again before
# publish) and tools/run-checks.sh (so the ordinary `tests` lane catches drift in
# the local loop rather than at the moment of release).

set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

CANON="$(grep -oP 'project\(micronotes VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)"
if [[ -z "$CANON" ]]; then
  printf 'check-doc-versions: could not read version from CMakeLists.txt\n' >&2
  exit 2
fi

fail=0
drift() { printf '  DRIFT  %s\n' "$*" >&2; fail=1; }

# expect_all <file> <label> <pcre-with-\K> : every match in <file> must equal $CANON.
#
# Every match, not the first: a README that names the current version in its
# status line and a stale one in its verify example is exactly the drift this
# exists to catch, and checking only the first match would pass it.
expect_all() {
  local file="$1" label="$2" pat="$3" v
  [[ -f "$file" ]] || { drift "$file missing (cannot check $label)"; return; }
  while IFS= read -r v; do
    [[ "$v" == "$CANON" ]] || drift "$file: $label 'v$v' != $CANON"
  done < <(grep -oP "$pat" "$file")
}

# README: "Tagged `vX.Y.Z`" status lines.
expect_all README.md "Tagged" 'Tagged `v\K[0-9]+\.[0-9]+\.[0-9]+(?=`)'
# README: install and verify examples -- micronotes_X.Y.Z_amd64.deb and siblings.
expect_all README.md "deb example" 'micronotes_\K[0-9]+\.[0-9]+\.[0-9]+(?=_amd64\.deb)'

# CHANGELOG.md: only the NEWEST "## [X.Y.Z]" header. The entries below it are
# history and legitimately name older versions. A trailing date or "Unreleased"
# is ignored; the [X.Y.Z] number alone is compared.
changelog_top="$(grep -oP '^## \[\K[0-9]+\.[0-9]+\.[0-9]+' CHANGELOG.md | head -1)"
if [[ -z "$changelog_top" ]]; then
  drift "CHANGELOG.md: no '## [X.Y.Z]' entry found"
elif [[ "$changelog_top" != "$CANON" ]]; then
  drift "CHANGELOG.md: newest entry [$changelog_top] != $CANON"
fi

if (( fail )); then
  printf 'check-doc-versions: FAILED -- canonical version is %s (CMakeLists.txt)\n' "$CANON" >&2
  exit 1
fi
printf 'check-doc-versions: OK -- all public docs state %s\n' "$CANON"
