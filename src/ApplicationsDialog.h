#pragma once

#include <QDialog>
#include <QString>
#include "ContainerInfo.h"
#include "PackageInfo.h"

class QTabWidget;
class QTableView;
class QLineEdit;
class QLabel;
class QPushButton;

class ContainerBackend;
class PackageTableModel;
class CatalogTableModel;

class ApplicationsDialog : public QDialog {
    Q_OBJECT
public:
    ApplicationsDialog(ContainerBackend *backend,
                       const ContainerInfo &container,
                       QWidget *parent = nullptr);

signals:
    void applicationsChanged();

private slots:
    void refresh();
    void onPackagesReady(const QString &jail, const QList<PackageInfo> &pkgs, const QString &err);
    void onSelectionChanged();
    void onCatalogSelectionChanged();
    void onUpdateSelected();
    void onRemoveSelected();
    void onInstallCatalog();
    void onInstallFromFile();
    void onLaunchSelected();
    void onAddMenuShortcut();
    void onAddDesktopShortcut();
    void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
    void buildUi();
    void buildInstalledTab(QWidget *parent);
    void buildInstallNewTab(QWidget *parent);
    void populateCatalog();
    void updateActionStates();
    bool checkFileCompatibility(const QString &filePath, QString *errorOut) const;

    PackageInfo selectedPackage() const;

    ContainerBackend *m_backend;
    ContainerInfo m_container;

    QTabWidget *m_tabs = nullptr;

    QTableView *m_installedView = nullptr;
    PackageTableModel *m_installedModel = nullptr;
    QPushButton *m_updateBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_launchBtn = nullptr;
    QPushButton *m_addToMenuBtn = nullptr;
    QPushButton *m_addToDesktopBtn = nullptr;
    QLabel *m_installedSummary = nullptr;

    QLineEdit *m_catalogSearch = nullptr;
    QTableView *m_catalogView = nullptr;
    CatalogTableModel *m_catalogModel = nullptr;
    QPushButton *m_installCatalogBtn = nullptr;
    QPushButton *m_installFromFileBtn = nullptr;
};
