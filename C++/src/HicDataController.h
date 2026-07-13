#ifndef CARTON_HIC_DATA_CONTROLLER_H
#define CARTON_HIC_DATA_CONTROLLER_H

#include <QFutureWatcher>
#include <QMutex>
#include <QObject>
#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QUrl>
#include <QVariantMap>
#include <QVariantList>

#include <memory>
#include <vector>

#include "HicTileCache.h"
#include "straw.h"

class HicDataController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString controlFilePath READ controlFilePath NOTIFY controlFilePathChanged)
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
    Q_PROPERTY(double colorMin READ colorMin WRITE setColorMin NOTIFY colorMaxChanged)
    Q_PROPERTY(bool colorMaxAuto READ colorMaxAuto NOTIFY colorMaxChanged)
    Q_PROPERTY(QString colorMap READ colorMap WRITE setColorMap NOTIFY colorMapChanged)
    Q_PROPERTY(QColor customLowColor READ customLowColor WRITE setCustomLowColor NOTIFY colorMapChanged)
    Q_PROPERTY(QColor customHighColor READ customHighColor WRITE setCustomHighColor NOTIFY colorMapChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY tracksChanged)
    Q_PROPERTY(int annotationCount READ annotationCount NOTIFY annotationsChanged)
    Q_PROPERTY(bool canUndoView READ canUndoView NOTIFY viewHistoryChanged)
    Q_PROPERTY(bool canRedoView READ canRedoView NOTIFY viewHistoryChanged)
    Q_PROPERTY(bool resolutionLocked READ resolutionLocked WRITE setResolutionLocked NOTIFY viewChanged)
    Q_PROPERTY(bool wholeGenomeView READ wholeGenomeView NOTIFY viewChanged)
    Q_PROPERTY(bool axisEndpointsOnly READ axisEndpointsOnly WRITE setAxisEndpointsOnly NOTIFY displayOptionsChanged)
    Q_PROPERTY(bool showGridlines READ showGridlines WRITE setShowGridlines NOTIFY displayOptionsChanged)
    Q_PROPERTY(bool showChromosomeContext READ showChromosomeContext WRITE setShowChromosomeContext NOTIFY displayOptionsChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY displayOptionsChanged)
    Q_PROPERTY(bool showTilesDebug READ showTilesDebug WRITE setShowTilesDebug NOTIFY displayOptionsChanged)
    Q_PROPERTY(int sparseFeatureLimit READ sparseFeatureLimit WRITE setSparseFeatureLimit NOTIFY annotationsChanged)
    Q_PROPERTY(QString selectedAnnotationId READ selectedAnnotationId NOTIFY annotationsChanged)

