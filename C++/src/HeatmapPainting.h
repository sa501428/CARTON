#ifndef CARTON_HEATMAP_PAINTING_H
#define CARTON_HEATMAP_PAINTING_H

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <vector>
#include "HeatmapColorMapping.h"
#include "straw.h"

inline void paintHeatmapRecords(QPainter& painter, const QRect& plot,
                         const std::vector<contactRecord>& records,
                         const std::vector<contactRecord>& controlRecords,
                         const QString& chrX, const QString& chrY, const QString& matrixType,
                         qint64 x0, qint64 x1, qint64 y0, qint64 y1, int resolution,
                         const HeatmapColorSettings& colors) {
    const double spanX = std::max<qint64>(1, x1 - x0);
    const double spanY = std::max<qint64>(1, y1 - y0);
    const bool splitVs = chrX == chrY &&
                         (matrixType == QStringLiteral("vs") || matrixType.endsWith(QStringLiteral("vs")));
    const bool mirror = chrX == chrY && !splitVs;
    const std::size_t total = records.size() + controlRecords.size();
    const std::size_t stride = std::max<std::size_t>(1, total / 400000U);
    const int cellWidth = std::max(1, static_cast<int>(std::ceil(std::max(1, resolution) / spanX * plot.width())));
    const int cellHeight = std::max(1, static_cast<int>(std::ceil(std::max(1, resolution) / spanY * plot.height())));
    painter.save();
    painter.setClipRect(plot, Qt::IntersectClip);
    painter.setPen(Qt::NoPen);
    auto drawCell = [&](qint64 binX, qint64 binY) {
        if (binX >= x1 || binX + resolution <= x0 || binY >= y1 || binY + resolution <= y0) return;
        const qreal x = plot.left() + (binX - x0) / spanX * plot.width();
        const qreal y = plot.top() + (binY - y0) / spanY * plot.height();
        painter.drawRect(QRectF(x, y, cellWidth, cellHeight));
    };
    auto drawRecords = [&](const std::vector<contactRecord>& source, bool control) {
        for (std::size_t i = 0; i < source.size(); i += stride) {
            const contactRecord& record = source[i];
            painter.setBrush(heatmapColorForValue(record.counts, colors));
            const int drawBinX = splitVs
                ? (control ? std::min(record.binX, record.binY) : std::max(record.binX, record.binY))
                : record.binX;
            const int drawBinY = splitVs
                ? (control ? std::max(record.binX, record.binY) : std::min(record.binX, record.binY))
                : record.binY;
            drawCell(drawBinX, drawBinY);
            if (mirror && record.binX != record.binY) drawCell(record.binY, record.binX);
        }
    };
    drawRecords(records, false);
    drawRecords(controlRecords, true);
    painter.restore();
}

#endif
