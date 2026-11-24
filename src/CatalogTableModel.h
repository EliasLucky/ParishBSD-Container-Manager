#pragma once
#include <QAbstractTableModel>
#include <QStringList>
#include "ContainerInfo.h"

struct CatalogEntry {
    QString category;
    QString name;
    QString package;
    QString description;
    QString requiresCap; // "-", "gui", "compat-windows", "compat-linux"
};

class CatalogTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        NameColumn = 0,
        CategoryColumn,
        RequirementColumn,
        DescriptionColumn,
        ColumnCount
    };

    explicit CatalogTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void reload();
    void setFilter(const QString &text);
    void setTargetContainer(const ContainerInfo &container);

    QString packageAt(int row) const;
    bool isRowCompatible(int row, QString *reasonOut = nullptr) const;

private:
    bool entryCompatible(const CatalogEntry &e, QString *reasonOut) const;

    ContainerInfo m_target;
    QList<CatalogEntry> m_all;
    QList<CatalogEntry> m_filtered;
};
