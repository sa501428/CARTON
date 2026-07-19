#ifndef CARTON_REGION_SET_MODEL_H
#define CARTON_REGION_SET_MODEL_H

#include <QAbstractListModel>
#include <QUrl>
#include <QVariantList>

class RegionSetModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY regionsChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY regionsChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY regionsChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(qint64 windowSize READ windowSize WRITE setWindowSize NOTIFY regionsChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY regionsChanged)

public:
    enum Role { EntryRole = Qt::UserRole + 1 };

    explicit RegionSetModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString kind() const;
    QString sourcePath() const;
    QString errorString() const;
    qint64 windowSize() const;
    QVariantList entries() const;
    void setWindowSize(qint64 value);

    Q_INVOKABLE bool loadBed(const QUrl& url);
    Q_INVOKABLE bool loadBedpe(const QUrl& url);
    Q_INVOKABLE bool loadBedpeAsBed(const QUrl& url);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap state() const;
    Q_INVOKABLE bool restoreState(const QVariantMap& state);

signals:
    void regionsChanged();
    void errorStringChanged();

private:
    struct AxisRegion {
        QString chr;
        qint64 start = 0;
        qint64 end = 0;
        QString label;
    };
    struct PairedRegion {
        AxisRegion x;
        AxisRegion y;
        QString label;
    };

    enum class Kind { None, Paired, Single, Projected };
    static QString localPath(const QUrl& url);
    static QString chromosomeKey(const QString& name);
    static QVector<AxisRegion> mergeRegions(QVector<AxisRegion> regions);
    bool parseBed(const QString& path, bool fromBedpe);
    bool parseBedpe(const QString& path);
    QVariantMap axisEntry(const AxisRegion& region, int index) const;
    QVariantMap pairedEntry(const PairedRegion& region, int index) const;
    void rebuildEntries();
    void setError(const QString& error);

    Kind m_kind = Kind::None;
    QString m_sourcePath;
    QString m_errorString;
    qint64 m_windowSize = 2000000;
    QVector<AxisRegion> m_axisRegions;
    QVector<PairedRegion> m_pairedRegions;
    QVariantList m_entries;
};

#endif
