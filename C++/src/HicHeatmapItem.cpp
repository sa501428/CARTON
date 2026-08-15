#include "HicHeatmapItem.h"
#include "HeatmapColorMapping.h"

#include <QMouseEvent>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int kMaxRenderedRecords = 500000;

class HeatmapRootNode final : public QSGNode {
public:
    HeatmapRootNode() {
        background = new QSGGeometryNode;
        auto* backgroundGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        backgroundGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        background->setGeometry(backgroundGeometry);
        background->setFlag(QSGNode::OwnsGeometry);
        backgroundMaterial = new QSGFlatColorMaterial;
        backgroundMaterial->setColor(QColor("#ffffff"));
        background->setMaterial(backgroundMaterial);
        background->setFlag(QSGNode::OwnsMaterial);
        appendChildNode(background);

        heatmap = new QSGGeometryNode;
        auto* heatmapGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        heatmapGeometry->setDrawingMode(QSGGeometry::DrawTriangles);
        heatmap->setGeometry(heatmapGeometry);
        heatmap->setFlag(QSGNode::OwnsGeometry);
        heatmap->setMaterial(new QSGVertexColorMaterial);
        heatmap->setFlag(QSGNode::OwnsMaterial);
        appendChildNode(heatmap);
    }

    QSGGeometryNode* background = nullptr;
    QSGFlatColorMaterial* backgroundMaterial = nullptr;
    QSGGeometryNode* heatmap = nullptr;
    int vertexCapacity = 0;
};

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
        connect(m_controller, &HicDataController::minimapChanged, this, &HicHeatmapItem::update);
        connect(m_controller, &HicDataController::viewChanged, this, &HicHeatmapItem::update);
        connect(m_controller, &HicDataController::colorMaxChanged, this, &HicHeatmapItem::update);
        connect(m_controller, &HicDataController::colorMapChanged, this, &HicHeatmapItem::update);
    }
    emit controllerChanged();
    update();
}

bool HicHeatmapItem::overviewMode() const {
    return m_overviewMode;
}

void HicHeatmapItem::setOverviewMode(bool enabled) {
    if (m_overviewMode == enabled) return;
    m_overviewMode = enabled;
    emit overviewModeChanged();
    update();
}

