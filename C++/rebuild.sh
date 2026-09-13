#!/usr/bin/env bash
# Erase the CARTON build directory and rebuild it from scratch.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-carton"
BUILD_TYPE="Release"
RUN_TESTS=1
RESET_SETTINGS=0

# QSettings identity from app_main.cpp. Preferences survive a rebuild, so
# clearing them is opt-in rather than part of "clean".
SETTINGS_DOMAIN="com.carton.CARTON"
SETTINGS_CACHE="${HOME}/Library/Caches/CARTON"

usage() {
    cat <<'USAGE'
Usage: ./rebuild.sh [options] [build-dir]

Deletes the build directory, reconfigures with CMake, and rebuilds everything
(app, vendored straw/igv-cpp, generated icon, smoke tests).

Options:
  --debug             Configure CMAKE_BUILD_TYPE=Debug instead of Release.
  --skip-tests        Build the tests but do not run ctest.
  --reset-settings    Also erase saved preferences (recent files, bookmarks,
                      display options) for a genuine first-run state.
  -h, --help          Show this message.

The build directory defaults to build-carton next to this script.
USAGE
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug) BUILD_TYPE="Debug" ;;
        --skip-tests) RUN_TESTS=0 ;;
        --reset-settings) RESET_SETTINGS=1 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "error: unknown option '$1' (try --help)" >&2; exit 1 ;;
        *) BUILD_DIR="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" ;;
    esac
    shift
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake was not found in PATH" >&2
    exit 1
fi

JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
if [[ ! ${JOBS} =~ ^[1-9][0-9]*$ ]]; then
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
fi
if [[ ! ${JOBS} =~ ^[1-9][0-9]*$ ]]; then
    JOBS=4
fi

# ── 1. Erase the old build ────────────────────────────────────────────────────
# The build directory is taken from the command line, so refuse to delete
# anything that is not recognisably a CMake build tree. A typo should not cost
# a source directory.
if [[ -e ${BUILD_DIR} ]]; then
    if [[ ! -f ${BUILD_DIR}/CMakeCache.txt ]]; then
        echo "error: ${BUILD_DIR} exists but has no CMakeCache.txt; refusing to delete it" >&2
        echo "       remove it by hand if it really is a stale build directory" >&2
        exit 1
    fi
    echo "→ Erasing ${BUILD_DIR}…"
    rm -rf "${BUILD_DIR}"
    echo "  ✓ Old build removed"
fi

# ── 2. Configure ──────────────────────────────────────────────────────────────
echo "→ Configuring (${BUILD_TYPE})…"
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
echo "  ✓ Configured"

# ── 3. Build ──────────────────────────────────────────────────────────────────
echo "→ Building with ${JOBS} jobs…"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
echo "  ✓ Built"

# ── 4. Test ───────────────────────────────────────────────────────────────────
if [[ ${RUN_TESTS} -eq 1 ]]; then
    echo "→ Running smoke tests…"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
    echo "  ✓ Tests passed"
fi

# ── 5. Optionally reset saved preferences ─────────────────────────────────────
if [[ ${RESET_SETTINGS} -eq 1 ]]; then
    echo "→ Erasing saved preferences…"
    # cfprefsd caches preference domains in memory and rewrites the plist on
    # exit, so deleting the file alone would silently restore the old values.
    defaults delete "${SETTINGS_DOMAIN}" >/dev/null 2>&1 || true
    rm -rf "${SETTINGS_CACHE}"
    echo "  ✓ ${SETTINGS_DOMAIN} preferences and ${SETTINGS_CACHE} removed"
fi

echo ""
echo "✓ Clean build ready: ${BUILD_DIR}/carton.app"
echo "  open \"${BUILD_DIR}/carton.app\""
