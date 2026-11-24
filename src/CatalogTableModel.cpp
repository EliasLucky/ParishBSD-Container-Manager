#include "CatalogTableModel.h"
#include <QColor>
#include <QFile>
#include <QTextStream>

CatalogTableModel::CatalogTableModel(QObject *parent) : QAbstractTableModel(parent)
{
    reload();
}

void CatalogTableModel::reload()
{
    m_all.clear();

    QFile f(QStringLiteral("/usr/local/share/parishbsd/applications.catalog"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const QStringList parts = line.split(QLatin1Char('|'));
            if (parts.size() < 4) continue;
            CatalogEntry e;
            e.category = parts[0].trimmed();
            e.name = parts[1].trimmed();
            e.package = parts[2].trimmed();
            e.description = parts[3].trimmed();
            e.requiresCap = (parts.size() >= 5 && !parts[4].trimmed().isEmpty())
                ? parts[4].trimmed()
                : QStringLiteral("-");
            m_all.append(e);
        }
    }

    setFilter(QString());
}

void CatalogTableModel::setFilter(const QString &text)
{
    beginResetModel();
    if (text.isEmpty()) {
        m_filtered = m_all;
    } else {
        m_filtered.clear();
        for (const CatalogEntry &e : m_all) {
            if (e.name.contains(text, Qt::CaseInsensitive) ||
                e.category.contains(text, Qt::CaseInsensitive) ||
                e.description.contains(text, Qt::CaseInsensitive) ||
                e.package.contains(text, Qt::CaseInsensitive)) {
                m_filtered.append(e);
            }
        }
    }
    endResetModel();
}

void CatalogTableModel::setTargetContainer(const ContainerInfo &container)
{
    beginResetModel();
    m_target = container;
    endResetModel();
}

bool CatalogTableModel::entryCompatible(const CatalogEntry &e, QString *reasonOut) const
{
    if (!m_target.isValid()) return true;

    if (e.requiresCap == QLatin1String("compat-windows") && !m_target.supportsWindowsCompat()) {
        if (reasonOut) *reasonOut = tr("Needs Windows app support");
        return false;
    }
    if (e.requiresCap == QLatin1String("compat-linux") && !m_target.supportsLinuxCompat()) {
        if (reasonOut) *reasonOut = tr("Needs Linux app support");
        return false;
    }
    if (e.requiresCap == QLatin1String("gui") && !m_target.supportsGui()) {
        if (reasonOut) *reasonOut = tr("Needs graphical app support");
        return false;
    }
    return true;
}

int CatalogTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_filtered.size();
}

int CatalogTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

Qt::ItemFlags CatalogTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_filtered.size()) return Qt::NoItemFlags;
    if (!entryCompatible(m_filtered.at(index.row()), nullptr)) {
        return Qt::ItemIsSelectable; // Visible & focusable for screen readers, not installable
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant CatalogTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_filtered.size()) return QVariant();
    const CatalogEntry &e = m_filtered.at(index.row());
    QString incompatReason;
    const bool ok = entryCompatible(e, &incompatReason);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case NameColumn:        return e.name;
        case CategoryColumn:    return e.category;
        case RequirementColumn:
            if (!ok) return incompatReason;
            if (e.requiresCap == QLatin1String("-")) return tr("Any");
            if (e.requiresCap == QLatin1String("gui")) return tr("Graphical");
            if (e.requiresCap == QLatin1String("compat-windows")) return tr("Windows apps");
            if (e.requiresCap == QLatin1String("compat-linux")) return tr("Linux apps");
            return e.requiresCap;
        case DescriptionColumn: return e.description;
        }
    }
    if (role == Qt::ForegroundRole && !ok) {
        return QColor(0x94, 0xa3, 0xb8);
    }
    if (role == Qt::ToolTipRole) {
        return ok ? e.description : incompatReason;
    }
    if (role == Qt::AccessibleTextRole) {
        return ok
            ? tr("%1, %2, %3").arg(e.name, e.category, e.description)
            : tr("%1, %2, incompatible: %3").arg(e.name, e.category, incompatReason);
    }
    return QVariant();
}

QVariant CatalogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
    case NameColumn:        return tr("Application");
    case CategoryColumn:    return tr("Category");
    case RequirementColumn: return tr("Requirement");
    case DescriptionColumn: return tr("Description");
    }
    return QVariant();
}

QString CatalogTableModel::packageAt(int row) const
{
    if (row < 0 || row >= m_filtered.size()) return QString();
    return m_filtered.at(row).package;
}

bool CatalogTableModel::isRowCompatible(int row, QString *reasonOut) const
{
    if (row < 0 || row >= m_filtered.size()) return false;
    return entryCompatible(m_filtered.at(row), reasonOut);
}
