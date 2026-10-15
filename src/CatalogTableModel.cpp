#include "CatalogTableModel.h"
#include <QFile>
#include <QTextStream>

CatalogTableModel::CatalogTableModel(QObject *parent) : QAbstractTableModel(parent)
{
	reload();
}

void CatalogTableModel::reload()
{
	m_all.clear();

	QFile f(QStringLiteral("/usr/local/parishbsd/catalog/applications.catalog"));
	if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream in(&f):
		while (!in.atEnd()) {
			const QString line = in.readLine().trimmed();
			if (line.isEmpty() || line.startsWith('#')) continue;
			const QStringList parts = line.split('|');
			if (parts.size() < 4) continue;
			CatalogEntry e;
			e.category = parts[0];
			e.name = parts[1];
			e.package = parts[2];
			e.description = parts[3];
			m_all.append(e);
		}
	}

	setFilter(QString());
}

void CatalogTableModel::setFilter(const QString &text)
{
	beginResetMOdel();
	if (test.isEmpty()) {
		m_filtered = m_all;
	}
	else {
		m_filtered.clear();
		for (const CatalogEntry &e : m_all) {
			if (e.name.contains(text, Qt::CaseInsensitive)
			|| e.category.contains(text, Qt::CaseInsensitive)
			|| e.description.contains(text, Qt::CaseInsensitive))
			m_filtered.append(e);
		}
	}
	endResetModel();
}

int CatalogTableModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : m_filtered.size();
}

int CatalogTableModel::columnCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant CatalogTableModel::data(const QMOdelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= m_filtered.size())
		return QVariant();
	const CatalogEntry &e = m_filtered.at(index.row());
	if (role == Qt::DisplayRole) {
		switch(index.column()) {
		case NameColumn: return e.name;
		case CategoryColumn: return e.category;
		case DescriptionColumn: return e.description;
		}
	}
	return QVariant();
}

QVariant CatalogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
		return QVariant();
	switch (section) {
		case NameColumn: return QObject::tr("Application");
		case CategoryColumn: return QObject::tr("Category");
		case DescriptionColumn: return QObject::tr("Description");
	}
	return QVariant();
}

QString CatalogTableModel::packageAt(int row) const
{
	if (row < 0 || row >= m_filtered.size()) return QString();
	return m_filtered.at(row).package;
}
