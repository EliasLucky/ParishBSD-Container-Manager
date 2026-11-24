#include "ContainerModel.h"
#include "ContainerBackend.h"
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

ContainerModel::ContainerModel(ContainerBackend *backend, QObject *parent)
    : QAbstractTableModel(parent), m_backend(backend)
{
    connect(m_backend, &ContainerBackend::containersReady,
            this, [this](QList<ContainerInfo> list, QString err) {
        if (!err.isEmpty()) {
            m_lastError = err;
            return; // Keep old data on error (fail-closed)
        }
        m_lastError.clear();
        m_allRows = list;
        applyFilter();
    });
}

void ContainerModel::applyFilter()
{
    beginResetModel();
    m_rows.clear();
    for (const auto &c : m_allRows) {
        if (m_filter.isEmpty() ||
            c.name.contains(m_filter, Qt::CaseInsensitive) ||
            c.sourceTemplate.contains(m_filter, Qt::CaseInsensitive) ||
            c.kind.contains(m_filter, Qt::CaseInsensitive)) {
            m_rows.append(c);
        }
    }
    endResetModel();
}

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
        return QIcon::fromTheme(c.isLocked()
            ? QStringLiteral("parish-container-vault-locked")
            : QStringLiteral("parish-container-vault-unlocked"));
    }
    if (c.isCompatibility) return QIcon::fromTheme(QStringLiteral("parish-container-compat"));
    return QIcon::fromTheme(QStringLiteral("parish-container-app"));
}

QVariant ContainerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return QVariant();
    const ContainerInfo &c = m_rows.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case IconColumn:      return QVariant();
        case NameColumn:      return c.name;
        case KindColumn:      return c.kind;
        case StateColumn:     return c.humanState();
        case RiskColumn:      return c.humanRiskLevel();
        case TemplateColumn:  return c.sourceTemplate;
        case DiskUsageColumn: return formatDiskUsage(c.diskUsage);
        }
    }
    if (role == Qt::DecorationRole && index.column() == IconColumn) return iconFor(c);
    if (role == Qt::DecorationRole && index.column() == StateColumn) return makeStateIcon(c.jailState);
    if (role == Qt::DecorationRole && index.column() == RiskColumn) return makeRiskIcon(c.riskColor());
    if (role == Qt::TextAlignmentRole && index.column() == DiskUsageColumn) {
        return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role == Qt::ToolTipRole) return c.accessibleDescription();

    if (role == Qt::AccessibleTextRole) {
        switch (index.column()) {
        case NameColumn:      return c.accessibleText();
        case KindColumn:      return c.kind;
        case StateColumn:     return c.humanState();
        case RiskColumn:      return c.humanRiskLevel();
        case TemplateColumn:  return c.sourceTemplate;
        case DiskUsageColumn: return formatDiskUsage(c.diskUsage);
        default:              return QVariant();
        }
    }
    if (role == Qt::AccessibleDescriptionRole) return c.accessibleDescription();
    return QVariant();
}

QVariant ContainerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
    case IconColumn:      return QString();
    case NameColumn:      return tr("Name");
    case KindColumn:      return tr("Kind");
    case StateColumn:     return tr("State");
    case RiskColumn:      return tr("Risk Level");
    case TemplateColumn:  return tr("Template");
    case DiskUsageColumn: return tr("Disk usage");
    }
    return QVariant();
}

void ContainerModel::setFilter(const QString &text)
{
    m_filter = text;
    applyFilter(); // Instant local filtering without spawning pkexec on every keystroke!
}

void ContainerModel::refresh()
{
    m_backend->requestContainers();
}

ContainerInfo ContainerModel::containerAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) return ContainerInfo();
    return m_rows.at(row);
}

ContainerInfo ContainerModel::containerByName(const QString &name) const
{
    for (const auto &c : m_allRows) {
        if (c.name == name) return c;
    }
    return ContainerInfo();
}

QIcon ContainerModel::makeStateIcon(const QString &state)
{
    QColor color;
    bool filled = true;
    if (state == QLatin1String("running")) {
        color = QColor(0x2e, 0x7d, 0x32);
    } else if (state == QLatin1String("contained") || state == QLatin1String("containment-failed")) {
        color = QColor(0xc6, 0x28, 0x28);
    } else if (state == QLatin1String("containing") || state == QLatin1String("stopped-maybe-contained")) {
        color = QColor(0xd9, 0x77, 0x06);
    } else {
        color = QColor(0x64, 0x74, 0x8b);
        filled = false;
    }

    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (filled) {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
    } else {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color, 1.5));
    }
    p.drawEllipse(QRectF(4, 4, 8, 8));
    p.end();
    return QIcon(pm);
}

QIcon ContainerModel::makeRiskIcon(const QColor &color)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(2, 2, 10, 10), 2.5, 2.5);
    p.end();
    return QIcon(pm);
}
