#!/usr/bin/env bash
# Build CARTON, generate .icns from logo.png, and produce a macOS DMG installer.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-"${SCRIPT_DIR}/build-carton"}"
VERSION="${CARTON_VERSION:-0.1.0}"
LOGO="${SCRIPT_DIR}/logo.png"
ICNS_DST="${SCRIPT_DIR}/carton.icns"
DMG_OUTPUT="${BUILD_DIR}/CARTON-${VERSION}-macOS.dmg"
CONFIGURE_LOG="${BUILD_DIR}/configure.log"
DEPLOY_LOG="${BUILD_DIR}/macdeployqt.log"
SMOKE_LOG="${BUILD_DIR}/package-smoke.log"
BUILD_APP_BUNDLE="${BUILD_DIR}/carton.app"
PACKAGE_DIR="${BUILD_DIR}/package"
APP_BUNDLE="${PACKAGE_DIR}/CARTON.app"

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: required tool '$1' was not found in PATH" >&2
        exit 1
    fi
}

for tool in cmake sips iconutil codesign hdiutil; do
    require_tool "${tool}"
done

MACDEPLOYQT="$(command -v macdeployqt || true)"
QT_PATHS="$(command -v qtpaths6 || command -v qtpaths || true)"
if [[ -z ${MACDEPLOYQT} || -z ${QT_PATHS} ]]; then
    echo "error: macdeployqt and qtpaths/qtpaths6 are required (install Qt 6 first)" >&2
    exit 1
fi

JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
if [[ ! ${JOBS} =~ ^[1-9][0-9]*$ ]]; then
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
fi
if [[ ! ${JOBS} =~ ^[1-9][0-9]*$ ]]; then
    JOBS=4
fi

# ── 1. Generate carton.icns from logo.png ─────────────────────────────────────
if [[ -f ${ICNS_DST} && ${ICNS_DST} -nt ${LOGO} && ${CARTON_REGENERATE_ICON:-0} != 1 ]]; then
    echo "→ Reusing current app icon (${ICNS_DST})"
else
    echo "→ Generating app icon from logo.png…"
    ICONSET="${BUILD_DIR}/carton.iconset"
    rm -rf "${ICONSET}"
    mkdir -p "${ICONSET}"

    make_icon() {
        local pts=$1 scale=${2:-1}
        local px=$(( pts * scale ))
        local suffix; [[ $scale -eq 2 ]] && suffix="@2x" || suffix=""
        local name="icon_${pts}x${pts}${suffix}.png"
        # Resize longest side to px (preserves aspect ratio), then pad to square
        sips -Z "${px}" "${LOGO}" --out "${ICONSET}/${name}" >/dev/null
        sips -p "${px}" "${px}" "${ICONSET}/${name}" --out "${ICONSET}/${name}" >/dev/null
    }

    make_icon 16;  make_icon 16  2
    make_icon 32;  make_icon 32  2
    make_icon 128; make_icon 128 2
    make_icon 256; make_icon 256 2
    make_icon 512; make_icon 512 2

    iconutil -c icns "${ICONSET}" -o "${ICNS_DST}"
    echo "  ✓ ${ICNS_DST}"
fi

# ── 2. Build ───────────────────────────────────────────────────────────────────
echo "→ Building…"
if ! cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF >"${CONFIGURE_LOG}" 2>&1; then
    echo "error: CMake configuration failed; complete output follows:" >&2
    sed -n '1,240p' "${CONFIGURE_LOG}" >&2
    exit 1
fi
echo "  ✓ Configured (details: ${CONFIGURE_LOG})"
cmake --build "${BUILD_DIR}" --target carton --parallel "${JOBS}"

# Deploy a staging copy. Never let macdeployqt mutate the development bundle:
# a later incremental link would replace only its executable, producing a mixed
# Homebrew/bundled Qt process that aborts while loading the platform plugin.
rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}"
cp -R "${BUILD_APP_BUNDLE}" "${APP_BUNDLE}"

