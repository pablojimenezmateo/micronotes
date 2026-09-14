#!/usr/bin/env bash
#
# Install the locally built .deb, building one first if none is there.
#
# Checks that the package starts on a clean system before handing it to apt --
# the whole point of the packaging work is that the artifact runs somewhere
# other than the machine that built it, and finding out otherwise after
# installing it is the slow way.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build/package-deb}"

if ! compgen -G "${BUILD_DIR}/*.deb" > /dev/null; then
  "${REPO_ROOT}/scripts/package-deb.sh"
fi

package_path="$(find "${BUILD_DIR}" -maxdepth 1 -type f -name '*.deb' | sort | tail -n 1)"
bash "${REPO_ROOT}/scripts/ci/verify-deb-runtime.sh" "${package_path}"
sudo apt install -y "${package_path}"
