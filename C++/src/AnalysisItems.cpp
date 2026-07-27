#include "AnalysisItems.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGVertexColorMaterial>
#include <QQuickWindow>
#include <QThreadPool>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <exception>
#include <new>

#include "MatrixAnalysis.h"

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxRotatedRecords = 250000;

class ColoredGeometryNode final : public QSGGeometryNode {
public:
    explicit ColoredGeometryNode(QSGGeometry::DrawingMode mode) {
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(mode);
        setGeometry(geometry);
        setFlag(QSGNode::OwnsGeometry);
        setMaterial(new QSGVertexColorMaterial);
        setFlag(QSGNode::OwnsMaterial);
    }

    int vertexCapacity = 0;
};

class OwnedTextureNode final : public QSGSimpleTextureNode {
public:
    OwnedTextureNode() { setOwnsTexture(true); }

    void replaceTexture(QSGTexture* next) {
        if (texture() == next) return;
        setTexture(next);
    }
    quint64 revision = 0;
};

class ProcessingExecutor final {
public:
    ProcessingExecutor() {
        pool.setMaxThreadCount(1);
        pool.setExpiryTimeout(-1);
        pool.setObjectName(QStringLiteral("Carton analysis executor"));
    }

    QThreadPool pool;
};

QThreadPool* processingThreadPool() {
    static ProcessingExecutor executor;
    return &executor.pool;
}

void setVertex(QSGGeometry::ColoredPoint2D& vertex, float x, float y, const QColor& color) {
    vertex.set(x, y, static_cast<uchar>(color.red()), static_cast<uchar>(color.green()),
               static_cast<uchar>(color.blue()), static_cast<uchar>(color.alpha()));
}

QString normalizedOperation(QString value) {
    value = value.trimmed().toLower();
    value.replace(QLatin1Char(' '), QLatin1Char('-'));
    if (value == QStringLiteral("gaussian-smoothing")) return QStringLiteral("gaussian");
    if (value == QStringLiteral("difference-of-gaussians")) return QStringLiteral("dog");
    if (value == QStringLiteral("laplacian-of-gaussian")) return QStringLiteral("log");
    if (value == QStringLiteral("steerable-filter")) return QStringLiteral("steerable");
    if (value == QStringLiteral("gabor-filter")) return QStringLiteral("gabor");
    if (value == QStringLiteral("local-binary-pattern")) return QStringLiteral("lbp");
    if (value == QStringLiteral("polar-transform")) return QStringLiteral("polar");
    return value;
}
}

AnalysisItemBase::AnalysisItemBase(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}

HicDataController* AnalysisItemBase::controller() const { return m_controller; }

void AnalysisItemBase::setController(HicDataController* controller) {
    if (m_controller == controller) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &HicDataController::recordsChanged, this, &QQuickItem::update);
        connect(m_controller, &HicDataController::viewChanged, this, &QQuickItem::update);
        connect(m_controller, &HicDataController::colorMaxChanged, this, &QQuickItem::update);
        connect(m_controller, &HicDataController::colorMapChanged, this, &QQuickItem::update);
    }
    emit controllerChanged();
    update();
}

QColor AnalysisItemBase::colorForValue(float value, float minimum, float maximum) const {
    if (!std::isfinite(value)) return m_controller ? m_controller->missingValueColor() : QColor("#4b5563");
    double low = maximum > minimum ? minimum : (m_controller ? m_controller->colorMin() : 0.0);
    double high = maximum > minimum ? maximum : (m_controller ? m_controller->colorMax() : 50.0);
    if (high <= low) high = low + 1.0;
    if (low < 0.0 && high > 0.0) {
        if (value >= 0.0f) {
            const double t = std::clamp(value / high, 0.0, 1.0);
            return QColor(255, static_cast<int>(255 * (1.0 - t)), static_cast<int>(255 * (1.0 - t)), 245);
        }
        const double t = std::clamp(value / low, 0.0, 1.0);
        return QColor(static_cast<int>(255 * (1.0 - t)), static_cast<int>(255 * (1.0 - t)), 255, 245);
    }
    const double t = std::clamp((static_cast<double>(value) - low) / (high - low), 0.0, 1.0);
    return QColor(255, static_cast<int>(255.0 * (1.0 - t)), static_cast<int>(255.0 * (1.0 - t)), 245);
}

