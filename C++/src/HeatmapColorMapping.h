#ifndef CARTON_HEATMAP_COLOR_MAPPING_H
#define CARTON_HEATMAP_COLOR_MAPPING_H

#include <QColor>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

// How a matrix type's values are distributed, which decides both the colour
// ramp's neutral point and the automatic range. This is the single source of
// truth: the renderer and HicDataController's auto-scaling both classify
// through it, because two independent lists had drifted apart and disagreed
// about several matrix types.
enum class HeatmapScaleKind {
    Sequential,  // 0..max counts, cosine similarity: no meaningful midpoint.
    Divergent,   // centred on 0: Pearson, log(O/E), log ratios, differences.
    Ratio        // centred on 1 and read logarithmically: O/E and plain ratios.
};

inline HeatmapScaleKind heatmapScaleKind(const QString& matrixType) {
    if (matrixType.contains(QStringLiteral("pearson"))) return HeatmapScaleKind::Divergent;
    // Cosine is bounded to 0..1 with no neutral point, so it stays sequential
    // even though it is a similarity metric like Pearson.
    if (matrixType.contains(QStringLiteral("cosine"))) return HeatmapScaleKind::Sequential;
    if (matrixType == QStringLiteral("logoe") || matrixType == QStringLiteral("logeovs") ||
        matrixType == QStringLiteral("logratio") || matrixType == QStringLiteral("diff")) {
        return HeatmapScaleKind::Divergent;
    }
    if (matrixType == QStringLiteral("oe") || matrixType == QStringLiteral("controloe") ||
        matrixType == QStringLiteral("oevs") || matrixType == QStringLiteral("oeratio") ||
        matrixType == QStringLiteral("explogoe") || matrixType == QStringLiteral("ratio") ||
        matrixType == QStringLiteral("ratio1")) {
        return HeatmapScaleKind::Ratio;
    }
    return HeatmapScaleKind::Sequential;
}

inline bool heatmapUsesLog1p(const QString& matrixType) {
    return matrixType == QStringLiteral("log") || matrixType == QStringLiteral("logcontrol") ||
           matrixType == QStringLiteral("logvs");
}

struct HeatmapColorSettings {
    double minimum = 0.0;
    double maximum = 50.0;
    QString matrixType = QStringLiteral("observed");
    QString colorMap = QStringLiteral("White-Red");
    QColor customLowColor = QColor("#2166ac");
    QColor customHighColor = QColor("#b2182b");
    QColor missingValueColor = QColor("#4b5563");
    bool zeroTransparent = false;
};

inline QColor interpolateHeatmapColor(const QColor& a, const QColor& b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (!std::isfinite(t)) t = 0.0;
    return QColor(
        static_cast<int>(a.red() + (b.red() - a.red()) * t),
        static_cast<int>(a.green() + (b.green() - a.green()) * t),
        static_cast<int>(a.blue() + (b.blue() - a.blue()) * t),
        static_cast<int>(a.alpha() + (b.alpha() - a.alpha()) * t));
}

inline QColor interpolateHeatmapStops(const std::vector<QColor>& stops, double t) {
    if (stops.empty()) return QColor("#d7191c");
    if (stops.size() == 1) return stops.front();
    t = std::clamp(t, 0.0, 1.0);
    if (!std::isfinite(t)) t = 0.0;
    const double scaled = t * static_cast<double>(stops.size() - 1);
    const auto index = static_cast<std::size_t>(std::floor(scaled));
    const std::size_t next = std::min(index + 1, stops.size() - 1);
    return interpolateHeatmapColor(stops[index], stops[next], scaled - std::floor(scaled));
}

