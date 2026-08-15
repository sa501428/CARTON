#ifndef CARTON_HEATMAP_COLOR_MAPPING_H
#define CARTON_HEATMAP_COLOR_MAPPING_H

#include <QColor>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

struct HeatmapColorSettings {
    double minimum = 0.0;
    double maximum = 50.0;
    QString matrixType = QStringLiteral("observed");
    QString colorMap = QStringLiteral("White-Red");
    QColor customLowColor = QColor("#ffffff");
    QColor customHighColor = QColor("#d7191c");
    QColor missingValueColor = QColor("#4b5563");
    bool zeroTransparent = false;
};

inline QColor interpolateHeatmapColor(const QColor& a, const QColor& b, double t) {
    t = std::clamp(t, 0.0, 1.0);
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
    const double scaled = t * static_cast<double>(stops.size() - 1);
    const auto index = static_cast<std::size_t>(std::floor(scaled));
    const std::size_t next = std::min(index + 1, stops.size() - 1);
    return interpolateHeatmapColor(stops[index], stops[next], scaled - std::floor(scaled));
}

inline QColor heatmapColorForValue(double value, const HeatmapColorSettings& settings) {
    if (!std::isfinite(value)) return settings.missingValueColor;
    if (value == 0.0 && settings.zeroTransparent) return QColor(0, 0, 0, 0);

    const bool pearson = settings.matrixType.contains(QStringLiteral("pearson"));
    const bool logRatio = settings.matrixType == QStringLiteral("logratio") ||
                          settings.matrixType == QStringLiteral("diff") ||
                          settings.matrixType == QStringLiteral("logoe") ||
                          settings.matrixType == QStringLiteral("explogoe");
    const bool ratioLike = settings.matrixType == QStringLiteral("oe") ||
                           settings.matrixType == QStringLiteral("controloe") ||
                           settings.matrixType == QStringLiteral("oeratio") ||
                           settings.matrixType == QStringLiteral("oevs") ||
                           settings.matrixType == QStringLiteral("logeovs") ||
                           settings.matrixType == QStringLiteral("ratio") ||
                           settings.matrixType == QStringLiteral("ratio1");
    if (pearson || logRatio || ratioLike) {
        if (!pearson && !logRatio && value <= 0.0) return settings.missingValueColor;
        double low = settings.minimum;
        double high = settings.maximum;
        if (low >= high) high = low + 1.0;
        double scaled = 0.0;
        if (pearson || logRatio) {
            scaled = std::clamp(value, low, high);
        } else {
            low = std::log(std::max(0.000001, settings.minimum));
            high = std::log(std::max(0.000001, settings.maximum));
            if (low >= high) high = low + 1.0;
            scaled = std::clamp(std::log(value), low, high);
        }
        const double midpoint = low < 0.0 && high > 0.0 ? 0.0 : (low + high) * 0.5;
        QColor mapped;
        if (scaled >= midpoint) {
            const double t = std::clamp((scaled - midpoint) / std::max(0.000001, high - midpoint), 0.0, 1.0);
            mapped = interpolateHeatmapColor(QColor("#ffffff"), QColor("#b2182b"), t);
        } else {
            const double t = std::clamp((midpoint - scaled) / std::max(0.000001, midpoint - low), 0.0, 1.0);
            mapped = interpolateHeatmapColor(QColor("#ffffff"), QColor("#2166ac"), t);
        }
        mapped.setAlpha(245);
        return mapped;
    }

    double scaledValue = std::max(0.0, value);
    double scaledMin = settings.minimum;
    double scaledMax = settings.maximum;
    if (settings.matrixType == QStringLiteral("log") ||
        settings.matrixType == QStringLiteral("logcontrol") ||
        settings.matrixType == QStringLiteral("logvs")) {
        scaledValue = std::log1p(scaledValue);
        scaledMin = std::log1p(std::max(0.0, scaledMin));
        scaledMax = std::log1p(std::max(0.0, scaledMax));
    }
    if (scaledMin >= scaledMax) scaledMax = scaledMin + 1.0;
    const double t = std::clamp((scaledValue - scaledMin) / (scaledMax - scaledMin), 0.0, 1.0);
    QColor color;
    if (settings.colorMap == QStringLiteral("Viridis")) {
        color = interpolateHeatmapStops(
            {QColor("#440154"), QColor("#31688e"), QColor("#35b779"), QColor("#fde725")}, t);
    } else if (settings.colorMap == QStringLiteral("Blue-White-Red")) {
        color = interpolateHeatmapStops({QColor("#2166ac"), QColor("#ffffff"), QColor("#b2182b")}, t);
    } else if (settings.colorMap == QStringLiteral("Grayscale")) {
        color = interpolateHeatmapColor(QColor("#ffffff"), QColor("#111111"), t);
    } else if (settings.colorMap == QStringLiteral("Custom")) {
        color = interpolateHeatmapColor(settings.customLowColor, settings.customHighColor, t);
    } else {
        color = interpolateHeatmapColor(QColor("#ffffff"), QColor("#d7191c"), t);
    }
    color.setAlpha(245);
    return color;
}

#endif
