#pragma once
#include <QAbstractTableModel>
#include "PackageInfo.h"

class PackageTableModel : public QAbstractTableModel {
	Q_OBJECT
public:
	enum Column { NameColumn, VersionColumn, StatusColumn, ColumnCount };

	explicit PackageTableModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QMOdelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

	void setPackages(const QList<PackageInfo> &packages);
	PackageInfo packageAt(int row) const;
private:
	QList<PackageInfo> m_packages;
};
