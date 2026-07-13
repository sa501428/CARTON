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
    if (records.empty()) {
        return root;
    }

    const qint64 x0 = m_controller->x0();
    const qint64 x1 = m_controller->x1();
    const qint64 y0 = m_controller->y0();
    const qint64 y1 = m_controller->y1();
    const qint64 viewWidth = std::max<qint64>(1, x1 - x0);
    const qint64 viewHeight = std::max<qint64>(1, y1 - y0);
    const double sx = width() / static_cast<double>(viewWidth);
    const double sy = height() / static_cast<double>(viewHeight);
    const double cellW = std::max(1.0, m_controller->resolution() * sx);
    const double cellH = std::max(1.0, m_controller->resolution() * sy);

    const int stride = std::max(1, static_cast<int>(std::ceil(records.size() / static_cast<double>(kMaxRenderedRecords))));
    const int renderedRecords = static_cast<int>((records.size() + stride - 1) / stride);
    auto* node = new QSGGeometryNode;
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), renderedRecords * 6);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices = geometry->vertexDataAsColoredPoint2D();

    int vi = 0;
    for (std::size_t i = 0; i < records.size(); i += stride) {
        const contactRecord& record = records[i];
        const double px = (record.binX - x0) * sx;
        const double py = (record.binY - y0) * sy;
        if (px + cellW < 0 || py + cellH < 0 || px > width() || py > height()) {
            continue;
        }
        const QColor color = colorForValue(record.counts);
        const uchar r = static_cast<uchar>(color.red());
        const uchar g = static_cast<uchar>(color.green());
        const uchar b = static_cast<uchar>(color.blue());
        const uchar a = static_cast<uchar>(color.alpha());
        const float left = static_cast<float>(px);
        const float top = static_cast<float>(py);
        const float right = static_cast<float>(px + cellW);
        const float bottom = static_cast<float>(py + cellH);
        vertices[vi++].set(left, top, r, g, b, a);
        vertices[vi++].set(right, top, r, g, b, a);
        vertices[vi++].set(left, bottom, r, g, b, a);
        vertices[vi++].set(right, top, r, g, b, a);
        vertices[vi++].set(right, bottom, r, g, b, a);
        vertices[vi++].set(left, bottom, r, g, b, a);
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
    m_controller->zoom(factor, p.x() / std::max(1.0, width()), p.y() / std::max(1.0, height()));
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
    m_controller->pan(-delta.x() / std::max(1.0, width()), -delta.y() / std::max(1.0, height()));
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
    const double t = std::clamp(std::log1p(std::max(0.0f, value)) / std::log1p(maxValue), 0.0, 1.0);
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
