#include "TemplateModel.h"
#include "ContainerBackend.h"

TemplateModel::TemplateModel(ContainerBackend *backend, QObject *parent) 
    : QAbstractTableModel(parent), m_backend(backend) {}

int TemplateModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_templates.size();
}

int TemplateModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 2; 
}

QVariant TemplateModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_templates.size()) return QVariant();
    const TemplateInfo &t = m_templates.at(index.row());

    if (role == Qt::DisplayRole) {
        if (index.column() == 0) return t.name;
        if (index.column() == 1) return t.description;
    }
    
    if (role == Qt::AccessibleTextRole) {
        return QString("%1, %2 template, %3").arg(t.name, t.tier, t.humanState());
    }
    if (role == Qt::AccessibleDescriptionRole) {
        return QString("Template %1. Description: %2").arg(t.name, t.description);
    }
    return QVariant();
}

void TemplateModel::setFilter(const QString &text) {
    m_filter = text;
    refresh();
}

void TemplateModel::refresh() {
    const QList<TemplateInfo> all = m_backend->listTemplates();
    beginResetModel();
    m_templates.clear();
    for (const auto &t : all) {
        if (m_filter.isEmpty() || t.name.contains(m_filter, Qt::CaseInsensitive)) {
            m_templates.append(t);
        }
    }
    endResetModel();
}

TemplateInfo TemplateModel::templateAt(int row) const {
    if (row < 0 || row >= m_templates.size()) return TemplateInfo();
    return m_templates.at(row);
}