public:
    explicit HicDataController(QObject* parent = nullptr);
    ~HicDataController() override;

    QString filePath() const;
    QString controlFilePath() const;
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
    double colorMin() const;
    bool colorMaxAuto() const;
    QString colorMap() const;
    QColor customLowColor() const;
    QColor customHighColor() const;
    int trackCount() const;
    int annotationCount() const;
    bool canUndoView() const;
    bool canRedoView() const;
    bool resolutionLocked() const;
    bool wholeGenomeView() const;
    bool axisEndpointsOnly() const;
    bool showGridlines() const;
    bool showChromosomeContext() const;
    bool darkMode() const;
    bool showTilesDebug() const;
    int sparseFeatureLimit() const;
    QString selectedAnnotationId() const;

    Q_INVOKABLE void openFile(const QUrl& url);
    Q_INVOKABLE void openControlFile(const QUrl& url);
    Q_INVOKABLE void openRecentMap(const QString& path);
    Q_INVOKABLE void openRecentControlMap(const QString& path);
    Q_INVOKABLE void loadTrack(const QUrl& url);
    Q_INVOKABLE void loadTrackFromPath(const QString& path);
    Q_INVOKABLE void loadAnnotations(const QUrl& url);
    Q_INVOKABLE void loadAnnotationsFromPath(const QString& path);
    Q_INVOKABLE void clearTracks();
    Q_INVOKABLE void clearAnnotations();
    Q_INVOKABLE QVariantList recentMaps() const;
    Q_INVOKABLE QVariantList recentControlMaps() const;
    Q_INVOKABLE QVariantList savedLocations() const;
    Q_INVOKABLE QVariantList savedStates() const;
    Q_INVOKABLE QVariantList chromosomeNames() const;
    Q_INVOKABLE QVariantList chromosomeBoundaries() const;
    Q_INVOKABLE QVariantList resolutions() const;
    Q_INVOKABLE QVariantList normalizations() const;
    Q_INVOKABLE QVariantList matrixTypes() const;
    Q_INVOKABLE QVariantList trackSummaries() const;
    Q_INVOKABLE QVariantList annotationLayerSummaries() const;
    Q_INVOKABLE QVariantList visibleTrackSegments(bool xAxis) const;
    Q_INVOKABLE QVariantList visibleAnnotations() const;
    Q_INVOKABLE QString positionText(double xFraction, double yFraction) const;
    Q_INVOKABLE void copyPosition(double xFraction, double yFraction) const;
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE void copyTopPosition(double xFraction) const;
    Q_INVOKABLE void copyLeftPosition(double yFraction) const;
    Q_INVOKABLE void jumpToDiagonal(double xFraction, double yFraction);
    Q_INVOKABLE void goTo(const QString& xLocation, const QString& yLocation);
    Q_INVOKABLE void saveCurrentLocation(const QString& name);
    Q_INVOKABLE void restoreSavedLocation(int index);
    Q_INVOKABLE void saveCurrentState(const QString& name);
    Q_INVOKABLE void restoreSavedState(int index);
    Q_INVOKABLE void exportState(const QUrl& url) const;
    Q_INVOKABLE void importState(const QUrl& url);
    Q_INVOKABLE void exportFigurePdf(const QUrl& url, int width, int height) const;
    Q_INVOKABLE void renameGenome(const QString& genomeId);
    Q_INVOKABLE void setWholeGenomeView();
    Q_INVOKABLE void undoView();
    Q_INVOKABLE void redoView();
    Q_INVOKABLE void beginInteraction();
    Q_INVOKABLE void endInteraction();
    Q_INVOKABLE void resetColorScale();
    Q_INVOKABLE void zoomToFractions(double xStartFraction, double yStartFraction,
                                     double xEndFraction, double yEndFraction);
    Q_INVOKABLE void requestVisibleRegion();
    Q_INVOKABLE void zoom(double factor, double centerX, double centerY);
    Q_INVOKABLE void pan(double dxFraction, double dyFraction);
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void setTrackName(int index, const QString& name);
    Q_INVOKABLE void setTrackColor(int index, const QColor& positiveColor, const QColor& negativeColor);
    Q_INVOKABLE void setTrackRange(int index, double minValue, double maxValue, bool logScale);
    Q_INVOKABLE void setTrackReduction(int index, const QString& reduction);
    Q_INVOKABLE void moveTrack(int from, int to);
    Q_INVOKABLE void removeTrack(int index);
    Q_INVOKABLE void addAnnotationLayer(const QString& name);
    Q_INVOKABLE void duplicateAnnotationLayer(int index);
    Q_INVOKABLE void mergeVisibleAnnotationLayers(const QString& name);
    Q_INVOKABLE void removeAnnotationLayer(int index);
    Q_INVOKABLE void clearAnnotationLayer(int index);
    Q_INVOKABLE void moveAnnotationLayer(int from, int to);
    Q_INVOKABLE void setAnnotationLayerVisible(int index, bool visible);
    Q_INVOKABLE void setAnnotationLayerTransparent(int index, bool transparent);
    Q_INVOKABLE void setAnnotationLayerSparse(int index, bool sparse);
    Q_INVOKABLE void setAnnotationLayerEnlarged(int index, bool enlarged);
    Q_INVOKABLE void setAnnotationLayerColor(int index, const QColor& color);
    Q_INVOKABLE void setActiveAnnotationLayer(int index);
    Q_INVOKABLE void exportAnnotationLayer(int index, const QUrl& url) const;
    Q_INVOKABLE void addAnnotationFromFractions(double xStartFraction, double yStartFraction,
                                                double xEndFraction, double yEndFraction);
    Q_INVOKABLE void selectAnnotationAt(double xFraction, double yFraction);
    Q_INVOKABLE void deleteSelectedAnnotation();
    Q_INVOKABLE void setSelectedAnnotationColor(const QColor& color);
    Q_INVOKABLE void setSelectedAnnotationAttribute(const QString& key, const QString& value);
    Q_INVOKABLE void toggleSelectedAnnotationHighlight();

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
    void setColorMin(double value);
    void setColorMap(const QString& value);
    void setCustomLowColor(const QColor& value);
    void setCustomHighColor(const QColor& value);
    void setResolutionLocked(bool value);
    void setAxisEndpointsOnly(bool value);
    void setShowGridlines(bool value);
    void setShowChromosomeContext(bool value);
    void setDarkMode(bool value);
    void setShowTilesDebug(bool value);
    void setSparseFeatureLimit(int value);

    std::vector<contactRecord> recordsSnapshot() const;
    std::vector<contactRecord> controlRecordsSnapshot() const;

signals:
    void filePathChanged();
    void controlFilePathChanged();
    void metadataChanged();
    void statusChanged();
    void busyChanged();
    void viewChanged();
    void recordsChanged();
    void colorMaxChanged();
    void colorMapChanged();
    void tracksChanged();
    void annotationsChanged();
    void viewHistoryChanged();
    void displayOptionsChanged();

