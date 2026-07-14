pragma Singleton
import QtQuick

QtObject {
    id: theme

    property bool dark: true
    property real uiScale: 1.0
    property real fontScale: 1.0
    property bool reducedMotion: false

    // Base surfaces
    readonly property color appBg: dark ? "#0e1014" : "#f2f3f7"
    readonly property color surface: dark ? "#181b22" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#1f232c" : "#f7f8fb"
    readonly property color surfaceSunken: dark ? "#111319" : "#eceff3"
    readonly property color surfaceHover: dark ? "#242833" : "#eef0f4"
    readonly property color surfacePressed: dark ? "#2a2f3c" : "#e5e8ee"
    readonly property color surfaceDisabled: dark ? "#181a20" : "#f1f2f5"
    readonly property color panelBg: dark ? "#11161d" : "#f7f9fc"
    readonly property color hoverSurface: dark ? "#202833" : "#edf2f7"
    readonly property color selectedSurface: dark ? "#263447" : "#e5edff"

    // Chrome (header / footer / sidebar frame)
    readonly property color chromeBg: dark ? "#0a0b0f" : "#171a21"
    readonly property color chromeBorder: dark ? "#22252e" : "#272b34"
    readonly property color chromeText: "#f2f4f8"
    readonly property color chromeTextMuted: "#9aa1b0"

    // Borders
    readonly property color border: dark ? "#2a2e38" : "#e2e5eb"
    readonly property color borderStrong: dark ? "#3a3f4c" : "#ccd1da"
    readonly property color borderSubtle: dark ? "#22252e" : "#ebedf1"

    // Text
    readonly property color textPrimary: dark ? "#eef0f4" : "#13161c"
    readonly property color textSecondary: dark ? "#9aa4b5" : "#5b6472"
    readonly property color textMuted: dark ? "#6d7686" : "#8991a0"
    readonly property color textDisabled: dark ? "#454b58" : "#b7bcc5"

    // Accent (indigo)
    readonly property color accent: dark ? "#818cf8" : "#5457e0"
    readonly property color accentHover: dark ? "#939df9" : "#4649cf"
    readonly property color accentPressed: dark ? "#6f79ea" : "#3b3ec0"
    readonly property color accentSoft: dark ? "#262a4a" : "#ecedfd"
    readonly property color accentSoftHover: dark ? "#2f3459" : "#e2e3fc"
    readonly property color accentForeground: "#ffffff"

    // Status accents used sparingly
    readonly property color warn: dark ? "#fbbf24" : "#c2790a"
    readonly property color danger: dark ? "#f87171" : "#dc2626"
    readonly property color success: dark ? "#34d399" : "#15803d"
    readonly property color missingData: dark ? "#4b5563" : "#94a3b8"

    // Shadow
    readonly property color shadow: dark ? "#70000000" : "#33222833"

    // Canvas overlay tones
    readonly property color gridline: dark ? "#22ffffff" : "#22000000"
    readonly property color boundaryLine: dark ? "#99cbd5e1" : "#77374151"
    readonly property color tileDebugLine: Qt.rgba(accent.r, accent.g, accent.b, 0.33)
    readonly property color guideLine: dark ? "#cceef0f4" : "#cc111827"
    readonly property color tooltipBg: "#ee111827"
    readonly property color footerBg: dark ? "#dd05060a" : "#dd171a21"

    // Shape
    readonly property int radiusSm: 6
    readonly property int radiusMd: 10
    readonly property int radiusLg: 16
    readonly property int radiusPill: 999

    // Spacing scale
    readonly property int space2: 2
    readonly property int space4: 4
    readonly property int space6: 6
    readonly property int space8: 8
    readonly property int space10: 10
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space20: 20
    readonly property int space24: 24
    readonly property int controlHeight: Math.round(32 * uiScale)
    readonly property int sidebarWidth: 272
    readonly property int inspectorWidth: 336
    readonly property int trackHeaderHeight: 28
    readonly property int animationFast: 100

    // Typography
    readonly property string fontFamily: "Helvetica Neue"
    readonly property int textXs: Math.round(11 * uiScale * fontScale)
    readonly property int textSm: Math.round(12 * uiScale * fontScale)
    readonly property int textBase: Math.round(13 * uiScale * fontScale)
    readonly property int textMd: Math.round(14 * uiScale * fontScale)
    readonly property int textLg: Math.round(16 * uiScale * fontScale)
    readonly property int textXl: Math.round(19 * uiScale * fontScale)
}
