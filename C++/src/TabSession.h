#ifndef CARTON_TAB_SESSION_H
#define CARTON_TAB_SESSION_H

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QUrl>
#include <QVariantList>

#include "HicDataController.h"
#include "RegionSetModel.h"

class TabSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString type READ type NOTIFY structureChanged)
    Q_PROPERTY(QString typeLabel READ typeLabel NOTIFY structureChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QVariantList cells READ cells NOTIFY cellsChanged)
    Q_PROPERTY(QVariantList maps READ maps NOTIFY cellsChanged)
    Q_PROPERTY(int cellCount READ cellCount NOTIFY cellsChanged)
    Q_PROPERTY(int mapCount READ mapCount NOTIFY cellsChanged)
    Q_PROPERTY(int regionCount READ regionCount NOTIFY cellsChanged)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY cellsChanged)
    Q_PROPERTY(int columnCount READ columnCount NOTIFY cellsChanged)
    Q_PROPERTY(int layoutColumns READ layoutColumns WRITE setLayoutColumns NOTIFY cellsChanged)
    Q_PROPERTY(bool transposed READ transposed WRITE setTransposed NOTIFY cellsChanged)
    Q_PROPERTY(QString diagonalMode READ diagonalMode WRITE setDiagonalMode NOTIFY cellsChanged)
    Q_PROPERTY(QString layerScope READ layerScope WRITE setLayerScope NOTIFY layerScopeChanged)
    Q_PROPERTY(bool linkNavigation READ linkNavigation WRITE setLinkNavigation NOTIFY linkingChanged)
    Q_PROPERTY(bool linkCrosshair READ linkCrosshair WRITE setLinkCrosshair NOTIFY linkingChanged)
    Q_PROPERTY(bool linkColorScale READ linkColorScale WRITE setLinkColorScale NOTIFY linkingChanged)
    Q_PROPERTY(int activeCellIndex READ activeCellIndex WRITE setActiveCellIndex NOTIFY activeCellChanged)
    Q_PROPERTY(HicDataController* activeController READ activeController NOTIFY activeCellChanged)
    Q_PROPERTY(RegionSetModel* regionSet READ regionSet CONSTANT)
    Q_PROPERTY(qint64 windowSize READ windowSize WRITE setWindowSize NOTIFY cellsChanged)

public:
    explicit TabSession(QObject* parent = nullptr);
    ~TabSession() override;

    QString type() const;
    QString typeLabel() const;
    QString title() const;
    QVariantList cells() const;
    QVariantList maps() const;
    int cellCount() const;
    int mapCount() const;
    int regionCount() const;
    int rowCount() const;
    int columnCount() const;
    int layoutColumns() const;
    bool transposed() const;
    QString diagonalMode() const;
    QString layerScope() const;
    bool linkNavigation() const;
    bool linkCrosshair() const;
    bool linkColorScale() const;
    int activeCellIndex() const;
    HicDataController* activeController() const;
    RegionSetModel* regionSet() const;
    qint64 windowSize() const;

    void setTitle(const QString& value);
    void setLayoutColumns(int value);
    void setTransposed(bool value);
    void setDiagonalMode(const QString& value);
    void setLayerScope(const QString& value);
    void setLinkNavigation(bool value);
    void setLinkCrosshair(bool value);
    void setLinkColorScale(bool value);
    void setActiveCellIndex(int value);
    void setWindowSize(qint64 value);

    Q_INVOKABLE void initialize(const QString& type);
    Q_INVOKABLE void addMap();
    Q_INVOKABLE void removeMap(int mapIndex);
    Q_INVOKABLE void setPrimaryFile(int cellIndex, const QUrl& url);
    Q_INVOKABLE void setControlFile(int cellIndex, const QUrl& url);
    Q_INVOKABLE bool loadRegions(const QUrl& url, const QString& format = QString());
    Q_INVOKABLE void loadTrack(const QUrl& url, const QString& scope = QString());
    Q_INVOKABLE void loadTrackFromPath(const QString& pathOrUrl, const QString& scope = QString());
    Q_INVOKABLE void loadAnnotations(const QUrl& url, const QString& scope = QString());
    Q_INVOKABLE void loadAnnotationResource(const QString& resourceId, const QString& scope = QString());
    Q_INVOKABLE void notifyViewportInteracted(int cellIndex);
    Q_INVOKABLE void addScopedAnnotation(int cellIndex, double xStartFraction, double yStartFraction,
                                         double xEndFraction, double yEndFraction,
                                         const QString& scope = QString());
    Q_INVOKABLE QVariantMap state() const;
    Q_INVOKABLE bool restoreState(const QVariantMap& state);
    Q_INVOKABLE bool exportState(const QUrl& url) const;
    Q_INVOKABLE bool importState(const QUrl& url);

signals:
    void structureChanged();
    void cellsChanged();
    void activeCellChanged();
    void titleChanged();
    void layerScopeChanged();
    void linkingChanged();
    void errorOccurred(const QString& message);

private:
    enum class Type { Single, MultiMap, MultiRegion, MapRegion, Pairwise };
    struct MapSpec {
        QString id;
        QString primaryPath;
        QString controlPath;
        QString label;
    };
    struct CellSpec {
        QString key;
        QPointer<HicDataController> controller;
        int mapIndex = 0;
        int regionIndex = -1;
        int xRegionIndex = -1;
        int yRegionIndex = -1;
        int row = 0;
        int column = 0;
        QString label;
        QString dataRole = QStringLiteral("primary");
        bool diagonal = false;
        bool blank = false;
    };

    static Type parseType(const QString& value);
    static QString typeName(Type type);
    static QString typeDisplayName(Type type);
    static QString pathFromUrl(const QUrl& url);
    void ensureMapCount(int count);
    void rebuildCells();
    HicDataController* createController(const QString& key);
    void applyCellRegion(CellSpec& cell);
    void applyCellMode(CellSpec& cell);
    void openMapForCell(CellSpec& cell);
    void updateCellModel();
    int mapIndexForCell(int cellIndex) const;
    QList<HicDataController*> targetControllers(const QString& requestedScope) const;
    QString resolvedScope(const QString& requestedScope) const;
    bool cellsShareNavigation(const CellSpec& source, const CellSpec& target) const;
    void propagateColor(HicDataController* source);

    Type m_type = Type::Single;
    QString m_title = QStringLiteral("Map");
    QVector<MapSpec> m_maps;
    QVector<CellSpec> m_cells;
    QVariantList m_cellModel;
    RegionSetModel* m_regionSet = nullptr;
    int m_activeCellIndex = 0;
    int m_layoutColumns = 2;
    bool m_transposed = false;
    QString m_diagonalMode = QStringLiteral("split");
    QString m_layerScope = QStringLiteral("default");
    bool m_linkNavigation = true;
    bool m_linkCrosshair = true;
    bool m_linkColorScale = false;
    bool m_initializing = false;
    QHash<QString, QVariantMap> m_pendingCellStates;
};

#endif
