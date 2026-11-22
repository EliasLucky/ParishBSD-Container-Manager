#include "ContainerModel.h"
#include "ContainerBackend.h"
#include <QPainter>
#include <QPixmap>
#include <QColor>
#include <QIcon>

ContainerModel::ContainerModel(ContainerBackend *backend, QObject *parent) : QAbstractTableModel(parent), m_backend(backend) {
    connect(m_backend, &ContainerBackend::containersReady, this, [this](QList<ContainerInfo> list, QString err) {
        if (!err.isEmpty()) {
            m_lastError = err;
            return; // Keep old data on error
        }
        m_lastError.clear();
        beginResetModel();
        m_rows.clear();
        for (const auto &c : list) {
            if (m_filter.isEmpty() || c.name.contains(m_filter, Qt::CaseInsensitive))
                m_rows.append(c);
        }
        endResetModel();
    });
}

int ContainerModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }
int ContainerModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

QIcon ContainerModel::iconFor(const ContainerInfo &c) const {
    if (c.isVault) return QIcon::fromTheme(c.isLocked() ? "parish-container-vault-locked" : "parish-container-vault-unlocked");
    if (c.isCompatibility) return QIcon::fromTheme("parish-container-compat");
    return QIcon::fromTheme("parish-container-app");
}

QVariant ContainerModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return QVariant();
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
    if (role == Qt::DecorationRole && index.column() == IconColumn) return iconFor(c);
    if (role == Qt::DecorationRole && index.column() == StateColumn) return makeStateIcon(c.jailState);
    if (role == Qt::TextAlignmentRole && index.column() == DiskUsageColumn) return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ToolTipRole) return c.accessibleDescription();

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
    if (role == Qt::AccessibleDescriptionRole) return c.accessibleDescription();
    return QVariant();
}

QVariant ContainerModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
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

void ContainerModel::setFilter(const QString &text) {
    m_filter = text;
    refresh();
}

void ContainerModel::refresh() {
    m_backend->requestContainers(); // ASYNC CALL
}

ContainerInfo ContainerModel::containerAt(int row) const {
    if (row < 0 || row >= m_rows.size()) return ContainerInfo();
    return m_rows.at(row);
}

QIcon ContainerModel::makeStateIcon(const QString &state) {
    QColor color;
    bool filled = true;
    if (state == QLatin1String("running")) color = QColor(0x4c, 0xaf, 0x50);
    else if (state == QLatin1String("contained") || state == QLatin1String("containment-failed")) color = QColor(0xdf, 0x38, 0x2e);
    else if (state == QLatin1String("containing") || state == QLatin1String("stopped-maybe-contained")) color = QColor(0xd4, 0xaf, 0x37);
    else { color = QColor(0x80, 0x80, 0x80); filled = false; }

    QPixmap pm(16,16); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (filled) { p.setBrush(color); p.setPen(Qt::NoPen); }
    else { p.setBrush(Qt::NoBrush); p.setPen(QPen(color, 1.5)); }
    p.drawEllipse(QRectF(4,4,8,8));
    p.end();
    return QIcon(pm);
}