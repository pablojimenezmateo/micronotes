#!/usr/bin/env bash
#
# release.sh -- drive a full micronotes release from a single version argument.
#
# Implements docs/release-checklist.md end to end. LOCAL-ONLY by default: it
# bumps the version, builds, runs the test gate, packages the .deb, proves the
# package starts on a clean system, signs it, and then STOPS, printing the
# staged artifacts. Passing --publish additionally commits, tags, pushes, and
# creates the GitHub release.
#
# Usage:
#   tools/release.sh <version> [--publish] [--skip-tests]
#                    [--changelog-file <file>] [--notes <file>] [--yes]
#
# Examples:
#   tools/release.sh 0.7.0                 # local dry build + package + sign, no git
#   tools/release.sh 0.7.0 --publish       # the whole thing, including gh release
#   tools/release.sh 0.7.0 --publish --changelog-file notes.md
#                                          # inject a curated CHANGELOG body (no TODO draft)

set -euo pipefail
INVOKE_PWD="$PWD"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

PUBLISH=0; SKIP_TESTS=0; ASSUME_YES=0; NOTES_FILE=""; CHANGELOG_FILE=""
VERSION=""
KEY_FPR="0E32 39B7 1B0F 9598 B71A FB7B 6D33 9CCB FC51 5D70"

log()  { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }
info() { printf '   %s\n' "$*"; }
die()  { printf '\033[1;31mrelease: %s\033[0m\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --publish)    PUBLISH=1 ;;
    --skip-tests) SKIP_TESTS=1 ;;
    --yes|-y)     ASSUME_YES=1 ;;
    --notes)      NOTES_FILE="$2"; shift ;;
    --changelog-file) CHANGELOG_FILE="$2"; shift ;;
    -h|--help)    sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*)           die "unknown flag '$1'" ;;
    *)            [[ -z "$VERSION" ]] && VERSION="$1" || die "unexpected arg '$1'" ;;
  esac
  shift
done

VERSION="${VERSION#v}"
[[ -n "$VERSION" ]] || die "usage: tools/release.sh <version> [--publish]"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "version must be X.Y.Z (got '$VERSION')"

