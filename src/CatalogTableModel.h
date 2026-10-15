#pragma once
#include <QAbstractTableModel>
#include <QStringList>

struct CatalogEntry {
	QString category;
	QString name;
	QString package;
	QString description;
};

class CatalogTableMOdel : public QAbstractTableModel {
	Q_OBJECT
public:
	enum Column {
		NameColumn,
		CategoryColumn,
		DescriptionColumn,
		ColumnCount
	}

	explicit CaatalogTableModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) constoverride;

	void reload();
	void setFilter(const QString &text);
	QString packageAt(int row) const;

private:
	QList<CatalogEntry> m_all;
	QSlist<CatalogEntry> m_filtered;
};
