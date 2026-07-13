#include "HicHeatmapItem.h"

#include <QMouseEvent>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int kMaxRenderedRecords = 220000;

QColor interpolateColor(const QColor& a, const QColor& b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return QColor(
        static_cast<int>(a.red() + (b.red() - a.red()) * t),
        static_cast<int>(a.green() + (b.green() - a.green()) * t),
        static_cast<int>(a.blue() + (b.blue() - a.blue()) * t),
        static_cast<int>(a.alpha() + (b.alpha() - a.alpha()) * t)
    );
}

QColor interpolateStops(const std::vector<QColor>& stops, double t) {
    if (stops.empty()) {
        return QColor("#d7191c");
    }
    if (stops.size() == 1) {
        return stops.front();
    }
    t = std::clamp(t, 0.0, 1.0);
    const double scaled = t * static_cast<double>(stops.size() - 1);
    const auto idx = static_cast<std::size_t>(std::floor(scaled));
    const std::size_t next = std::min(idx + 1, stops.size() - 1);
    return interpolateColor(stops[idx], stops[next], scaled - std::floor(scaled));
}
}

HicHeatmapItem::HicHeatmapItem(QQuickItem* parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

HicDataController* HicHeatmapItem::controller() const {
    return m_controller;
}

void HicHeatmapItem::setController(HicDataController* controller) {
    if (m_controller == controller) {
        return;
    }
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &HicDataController::recordsChanged, this, &HicHeatmapItem::update);
        connect(m_controller, &HicDataController::viewChanged, this, &HicHeatmapItem::update);
        connect(m_controller, &HicDataController::colorMaxChanged, this, &HicHeatmapItem::update);
        connect(m_controller, &HicDataController::colorMapChanged, this, &HicHeatmapItem::update);
    }
    emit controllerChanged();
    update();
}

