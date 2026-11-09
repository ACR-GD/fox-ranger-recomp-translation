#!/usr/bin/env bash
set -euo pipefail

BUNDLE="${1:-build/FoxRanger.app}"
HINT="${2:-${SDL2_ROOT:-${SDL2_DIR:-}}}"

echo "==> Bundling macOS dependencies into ${BUNDLE}..."

if [ ! -d "${BUNDLE}" ]; then
  echo "Error: Bundle ${BUNDLE} not found!" >&2
  exit 1
fi

FRAMEWORKS_DIR="${BUNDLE}/Contents/Frameworks"
RESOURCES_DIR="${BUNDLE}/Contents/Resources"
mkdir -p "${FRAMEWORKS_DIR}"
mkdir -p "${RESOURCES_DIR}"

# 1. Look for SDL2.framework
FOUND_FRAMEWORK=""
if [ -d "${HINT}/SDL2.framework" ]; then
  FOUND_FRAMEWORK="${HINT}/SDL2.framework"
elif [ -d "/Library/Frameworks/SDL2.framework" ]; then
  FOUND_FRAMEWORK="/Library/Frameworks/SDL2.framework"
fi

if [ -n "${FOUND_FRAMEWORK}" ]; then
  echo "==> Found SDL2 framework at: ${FOUND_FRAMEWORK}"
  rm -rf "${FRAMEWORKS_DIR}/SDL2.framework"
  ditto "${FOUND_FRAMEWORK}" "${FRAMEWORKS_DIR}/SDL2.framework"
  codesign --force --sign - "${FRAMEWORKS_DIR}/SDL2.framework" 2>/dev/null || true
fi

# 2. Look for libSDL2-2.0.0.dylib / libSDL2.dylib
# Always ensure dylib is also present or linked if needed
DYLIB_CANDIDATE=""
SEARCH_PATHS=()
[ -n "${HINT}" ] && SEARCH_PATHS+=("${HINT}" "${HINT}/lib")
[ -n "${SDL2_ROOT:-}" ] && SEARCH_PATHS+=("${SDL2_ROOT}" "${SDL2_ROOT}/lib")
SEARCH_PATHS+=(
  "/opt/homebrew/lib"
  "/opt/homebrew/opt/sdl2/lib"
  "/usr/local/lib"
  "/usr/local/opt/sdl2/lib"
)

for p in "${SEARCH_PATHS[@]}"; do
  if [ -f "${p}/libSDL2-2.0.0.dylib" ]; then
    DYLIB_CANDIDATE="${p}/libSDL2-2.0.0.dylib"
    break
  elif [ -f "${p}/libSDL2.dylib" ]; then
    DYLIB_CANDIDATE="${p}/libSDL2.dylib"
    break
  fi
done

if [ -z "${DYLIB_CANDIDATE}" ]; then
  DYLIB_CANDIDATE=$(find /var/folders /opt /tmp -name "libSDL2-2.0.0.dylib" -type f 2>/dev/null | head -n 1 || true)
fi

if [ -n "${DYLIB_CANDIDATE}" ]; then
  echo "==> Found SDL2 dylib at: ${DYLIB_CANDIDATE}"
  cp -L "${DYLIB_CANDIDATE}" "${FRAMEWORKS_DIR}/libSDL2-2.0.0.dylib"
  chmod 755 "${FRAMEWORKS_DIR}/libSDL2-2.0.0.dylib"
  install_name_tool -id "@rpath/libSDL2-2.0.0.dylib" "${FRAMEWORKS_DIR}/libSDL2-2.0.0.dylib" 2>/dev/null || true
  codesign --force --sign - "${FRAMEWORKS_DIR}/libSDL2-2.0.0.dylib" 2>/dev/null || true
fi

# 3. Bundle built-in resources if present
if [ -d "enhanced_music" ] && [ ! -d "${RESOURCES_DIR}/enhanced_music" ]; then
  echo "==> Bundling enhanced_music into Resources..."
  cp -R "enhanced_music" "${RESOURCES_DIR}/"
fi
if [ -d "translations" ] && [ ! -d "${RESOURCES_DIR}/translations" ]; then
  echo "==> Bundling translations into Resources..."
  cp -R "translations" "${RESOURCES_DIR}/"
fi
if [ -f "packaging/foxranger.ini.default" ] && [ ! -f "${RESOURCES_DIR}/foxranger.ini" ]; then
  echo "==> Bundling default foxranger.ini into Resources..."
  cp "packaging/foxranger.ini.default" "${RESOURCES_DIR}/foxranger.ini"
fi

# 4. Codesign bundle and inner binaries
echo "==> Codesigning bundle and frameworks (ad-hoc)..."
if [ -f "${BUNDLE}/Contents/MacOS/FoxRanger" ]; then
  codesign --force --sign - "${BUNDLE}/Contents/MacOS/FoxRanger"
fi

codesign --force --deep --sign - "${BUNDLE}"
codesign --verify --deep --strict --verbose=1 "${BUNDLE}"
echo "==> Bundle successfully created and signed: ${BUNDLE}"