# ── 3. Deploy linked Qt runtime dependencies ──────────────────────────────────
echo "→ Deploying Qt runtime…"
QT_LIB_DIR="$("${QT_PATHS}" --query QT_INSTALL_LIBS)"
QT_PLUGIN_DIR="$("${QT_PATHS}" --query QT_INSTALL_PLUGINS)"
QT_QML_DIR="$("${QT_PATHS}" --query QT_INSTALL_QML)"
QML_DEST="${APP_BUNDLE}/Contents/Resources/qml"

copy_qt_plugin() {
    local relative=$1
    local source="${QT_PLUGIN_DIR}/${relative}"
    if [[ ! -f ${source} ]]; then
        echo "error: required Qt plugin is missing: ${source}" >&2
        exit 1
    fi
    mkdir -p "${APP_BUNDLE}/Contents/PlugIns/$(dirname "${relative}")"
    cp -L "${source}" "${APP_BUNDLE}/Contents/PlugIns/${relative}"
}

# Copy only files belonging to one QML module, without recursively copying its
# sibling modules. Homebrew's Qt tree contains many optional products beneath
# QtQuick; handing that whole tree to macdeployqt is what pulled in QtPdf,
# QtVirtualKeyboard, Qt3D, and their unresolved optional dependencies.
copy_qml_module_files() {
    local relative=$1
    local source="${QT_QML_DIR}/${relative}"
    if [[ ! -d ${source} ]]; then
        echo "error: required Qt QML module is missing: ${source}" >&2
        exit 1
    fi
    mkdir -p "${QML_DEST}/${relative}"
    find -L "${source}" -mindepth 1 -maxdepth 1 ! -type d \
        -exec cp -L {} "${QML_DEST}/${relative}/" \;
}

# Native macOS display, headless smoke testing, SVG icons, and native TLS.
copy_qt_plugin platforms/libqcocoa.dylib
copy_qt_plugin platforms/libqoffscreen.dylib
copy_qt_plugin imageformats/libqsvg.dylib
copy_qt_plugin tls/libqsecuretransportbackend.dylib

for module in \
    QtQuick QtQml QML QtQml/Models QtQml/WorkerScript \
    QtQuick/Controls QtQuick/Templates QtQuick/Controls/impl QtQuick/Window \
    QtQuick/Effects QtQuick/Layouts QtQuick/Shapes Qt/labs/qmlmodels \
    QtQuick/Dialogs Qt/labs/folderlistmodel; do
    copy_qml_module_files "${module}"
done

# CARTON explicitly selects the Basic controls style. Dialog implementations
# also contain a required qml/ subtree, so copy these two module trees intact.
cp -RL "${QT_QML_DIR}/QtQuick/Controls/Basic" "${QML_DEST}/QtQuick/Controls/"
cp -RL "${QT_QML_DIR}/QtQuick/Dialogs/quickimpl" "${QML_DEST}/QtQuick/Dialogs/"

# Tell macdeployqt about every copied plugin in a single pass. -no-plugins stops
# it from adding unrelated optional plugins; -executable still deploys and
# rewrites the dependencies of the curated plugins above.
DEPLOY_ARGS=()
while IFS= read -r binary; do
    DEPLOY_ARGS+=("-executable=${binary}")
done < <(find "${APP_BUNDLE}/Contents/PlugIns" "${QML_DEST}" \
    -type f -name '*.dylib' -print)

if ! "${MACDEPLOYQT}" "${APP_BUNDLE}" -no-plugins \
        "-libpath=${QT_LIB_DIR}" "${DEPLOY_ARGS[@]}" \
        -verbose=1 >"${DEPLOY_LOG}" 2>&1; then
    echo "error: Qt deployment failed; complete output follows:" >&2
    sed -n '1,260p' "${DEPLOY_LOG}" >&2
    exit 1
fi
if grep -q '^ERROR:' "${DEPLOY_LOG}"; then
    echo "error: Qt deployment reported unresolved dependencies:" >&2
    grep -n '^ERROR:' "${DEPLOY_LOG}" >&2
    exit 1
