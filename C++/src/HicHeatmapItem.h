#ifndef CARTON_HIC_HEATMAP_ITEM_H
#define CARTON_HIC_HEATMAP_ITEM_H

#include <QPointer>
#include <QQuickItem>

#include "HicDataController.h"

class HicHeatmapItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(HicDataController* controller READ controller WRITE setController NOTIFY controllerChanged)

public:
    explicit HicHeatmapItem(QQuickItem* parent = nullptr);

    HicDataController* controller() const;
    void setController(HicDataController* controller);

signals:
    void controllerChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QColor colorForValue(float value) const;

    QPointer<HicDataController> m_controller;
    QPointF m_lastMousePosition;
    bool m_dragging = false;
};

#endif
