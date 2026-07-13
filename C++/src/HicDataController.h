#ifndef CARTON_HIC_DATA_CONTROLLER_H
#define CARTON_HIC_DATA_CONTROLLER_H

#include <QFutureWatcher>
#include <QMutex>
#include <QObject>
#include <QColor>
#include <QUrl>
#include <QVariantList>

#include <memory>
#include <vector>

#include "HicTileCache.h"
#include "straw.h"

class HicDataController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString genomeId READ genomeId NOTIFY metadataChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString chrX READ chrX WRITE setChrX NOTIFY viewChanged)
    Q_PROPERTY(QString chrY READ chrY WRITE setChrY NOTIFY viewChanged)
    Q_PROPERTY(QString matrixType READ matrixType WRITE setMatrixType NOTIFY viewChanged)
    Q_PROPERTY(QString norm READ norm WRITE setNorm NOTIFY viewChanged)
    Q_PROPERTY(int resolution READ resolution WRITE setResolution NOTIFY viewChanged)
    Q_PROPERTY(qint64 x0 READ x0 WRITE setX0 NOTIFY viewChanged)
    Q_PROPERTY(qint64 x1 READ x1 WRITE setX1 NOTIFY viewChanged)
    Q_PROPERTY(qint64 y0 READ y0 WRITE setY0 NOTIFY viewChanged)
    Q_PROPERTY(qint64 y1 READ y1 WRITE setY1 NOTIFY viewChanged)
    Q_PROPERTY(int recordCount READ recordCount NOTIFY recordsChanged)
    Q_PROPERTY(double colorMax READ colorMax WRITE setColorMax NOTIFY colorMaxChanged)
    Q_PROPERTY(QString colorMap READ colorMap WRITE setColorMap NOTIFY colorMapChanged)
    Q_PROPERTY(QColor customLowColor READ customLowColor WRITE setCustomLowColor NOTIFY colorMapChanged)
    Q_PROPERTY(QColor customHighColor READ customHighColor WRITE setCustomHighColor NOTIFY colorMapChanged)

public:
    explicit HicDataController(QObject* parent = nullptr);
    ~HicDataController() override;

    QString filePath() const;
    QString genomeId() const;
    QString status() const;
    bool busy() const;
    QString chrX() const;
    QString chrY() const;
    QString matrixType() const;
    QString norm() const;
    int resolution() const;
    qint64 x0() const;
    qint64 x1() const;
    qint64 y0() const;
    qint64 y1() const;
    int recordCount() const;
    double colorMax() const;
    QString colorMap() const;
    QColor customLowColor() const;
    QColor customHighColor() const;

    Q_INVOKABLE void openFile(const QUrl& url);
    Q_INVOKABLE QVariantList chromosomeNames() const;
    Q_INVOKABLE QVariantList resolutions() const;
    Q_INVOKABLE QVariantList normalizations() const;
    Q_INVOKABLE void requestVisibleRegion();
    Q_INVOKABLE void zoom(double factor, double centerX, double centerY);
    Q_INVOKABLE void pan(double dxFraction, double dyFraction);
    Q_INVOKABLE void resetView();

    void setChrX(const QString& value);
    void setChrY(const QString& value);
    void setMatrixType(const QString& value);
    void setNorm(const QString& value);
    void setResolution(int value);
    void setX0(qint64 value);
    void setX1(qint64 value);
    void setY0(qint64 value);
    void setY1(qint64 value);
    void setColorMax(double value);
    void setColorMap(const QString& value);
    void setCustomLowColor(const QColor& value);
    void setCustomHighColor(const QColor& value);

    std::vector<contactRecord> recordsSnapshot() const;

signals:
    void filePathChanged();
    void metadataChanged();
    void statusChanged();
    void busyChanged();
    void viewChanged();
    void recordsChanged();
    void colorMaxChanged();
    void colorMapChanged();

private:
    struct TileResult {
        quint64 requestId = 0;
        HicTile tile;
        QString error;
        bool fromCache = false;
    };

    static QString localPathFromUrl(const QUrl& url);
    static HicTileKey makeKey(const QString& filePath, const QString& matrixType, const QString& norm,
                              const QString& chrX, const QString& chrY, int resolution,
                              qint64 x0, qint64 x1, qint64 y0, qint64 y1);

    void setStatus(const QString& value);
    void setBusy(bool value);
    void applyMetadata(const HicFileMetadata& metadata);
    chromosome chromosomeByName(const QString& name) const;
    qint64 chromosomeLength(const QString& name) const;
    void clampRegion();
    void orientTileForRequestedAxes(HicTile& tile) const;
    void scheduleRequest();
    void startTileLoad(const HicTileKey& key, quint64 requestId);

    mutable QMutex m_mutex;
    QString m_filePath;
    QString m_genomeId;
    QString m_status = "Open a .hic file to begin.";
    bool m_busy = false;
    QString m_chrX;
    QString m_chrY;
    QString m_matrixType = "observed";
    QString m_norm = "NONE";
    QString m_unit = "BP";
    int m_resolution = 0;
    qint64 m_x0 = 0;
    qint64 m_x1 = 0;
    qint64 m_y0 = 0;
    qint64 m_y1 = 0;
    double m_colorMax = 50.0;
    QString m_colorMap = "White-Red";
    QColor m_customLowColor = QColor("#ffffff");
    QColor m_customHighColor = QColor("#d7191c");
    HicFileMetadata m_metadata;
    std::vector<contactRecord> m_records;
    std::unique_ptr<HicTileCache> m_cache;
    QFutureWatcher<HicFileMetadata> m_metadataWatcher;
    QFutureWatcher<TileResult> m_tileWatcher;
    quint64 m_requestSerial = 0;
    bool m_reloadPending = false;
};

#endif
