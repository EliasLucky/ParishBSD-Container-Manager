#include "ContainerModel.h"
#include "ContainerBackend.h"

#include <QPainter>
#include <QPixmap>
#include <QColor>

ContainerModel::ContainerModel(ContainerBackend *backend, QObject *parent) : QAbstractTableModel(parent), m_backend(backend) {}

int ContainerModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return m_rows.size();
}

int ContainerModel::columnCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return ColumnCount;
}

QVariant ContainerModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= m_rows.size())
		return QVariant();
	const ContainerInfo &c = m_rows.at(index.row());

	if (role == Qt::DisplayRole) {
		switch (index.column()) {
		case NameColumn: return c.name;
		case KindColumn: return c.kind;
		case StateColumn: return c.state;
		case TemplateColumn: return c.personality;
		case DiskUsageColumn: return formatDiskUsage(c.diskUsage);
		}
	}

	if (role == Qt::DecorationRole && index.column() == StateColumn)
		return makeStateIcon(c.state);
	
	if (role == Qt::TextAlignmentRole && index.column() == DiskUsageColumn)
		return int(Qt::AlignRight | Qt::AlignVCenter);

	if (role == Qt::ToolTipRole) {
		return QString("%1\n%2 (%3)\nPersonality: %4").arg(c.name, c.kind, c.state, c.personality);
	}

	return QVariant();
}

QVariant ContainerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole)
		return QVariant();

	if (orientation == Qt::Horizontal) {
		switch (section) {
		case NameColumn: return tr("Name");
		case KindColumn: return tr("Kind");
		case StateColumn: return tr("State");
		case TemplateColumn: return tr("Personality");
		case DiskUsageColumn: return tr("Disk usage");
		}
	}
	return QVariant();
}

void ContainerModel::refresh()
{
	QString err;
	const QList<ContainerInfo> newRows = m_backend->listContainers(&err);

	beginResetModel();
	m_rows = newRows;
	endResetModel();
}

ContainerInfo ContainerModel::containerAt(int row) const
{
	if (row < 0 || row >= m_rows.size())
		return ContainerInfo();
	return m_rows.at(row);
}

int ContainerModel::rowForName(const QString &name) const
{
	for (int i = 0; i < m_rows.size(); ++i)
		if (m_rows.at(i).name == name)
			return i;
	return -1;
}

QIcon ContainerModel::makeStateIcon(const QString &state)
{
	QColor color;
	bool filled = true;

	if (state == QLatin1String("running")) {
		color = QColor(0x4c, 0xaf, 0x50); // green
	}
	else if (state == QLatin1String("unlocked")) {
		color = QColor(0xd4, 0xaf, 0x37); // gold
	}
	else if (state == QLatin1String("locked")) {
		color = QColor(0xd4, 0xaf, 0x37); // gold
	}
	else {
		color = QColor(0x80, 0x80, 0x80); // gray, hollow
		filled = false;
	}

	QPixmap pm(16,16);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	if (filled) {
		p.setBrush(color);
		p.setPen(Qt::NoPen);
	}
	else {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color, 1.5));
	}
	p.drawEllipse(QRectF(4,4,8,8));
	p.end();
	return QIcon(pm);
}
