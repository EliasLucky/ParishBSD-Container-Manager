#include "TreeAssemblyModel.h"
#include "ContainerModel.h"
#include "TemplateModel.h"
#include <QPainter>
#include <QPixmap>

TreeAssemblyModel::TreeAssemblyModel(ContainerModel *cModel, TemplateModel *tModel, QObject *parent)
    : QAbstractItemModel(parent), m_cModel(cModel), m_tModel(tModel) {
    connect(m_cModel, &QAbstractItemModel::modelReset, this, &TreeAssemblyModel::refresh);
    connect(m_tModel, &QAbstractItemModel::modelReset, this, &TreeAssemblyModel::refresh);
}

QModelIndex TreeAssemblyModel::index(int row, int column, const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent)) return QModelIndex();
    if (!parent.isValid()) return createIndex(row, column, quintptr(0));
    return createIndex(row, column, quintptr(parent.row() + 1));
}

QModelIndex TreeAssemblyModel::parent(const QModelIndex &index) const {
    if (!index.isValid()) return QModelIndex();
    quintptr id = index.internalId();
    if (id == 0) return QModelIndex();
    return createIndex(id - 1, 0, quintptr(0));
}

int TreeAssemblyModel::rowCount(const QModelIndex &parent) const {
    if (!parent.isValid()) return 3;
    if (parent.internalId() != 0) return 0;
    return groupRowCount(parent.row());
}

int TreeAssemblyModel::columnCount(const QModelIndex &parent) const {
    return ColumnCount;
}

int TreeAssemblyModel::groupRowCount(int group) const {
    if (group == 0) return m_cModel->rowCount();
    if (group == 1) {
        int c = 0; for (auto &t : m_tModel->allTemplates()) if (t.shipped && !t.hidden) c++;
        return c;
    }
    if (group == 2) {
        int c = 0; for (auto &t : m_tModel->allTemplates()) if (!t.shipped && !t.hidden) c++;
        return c;
    }
    return 0;
}

QString TreeAssemblyModel::groupName(int group) const {
    if (group == 0) return "Compartments";
    if (group == 1) return "Shipped Templates";
    if (group == 2) return "Custom Templates";
    return "";
}

int TreeAssemblyModel::getUseCount(const QString &templateName) const {
    int count = 0;
    for (const auto &c : m_cModel->allContainers()) {
        if (c.sourceTemplate == templateName) count++;
    }
    return count;
}

QVariant TreeAssemblyModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return QVariant();
    quintptr id = index.internalId();

    if (id == 0) {
        if (role == Qt::DisplayRole && index.column() == 0) return groupName(index.row());
        return QVariant();
    }

    int group = id - 1;
    int row = index.row();

    if (group == 0) {
        ContainerInfo c = m_cModel->containerAt(row);
        if (role == Qt::DisplayRole) {
            if (index.column() == NameColumn) return c.name;
            if (index.column() == SourceColumn) return c.sourceTemplate;
            if (index.column() == StateColumn) return c.humanState();
            if (index.column() == DescriptionColumn) return QString("Kind: %1").arg(c.kind);
        }
        if (role == Qt::ToolTipRole && index.column() == DescriptionColumn) return c.accessibleDescription();
        if (role == Qt::AccessibleTextRole) return c.accessibleText();
        if (role == Qt::AccessibleDescriptionRole) return c.accessibleDescription();
        if (role == Qt::DecorationRole && index.column() == NameColumn) {
            QPixmap pm(16,16); pm.fill(c.isVault ? Qt::darkYellow : Qt::darkBlue);
            return QIcon(pm);
        }
    } else {
        QList<TemplateInfo> tmpls;
        for (auto &t : m_tModel->allTemplates()) if ((group==1 && t.shipped) || (group==2 && !t.shipped)) tmpls.append(t);
        if (row >= tmpls.size()) return QVariant();
        TemplateInfo t = tmpls.at(row);

        if (role == Qt::DisplayRole) {
            if (index.column() == NameColumn) return t.name;
            if (index.column() == SourceColumn) return QString();
            if (index.column() == StateColumn) return t.humanState();
            if (index.column() == DescriptionColumn) {
                int uses = getUseCount(t.name);
                QString useStr = uses == 1 ? "1 compartment uses this. " : QString("%1 compartments use this. ").arg(uses);
                return useStr + t.description;
            }
        }
        if (role == Qt::ToolTipRole && index.column() == DescriptionColumn) return t.description;
        if (role == Qt::AccessibleTextRole) return QString("%1, %2").arg(t.name, t.humanState());
        if (role == Qt::AccessibleDescriptionRole) {
            int uses = getUseCount(t.name);
            return QString("Template %1. %2. %3 compartments use this.")
                .arg(t.name, t.description).arg(uses);
        }
    }
    return QVariant();
}

QVariant TreeAssemblyModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
        case NameColumn: return "Name";
        case SourceColumn: return "Source";
        case StateColumn: return "State";
        case DescriptionColumn: return "Description";
    }
    return QVariant();
}

void TreeAssemblyModel::refresh() {
    beginResetModel();
    endResetModel();
}

ContainerInfo TreeAssemblyModel::getContainer(const QModelIndex &index) const {
    if (!index.isValid() || index.internalId() != 1) return ContainerInfo();
    return m_cModel->containerAt(index.row());
}