RotatedHeatmapItem::RotatedHeatmapItem(QQuickItem* parent) : AnalysisItemBase(parent) {
    connect(this, &RotatedHeatmapItem::settingsChanged, this, &QQuickItem::update);
}
qint64 RotatedHeatmapItem::maxDistance() const { return m_maxDistance; }
void RotatedHeatmapItem::setMaxDistance(qint64 value) {
    value = std::max<qint64>(1, value);
    if (m_maxDistance == value) return;
    m_maxDistance = value; emit settingsChanged();
}
bool RotatedHeatmapItem::flipped() const { return m_flipped; }
void RotatedHeatmapItem::setFlipped(bool value) {
    if (m_flipped == value) return;
    m_flipped = value; emit settingsChanged();
}

QSGNode* RotatedHeatmapItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* node = static_cast<ColoredGeometryNode*>(oldNode);
    if (!node) node = new ColoredGeometryNode(QSGGeometry::DrawTriangles);
    auto* geometry = node->geometry();
    if (!m_controller || width() <= 0 || height() <= 0 || m_controller->chrX() != m_controller->chrY()) {
        if (node->vertexCapacity > 0) {
            geometry->allocate(0);
            node->vertexCapacity = 0;
        } else {
            geometry->setVertexCount(0);
        }
        node->markDirty(QSGNode::DirtyGeometry);
        return node;
    }
    std::vector<contactRecord> records;
    int resolution = std::max(1, m_controller->resolution());
    try {
        m_controller->rotatedRecordsSnapshot(
            records, resolution, m_maxDistance,
            std::clamp(static_cast<int>(std::ceil(width())), 1, 16384),
            std::clamp(static_cast<int>(std::ceil(height())), 1, 16384),
            kMaxRotatedRecords);
    } catch (const std::bad_alloc&) {
        geometry->allocate(0);
        node->vertexCapacity = 0;
        node->markDirty(QSGNode::DirtyGeometry);
        return node;
    }
    const qint64 viewStart = std::min(m_controller->x0(), m_controller->y0());
    const qint64 viewEnd = std::max(m_controller->x1(), m_controller->y1());
    const double span = std::max<qint64>(1, viewEnd - viewStart);
    const int requiredVertices = static_cast<int>(records.size()) * 6;
    if (requiredVertices > node->vertexCapacity || requiredVertices < node->vertexCapacity / 3) {
        geometry->allocate(requiredVertices);
        node->vertexCapacity = requiredVertices;
    } else {
        geometry->setVertexCount(requiredVertices);
    }
    auto* vertices = geometry->vertexDataAsColoredPoint2D();
    int vi = 0;
    const double halfWidth = std::max(0.5, width() * resolution / span * 0.7);
    const double binHeight = std::max(0.5, height() * resolution / static_cast<double>(m_maxDistance));
    for (const contactRecord& record : records) {
        const double midpoint = (static_cast<double>(record.binX) + record.binY + resolution) * 0.5;
        const double distance = std::abs(static_cast<double>(record.binY) - record.binX);
        const double cx = (midpoint - viewStart) / span * width();
        double cy = std::clamp(distance / static_cast<double>(m_maxDistance), 0.0, 1.0) * height();
        if (m_flipped) cy = height() - cy;
        const double vertical = m_flipped ? -binHeight : binHeight;
        const QColor color = colorForValue(record.counts);
        setVertex(vertices[vi++], static_cast<float>(cx - halfWidth), static_cast<float>(cy), color);
        setVertex(vertices[vi++], static_cast<float>(cx), static_cast<float>(cy + vertical), color);
        setVertex(vertices[vi++], static_cast<float>(cx + halfWidth), static_cast<float>(cy), color);
        setVertex(vertices[vi++], static_cast<float>(cx - halfWidth), static_cast<float>(cy), color);
        setVertex(vertices[vi++], static_cast<float>(cx), static_cast<float>(cy - vertical), color);
        setVertex(vertices[vi++], static_cast<float>(cx + halfWidth), static_cast<float>(cy), color);
    }
    geometry->setVertexCount(vi);
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

BullseyeItem::BullseyeItem(QQuickItem* parent) : AnalysisItemBase(parent) {
    connect(this, &BullseyeItem::settingsChanged, this, &QQuickItem::update);
}
qint64 BullseyeItem::centerX() const { return m_centerX; }
void BullseyeItem::setCenterX(qint64 value) { if (m_centerX != value) { m_centerX = value; emit settingsChanged(); } }
qint64 BullseyeItem::centerY() const { return m_centerY; }
void BullseyeItem::setCenterY(qint64 value) { if (m_centerY != value) { m_centerY = value; emit settingsChanged(); } }
int BullseyeItem::radiusBins() const { return m_radiusBins; }
void BullseyeItem::setRadiusBins(int value) {
    value = std::clamp(value, 1, 100);
    if (m_radiusBins != value) { m_radiusBins = value; emit settingsChanged(); }
}

