#include "ContainerModel.h"
#include "ContainerBackend.h"

#include <QPainter>
#include <QPixmap>
#include <QColor>
#include <QIcon>

ContainerModel::ContainerModel(ContainerBackend *backend, QObject *parent) : QAbstractTableModel(parent), m_backend(backend) {}

int ContainerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int ContainerModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QIcon ContainerModel::iconFor(const ContainerInfo &c) const
{
    if (c.isVault) {
        return QIcon::fromTheme("folder-locked");
    }
    if (c.isCompatibility) {
        return QIcon::fromTheme("applications-other");
    }
    return QIcon::fromTheme("package-x-generic");
}

QVariant ContainerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();
    const ContainerInfo &c = m_rows.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case IconColumn: return QVariant();
        case NameColumn: return c.name;
        case KindColumn: return c.kind;
        case StateColumn: return c.humanState();
        case TemplateColumn: return c.sourceTemplate;
        case DiskUsageColumn: return formatDiskUsage(c.diskUsage);
        }
    }

    if (role == Qt::DecorationRole && index.column() == IconColumn)
        return iconFor(c);

    if (role == Qt::DecorationRole && index.column() == StateColumn)
        return makeStateIcon(c.jailState);

    if (role == Qt::TextAlignmentRole && index.column() == DiskUsageColumn)
        return int(Qt::AlignRight | Qt::AlignVCenter);

    if (role == Qt::ToolTipRole)
        return c.accessibleDescription();

    if (role == Qt::AccessibleTextRole) {
        switch (index.column()) {
        case NameColumn: return c.accessibleText();
        case KindColumn: return c.kind;
        case StateColumn: return c.humanState();
        case TemplateColumn: return c.sourceTemplate;
        case DiskUsageColumn: return formatDiskUsage(c.diskUsage);
        default: return QVariant();
        }
    }
    if (role == Qt::AccessibleDescriptionRole) {
        return c.accessibleDescription();
    }

    return QVariant();
}

QVariant ContainerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case IconColumn: return QString();
    case NameColumn: return tr("Name");
    case KindColumn: return tr("Kind");
    case StateColumn: return tr("State");
    case TemplateColumn: return tr("Template");
    case DiskUsageColumn: return tr("Disk usage");
    }
    return QVariant();
}

void ContainerModel::refresh()
{
    QString err;
    const QList<ContainerInfo> newRows = m_backend->listContainers(&err);

    beginResetModel();
    if (err.isEmpty()) {
        m_rows = newRows;
        m_lastError.clear();
    } else {
        m_lastError = err;
    }
    endResetModel();
}

ContainerInfo ContainerModel::containerAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) return ContainerInfo();
    return m_rows.at(row);
}

int ContainerModel::rowForName(const QString &name) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).name == name) return i;
    return -1;
}

QIcon ContainerModel::makeStateIcon(const QString &state)
{
    QColor color;
    bool filled = true;

    if (state == QLatin1String("running")) {
        color = QColor(0x4c, 0xaf, 0x50); // green
    }
    else if (state == QLatin1String("contained") || state == QLatin1String("containment-failed")) {
        color = QColor(0xdf, 0x38, 0x2e); // red
    }
    else if (state == QLatin1String("containing") || state == QLatin1String("stopped-maybe-contained")) {
        color = QColor(0xd4, 0xaf, 0x37); // amber
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