if [[ -n "$CHANGELOG_FILE" ]]; then
  [[ "$CHANGELOG_FILE" == /* ]] || CHANGELOG_FILE="$INVOKE_PWD/$CHANGELOG_FILE"
  [[ -f "$CHANGELOG_FILE" ]] || die "changelog file '$CHANGELOG_FILE' not found"
fi
if [[ -n "$NOTES_FILE" ]]; then
  [[ "$NOTES_FILE" == /* ]] || NOTES_FILE="$INVOKE_PWD/$NOTES_FILE"
  [[ -f "$NOTES_FILE" ]] || die "notes file '$NOTES_FILE' not found"
fi

OLD_VERSION="$(grep -oP 'project\(micronotes VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)"
[[ -n "$OLD_VERSION" ]] || die "could not read current version from CMakeLists.txt"
TAG="v$VERSION"
DEB="micronotes_${VERSION}_amd64.deb"
BUILD_DIR="build-release-package"
DATE="$(date +%F)"

log "micronotes release $OLD_VERSION -> $VERSION  ($([[ $PUBLISH == 1 ]] && echo PUBLISH || echo local-only))"

# --- guards ----------------------------------------------------------------
[[ "$VERSION" != "$OLD_VERSION" ]] || die "version $VERSION equals current; bump it"
if [[ $PUBLISH == 1 ]]; then
  [[ "$(git rev-parse --abbrev-ref HEAD)" == "main" ]] || die "--publish requires the 'main' branch"
  [[ -z "$(git status --porcelain)" ]] || die "--publish requires a clean working tree"
  git rev-parse "$TAG" >/dev/null 2>&1 && die "tag $TAG already exists"
  command -v gh >/dev/null || die "--publish needs the gh CLI"
fi

# Can the release key actually sign, right now?
#
# Being in the keyring is not the question -- the question is whether
# gpg-agent will hand over the secret key without a pinentry prompt this
# process cannot answer. Asked HERE, before the build and the test gate, because
# finding out at step 6 costs a full Release build and a test run and leaves the
# tree bumped half-way through a release. The probe is a real detached signature
# over a temporary file, because nothing weaker distinguishes "key present" from
# "key usable".
HAVE_KEY=0
if gpg --list-secret-keys "${KEY_FPR// /}" >/dev/null 2>&1; then
  _probe="$(mktemp)"; printf 'micronotes release signing probe\n' > "$_probe"
  if gpg --local-user "${KEY_FPR// /}" --detach-sign --armor --yes \
         --output "$_probe.asc" "$_probe" >/dev/null 2>&1; then
    HAVE_KEY=1
  fi
  rm -f "$_probe" "$_probe.asc"
  if [[ $HAVE_KEY == 0 ]]; then
    die "release key $KEY_FPR is in the keyring but will not sign -- gpg-agent
   needs the passphrase and cannot prompt from here. Unlock it once in your own
   terminal, then re-run:

     printf test | gpg --local-user ${KEY_FPR// /} --detach-sign --armor -o /dev/null -

   There is deliberately no flag to publish unsigned: every release carries a
   signature over the package and one over its checksum."
  fi
elif [[ $PUBLISH == 1 ]]; then
  die "release key $KEY_FPR is not in this keyring; --publish will not ship an unsigned release"
fi

confirm() {
  [[ $ASSUME_YES == 1 ]] && return 0
  read -r -p "   $1 [y/N] " a; [[ "$a" == [yY] ]]
}

# --- 1. bump version -------------------------------------------------------
# Rewrite the version by PATTERN, not by the old literal, so a surface that has
# already drifted (a README example frozen at an ancient tag, say) self-heals
# instead of being skipped by an s/OLD/NEW/ that no longer matches.
# tools/check-doc-versions.sh (step 3) then hard-asserts every public surface
# agrees with what was just baked in.
log "1/8  Bump version in CMakeLists.txt + README.md"
sed -i "s/project(micronotes VERSION ${OLD_VERSION}/project(micronotes VERSION ${VERSION}/" CMakeLists.txt
sed -i -E "s/Tagged \`v[0-9]+\.[0-9]+\.[0-9]+\`/Tagged \`v${VERSION}\`/g; \
           s/micronotes_[0-9]+\.[0-9]+\.[0-9]+_amd64\.deb/micronotes_${VERSION}_amd64.deb/g" README.md

# Read the version back with the SAME anchored pattern OLD_VERSION uses, and
# assert it. An unanchored `grep 'VERSION \K[0-9.]+' | head -1` would match
# `cmake_minimum_required(VERSION 3.28)` on line 1 and cheerfully report 3.28
# for every release -- confirming a value by re-reading it is only worth
# anything if the read can fail.
BAKED_CMAKE="$(grep -oP 'project\(micronotes VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)"
[[ "$BAKED_CMAKE" == "$VERSION" ]] \
  || die "CMakeLists.txt reports '$BAKED_CMAKE' after the bump, expected '$VERSION'"
info "CMakeLists: $BAKED_CMAKE"

# --- 2. changelog draft ----------------------------------------------------
log "2/8  Draft CHANGELOG.md section"
PREV_TAG="$(git describe --tags --abbrev=0 2>/dev/null || echo '')"
{
  echo "## [$VERSION] - $DATE"
  echo
  if [[ -n "$CHANGELOG_FILE" ]]; then
    # Curated section body supplied by --changelog-file (header/date stay ours,
    # so the version and the date cannot be curated into disagreeing with the
    # thing being built).
    cat "$CHANGELOG_FILE"
  else
    echo "<!-- TODO(release): summarize the cycle. Draft from commits since ${PREV_TAG:-the start}: -->"
    echo "### Changes"
    git log ${PREV_TAG:+$PREV_TAG..HEAD} --no-merges --pretty='- %s' | grep -vE '^- (release|chore): ' || true
  fi
  echo
} > "$REPO/.changelog-section.tmp"
# Insert the new section just before the most recent existing version block.
awk 'NR==FNR{sec=sec $0 ORS; next}
     !done && /^## \[/ { printf "%s", sec; done=1 }
     { print }' "$REPO/.changelog-section.tmp" CHANGELOG.md > CHANGELOG.md.new
mv CHANGELOG.md.new CHANGELOG.md
rm -f "$REPO/.changelog-section.tmp"
if [[ -n "$CHANGELOG_FILE" ]]; then
  info "inserted curated '## [$VERSION] - $DATE' from $CHANGELOG_FILE"
else
  info "added '## [$VERSION] - $DATE' (review & tighten the TODO before publishing)"
fi

# --- 3. verify doc/version consistency ------------------------------------
# Hard gate: every public surface (README status + install examples, newest
# CHANGELOG entry) must state the version just baked into CMakeLists.txt. Runs
# AFTER the changelog draft so the new entry is in place. This is what stops the
# stated version from drifting from the shipped one.
log "3/8  Verify doc version consistency"
bash tools/check-doc-versions.sh || die "docs still drift from $VERSION (see DRIFT lines above)"

# --- 4. build --------------------------------------------------------------
# Plain Release, deliberately WITHOUT -DCMAKE_INTERPROCEDURAL_OPTIMIZATION.
# micronotes measured LTO rather than assuming it, and it came out a mixed
# result rather than a win (docs/performance.md, "The tenth pass"); the hot
# per-token/per-block/per-frame units are defined in headers instead, so the
# cross-TU inlining LTO would buy is already had. Turning it on here would ship
# codegen no perf run has ever measured.
#
# A build tree of its own, not `build`: the default tree is the Debug one every
# other lane uses, and reconfiguring it to Release for a release would leave the
# next `cmake --build build` quietly producing an optimised tree nobody asked
# for.
log "4/8  Build (Release, no LTO -- see docs/performance.md)"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)"
BAKED="$("$BUILD_DIR/bin/micronotes" --version 2>/dev/null | grep -oP '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
[[ "$BAKED" == "$VERSION" ]] || die "built binary reports '$BAKED', expected '$VERSION'"
info "binary reports $BAKED"

# --- 5. test gate ----------------------------------------------------------
# Route through tools/run-checks.sh so the release gate leaves the same
# deterministic /tmp/micronotes-release.log the other lanes leave, instead of a
# bare ctest run. The `release` mode tests the already-built tree in $BUILD_DIR
# without reconfiguring it, so what is tested is what gets signed.
if [[ $SKIP_TESTS == 0 ]]; then
  log "5/8  Test gate (tools/run-checks.sh release)"
  MICRONOTES_RELEASE_BUILD_DIR="$BUILD_DIR" bash tools/run-checks.sh release \
    || die "the test gate failed -- refusing to package"
else
  log "5/8  Test gate SKIPPED (--skip-tests)"
fi

# --- 6. package + verify + checksum + sign --------------------------------
log "6/8  Package .deb, verify it starts, checksum + GPG sign"
( cd "$BUILD_DIR" && cpack -G DEB )
DEB_PATH="$(find "$BUILD_DIR" -maxdepth 1 -name "$DEB" -print -quit)"
[[ -n "$DEB_PATH" ]] || DEB_PATH="$(find "$BUILD_DIR" -maxdepth 1 -name 'micronotes_*_amd64.deb' -print -quit)"
[[ -n "$DEB_PATH" ]] || die "cpack did not produce a .deb"

# Install-and-launch the artifact BEFORE it is signed. Building, testing and
# signing a package says nothing about whether it starts on a machine that is
# not this one: micronotes links libSDL3.so.0, no distro packages SDL3, so
# dpkg-shlibdeps emits no dependency for it and the gap is invisible to every
# other check. microide shipped that exact package for twenty releases.
bash "$REPO/scripts/ci/verify-deb-runtime.sh" "$DEB_PATH" \
  || die "the built .deb does not start on a clean system -- refusing to sign it"

cp "$DEB_PATH" "$REPO/$DEB"
( cd "$REPO" && sha256sum "$DEB" > "$DEB.sha256" )
info "checksum: $(cut -d' ' -f1 "$REPO/$DEB.sha256")"
if [[ $HAVE_KEY == 1 ]]; then
  # The guard above already proved this key signs, so a failure here is a real
  # one and must stop the release rather than leave an unsigned artifact behind
  # under a `set -e` abort with nothing said.
  gpg --local-user "${KEY_FPR// /}" --detach-sign --armor --yes "$REPO/$DEB" \
    || die "signing $DEB failed"
  gpg --local-user "${KEY_FPR// /}" --detach-sign --armor --yes "$REPO/$DEB.sha256" \
    || die "signing $DEB.sha256 failed"
  # Round-trip what was just written, rather than trusting that it was.
  ( cd "$REPO" && gpg --verify "$DEB.asc" "$DEB" ) \
    || die "the signature just written does not verify against $DEB"
  # The public half ships with every release, so a user can verify without
  # having met the project before.
  gpg --armor --export "${KEY_FPR// /}" > "$REPO/micronotes-signing-key.asc"
  info "signed: $DEB.asc, $DEB.sha256.asc"
else
  info "WARNING: release key $KEY_FPR not in keyring -- skipping signatures"
fi

# --- 7. stop here unless publishing ---------------------------------------
ARTIFACTS=("$DEB" "$DEB.sha256" "$DEB.asc" "$DEB.sha256.asc" "micronotes-signing-key.asc")
if [[ $PUBLISH == 0 ]]; then
  log "7/8  LOCAL-ONLY -- staged, nothing pushed"
  info "Artifacts in $REPO:"
  for a in "${ARTIFACTS[@]}"; do [[ -e "$REPO/$a" ]] && printf '     %s\n' "$a"; done
  cat <<EOF

   Review the CHANGELOG section and the bumped docs, then either:
     - re-run with --publish to commit, tag, push, and create the GitHub release, or
     - finish by hand per docs/release-checklist.md.
   Verify the package locally:  gpg --verify $DEB.asc $DEB
EOF
  exit 0
fi

# --- 8. publish ------------------------------------------------------------
log "8/8  Publish: commit, tag, push, GitHub release"
# Final gate before anything leaves the machine, in case the CHANGELOG or the
# docs were hand-edited after step 3.
bash tools/check-doc-versions.sh || die "docs drift from $VERSION -- refusing to publish"
git -C "$REPO" diff --stat
confirm "Commit version + changelog, tag $TAG, push, and create the GitHub release?" \
  || die "aborted before publish (local changes are staged on disk)"

git add -A
git commit -m "release: micronotes $TAG"
git tag -s "$TAG" -m "micronotes $TAG"
git push origin main
git push origin "$TAG"

NOTES_ARG=()
[[ -n "$NOTES_FILE" ]] && NOTES_ARG=(--notes-file "$NOTES_FILE") || NOTES_ARG=(--generate-notes)
gh release create "$TAG" "${NOTES_ARG[@]}" --title "micronotes $TAG"
gh release upload "$TAG" \
  "$REPO/$DEB" "$REPO/$DEB.sha256" "$REPO/$DEB.asc" "$REPO/$DEB.sha256.asc" \
  "$REPO/micronotes-signing-key.asc"
gh release view "$TAG" --json assets --jq '.assets[].name'

log "Done -- $TAG published. Round-trip verify: gpg --verify $DEB.asc $DEB"
