#include "HicHeatmapItem.h"

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
    m_controller->renderRecordsSnapshot(records, controlRecords, dataResolution, kMaxRenderedRecords);
    const QString matrixType = m_controller->matrixType();
    const bool isVsMode = (matrixType == QStringLiteral("vs") || matrixType.endsWith(QStringLiteral("vs"))) && !controlRecords.empty();
    if (records.empty() && controlRecords.empty()) {
        root->heatmap->geometry()->setVertexCount(0);
        root->heatmap->markDirty(QSGNode::DirtyGeometry);
        return root;
    }

    const qint64 x0 = m_controller->x0();
    const qint64 x1 = m_controller->x1();
    const qint64 y0 = m_controller->y0();
    const qint64 y1 = m_controller->y1();
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
    const double maxValue = m_controller ? m_controller->colorMax() : 50.0;
    const double minValue = m_controller ? m_controller->colorMin() : 0.0;
    const QString matrixType = m_controller ? m_controller->matrixType() : QStringLiteral("observed");
    if (!std::isfinite(value)) {
        return m_controller ? m_controller->missingValueColor() : QColor("#4b5563");
    }
    if (value == 0.0f && m_controller && m_controller->zeroTransparent()) return QColor(0, 0, 0, 0);

    const bool pearson = matrixType.contains(QStringLiteral("pearson"));
    const bool logRatio = matrixType == QStringLiteral("logratio") || matrixType == QStringLiteral("diff") ||
                          matrixType == QStringLiteral("logoe") || matrixType == QStringLiteral("explogoe");
    const bool ratioLike = matrixType == QStringLiteral("oe") || matrixType == QStringLiteral("controloe") ||
                           matrixType == QStringLiteral("oeratio") || matrixType == QStringLiteral("oevs") ||
                           matrixType == QStringLiteral("logeovs") || matrixType == QStringLiteral("ratio") ||
                           matrixType == QStringLiteral("ratio1");
    if (pearson || logRatio || ratioLike) {
        if (!pearson && !logRatio && value <= 0.0f) {
            return m_controller ? m_controller->missingValueColor() : QColor("#4b5563");
        }
        double scaled = 0.0;
        double low = minValue;
        double high = maxValue;
        if (low >= high) {
            high = low + 1.0;
        }
        if (pearson) {
            scaled = std::clamp(static_cast<double>(value), low, high);
        } else if (logRatio) {
            scaled = std::clamp(static_cast<double>(value), low, high);
        } else {
            low = std::log(std::max(0.000001, minValue));
            high = std::log(std::max(0.000001, maxValue));
            if (low >= high) {
                high = low + 1.0;
            }
            scaled = std::clamp(std::log(static_cast<double>(value)), low, high);
        }
        const double midpoint = low < 0.0 && high > 0.0 ? 0.0 : (low + high) * 0.5;
        const double positiveRange = std::max(0.000001, high - midpoint);
        const double negativeRange = std::max(0.000001, midpoint - low);
        int r = 255;
        int g = 255;
        int b = 255;
        if (scaled >= midpoint) {
            const double t = std::clamp((scaled - midpoint) / positiveRange, 0.0, 1.0);
            r = 255;
            g = static_cast<int>(255.0 * (1.0 - t));
            b = static_cast<int>(255.0 * (1.0 - t));
        } else {
            const double t = std::clamp((midpoint - scaled) / negativeRange, 0.0, 1.0);
            b = 255;
            r = static_cast<int>(255.0 * (1.0 - t));
            g = static_cast<int>(255.0 * (1.0 - t));
        }
        return QColor(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), 245);
    }

    double scaledValue = static_cast<double>(std::max(0.0f, value));
    double scaledMin = minValue;
    double scaledMax = maxValue;
    if (matrixType == QStringLiteral("log") || matrixType == QStringLiteral("logcontrol") ||
        matrixType == QStringLiteral("logvs")) {
        scaledValue = std::log1p(scaledValue);
        scaledMin = std::log1p(std::max(0.0, scaledMin));
        scaledMax = std::log1p(std::max(0.0, scaledMax));
    }
    if (scaledMin >= scaledMax) {
        scaledMax = scaledMin + 1.0;
    }
    const double t = std::clamp((scaledValue - scaledMin) / (scaledMax - scaledMin), 0.0, 1.0);
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
