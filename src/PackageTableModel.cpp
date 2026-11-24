#include "PackageTableModel.h"
#include <QColor>

PackageTableModel::PackageTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int PackageTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_packages.size();
}

int PackageTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PackageTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_packages.size()) return QVariant();
    const PackageInfo &p = m_packages.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case NameColumn:
            return p.name;
        case VersionColumn:
            return p.version;
        case StatusColumn:
            if (p.isCustom()) {
                return tr("Custom (%1)").arg(p.installMethod.isEmpty() ? tr("file") : p.installMethod);
            }
            if (p.hasUpdate()) {
                return tr("Update available -> %1").arg(p.latestVersion);
            }
            return tr("Up to date");
        }
    }

    if (role == Qt::ForegroundRole && index.column() == StatusColumn) {
        if (p.isCustom()) return QColor(0x6d, 0x28, 0xd9);
        if (p.hasUpdate()) return QColor(0xb4, 0x53, 0x09);
        return QColor(0x2e, 0x7d, 0x32);
    }

    if (role == Qt::ToolTipRole && p.isCustom()) {
        return tr("Installed from %1 on %2").arg(p.comment, p.installDate);
    }

    if (role == Qt::AccessibleTextRole) {
        return tr("%1, version %2, %3").arg(p.name, p.version,
            data(this->index(index.row(), StatusColumn), Qt::DisplayRole).toString());
    }
    return QVariant();
}

QVariant PackageTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
    case NameColumn:    return tr("Name");
    case VersionColumn: return tr("Version");
    case StatusColumn:  return tr("Status");
    }
    return QVariant();
}

void PackageTableModel::setPackages(const QList<PackageInfo> &packages)
{
    beginResetModel();
    m_packages = packages;
    endResetModel();
}

PackageInfo PackageTableModel::packageAt(int row) const
{
    if (row < 0 || row >= m_packages.size()) return PackageInfo();
    return m_packages.at(row);
}