private:
    struct TileResult {
        quint64 requestId = 0;
        HicTile tile;
        HicTile controlTile;
        QString error;
        bool fromCache = false;
        bool hasControl = false;
    };

    static QString localPathFromUrl(const QUrl& url);
    static QString settingsKey();
    static HicTileKey makeKey(const QString& filePath, const QString& matrixType, const QString& norm,
                              const QString& chrX, const QString& chrY, int resolution,
                              qint64 x0, qint64 x1, qint64 y0, qint64 y1);

    enum class MatrixFamily {
        Linear,
        Log,
        Divergent,
        Pearson
    };

    void setStatus(const QString& value);
    void setBusy(bool value);
    void applyMetadata(const HicFileMetadata& metadata);
    chromosome chromosomeByName(const QString& name) const;
    qint64 chromosomeLength(const QString& name) const;
    qint64 genomeLength() const;
    bool isAllChromosome(const QString& name) const;
    bool matrixNeedsControl(const QString& matrixType) const;
    bool matrixNeedsPrimary(const QString& matrixType) const;
    bool matrixIsVs(const QString& matrixType) const;
    bool matrixIsPearson(const QString& matrixType) const;
    bool matrixIsDivergent(const QString& matrixType) const;
    QString primaryDataMatrixType(const QString& matrixType) const;
    QString controlDataMatrixType(const QString& matrixType) const;
    bool validateMatrixMode(const QString& matrixType);
    void clampRegion();
    void updateAutoColorScale(const std::vector<contactRecord>& records);
    void updateAutoColorScale(const std::vector<contactRecord>& records,
                              const std::vector<contactRecord>& controlRecords);
    void pushViewHistory();
    void restoreView(const QVariantMap& view);
    void orientTileForRequestedAxes(HicTile& tile) const;
    std::vector<contactRecord> transformRecordsForDisplay(const QString& matrixType,
                                                          const std::vector<contactRecord>& primary,
                                                          const std::vector<contactRecord>& control) const;
    std::vector<contactRecord> transformPearsonLike(const std::vector<contactRecord>& records) const;
    std::vector<contactRecord> mergeRecordPairs(const std::vector<contactRecord>& primary,
                                                const std::vector<contactRecord>& control,
                                                const QString& matrixType) const;
    void addRecent(const QString& group, const QString& path);
    QVariantList recentList(const QString& group) const;
    QVariantMap currentViewState(const QString& name = QString()) const;
    bool applyViewState(const QVariantMap& state);
    QString readTextResource(const QString& pathOrUrl) const;
    void scheduleRequest();
    void startTileLoad(const HicTileKey& key, quint64 requestId);

    struct TrackSegment {
        QString name;
        QString chr;
        qint64 start = 0;
        qint64 end = 0;
        double value = 0.0;
        QColor color = QColor("#4b7bec");
        QColor negativeColor = QColor("#d1495b");
        double minValue = 0.0;
        double maxValue = 1.0;
        bool logScale = false;
        QString reduction = QStringLiteral("mean");
        QString source;
        bool coverage = false;
        bool eigenvector = false;
    };

    struct Annotation2D {
        QString id;
        QString name;
        QString chr1;
        qint64 start1 = 0;
        qint64 end1 = 0;
        QString chr2;
        qint64 start2 = 0;
        qint64 end2 = 0;
        QColor color = QColor("#111111");
        QMap<QString, QString> attributes;
        bool highlighted = false;
    };

    struct AnnotationLayer {
        QString name;
        QColor color = QColor("#111111");
        bool visible = true;
        bool transparent = false;
        bool sparse = false;
        bool enlarged = false;
        QVector<Annotation2D> annotations;
        QVector<Annotation2D> undoStack;
    };

    mutable QMutex m_mutex;
    QString m_filePath;
    QString m_controlFilePath;
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
    double m_colorMin = 0.0;
    double m_colorMax = 50.0;
    bool m_colorMaxAuto = true;
    QString m_colorMap = "White-Red";
    QColor m_customLowColor = QColor("#ffffff");
    QColor m_customHighColor = QColor("#d7191c");
    HicFileMetadata m_metadata;
    std::vector<contactRecord> m_records;
    std::vector<contactRecord> m_controlRecords;
    std::vector<TrackSegment> m_tracks;
    QVector<AnnotationLayer> m_annotationLayers;
    int m_activeAnnotationLayer = 0;
    QString m_selectedAnnotationId;
    bool m_resolutionLocked = false;
    bool m_axisEndpointsOnly = false;
    bool m_showGridlines = true;
    bool m_showChromosomeContext = true;
    bool m_darkMode = false;
    bool m_showTilesDebug = false;
    int m_sparseFeatureLimit = 10000;
    std::unique_ptr<HicTileCache> m_cache;
    QFutureWatcher<HicFileMetadata> m_metadataWatcher;
    QFutureWatcher<TileResult> m_tileWatcher;
    quint64 m_requestSerial = 0;
    bool m_reloadPending = false;
    QVector<QVariantMap> m_undoStack;
    QVector<QVariantMap> m_redoStack;
    bool m_restoringView = false;
    bool m_interactionActive = false;
};

#endif
