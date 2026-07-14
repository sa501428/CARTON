#ifndef CARTON_WORKSPACE_LIST_MODEL_H
#define CARTON_WORKSPACE_LIST_MODEL_H

#include <QAbstractListModel>
#include <QVariantList>

class WorkspaceListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { EntryRole = Qt::UserRole + 1 };

    explicit WorkspaceListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setEntries(QVariantList entries);

signals:
    void countChanged();

private:
    QVariantList m_entries;
};

#endif
