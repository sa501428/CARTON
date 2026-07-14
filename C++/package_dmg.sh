#!/usr/bin/env bash
# Build CARTON, generate .icns from logo.png, and produce a macOS DMG installer.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-"${SCRIPT_DIR}/build-carton"}"
VERSION="0.1.0"
LOGO="${SCRIPT_DIR}/logo.png"
ICNS_DST="${SCRIPT_DIR}/carton.icns"
DMG_OUTPUT="${BUILD_DIR}/CARTON-${VERSION}-macOS.dmg"

# ── 1. Generate carton.icns from logo.png ─────────────────────────────────────
echo "→ Generating app icon from logo.png…"
ICONSET="${BUILD_DIR}/carton.iconset"
mkdir -p "${ICONSET}"

make_icon() {
    local pts=$1 scale=${2:-1}
    local px=$(( pts * scale ))
    local suffix; [[ $scale -eq 2 ]] && suffix="@2x" || suffix=""
    local name="icon_${pts}x${pts}${suffix}.png"
    # Resize longest side to px (preserves aspect ratio), then pad to square
    sips -Z "${px}" "${LOGO}"              --out "${ICONSET}/${name}" >/dev/null
    sips -p "${px}" "${px}" "${ICONSET}/${name}" --out "${ICONSET}/${name}" >/dev/null
}

make_icon 16;  make_icon 16  2
make_icon 32;  make_icon 32  2
make_icon 128; make_icon 128 2
make_icon 256; make_icon 256 2
make_icon 512; make_icon 512 2

iconutil -c icns "${ICONSET}" -o "${ICNS_DST}"
echo "  ✓ ${ICNS_DST}"

# ── 2. Build ───────────────────────────────────────────────────────────────────
echo "→ Building…"
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target carton straw -j"$(sysctl -n hw.logicalcpu)"

# ── 3. Deploy Qt frameworks into .app ─────────────────────────────────────────
echo "→ Running macdeployqt…"
MACDEPLOYQT="$(command -v macdeployqt || echo /opt/homebrew/bin/macdeployqt)"
QT_LIB_DIR="$(qtpaths --query QT_INSTALL_LIBS)"
"${MACDEPLOYQT}" \
    "${BUILD_DIR}/carton.app" \
    "-qmldir=${SCRIPT_DIR}/qml" \
    "-libpath=${QT_LIB_DIR}"

# macdeployqt rewrites load commands (rpaths) after the linker's ad-hoc
# signature was embedded, which invalidates it; re-sign or launch fails
# with a "Code Signature Invalid" SIGKILL.
echo "→ Re-signing app bundle…"
codesign --force --deep --sign - "${BUILD_DIR}/carton.app"

# ── 4. Stage DMG contents ─────────────────────────────────────────────────────
echo "→ Staging DMG…"
STAGING="${BUILD_DIR}/dmg-staging"
TEMP_DMG="${BUILD_DIR}/CARTON-rw.dmg"
MOUNT_POINT=""
DMG_ATTACHED=0

cleanup() {
    if [[ ${DMG_ATTACHED} -eq 1 ]]; then
        if hdiutil detach "${MOUNT_POINT}" >/dev/null 2>&1; then
            DMG_ATTACHED=0
        fi
    fi
    if [[ ${DMG_ATTACHED} -eq 0 && -n ${MOUNT_POINT} ]]; then
        rm -rf "${MOUNT_POINT}"
    fi
}
trap cleanup EXIT

rm -rf "${STAGING}" "${TEMP_DMG}" "${DMG_OUTPUT}"
mkdir -p "${STAGING}"
MOUNT_POINT="$(mktemp -d "${BUILD_DIR}/dmg-mount.XXXXXX")"
cp -R "${BUILD_DIR}/carton.app" "${STAGING}/CARTON.app"
ln -s /Applications "${STAGING}/Applications"

# ── 5. Build writable DMG, position icons, convert to read-only ───────────────
echo "→ Creating DMG…"
VOLNAME="CARTON ${VERSION}"

hdiutil create \
    -ov \
    -srcfolder "${STAGING}" \
    -volname "${VOLNAME}" \
    -format UDRW \
    "${TEMP_DMG}" >/dev/null

hdiutil attach \
    -readwrite \
    -noverify \
    -noautoopen \
    -mountpoint "${MOUNT_POINT}" \
    "${TEMP_DMG}" >/dev/null
DMG_ATTACHED=1

# Set icon positions and view options via Finder AppleScript. Finder automation
# may be disabled (for example in CI), so this cosmetic step is optional.
if ! osascript - "${MOUNT_POINT}" <<'APPLESCRIPT'
on run argv
set mountPoint to item 1 of argv
tell application "Finder"
    tell folder (POSIX file mountPoint as alias)
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set bounds of container window to {200, 120, 760, 460}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 128
        set position of item "CARTON.app"    of container window to {160, 180}
        set position of item "Applications" of container window to {400, 180}
        close
        open
        update without registering applications
        delay 2
    end tell
end tell
end run
APPLESCRIPT
then
    echo "  ! Finder automation unavailable; using the default DMG layout." >&2
fi

# Copy icon to volume root so Finder can display it on the DMG itself
cp "${ICNS_DST}" "${MOUNT_POINT}/.VolumeIcon.icns"
SetFile -a C "${MOUNT_POINT}" 2>/dev/null || true

hdiutil detach "${MOUNT_POINT}" >/dev/null
DMG_ATTACHED=0
rmdir "${MOUNT_POINT}"
hdiutil convert "${TEMP_DMG}" -format UDZO -imagekey zlib-level=9 -o "${DMG_OUTPUT}" >/dev/null
rm -f "${TEMP_DMG}"

echo ""
echo "✓ Installer ready: ${DMG_OUTPUT}"