QSGNode* HicHeatmapItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    delete oldNode;

    auto* root = new QSGNode;
    auto* background = new QSGGeometryNode;
    auto* backgroundGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
    backgroundGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    auto* bgVertices = backgroundGeometry->vertexDataAsPoint2D();
    bgVertices[0].set(0, 0);
    bgVertices[1].set(static_cast<float>(width()), 0);
    bgVertices[2].set(0, static_cast<float>(height()));
    bgVertices[3].set(static_cast<float>(width()), static_cast<float>(height()));
    background->setGeometry(backgroundGeometry);
    background->setFlag(QSGNode::OwnsGeometry);
    auto* backgroundMaterial = new QSGFlatColorMaterial;
    backgroundMaterial->setColor(QColor("#ffffff"));
    background->setMaterial(backgroundMaterial);
    background->setFlag(QSGNode::OwnsMaterial);
    root->appendChildNode(background);

    if (!m_controller || width() <= 0 || height() <= 0) {
        return root;
    }

    const std::vector<contactRecord> records = m_controller->recordsSnapshot();
    const std::vector<contactRecord> controlRecords = m_controller->controlRecordsSnapshot();
    const bool isVsMode = m_controller->matrixType() == QStringLiteral("vs") && !controlRecords.empty();
    if (records.empty() && controlRecords.empty()) {
        return root;
    }

    const qint64 x0 = m_controller->x0();
    const qint64 x1 = m_controller->x1();
    const qint64 y0 = m_controller->y0();
    const qint64 y1 = m_controller->y1();
    const qint64 viewWidth = std::max<qint64>(1, x1 - x0);
    const qint64 viewHeight = std::max<qint64>(1, y1 - y0);
    const double side = std::min(width(), height());
    const double scale = side / static_cast<double>(std::max(viewWidth, viewHeight));
    const double originX = (width() - side) * 0.5;
    const double originY = (height() - side) * 0.5;
    const double cellSize = std::max(1.0, m_controller->resolution() * scale);
    const bool mirrorIntra = m_controller->chrX() == m_controller->chrY() && !isVsMode;
    const bool splitVsIntra = m_controller->chrX() == m_controller->chrY() && isVsMode;

    const int stride = std::max(1, static_cast<int>(std::ceil(records.size() / static_cast<double>(kMaxRenderedRecords))));
    const int controlStride = std::max(1, static_cast<int>(std::ceil(controlRecords.size() / static_cast<double>(kMaxRenderedRecords))));
    const int renderedRecords = static_cast<int>((records.size() + stride - 1) / stride)
                                + static_cast<int>((controlRecords.size() + controlStride - 1) / controlStride);
    const int verticesPerRecord = mirrorIntra ? 12 : 6;
    auto* node = new QSGGeometryNode;
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), renderedRecords * verticesPerRecord);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices = geometry->vertexDataAsColoredPoint2D();

    int vi = 0;
    auto appendQuad = [&](qint64 genomeX, qint64 genomeY, const QColor& color) {
        const double px = originX + (genomeX - x0) * scale;
        const double py = originY + (genomeY - y0) * scale;
        if (px + cellSize < 0 || py + cellSize < 0 || px > width() || py > height()) {
            return;
        }
        const uchar r = static_cast<uchar>(color.red());
        const uchar g = static_cast<uchar>(color.green());
        const uchar b = static_cast<uchar>(color.blue());
        const uchar a = static_cast<uchar>(color.alpha());
        const float left = static_cast<float>(px);
        const float top = static_cast<float>(py);
        const float right = static_cast<float>(px + cellSize);
        const float bottom = static_cast<float>(py + cellSize);
        vertices[vi++].set(left, top, r, g, b, a);
        vertices[vi++].set(right, top, r, g, b, a);
        vertices[vi++].set(left, bottom, r, g, b, a);
        vertices[vi++].set(right, top, r, g, b, a);
        vertices[vi++].set(right, bottom, r, g, b, a);
        vertices[vi++].set(left, bottom, r, g, b, a);
    };

    for (std::size_t i = 0; i < records.size(); i += stride) {
        const contactRecord& record = records[i];
        const QColor color = colorForValue(record.counts);
        if (splitVsIntra) {
            appendQuad(std::max(record.binX, record.binY), std::min(record.binX, record.binY), color);
        } else {
            appendQuad(record.binX, record.binY, color);
        }
        if (mirrorIntra && record.binX != record.binY) {
            appendQuad(record.binY, record.binX, color);
        }
    }
    for (std::size_t i = 0; i < controlRecords.size(); i += controlStride) {
        const contactRecord& record = controlRecords[i];
        const QColor color = colorForValue(record.counts);
        if (splitVsIntra) {
            appendQuad(std::min(record.binX, record.binY), std::max(record.binX, record.binY), color);
        } else {
            appendQuad(record.binX, record.binY, color);
        }
    }
    geometry->setVertexCount(vi);

    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto* material = new QSGVertexColorMaterial;
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    root->appendChildNode(node);

    return root;
}

void HicHeatmapItem::wheelEvent(QWheelEvent* event) {
    if (!m_controller) {
        event->ignore();
        return;
    }
    const double factor = event->angleDelta().y() > 0 ? 1.35 : 0.74;
    const QPointF p = event->position();
    const double side = std::max(1.0, std::min(width(), height()));
    const double originX = (width() - side) * 0.5;
    const double originY = (height() - side) * 0.5;
    m_controller->zoom(factor,
                       std::clamp((p.x() - originX) / side, 0.0, 1.0),
                       std::clamp((p.y() - originY) / side, 0.0, 1.0));
    event->accept();
}

void HicHeatmapItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastMousePosition = event->position();
        event->accept();
    }
}

void HicHeatmapItem::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging || !m_controller) {
        event->ignore();
        return;
    }
    const QPointF delta = event->position() - m_lastMousePosition;
    m_lastMousePosition = event->position();
    const double side = std::max(1.0, std::min(width(), height()));
    m_controller->pan(-delta.x() / side, -delta.y() / side);
    event->accept();
}