QSGNode* BullseyeItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* node = static_cast<ColoredGeometryNode*>(oldNode);
    if (!node) node = new ColoredGeometryNode(QSGGeometry::DrawTriangles);
    auto* geometry = node->geometry();
    if (!m_controller || width() <= 0 || height() <= 0) {
        geometry->allocate(0); node->markDirty(QSGNode::DirtyGeometry); return node;
    }
    const auto pixels = MatrixAnalysis::bullseye(m_controller->analysisRecordsSnapshot(),
                                                  std::max(1, m_controller->resolution()),
                                                  m_centerX, m_centerY, m_radiusBins,
                                                  m_controller->chrX() == m_controller->chrY());
    geometry->allocate(static_cast<int>(pixels.size() * 6));
    auto* vertices = geometry->vertexDataAsColoredPoint2D();
    int vi = 0;
    const double cx = width() * 0.5;
    const double cy = height() * 0.5;
    const double scale = std::min(width(), height()) * 0.46 / std::max(1, m_radiusBins);
    for (const auto& pixel : pixels) {
        const double inner = std::max(0.0, pixel.radius - 0.48) * scale;
        const double outer = (pixel.radius + 0.48) * scale;
        const double halfAngle = pixel.radius < 0.5 ? kPi : std::min(kPi, 0.48 / pixel.radius);
        const double a0 = pixel.angle - halfAngle;
        const double a1 = pixel.angle + halfAngle;
        const QColor color = pixel.present ? colorForValue(pixel.value) : QColor("#eef0f3");
        const QPointF p0(cx + std::cos(a0) * inner, cy + std::sin(a0) * inner);
        const QPointF p1(cx + std::cos(a0) * outer, cy + std::sin(a0) * outer);
        const QPointF p2(cx + std::cos(a1) * outer, cy + std::sin(a1) * outer);
        const QPointF p3(cx + std::cos(a1) * inner, cy + std::sin(a1) * inner);
        setVertex(vertices[vi++], p0.x(), p0.y(), color);
        setVertex(vertices[vi++], p1.x(), p1.y(), color);
        setVertex(vertices[vi++], p2.x(), p2.y(), color);
        setVertex(vertices[vi++], p0.x(), p0.y(), color);
        setVertex(vertices[vi++], p2.x(), p2.y(), color);
        setVertex(vertices[vi++], p3.x(), p3.y(), color);
    }
    geometry->setVertexCount(vi);
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

Virtual4CItem::Virtual4CItem(QQuickItem* parent) : AnalysisItemBase(parent) {
    connect(this, &Virtual4CItem::settingsChanged, this, &QQuickItem::update);
}
qint64 Virtual4CItem::anchor() const { return m_anchor; }
void Virtual4CItem::setAnchor(qint64 value) { if (m_anchor != value) { m_anchor = value; emit settingsChanged(); } }
QString Virtual4CItem::axis() const { return m_axis; }
void Virtual4CItem::setAxis(const QString& value) {
    const QString next = value.trimmed().toLower() == QStringLiteral("column")
        ? QStringLiteral("column") : QStringLiteral("row");
    if (m_axis != next) { m_axis = next; emit settingsChanged(); }
}

QSGNode* Virtual4CItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* node = static_cast<ColoredGeometryNode*>(oldNode);
    if (!node) node = new ColoredGeometryNode(QSGGeometry::DrawLineStrip);
    auto* geometry = node->geometry();
    if (!m_controller || width() <= 0 || height() <= 0) {
        geometry->allocate(0); node->markDirty(QSGNode::DirtyGeometry); return node;
    }
    const bool anchorOnX = m_axis == QStringLiteral("column");
    const auto values = MatrixAnalysis::virtual4C(
        m_controller->analysisRecordsSnapshot(), std::max(1, m_controller->resolution()), m_anchor,
        anchorOnX ? m_controller->y0() : m_controller->x0(),
        anchorOnX ? m_controller->y1() : m_controller->x1(),
        m_controller->chrX() == m_controller->chrY(), anchorOnX);
    geometry->allocate(static_cast<int>(values.size()));
    auto* vertices = geometry->vertexDataAsColoredPoint2D();
    const float maximum = values.empty() ? 1.0f : std::max(1e-6f, *std::max_element(values.begin(), values.end()));
    const QColor color("#3478f6");
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float x = values.size() <= 1 ? 0.0f : static_cast<float>(i) / (values.size() - 1) * width();
        const float y = height() - std::clamp(values[i] / maximum, 0.0f, 1.0f) * height();
        setVertex(vertices[i], x, y, color);
    }
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

