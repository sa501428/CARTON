#include "WorkspaceListModel.h"

WorkspaceListModel::WorkspaceListModel(QObject* parent) : QAbstractListModel(parent) {}

int WorkspaceListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant WorkspaceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size() || role != EntryRole)
        return {};
    return m_entries[index.row()];
}

QHash<int, QByteArray> WorkspaceListModel::roleNames() const {
    return {{EntryRole, QByteArrayLiteral("entry")}};
}

void WorkspaceListModel::setEntries(QVariantList entries) {
    if (m_entries.size() == entries.size()) {
        if (m_entries == entries) return;
        m_entries = std::move(entries);
        if (!m_entries.isEmpty())
            emit dataChanged(index(0, 0), index(m_entries.size() - 1, 0), {EntryRole});
        return;
    }
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    emit countChanged();
}
