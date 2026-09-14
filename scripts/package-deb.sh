#!/usr/bin/env bash
#
# Build a .deb from the current tree, without any of the release ceremony.
#
# For trying the packaging out -- `tools/release.sh` is what cuts a release, and
# it does the version bump, the test gate, the clean-system verification and the
# signing that this script deliberately does not.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build/package-deb}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" --target micronotes -j"$(nproc)"
cpack --config "${BUILD_DIR}/CPackConfig.cmake" -G DEB

find "${BUILD_DIR}" -maxdepth 1 -type f -name '*.deb' -print | sort
