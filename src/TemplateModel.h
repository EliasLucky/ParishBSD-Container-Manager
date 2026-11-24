#pragma once
#include <QAbstractTableModel>
#include <QString>
#include <QStringList>
#include "TemplateInfo.h"

class ContainerBackend;

class TemplateModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit TemplateModel(ContainerBackend *backend, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setFilter(const QString &text);
    void refresh();
    QString lastError() const { return m_lastError; }
    QList<TemplateInfo> allTemplates() const { return m_templates; }
    QStringList readyTemplateNames() const;
    TemplateInfo templateByName(const QString &name) const;

private:
    void applyFilter();

    ContainerBackend *m_backend;
    QList<TemplateInfo> m_allTemplates;
    QList<TemplateInfo> m_templates;
    QString m_filter;
    QString m_lastError;
};
