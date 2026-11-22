#pragma once
#include <QAbstractTableModel>
#include <QList>
#include <QIcon>
#include <QString>
#include "ContainerInfo.h"

class ContainerBackend;

class ContainerModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { IconColumn = 0, NameColumn, KindColumn, StateColumn, TemplateColumn, DiskUsageColumn, ColumnCount };
    explicit ContainerModel(ContainerBackend *backend, QObject *parent = nullptr);
    
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setFilter(const QString &text);
    void refresh();
    QString lastError() const { return m_lastError; }
    QList<ContainerInfo> allContainers() const { return m_rows; }
    ContainerInfo containerAt(int row) const;

private:
    static QIcon makeStateIcon(const QString &state);
    QIcon iconFor(const ContainerInfo &c) const;
    ContainerBackend *m_backend;
    QList<ContainerInfo> m_rows;
    QString m_filter;
    QString m_lastError;
};