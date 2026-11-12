#pragma once
#include <QAbstractTableModel>
#include "TemplateInfo.h"

class ContainerBackend;

class TemplateModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit TemplateModel(ContainerBackend *backend, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void refresh();
    TemplateInfo templateAt(int row) const;
    QList<TemplateInfo> allTemplates() const { return m_templates; }

private:
    ContainerBackend *m_backend;
    QList<TemplateInfo> m_templates;
};