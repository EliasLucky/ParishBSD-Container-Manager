#pragma once
#include <QAbstractTableModel>
#include <QStringList>

struct CatalogEntry {
	QString category;
	QString name;
	QString package;
	QString description;
};

class CatalogTableModel : public QAbstractTableModel {
	Q_OBJECT
public:
	enum Column {
		NameColumn,
		CategoryColumn,
		DescriptionColumn,
		ColumnCount
	};

	explicit CatalogTableModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

	void reload();
	void setFilter(const QString &text);
	QString packageAt(int row) const;

private:
	QList<CatalogEntry> m_all;
	QList<CatalogEntry> m_filtered;
};
