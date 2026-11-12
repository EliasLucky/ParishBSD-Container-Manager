#pragma once
#include <QAbstractItemModel>
#include <QIcon>
#include "ContainerInfo.h"
#include "TemplateInfo.h"

class ContainerModel;
class TemplateModel;

class TreeAssemblyModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { NameColumn = 0, SourceColumn, StateColumn, DescriptionColumn, ColumnCount };

    explicit TreeAssemblyModel(ContainerModel *cModel, TemplateModel *tModel, QObject *parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void refresh();

private:
    ContainerModel *m_cModel;
    TemplateModel *m_tModel;
    
    // 0 = Compartments, 1 = Shipped Templates, 2 = Custom Templates
    int groupRowCount(int group) const;
    QString groupName(int group) const;
};