#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QIcon>

#include "ContainerInfo.h"

class ContainerBacked;

// Table model for the container list.
// Columns: Name, Kind, State, Template, Disk usage.
class ContainerModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	enum Column {
		NameColumn = 0,
		KindColumn,
		StateColumn,
		TemplateColumn,
		DiskUsageColumn,
		ColumnCount
	};

	explicit ContainerModel(ContainerBackend *backend, QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

	void refresh(); // reload using backend
	
	ContainerInfo containerAt(int row) const;
	int rowForName(const QString &name) const;
private:
	static QIcon makeStateIcon(const QString &state);

	ContainerBackend *m_backend;
	QList<ContainerInfo> m_rows;
};
