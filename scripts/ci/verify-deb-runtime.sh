#!/usr/bin/env bash
#
# Prove that a built .deb actually starts on a machine that is not this one.
#
# micronotes links libSDL3.so.0 and libSDL3_ttf.so.0, and no Debian-family
# distro packages SDL3 yet -- so SDL3 is a from-source install under /usr/local
# on every machine that can build this project. dpkg-shlibdeps can only emit a
# `Depends:` for a library an installed .deb owns, so it maps those two to
# nothing and says nothing about it. Without the bundling block in CMakeLists.txt
# the result is a package that `apt install ./micronotes.deb` accepts -- every
# listed dependency really is satisfied -- and whose binary then dies in the
# loader on any machine but the one that built it. microide shipped exactly that
# package for twenty releases, because building, testing and signing an artifact
# says nothing about whether it starts.
#
# This script closes that hole. It runs two independent checks:
#
#   1. STATIC   every NEEDED soname of the packaged binary must be satisfiable
#               either from inside the package (via its RUNPATH) or from a
#               library a stock system ships. No unresolved soname may remain.
#   2. DYNAMIC  the packaged binary is executed with the ld.so cache inhibited
#               and the library path restricted to stock system directories,
#               which is what a clean install actually looks like. It must print
#               its version and exit 0.
#
# The dynamic check is the one that catches the bug. The static one is kept
# because it names the offending soname instead of just reporting a loader
# failure.
#
# Usage: scripts/ci/verify-deb-runtime.sh <path-to-deb>

set -euo pipefail

DEB="${1:-}"
if [[ -z "$DEB" || ! -f "$DEB" ]]; then
  echo "usage: $0 <path-to-deb>" >&2
  exit 2
fi
DEB="$(readlink -f "$DEB")"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== verifying $(basename "$DEB")"
dpkg-deb -x "$DEB" "$WORK/root"

BIN="$WORK/root/usr/bin/micronotes"
[[ -x "$BIN" ]] || { echo "FAIL: package contains no executable at usr/bin/micronotes" >&2; exit 1; }

# ---------------------------------------------------------------- static pass
# The directories a stock Debian-family system has. /usr/local is deliberately
# excluded: it is exactly where this machine's hand-built SDL3 lives, and
# counting it is what would make the bug invisible.
SYSTEM_LIB_DIRS=(/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib64)

mapfile -t needed < <(readelf -d "$BIN" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
[[ ${#needed[@]} -gt 0 ]] || { echo "FAIL: readelf reported no NEEDED entries; wrong binary?" >&2; exit 1; }

runpath="$(readelf -d "$BIN" | sed -n 's/.*(RUNPATH).*\[\(.*\)\]/\1/p')"
[[ -n "$runpath" ]] || runpath="$(readelf -d "$BIN" | sed -n 's/.*(RPATH).*\[\(.*\)\]/\1/p')"

# Expand $ORIGIN against the binary's directory inside the extracted package.
bundled_dirs=()
if [[ -n "$runpath" ]]; then
  IFS=':' read -ra rp_entries <<< "$runpath"
  for entry in "${rp_entries[@]}"; do
    expanded="${entry//\$ORIGIN/$(dirname "$BIN")}"
    expanded="${expanded//\$\{ORIGIN\}/$(dirname "$BIN")}"
    bundled_dirs+=("$expanded")
  done
fi

missing=()
for soname in "${needed[@]}"; do
  found=""
  for dir in "${bundled_dirs[@]}" "${SYSTEM_LIB_DIRS[@]}"; do
    if [[ -e "$dir/$soname" ]]; then found="$dir/$soname"; break; fi
  done
  if [[ -z "$found" ]]; then
    missing+=("$soname")
  else
    case "$found" in
      "$WORK"/*) echo "   bundled  $soname" ;;
      *)         echo "   system   $soname" ;;
    esac
  fi
done

if [[ ${#missing[@]} -gt 0 ]]; then
  echo >&2
  echo "FAIL: the package needs sonames that neither it nor a stock system provides:" >&2
  for soname in "${missing[@]}"; do echo "  - $soname" >&2; done
  echo >&2
  echo "Either bundle them (see the runtime-library bundling block in CMakeLists.txt)" >&2
  echo "or add a Depends: entry for the package that ships them." >&2
  exit 1
fi

# --------------------------------------------------------------- dynamic pass
# --inhibit-cache stops the loader from consulting /etc/ld.so.cache, which on
# this machine happily points at /usr/local. Combined with an explicit
# --library-path this is a faithful stand-in for a clean install, and it needs
# neither a container nor root.
LOADER=/lib64/ld-linux-x86-64.so.2
[[ -x "$LOADER" ]] || { echo "SKIP: no $LOADER; dynamic check not run" >&2; exit 0; }

lib_path="$(IFS=:; echo "${SYSTEM_LIB_DIRS[*]}")"
for dir in "${bundled_dirs[@]}"; do
  lib_path="$dir:$lib_path"
done

echo "== launching the packaged binary with a clean-machine library path"
set +e
out="$(SDL_VIDEODRIVER=dummy "$LOADER" --inhibit-cache --library-path "$lib_path" \
        "$BIN" --version 2>&1)"
rc=$?
set -e

if [[ $rc -ne 0 ]]; then
  echo >&2
  echo "FAIL: the packaged binary does not start on a clean system (exit $rc):" >&2
  echo "  $out" >&2
  exit 1
fi

echo "   $out"
echo "OK: package starts on a clean system"
