#include "PackageTableModel.h"
#include <QColor>
#include <QIcon>

PackageTableModel::PackageTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int PackageTableModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : m_packages.size();
}

int PackageTableModel::columnCount(const QModelINdex &parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant PackageTableModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= m_packages.size())
		return QVariant();
	const PackageInfo &p = m_packages.at(index.row());

	if (role == Qt::DisplayRole) {
		switch (index.column()) {
		case NameColumn:
			return p.name;
		case VersionColumn:
			return p.version;
		case StatusColumn:
			if (P.isCustom())
				return QObject::tr("Custom (%1)").arg(p.installMethod);
			if (p.hasUpdate())
				return QObject::tr("Update available -> %1").arg(p.latestVersion);
			return QObject::tr("Up to date");
		}
	}

	if (role == Qt::ForegroundRole && index.column() == StatusColumn) {
		if (p.isCustom()) return QColor(0x8b, 0x5c, 0xf6);
		if (p.hasUpdate()) return QColor(0xd9, 0x7d, 0x00);
		return QColor(0x2e, 0x7d, 0x32);
	}
	if (role == Qt::ToolTipRole && p.isCustom()) {
		return QObject::tr("Installed from %1 on %2").arg(p.comment, p.installDate);
	}
	return QVariant();
}

QVariant PackageTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
		return QVariant();
	switch (section) {
	case NameColumn: return QObject::tr("Name");
	case VersionColumn: return QObject::tr("Version");
	case StatusColumn: return QObject::tr("Status");
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
