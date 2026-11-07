#!/usr/bin/env bash
# package_release.sh - Build and package Fox Ranger recomp for release
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${ROOT_DIR}"

echo "=== Fox Ranger (1992) Release Packager ==="

# 1. Ensure icons exist
if [ ! -f "packaging/FoxRanger.icns" ] && [ "$(uname)" = "Darwin" ]; then
    echo "Generating application icons..."
    python3 -c "import sys; sys.path = [p for p in sys.path if not p.endswith('/tools')]; import tools.generate_icon as g; g.main()"
fi

# 2. Configure build
BUILD_DIR="${ROOT_DIR}/build"
echo "Configuring in ${BUILD_DIR}..."
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release

# 3. Build target
echo "Building Fox Ranger..."
cmake --build "${BUILD_DIR}" --config Release

# 4. Run CPack
echo "Packaging release archives with CPack..."
cd "${BUILD_DIR}"

if [ "$(uname)" = "Darwin" ]; then
    cpack -G ZIP
    echo "Generating SHA-256 checksums..."
    shasum -a 256 FoxRanger-*.zip > FoxRanger-checksums-sha256.txt || true
    echo "Release package created: ${BUILD_DIR}/FoxRanger-1.0.0-Darwin.zip"
elif [ "$(expr substr $(uname -s) 1 5)" = "Linux" ]; then
    cpack -G "TGZ;ZIP"
    sha256sum FoxRanger-*.tar.gz FoxRanger-*.zip > FoxRanger-checksums-sha256.txt || true
    echo "Release packages created in ${BUILD_DIR}"
fi

echo "=== Packaging Complete ==="
