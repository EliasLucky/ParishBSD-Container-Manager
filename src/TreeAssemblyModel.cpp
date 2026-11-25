#include "TreeAssemblyModel.h"
#include "ContainerModel.h"
#include "TemplateModel.h"
#include <QFont>

TreeAssemblyModel::TreeAssemblyModel(ContainerModel *cModel, TemplateModel *tModel, QObject *parent)
    : QAbstractItemModel(parent), m_cModel(cModel), m_tModel(tModel)
{
    connect(m_cModel, &QAbstractItemModel::modelReset, this, &TreeAssemblyModel::refresh);
    connect(m_tModel, &QAbstractItemModel::modelReset, this, &TreeAssemblyModel::refresh);
}

QModelIndex TreeAssemblyModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) return QModelIndex();
    if (!parent.isValid()) return createIndex(row, column, quintptr(0));
    return createIndex(row, column, quintptr(parent.row() + 1));
}

QModelIndex TreeAssemblyModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) return QModelIndex();
    const quintptr id = index.internalId();
    if (id == 0) return QModelIndex();
    return createIndex(static_cast<int>(id - 1), 0, quintptr(0));
}

int TreeAssemblyModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) return 3;
    if (parent.internalId() != 0) return 0;
    return groupRowCount(parent.row());
}

int TreeAssemblyModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return ColumnCount;
}

QList<TemplateInfo> TreeAssemblyModel::visibleTemplatesForGroup(int group) const
{
    QList<TemplateInfo> tmpls;
    const bool wantShipped = (group == 1);
    for (const auto &t : m_tModel->allTemplates()) {
        if (!t.hidden && t.shipped == wantShipped) {
            tmpls.append(t);
        }
    }
    return tmpls;
}

int TreeAssemblyModel::groupRowCount(int group) const
{
    if (group == 0) return m_cModel->rowCount();
    if (group == 1 || group == 2) return visibleTemplatesForGroup(group).size();
    return 0;
}

QString TreeAssemblyModel::groupName(int group) const
{
    if (group == 0) return tr("Compartments");
    if (group == 1) return tr("Shipped Templates");
    if (group == 2) return tr("Custom Templates");
    return QString();
}

int TreeAssemblyModel::getUseCount(const QString &templateName) const
{
    int count = 0;
    for (const auto &c : m_cModel->allContainers()) {
        if (c.sourceTemplate == templateName) count++;
    }
    return count;
}

QVariant TreeAssemblyModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    const quintptr id = index.internalId();

    if (id == 0) {
        if (role == Qt::DisplayRole && index.column() == NameColumn) {
            return QStringLiteral("%1 (%2)").arg(groupName(index.row())).arg(groupRowCount(index.row()));
        }
        if (role == Qt::FontRole && index.column() == NameColumn) {
            QFont f;
            f.setBold(true);
            return f;
        }
        if (role == Qt::AccessibleTextRole && index.column() == NameColumn) {
            return tr("%1 group, %2 items").arg(groupName(index.row())).arg(groupRowCount(index.row()));
        }
        return QVariant();
    }

    const int group = static_cast<int>(id - 1);
    const int row = index.row();

    if (group == 0) {
        const ContainerInfo c = m_cModel->containerAt(row);
        if (!c.isValid()) return QVariant();

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case NameColumn:        return c.name;
            case SourceColumn:      return c.sourceTemplate;
            case StateColumn:       return c.humanState();
            case RiskColumn:        return c.humanRiskLevel();
            case DescriptionColumn: {
                QString base = tr("%1 · %2").arg(c.kind, formatDiskUsage(c.diskUsage));
                if (c.needsRecovery) {
                    base += tr(" · Recovery needed");
                } else if (c.isOsOutdated()) {
                    base += tr(" · OS update (%1)").arg(c.osVersion);
                }
                return base;
            }
            }
        }
        if (role == Qt::DecorationRole && index.column() == StateColumn) {
            return ContainerModel::makeStateIcon(c.jailState);
        }
        if (role == Qt::DecorationRole && index.column() == RiskColumn) {
            return ContainerModel::makeRiskIcon(c.riskColor());
        }
        if (role == Qt::ToolTipRole) return c.accessibleDescription();
        if (role == Qt::AccessibleTextRole) return c.accessibleText();
        if (role == Qt::AccessibleDescriptionRole) return c.accessibleDescription();
    } else if (group == 1 || group == 2) {
        const QList<TemplateInfo> tmpls = visibleTemplatesForGroup(group);
        if (row < 0 || row >= tmpls.size()) return QVariant();
        const TemplateInfo &t = tmpls.at(row);

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case NameColumn:   return t.name;
            case SourceColumn: return t.shipped ? tr("Shipped") : tr("User");
            case StateColumn:  return t.humanState();
            case RiskColumn:   return t.kind;
            case DescriptionColumn: {
                const int uses = getUseCount(t.name);
                QString useStr = (uses == 1)
                    ? tr("1 compartment uses this. ")
                    : tr("%1 compartments use this. ").arg(uses);
                if (t.needsRecovery) {
                    useStr += tr("[Recovery needed] ");
                } else if (t.isOsOutdated() && !t.osVersion.isEmpty()) {
                    useStr += tr("[OS %1 -> %2] ").arg(t.osVersion, t.hostOsVersion);
                }
                return useStr + t.description;
            }
            }
        }
        if (role == Qt::ToolTipRole && index.column() == DescriptionColumn) return t.description;
        if (role == Qt::AccessibleTextRole) {
            return tr("%1, %2 template, %3").arg(t.name, t.kind, t.humanState());
        }
        if (role == Qt::AccessibleDescriptionRole) {
            const int uses = getUseCount(t.name);
            return tr("Template %1. %2. %3 compartments use this.")
                .arg(t.name, t.description)
                .arg(uses);
        }
    }
    return QVariant();
}

QVariant TreeAssemblyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
    case NameColumn:        return tr("Name");
    case SourceColumn:      return tr("Source");
    case StateColumn:       return tr("State");
    case RiskColumn:        return tr("Risk / Kind");
    case DescriptionColumn: return tr("Description");
    }
    return QVariant();
}

void TreeAssemblyModel::refresh()
{
    beginResetModel();
    endResetModel();
}

ContainerInfo TreeAssemblyModel::getContainer(const QModelIndex &index) const
{
    if (!index.isValid() || index.internalId() != 1) return ContainerInfo();
    return m_cModel->containerAt(index.row());
}

TemplateInfo TreeAssemblyModel::getTemplate(const QModelIndex &index) const
{
    if (!index.isValid()) return TemplateInfo();
    const quintptr id = index.internalId();
    if (id != 2 && id != 3) return TemplateInfo();
    const QList<TemplateInfo> tmpls = visibleTemplatesForGroup(static_cast<int>(id - 1));
    if (index.row() < 0 || index.row() >= tmpls.size()) return TemplateInfo();
    return tmpls.at(index.row());
}