QSGNode* HicHeatmapItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* root = static_cast<HeatmapRootNode*>(oldNode);
    if (!root) root = new HeatmapRootNode;

    auto* backgroundGeometry = root->background->geometry();
    auto* bgVertices = backgroundGeometry->vertexDataAsPoint2D();
    bgVertices[0].set(0, 0);
    bgVertices[1].set(static_cast<float>(width()), 0);
    bgVertices[2].set(0, static_cast<float>(height()));
    bgVertices[3].set(static_cast<float>(width()), static_cast<float>(height()));
    root->background->markDirty(QSGNode::DirtyGeometry);

    if (!m_controller || width() <= 0 || height() <= 0) {
        root->heatmap->geometry()->setVertexCount(0);
        root->heatmap->markDirty(QSGNode::DirtyGeometry);
        return root;
    }

    const QColor zeroColor = colorForValue(0.0f);
    if (root->backgroundMaterial->color() != zeroColor) {
        root->backgroundMaterial->setColor(zeroColor);
        root->background->markDirty(QSGNode::DirtyMaterial);
    }

    std::vector<contactRecord> records;
    std::vector<contactRecord> controlRecords;
    int dataResolution = m_controller->resolution();
    if (m_overviewMode) {
        m_controller->renderMinimapRecordsSnapshot(records, dataResolution, 25000);
    } else {
        m_controller->renderRecordsSnapshot(records, controlRecords, dataResolution, kMaxRenderedRecords);
    }
    const QString matrixType = m_controller->matrixType();
    const bool isVsMode = (matrixType == QStringLiteral("vs") || matrixType.endsWith(QStringLiteral("vs"))) && !controlRecords.empty();
    if (records.empty() && controlRecords.empty()) {
        root->heatmap->geometry()->setVertexCount(0);
        root->heatmap->markDirty(QSGNode::DirtyGeometry);
        return root;
    }

    const qint64 x0 = m_overviewMode ? 0 : m_controller->x0();
    const qint64 x1 = m_overviewMode ? m_controller->xChromosomeLength() : m_controller->x1();
    const qint64 y0 = m_overviewMode ? 0 : m_controller->y0();
    const qint64 y1 = m_overviewMode ? m_controller->yChromosomeLength() : m_controller->y1();
    const qint64 viewWidth = std::max<qint64>(1, x1 - x0);
    const qint64 viewHeight = std::max<qint64>(1, y1 - y0);
    const double scaleX = width() / static_cast<double>(viewWidth);
    const double scaleY = height() / static_cast<double>(viewHeight);
    const bool mirrorIntra = m_controller->chrX() == m_controller->chrY() && !isVsMode;
    const bool splitVsIntra = m_controller->chrX() == m_controller->chrY() && isVsMode;

    const int renderedRecords = static_cast<int>(records.size() + controlRecords.size());
    const int verticesPerRecord = mirrorIntra ? 12 : 6;
    const int requiredVertices = renderedRecords * verticesPerRecord;
    auto* geometry = root->heatmap->geometry();
    if (requiredVertices > root->vertexCapacity || requiredVertices < root->vertexCapacity / 3) {
        geometry->allocate(requiredVertices);
        root->vertexCapacity = requiredVertices;
    } else {
        geometry->setVertexCount(requiredVertices);
    }
    auto* vertices = geometry->vertexDataAsColoredPoint2D();

    int vi = 0;
    auto appendQuad = [&](qint64 genomeX, qint64 genomeY, const QColor& color) {
        const double px0 = (genomeX - x0) * scaleX;
        const double py0 = (genomeY - y0) * scaleY;
        const double px1 = (genomeX + dataResolution - x0) * scaleX;
        const double py1 = (genomeY + dataResolution - y0) * scaleY;
        const double leftD = std::max(0.0, std::min(px0, px1));
        const double topD = std::max(0.0, std::min(py0, py1));
        const double rightD = std::min(static_cast<double>(width()), std::max(px0, px1));
        const double bottomD = std::min(static_cast<double>(height()), std::max(py0, py1));
        if (rightD <= 0.0 || bottomD <= 0.0 || leftD >= width() || topD >= height()) {
            return;
        }
        const uchar r = static_cast<uchar>(color.red());
        const uchar g = static_cast<uchar>(color.green());
        const uchar b = static_cast<uchar>(color.blue());
        const uchar a = static_cast<uchar>(color.alpha());
        const float left = static_cast<float>(leftD);
        const float top = static_cast<float>(topD);
        const float right = static_cast<float>(rightD);
        const float bottom = static_cast<float>(bottomD);
        vertices[vi++].set(left, top, r, g, b, a);
        vertices[vi++].set(right, top, r, g, b, a);
        vertices[vi++].set(left, bottom, r, g, b, a);
        vertices[vi++].set(right, top, r, g, b, a);
        vertices[vi++].set(right, bottom, r, g, b, a);
        vertices[vi++].set(left, bottom, r, g, b, a);
    };

    for (std::size_t i = 0; i < records.size(); ++i) {
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
    for (std::size_t i = 0; i < controlRecords.size(); ++i) {
        const contactRecord& record = controlRecords[i];
        const QColor color = colorForValue(record.counts);
        if (splitVsIntra) {
            appendQuad(std::min(record.binX, record.binY), std::max(record.binX, record.binY), color);
        } else {
            appendQuad(record.binX, record.binY, color);
        }
    }
    geometry->setVertexCount(vi);
    root->heatmap->markDirty(QSGNode::DirtyGeometry);

    return root;
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
    HeatmapColorSettings settings;
    if (m_controller) {
        settings.minimum = m_controller->colorMin();
        settings.maximum = m_controller->colorMax();
        settings.matrixType = m_controller->matrixType();
        settings.colorMap = m_controller->colorMap();
        settings.customLowColor = m_controller->customLowColor();
        settings.customHighColor = m_controller->customHighColor();
        settings.missingValueColor = m_controller->missingValueColor();
        settings.zeroTransparent = m_controller->zeroTransparent();
    }
    return heatmapColorForValue(value, settings);
}