fi
if [[ ! -f ${APP_BUNDLE}/Contents/Frameworks/QtSvg.framework/Versions/A/QtSvg ]]; then
    echo "error: SVG support was not deployed" >&2
    exit 1
fi
if [[ ! -x ${APP_BUNDLE}/Contents/MacOS/carton ]]; then
    echo "error: deployed app bundle is missing its CARTON executable" >&2
    exit 1
fi
echo "  ✓ Qt runtime deployed (details: ${DEPLOY_LOG})"

# macdeployqt rewrites load commands (rpaths) after the linker's ad-hoc
# signature was embedded, which invalidates it; re-sign or launch fails
# with a "Code Signature Invalid" SIGKILL.
echo "→ Re-signing app bundle…"
# QML plugins live below Resources rather than PlugIns, so codesign --deep does
# not reliably discover them. macdeployqt modifies their load commands and
# invalidates their original signatures; sign every Mach-O file explicitly
# before sealing the outer app bundle.
while IFS= read -r candidate; do
    if file "${candidate}" | grep -q 'Mach-O'; then
        codesign --force --sign - "${candidate}" >/dev/null 2>&1
    fi
done < <(find "${APP_BUNDLE}/Contents/Frameworks" \
    "${APP_BUNDLE}/Contents/PlugIns" "${QML_DEST}" -type f -print)
codesign --force --sign - "${APP_BUNDLE}"
codesign --verify --deep --strict "${APP_BUNDLE}"
echo "  ✓ App bundle signature verified"

echo "→ Smoke-testing packaged app…"
env -i PATH=/usr/bin:/bin:/usr/sbin:/sbin HOME="${BUILD_DIR}/smoke-home" \
    QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=null \
    "${APP_BUNDLE}/Contents/MacOS/carton" >"${SMOKE_LOG}" 2>&1 &
SMOKE_PID=$!
sleep 2
if ! kill -0 "${SMOKE_PID}" 2>/dev/null; then
    wait "${SMOKE_PID}" || true
    echo "error: packaged app failed its headless startup test:" >&2
    sed -n '1,200p' "${SMOKE_LOG}" >&2
    exit 1
fi
kill "${SMOKE_PID}"
wait "${SMOKE_PID}" 2>/dev/null || true
echo "  ✓ Packaged app initialized successfully"

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
cp -R "${APP_BUNDLE}" "${STAGING}/CARTON.app"
ln -s /Applications "${STAGING}/Applications"

# ── 5. Build DMG ──────────────────────────────────────────────────────────────
echo "→ Creating DMG…"
VOLNAME="CARTON ${VERSION}"

# Finder automation is opt-in because it opens Finder, requires Automation
# permission, and is unsuitable for CI/restricted shells. The default path
# creates the same installable DMG with the standard Finder layout.
if [[ ${CARTON_DMG_FINDER_LAYOUT:-0} == 1 ]]; then
    require_tool osascript
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
        echo "  ! Finder automation unavailable; continuing with the default layout." >&2
    fi

    cp "${ICNS_DST}" "${MOUNT_POINT}/.VolumeIcon.icns"
    if command -v SetFile >/dev/null 2>&1; then
        SetFile -a C "${MOUNT_POINT}" 2>/dev/null || true
    fi

    hdiutil detach "${MOUNT_POINT}" >/dev/null
    DMG_ATTACHED=0
    rmdir "${MOUNT_POINT}"
    MOUNT_POINT=""
    hdiutil convert "${TEMP_DMG}" -format UDZO -imagekey zlib-level=9 -o "${DMG_OUTPUT}" >/dev/null
    rm -f "${TEMP_DMG}"
else
    rmdir "${MOUNT_POINT}"
    MOUNT_POINT=""
    hdiutil create \
        -ov \
        -srcfolder "${STAGING}" \
        -volname "${VOLNAME}" \
        -format UDZO \
        -imagekey zlib-level=9 \
        "${DMG_OUTPUT}" >/dev/null
fi

hdiutil verify "${DMG_OUTPUT}" >/dev/null
echo "  ✓ DMG verified"

echo ""
echo "✓ Installer ready: ${DMG_OUTPUT}"
