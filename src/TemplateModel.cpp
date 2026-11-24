#include "TemplateModel.h"
#include "ContainerBackend.h"

TemplateModel::TemplateModel(ContainerBackend *backend, QObject *parent)
    : QAbstractTableModel(parent), m_backend(backend)
{
    connect(m_backend, &ContainerBackend::templatesReady,
            this, [this](QList<TemplateInfo> list, QString err) {
        if (!err.isEmpty()) {
            m_lastError = err;
            return; // Keep old data on error
        }
        m_lastError.clear();
        m_allTemplates = list;
        applyFilter();
    });
}

void TemplateModel::applyFilter()
{
    beginResetModel();
    m_templates.clear();
    for (const auto &t : m_allTemplates) {
        if (m_filter.isEmpty() ||
            t.name.contains(m_filter, Qt::CaseInsensitive) ||
            t.description.contains(m_filter, Qt::CaseInsensitive) ||
            t.kind.contains(m_filter, Qt::CaseInsensitive)) {
            m_templates.append(t);
        }
    }
    endResetModel();
}

int TemplateModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_templates.size();
}

int TemplateModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 2;
}

QVariant TemplateModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_templates.size()) return QVariant();
    const TemplateInfo &t = m_templates.at(index.row());

    if (role == Qt::DisplayRole) {
        if (index.column() == 0) return t.name;
        if (index.column() == 1) return t.description;
    }
    if (role == Qt::AccessibleTextRole) {
        return tr("%1, %2 template, %3").arg(t.name, t.tier, t.humanState());
    }
    if (role == Qt::AccessibleDescriptionRole) {
        return tr("Template %1. Description: %2").arg(t.name, t.description);
    }
    return QVariant();
}

void TemplateModel::setFilter(const QString &text)
{
    m_filter = text;
    applyFilter(); // Instant local filtering without triggering pkexec
}

void TemplateModel::refresh()
{
    m_backend->requestTemplates();
}

QStringList TemplateModel::readyTemplateNames() const
{
    QStringList names;
    for (const auto &t : m_allTemplates) {
        if (t.isReady() && !t.hidden) names.append(t.name);
    }
    return names;
}

TemplateInfo TemplateModel::templateByName(const QString &name) const
{
    for (const auto &t : m_allTemplates) {
        if (t.name == name) return t;
    }
    return TemplateInfo();
}
