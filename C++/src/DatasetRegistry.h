#ifndef CARTON_DATASET_REGISTRY_H
#define CARTON_DATASET_REGISTRY_H

#include <QColor>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QWaitCondition>
#include <QSemaphore>

#include <memory>

#include "GenomicTrackReader.h"
#include "HicTileCache.h"
#include "WorkspaceListModel.h"
#include "straw.h"

struct PooledTrackRange {
    qsizetype begin = 0;
    qsizetype end = 0;
    qint64 maximumSpan = 0;
};

struct PooledTrackData {
    QString id;
    QString source;
    QString name;
    QString format;
    QString warning;
    QVector<GenomicTrackFeature> features;
    QHash<QString, PooledTrackRange> chromosomeRanges;
    double minimum = 0.0;
    double maximum = 1.0;
};

struct PooledAnnotation {
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

struct PooledAnnotationData {
    QString id;
    QString source;
    QString name;
    bool custom = false;
    QVector<PooledAnnotation> annotations;
};

struct PooledHicMetadataResult {
    QString id;
    QString source;
    QString error;
    std::shared_ptr<const HicFileMetadata> metadata;
};

struct PooledTrackResult {
    QString id;
    QString error;
    std::shared_ptr<const PooledTrackData> data;
};

struct PooledAnnotationResult {
    QString id;
    QString error;
    std::shared_ptr<PooledAnnotationData> data;
};

class DatasetRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* resourcesModel READ resourcesModel CONSTANT)
    Q_PROPERTY(int resourceCount READ resourceCount NOTIFY resourcesChanged)
    Q_PROPERTY(int cacheLimitMB READ cacheLimitMB WRITE setCacheLimitMB NOTIFY cacheStatsChanged)
    Q_PROPERTY(int cacheTileCount READ cacheTileCount NOTIFY cacheStatsChanged)
    Q_PROPERTY(int cacheRecordCount READ cacheRecordCount NOTIFY cacheStatsChanged)
    Q_PROPERTY(double cacheMemoryMB READ cacheMemoryMB NOTIFY cacheStatsChanged)

public:
    static DatasetRegistry* instance();

    QAbstractItemModel* resourcesModel() const;
    int resourceCount() const;
    int cacheLimitMB() const;
    int cacheTileCount() const;
    int cacheRecordCount() const;
    double cacheMemoryMB() const;
    void setCacheLimitMB(int value);

    PooledHicMetadataResult loadHicMetadata(const QString& pathOrUrl);
    PooledTrackResult loadTrack(const QString& pathOrUrl);
    PooledAnnotationResult loadAnnotations(const QString& pathOrUrl);
    PooledAnnotationResult annotationById(const QString& id) const;
    PooledAnnotationResult createCustomAnnotations(const QString& name,
                                                   const QVector<PooledAnnotation>& annotations = {});
    PooledAnnotationResult restoreCustomAnnotations(const QString& resourceId, const QString& name,
                                                    const QVector<PooledAnnotation>& annotations);
    PooledAnnotationResult forkAnnotations(const QString& sourceId, const QString& name,
                                           const QVector<PooledAnnotation>& annotations);

    HicTileCache* tileCache();
    void notifyCacheChanged();
    void acquireReadSlot();
    void releaseReadSlot();
    void notifyAnnotationChanged(const QString& id);

    Q_INVOKABLE QVariantList resources(const QString& kind = QString()) const;
    Q_INVOKABLE QString canonicalResourceId(const QString& kind, const QString& pathOrUrl) const;
    Q_INVOKABLE QString resourcePath(const QString& id) const;
    Q_INVOKABLE QString resourceKind(const QString& id) const;
    Q_INVOKABLE void clearUnusedResources();
    Q_INVOKABLE bool exportWorkspace(const QUrl& url, const QVariantList& tabStates) const;
    Q_INVOKABLE QVariantList importWorkspace(const QUrl& url) const;

signals:
    void resourcesChanged();
    void cacheStatsChanged();

private:
    explicit DatasetRegistry(QObject* parent = nullptr);

    struct HicEntry {
        bool loading = false;
        QString error;
        std::shared_ptr<const HicFileMetadata> metadata;
        QWaitCondition ready;
    };

    struct ResourceSummary {
        QString id;
        QString kind;
        QString name;
        QString source;
        qint64 itemCount = 0;
        bool custom = false;
    };

    static QString canonicalSource(const QString& pathOrUrl);
    static QString chromosomeKey(const QString& name);
    static QString displayName(const QString& pathOrUrl);
    void recordResourceLocked(const ResourceSummary& summary);
    void scheduleModelRefresh();
    void refreshModel();

    mutable QMutex m_mutex;
    QHash<QString, std::shared_ptr<HicEntry>> m_hicEntries;
    QHash<QString, std::shared_ptr<const PooledTrackData>> m_tracks;
    QHash<QString, std::shared_ptr<PooledAnnotationData>> m_annotations;
    QHash<QString, ResourceSummary> m_summaries;
    std::shared_ptr<HicTileCache> m_tileCache;
    WorkspaceListModel* m_resourcesModel = nullptr;
    int m_cacheLimitMB = 128;
    bool m_refreshPending = false;
    QSemaphore m_readSlots{6};
};

#endif
