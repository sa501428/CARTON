#ifndef CARTON_ANALYSIS_ITEMS_H
#define CARTON_ANALYSIS_ITEMS_H

#include <QPointer>
#include <QQuickItem>
#include <QFutureWatcher>
#include <QImage>

#include "HicDataController.h"

class AnalysisItemBase : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(HicDataController* controller READ controller WRITE setController NOTIFY controllerChanged)

public:
    explicit AnalysisItemBase(QQuickItem* parent = nullptr);
    HicDataController* controller() const;
    void setController(HicDataController* controller);

signals:
    void controllerChanged();

protected:
    QColor colorForValue(float value, float minimum = 0.0f, float maximum = 0.0f) const;
    QPointer<HicDataController> m_controller;
};

class RotatedHeatmapItem : public AnalysisItemBase {
    Q_OBJECT
    Q_PROPERTY(qint64 maxDistance READ maxDistance WRITE setMaxDistance NOTIFY settingsChanged)
    Q_PROPERTY(bool flipped READ flipped WRITE setFlipped NOTIFY settingsChanged)

public:
    explicit RotatedHeatmapItem(QQuickItem* parent = nullptr);
    qint64 maxDistance() const;
    void setMaxDistance(qint64 value);
    bool flipped() const;
    void setFlipped(bool value);

signals:
    void settingsChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    qint64 m_maxDistance = 2000000;
    bool m_flipped = false;
};

class BullseyeItem : public AnalysisItemBase {
    Q_OBJECT
    Q_PROPERTY(qint64 centerX READ centerX WRITE setCenterX NOTIFY settingsChanged)
    Q_PROPERTY(qint64 centerY READ centerY WRITE setCenterY NOTIFY settingsChanged)
    Q_PROPERTY(int radiusBins READ radiusBins WRITE setRadiusBins NOTIFY settingsChanged)

public:
    explicit BullseyeItem(QQuickItem* parent = nullptr);
    qint64 centerX() const;
    void setCenterX(qint64 value);
    qint64 centerY() const;
    void setCenterY(qint64 value);
    int radiusBins() const;
    void setRadiusBins(int value);

signals:
    void settingsChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    qint64 m_centerX = 0;
    qint64 m_centerY = 0;
    int m_radiusBins = 12;
};

class Virtual4CItem : public AnalysisItemBase {
    Q_OBJECT
    Q_PROPERTY(qint64 anchor READ anchor WRITE setAnchor NOTIFY settingsChanged)
    Q_PROPERTY(QString axis READ axis WRITE setAxis NOTIFY settingsChanged)

public:
    explicit Virtual4CItem(QQuickItem* parent = nullptr);
    qint64 anchor() const;
    void setAnchor(qint64 value);
    QString axis() const;
    void setAxis(const QString& value);

signals:
    void settingsChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    qint64 m_anchor = 0;
    QString m_axis = QStringLiteral("row");
};

class ProcessedHeatmapItem : public AnalysisItemBase {
    Q_OBJECT
    Q_PROPERTY(QString operation READ operation WRITE setOperation NOTIFY settingsChanged)
    Q_PROPERTY(double parameter READ parameter WRITE setParameter NOTIFY settingsChanged)
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold NOTIFY settingsChanged)
    Q_PROPERTY(int maximumBins READ maximumBins WRITE setMaximumBins NOTIFY settingsChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY resultChanged)
    Q_PROPERTY(double resultMin READ resultMin NOTIFY resultChanged)
    Q_PROPERTY(double resultMax READ resultMax NOTIFY resultChanged)

public:
    explicit ProcessedHeatmapItem(QQuickItem* parent = nullptr);
    QString operation() const;
    void setOperation(const QString& value);
    double parameter() const;
    void setParameter(double value);
    double threshold() const;
    void setThreshold(double value);
    int maximumBins() const;
    void setMaximumBins(int value);
    QString errorString() const;
    double resultMin() const;
    double resultMax() const;

signals:
    void settingsChanged();
    void resultChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    struct ProcessingResult {
        quint64 generation = 0;
        QImage image;
        QString error;
        double minimum = 0.0;
        double maximum = 0.0;
    };
    void scheduleProcessing();
    void startProcessing();

    QString m_operation = QStringLiteral("gradient-magnitude");
    double m_parameter = 1.0;
    double m_threshold = 0.0;
    int m_maximumBins = 512;
    QString m_errorString;
    double m_resultMin = 0.0;
    double m_resultMax = 0.0;
    QFutureWatcher<ProcessingResult> m_processingWatcher;
    quint64 m_processingGeneration = 0;
    quint64 m_imageRevision = 0;
    bool m_processingPending = false;
    QImage m_processedImage;
};

#endif