ProcessedHeatmapItem::ProcessedHeatmapItem(QQuickItem* parent) : AnalysisItemBase(parent) {
    connect(this, &ProcessedHeatmapItem::settingsChanged, this, &ProcessedHeatmapItem::scheduleProcessing);
    connect(this, &AnalysisItemBase::controllerChanged, this, [this]() {
        if (m_controller) {
            connect(m_controller, &HicDataController::recordsChanged,
                    this, &ProcessedHeatmapItem::scheduleProcessing);
            connect(m_controller, &HicDataController::viewChanged,
                    this, &ProcessedHeatmapItem::scheduleProcessing);
        }
        scheduleProcessing();
    });
    connect(&m_processingWatcher, &QFutureWatcher<ProcessingResult>::finished, this, [this]() {
        ProcessingResult result;
        try {
            result = m_processingWatcher.result();
        } catch (const std::bad_alloc&) {
            result.generation = m_processingGeneration;
            result.error = QStringLiteral("The analysis ran out of memory. Zoom in or lower the processing limit.");
        } catch (const std::exception& exception) {
            result.generation = m_processingGeneration;
            result.error = QStringLiteral("The analysis failed: %1").arg(QString::fromUtf8(exception.what()));
        } catch (...) {
            result.generation = m_processingGeneration;
            result.error = QStringLiteral("The analysis failed unexpectedly.");
        }
        if (result.generation == m_processingGeneration) {
            m_processedImage = result.image;
            m_errorString = result.error;
            m_resultMin = result.minimum;
            m_resultMax = result.maximum;
            ++m_imageRevision;
            emit resultChanged();
            update();
        }
        if (m_processingPending || result.generation != m_processingGeneration) {
            m_processingPending = false;
            startProcessing();
        }
    });
}
QString ProcessedHeatmapItem::operation() const { return m_operation; }
void ProcessedHeatmapItem::setOperation(const QString& value) {
    const QString next = normalizedOperation(value);
    if (m_operation != next) { m_operation = next; emit settingsChanged(); }
}
double ProcessedHeatmapItem::parameter() const { return m_parameter; }
void ProcessedHeatmapItem::setParameter(double value) {
    value = std::isfinite(value) ? std::clamp(value, 0.1, 100.0) : 1.0;
    if (!qFuzzyCompare(m_parameter, value)) { m_parameter = value; emit settingsChanged(); }
}
double ProcessedHeatmapItem::threshold() const { return m_threshold; }
void ProcessedHeatmapItem::setThreshold(double value) {
    if (!std::isfinite(value)) value = 0.0;
    if (!qFuzzyCompare(m_threshold, value)) { m_threshold = value; emit settingsChanged(); }
}
int ProcessedHeatmapItem::maximumBins() const { return m_maximumBins; }
void ProcessedHeatmapItem::setMaximumBins(int value) {
    value = std::clamp(value, 32, 1024);
    if (m_maximumBins != value) { m_maximumBins = value; emit settingsChanged(); }
}
QString ProcessedHeatmapItem::errorString() const { return m_errorString; }
double ProcessedHeatmapItem::resultMin() const { return m_resultMin; }
double ProcessedHeatmapItem::resultMax() const { return m_resultMax; }

void ProcessedHeatmapItem::scheduleProcessing() {
    ++m_processingGeneration;
    if (m_processingWatcher.isRunning()) {
        m_processingPending = true;
        return;
    }
    startProcessing();
}