// Every colour map applies to every matrix type. Previously the divergent and
// ratio branches hard-coded blue/white/red and silently discarded the user's
// selection, so picking Viridis, Grayscale or Custom did nothing once the view
// was switched to O/E or Pearson.
inline std::vector<QColor> heatmapRampStops(const HeatmapColorSettings& settings, bool diverging) {
    if (settings.colorMap == QStringLiteral("Viridis")) {
        return {QColor("#440154"), QColor("#31688e"), QColor("#35b779"), QColor("#fde725")};
    }
    if (settings.colorMap == QStringLiteral("Grayscale")) {
        // A symmetric grey ramp would make the two signs indistinguishable, so
        // grayscale stays a straight light-to-dark run in both cases.
        return {QColor("#ffffff"), QColor("#111111")};
    }
    if (settings.colorMap == QStringLiteral("Custom")) {
        const QColor low = settings.customLowColor.isValid() ? settings.customLowColor : QColor("#2166ac");
        const QColor high = settings.customHighColor.isValid() ? settings.customHighColor : QColor("#b2182b");
        return diverging ? std::vector<QColor>{low, QColor("#ffffff"), high}
                         : std::vector<QColor>{low, high};
    }
    if (settings.colorMap == QStringLiteral("Blue-White-Red")) {
        return {QColor("#2166ac"), QColor("#ffffff"), QColor("#b2182b")};
    }
    // White-Red, the default. On divergent data it grows a blue arm so the
    // neutral value still lands on white.
    return diverging ? std::vector<QColor>{QColor("#2166ac"), QColor("#ffffff"), QColor("#d7191c")}
                     : std::vector<QColor>{QColor("#ffffff"), QColor("#d7191c")};
}

// Position of a value inside the colour ramp, with the class's neutral value
// pinned to the middle of the ramp: 0 for divergent matrices, 1 for ratios.
inline double heatmapRampPosition(double value, const HeatmapColorSettings& settings,
                                  HeatmapScaleKind kind) {
    double minimum = settings.minimum;
    double maximum = settings.maximum;

    if (kind == HeatmapScaleKind::Ratio) {
        // Guard the log domain. The old code ran std::log() over a minimum of
        // -5 (the divergent default leaking into ratio types), producing a
        // lower bound of log(1e-6) = -13.8 that squashed every depleted bin
        // into a sliver of the ramp and left O/E maps looking white.
        if (minimum <= 0.0 || !std::isfinite(minimum)) minimum = 1.0 / 5.0;
        if (maximum <= 0.0 || !std::isfinite(maximum)) maximum = 5.0;
        if (minimum >= maximum) {
            minimum = std::min(minimum, 1.0 / 5.0);
            maximum = std::max(maximum, 5.0);
        }
        const double logValue = std::log(value);
        const double logLow = std::log(minimum);
        const double logHigh = std::log(maximum);
        if (logLow < 0.0 && logHigh > 0.0) {
            return logValue >= 0.0 ? 0.5 + 0.5 * std::clamp(logValue / logHigh, 0.0, 1.0)
                                   : 0.5 - 0.5 * std::clamp(logValue / logLow, 0.0, 1.0);
        }
        return std::clamp((logValue - logLow) / std::max(0.000001, logHigh - logLow), 0.0, 1.0);
    }

    if (kind == HeatmapScaleKind::Divergent) {
        if (minimum >= maximum) maximum = minimum + 1.0;
        if (minimum < 0.0 && maximum > 0.0) {
            return value >= 0.0 ? 0.5 + 0.5 * std::clamp(value / maximum, 0.0, 1.0)
                                : 0.5 - 0.5 * std::clamp(value / minimum, 0.0, 1.0);
        }
        return std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0);
    }

    double scaled = std::max(0.0, value);
    if (heatmapUsesLog1p(settings.matrixType)) {
        scaled = std::log1p(scaled);
        minimum = std::log1p(std::max(0.0, minimum));
        maximum = std::log1p(std::max(0.0, maximum));
    }
    if (minimum >= maximum) maximum = minimum + 1.0;
    return std::clamp((scaled - minimum) / (maximum - minimum), 0.0, 1.0);
}

inline QColor heatmapColorForValue(double value, const HeatmapColorSettings& settings) {
    if (!std::isfinite(value)) return settings.missingValueColor;
    if (value == 0.0 && settings.zeroTransparent) return QColor(0, 0, 0, 0);

    const HeatmapScaleKind kind = heatmapScaleKind(settings.matrixType);
    // Only ratios are undefined at or below zero. Divergent matrices are
    // legitimately negative across half their range, and one of them
    // ("logeovs") used to be classified as a ratio, which painted every
    // depleted bin with the missing-data colour.
    if (kind == HeatmapScaleKind::Ratio && value <= 0.0) return settings.missingValueColor;

    const bool diverging = kind != HeatmapScaleKind::Sequential;
    QColor color = interpolateHeatmapStops(heatmapRampStops(settings, diverging),
                                           heatmapRampPosition(value, settings, kind));
    color.setAlpha(245);
    return color;
}

#endif