void HicHeatmapItem::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

QColor HicHeatmapItem::colorForValue(float value) const {
    const double maxValue = m_controller ? m_controller->colorMax() : 50.0;
    const double minValue = m_controller ? m_controller->colorMin() : 0.0;
    const QString matrixType = m_controller ? m_controller->matrixType() : QStringLiteral("observed");
    if (!std::isfinite(value)) {
        return QColor("#808080");
    }

    const bool pearson = matrixType.contains(QStringLiteral("pearson"));
    const bool logRatio = matrixType == QStringLiteral("logratio") || matrixType == QStringLiteral("diff") ||
                          matrixType == QStringLiteral("logoe") || matrixType == QStringLiteral("explogoe");
    const bool ratioLike = matrixType == QStringLiteral("oe") || matrixType == QStringLiteral("controloe") ||
                           matrixType == QStringLiteral("oeratio") || matrixType == QStringLiteral("oevs") ||
                           matrixType == QStringLiteral("logeovs") || matrixType == QStringLiteral("ratio") ||
                           matrixType == QStringLiteral("ratio1");
    if (pearson || logRatio || ratioLike) {
        if (!pearson && !logRatio && value <= 0.0f) {
            return QColor("#808080");
        }
        double scaled = 0.0;
        double threshold = 1.0;
        if (pearson) {
            scaled = std::clamp(static_cast<double>(value), -1.0, 1.0);
            threshold = 1.0;
        } else if (logRatio) {
            threshold = std::max(std::abs(minValue), std::abs(maxValue));
            scaled = std::clamp(static_cast<double>(value), -threshold, threshold);
        } else {
            threshold = std::log(std::max(1.01, maxValue));
            scaled = std::clamp(std::log(static_cast<double>(value)), -threshold, threshold);
        }
        int r = 255;
        int g = 255;
        int b = 255;
        if (scaled > 0.0) {
            r = 255;
            g = static_cast<int>(255.0 * (threshold - scaled) / threshold);
            b = static_cast<int>(255.0 * (threshold - scaled) / threshold);
        } else {
            scaled = std::abs(scaled);
            b = 255;
            r = static_cast<int>(255.0 * (threshold - scaled) / threshold);
            g = static_cast<int>(255.0 * (threshold - scaled) / threshold);
        }
        return QColor(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), 245);
    }

    double scaledValue = static_cast<double>(std::max(0.0f, value));
    double scaledMax = std::max(1.0, maxValue);
    if (matrixType == QStringLiteral("log") || matrixType == QStringLiteral("logcontrol")) {
        scaledValue = std::log1p(scaledValue);
        scaledMax = std::log1p(scaledMax);
    }
    const double t = std::clamp(scaledValue / scaledMax, 0.0, 1.0);
    const QString colorMap = m_controller ? m_controller->colorMap() : QStringLiteral("White-Red");
    QColor color;
    if (colorMap == QStringLiteral("Viridis")) {
        color = interpolateStops({QColor("#440154"), QColor("#31688e"), QColor("#35b779"), QColor("#fde725")}, t);
    } else if (colorMap == QStringLiteral("Blue-White-Red")) {
        color = interpolateStops({QColor("#2166ac"), QColor("#ffffff"), QColor("#b2182b")}, t);
    } else if (colorMap == QStringLiteral("Grayscale")) {
        color = interpolateColor(QColor("#ffffff"), QColor("#111111"), t);
    } else if (colorMap == QStringLiteral("Custom") && m_controller) {
        color = interpolateColor(m_controller->customLowColor(), m_controller->customHighColor(), t);
    } else {
        color = interpolateColor(QColor("#ffffff"), QColor("#d7191c"), t);
    }
    color.setAlpha(245);
    return color;
}
