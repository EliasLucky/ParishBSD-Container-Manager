#pragma once

#include <QDialog>
#include <QString>

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
    ApplicationsDialog(ContainerBackend *backend, const QString &jailName, QWidget *parent = nullptr);

signals:
    void applicationsChanged();

private slots:
    void refresh();
    void onSelectionChanged();
    void onUpdateSelected();
    void onRemoveSelected();
    void onInstallCatalog();
    void onInstallFromFile();
    void onLaunchSelected();
    void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
    void buildUi();
    void buildInstalledTab(QWidget *parent);
    void buildInstallNewTab(QWidget *parent);
    void populateInstalled();
    void populateCatalog();
    void updateActionStates();

    PackageInfo selectedPackage() const;

    ContainerBackend *m_backend;
    QString m_jail;

    QTabWidget *m_tabs = nullptr;

    QTableView *m_installedView = nullptr;
    PackageTableModel *m_installedModel = nullptr;
    QPushButton *m_updateBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_launchBtn = nullptr;
    QLabel *m_installedSummary = nullptr;

    QLineEdit *m_catalogSearch = nullptr;
    QTableView *m_catalogView = nullptr;
    CatalogTableModel *m_catalogModel = nullptr;
    QPushButton *m_installFromFileBtn = nullptr;
};