void ProcessedHeatmapItem::startProcessing() {
    if (!m_controller) {
        m_processedImage = {};
        m_errorString.clear();
        ++m_imageRevision;
        update();
        return;
    }
    const quint64 generation = m_processingGeneration;
    std::vector<contactRecord> records;
    try {
        records = m_controller->analysisRecordsSnapshot();
    } catch (const std::bad_alloc&) {
        m_processedImage = {};
        m_errorString = QStringLiteral("The analysis input is too large. Zoom in before processing.");
        ++m_imageRevision;
        emit resultChanged();
        update();
        return;
    }
    const int resolution = std::max(1, m_controller->resolution());
    const qint64 x0 = m_controller->x0();
    const qint64 x1 = m_controller->x1();
    const qint64 y0 = m_controller->y0();
    const qint64 y1 = m_controller->y1();
    const bool reflect = m_controller->chrX() == m_controller->chrY();
    const int maximumBins = m_maximumBins;
    const QString operation = m_operation;
    const double parameter = m_parameter;
    const double threshold = m_threshold;
    try {
        m_processingWatcher.setFuture(QtConcurrent::run(
            processingThreadPool(),
            [generation, records = std::move(records), resolution, x0, x1, y0, y1, reflect, maximumBins,
             operation, parameter, threshold]() {
                ProcessingResult rendered;
                rendered.generation = generation;
                try {
                    const auto input = MatrixAnalysis::makeDense(records, resolution, x0, x1, y0, y1,
                                                                 reflect, maximumBins);
                    const auto result = MatrixAnalysis::process(input, operation, parameter, threshold);
                    rendered.error = result.error;
                    rendered.minimum = result.minimum;
                    rendered.maximum = result.maximum;
                    if (!result.valid()) return rendered;
                    rendered.image = QImage(result.width, result.height, QImage::Format_RGBA8888);
                    if (rendered.image.isNull()) {
                        rendered.error = QStringLiteral("Could not allocate the processed image.");
                        return rendered;
                    }
                    const double low = result.minimum;
                    const double high = result.maximum > result.minimum ? result.maximum : result.minimum + 1.0;
                    for (int y = 0; y < result.height; ++y) for (int x = 0; x < result.width; ++x) {
                        const float value = result.values[static_cast<std::size_t>(y * result.width + x)];
                        QColor color;
                        if (!std::isfinite(value)) color = QColor("#4b5563");
                        else if (low < 0.0 && high > 0.0) {
                            if (value >= 0.0f) {
                                const double t = std::clamp(value / high, 0.0, 1.0);
                                color = QColor(255, static_cast<int>(255 * (1.0 - t)),
                                               static_cast<int>(255 * (1.0 - t)), 255);
                            } else {
                                const double t = std::clamp(value / low, 0.0, 1.0);
                                color = QColor(static_cast<int>(255 * (1.0 - t)),
                                               static_cast<int>(255 * (1.0 - t)), 255, 255);
                            }
                        } else {
                            const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
                            color = QColor(255, static_cast<int>(255 * (1.0 - t)),
                                           static_cast<int>(255 * (1.0 - t)), 255);
                        }
                        rendered.image.setPixelColor(x, y, color);
                    }
                } catch (const std::bad_alloc&) {
                    rendered.image = {};
                    rendered.error = QStringLiteral("The analysis ran out of memory. Zoom in or lower the processing limit.");
                } catch (const std::exception& exception) {
                    rendered.image = {};
                    rendered.error = QStringLiteral("The analysis failed: %1").arg(QString::fromUtf8(exception.what()));
                } catch (...) {
                    rendered.image = {};
                    rendered.error = QStringLiteral("The analysis failed unexpectedly.");
                }
                return rendered;
            }));
    } catch (const std::bad_alloc&) {
        m_processedImage = {};
        m_errorString = QStringLiteral("The analysis could not be queued because memory is exhausted.");
        ++m_imageRevision;
        emit resultChanged();
        update();
    } catch (const std::exception& exception) {
        m_processedImage = {};
        m_errorString = QStringLiteral("The analysis could not be queued: %1")
                            .arg(QString::fromUtf8(exception.what()));
        ++m_imageRevision;
        emit resultChanged();
        update();
    }
}

QSGNode* ProcessedHeatmapItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* node = static_cast<OwnedTextureNode*>(oldNode);
    if (!window() || width() <= 0 || height() <= 0 || m_processedImage.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) node = new OwnedTextureNode;
    if (node->revision != m_imageRevision) {
        QSGTexture* texture = window()->createTextureFromImage(m_processedImage);
        if (!texture) {
            delete node;
            return nullptr;
        }
        node->replaceTexture(texture);
        node->revision = m_imageRevision;
    }
    node->setRect(boundingRect());
    node->setFiltering(QSGTexture::Linear);
    return node;
}
