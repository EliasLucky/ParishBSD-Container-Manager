#include "ApplicationsDialog.h"
#include "ContainerBackend.h"
#include "PackageTableModel.h"
#include "CatalogTableModel.h"

#include <QTabWidget>
#include <QTableView>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QItemSelectionModel>

ApplicationsDialog::ApplicationsDialog(ContainerBackend *backend, const QString &jailName, QWidget *parent) : QDialog(parent), m_backend(backend), m_jail(jailName)
{
    setWindowTitle(tr("Applications in '%1'").arg(jailName));
    setMinimumSize(820,560);
    buildUi();

    connect(m_backend, &ContainerBackend::operationFinished, this, &ApplicationsDialog::onOperationFinished);

    refresh();
    populateCatalog();
}

void ApplicationsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    auto *installedPage = new QWidget(this);
    buildInstalledTab(installedPage);
    m_tabs->addTab(installedPage, tr("Installed"));

    auto *catalogPage = new QWidget(this);
    buildInstallNewTab(catalogPage);
    m_tabs->addTab(catalogPage, tr("Install New"));

    root->addWidget(m_tabs);

    auto *bottom = new QHBoxLayout();
    bottom->addStretch();
    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(closeBtn);
    root->addLayout(bottom);
}

void ApplicationsDialog::buildInstalledTab(QWidget *parent)
{   
    auto *layout = new QVBoxLayout(parent);
    m_installedSummary = new QLabel(parent);
    layout->addWidget(m_installedSummary);
    
    m_installedModel = new PackageTableModel(this);
    m_installedView = new QTableView(parent);
    m_installedView->setModel(m_installedModel);
    m_installedView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_installedView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_installedView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_installedView->setAlternatingRowColors(true);
    m_installedView->horizontalHeader()->setStretchLastSection(true);
    m_installedView->verticalHeader()->setVisible(false);
    m_installedView->setSortingEnabled(true);
    m_installedView->sortByColumn(PackageTableModel::NameColumn, Qt::AscendingOrder);
    layout->addWidget(m_installedView,1);

    auto *buttons = new QHBoxLayout();
    m_launchBtn = new QPushButton(tr("Launch"), parent);
    m_updateBtn = new QPushButton(tr("Update"), parent);
    m_removeBtn = new QPushButton(tr("Remove"), parent);
    auto *installNewBtn = new QPushButton(tr("Install New..."), parent);
    buttons->addWidget(m_launchBtn);
    buttons->addWidget(m_updateBtn);
    buttons->addWidget(m_removeBtn);
    buttons->addSpacing(20);
    buttons->addStretch();
    buttons->addWidget(installNewBtn);
    layout->addLayout(buttons);

    connect(m_updateBtn, &QPushButton::clicked, this, &ApplicationsDialog::onUpdateSelected);
    connect(m_removeBtn, &QPushButton::clicked, this, &ApplicationsDialog::onRemoveSelected);
    connect(m_launchBtn, &QPushButton::clicked, this, &ApplicationsDialog::onLaunchSelected);
    connect(installNewBtn, &QPushButton::clicked, this, [this]() {
        m_tabs->setCurrentIndex(1);     
    });

    connect(m_installedView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ApplicationsDialog::onSelectionChanged);
    connect(m_installedView, &QTableView::doubleClicked, this, &ApplicationsDialog::onLaunchSelected);
}

void ApplicationsDialog::buildInstallNewTab(QWidget *parent)
{ 
    auto *layout = new QVBoxLayout(parent);

    auto *searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel(tr("Search:"), parent));
    m_catalogSearch = new QLineEdit(parent);
    m_catalogSearch->setClearButtonEnabled(true);
    searchRow->addWidget(m_catalogSearch,1);
    layout->addLayout(searchRow);

    m_catalogModel = new CatalogTableModel(this);
    m_catalogView = new QTableView(parent);
    m_catalogView->setModel(m_catalogModel);
    m_catalogView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_catalogView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_catalogView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_catalogView->horizontalHeader()->setStretchLastSection(true);
    m_catalogView->verticalHeader()->setVisible(false);
    m_catalogView->setSortingEnabled(true);
    layout->addWidget(m_catalogView,1);

    connect(m_catalogSearch, &QLineEdit::textChanged, m_catalogModel, &CatalogTableModel::setFilter);

    auto *buttons = new QHBoxLayout();
    m_installFromFileBtn = new QPushButton(tr("Install from File..."), parent);
    auto *installBtn = new QPushButton(tr("Install Selected from Catalog"),parent);
    buttons->addWidget(m_installFromFileBtn);
    buttons->addStretch();
    buttons->addWidget(installBtn);
    layout->addLayout(buttons);

    connect(m_installFromFileBtn, &QPushButton::clicked, this, &ApplicationsDialog::onInstallFromFile);
    connect(installBtn, &QPushButton::clicked, this, &ApplicationsDialog::onInstallCatalog);
}

void ApplicationsDialog::refresh()
{
    populateInstalled();
}

PackageInfo ApplicationsDialog::selectedPackage() const
{
    const auto sel =  m_installedView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return PackageInfo();
    return m_installedModel->packageAt(sel.first().row());
}

void ApplicationsDialog::onSelectionChanged()
{
    updateActionStates();
}

void ApplicationsDialog::updateActionStates()
{
    const PackageInfo p = selectedPackage();
    const bool valid = p.isValid();
    const bool custom = valid && p.isCustom();

    m_updateBtn->setEnabled(valid && p.hasUpdate() && !custom);
    m_removeBtn->setEnabled(valid);
    m_launchBtn->setEnabled(valid);
}

void ApplicationsDialog::populateInstalled()
{
    QString err;
    QList<PackageInfo> pkgs = m_backend->listPackages(m_jail, &err);
    m_installedModel->setPackages(pkgs);

    if (m_installedSummary) {
        int native = 0, custom = 0, updatable = 0;
        for (const auto &p : pkgs) {
            if (p.isCustom()) ++custom; else ++native;
            if (p.hasUpdate()) ++updatable;
        }
        m_installedSummary->setText(
            tr("%1 native package(s), %2 custom app(s), %3 update(s) available").arg(native, custom, updatable));
    }

    if (!err.isEmpty())
        QMessageBox::warning(this, tr("Package list"), err);

    updateActionStates();
}

void ApplicationsDialog::populateCatalog()
{
    m_catalogModel->reload();
}

void ApplicationsDialog::onUpdateSelected()
{
    m_backend->upgradePackages(m_jail);
}

void ApplicationsDialog::onRemoveSelected()
{
    const auto sel = m_installedView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;

    QStringList native, custom;
    for (const QModelIndex &idx : sel) {
        const PackageInfo p = m_installedModel->packageAt(idx.row());
        if (p.isCustom()) custom << p.name;
        else native << p.name;
    }

    QString msg = tr("Remove the following from '%1'?\n").arg(m_jail);
    if (!native.isEmpty())
        msg += tr("\nPackages:\n  %1").arg(native.join("\n  "));    
    if (!custom.isEmpty())
        msg += tr("\nCustom apps:\n  %1\n\n"
            "Custom apps will only have their metadata removed; "
            "their files stay in the container.").arg(custom.join("\n  "));

    if (QMessageBox::question(this, tr("Remove"), msg) != QMessageBox::Yes) return;

    for (const QString &n : native) m_backend->removePackage(m_jail, n);
    for (const QString &c : custom) m_backend->removeCustomApp(m_jail, c);
}

void ApplicationsDialog::onInstallCatalog()
{
    const auto sel = m_catalogView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const QString pkg = m_catalogModel->packageAt(sel.first().row());
    if (pkg.isEmpty()) return;

    if (QMessageBox::question(this, tr("Install"),
        tr("Install '%1' in '%2'?").arg(pkg, m_jail)) != QMessageBox::Yes) return;

    m_backend->installPackage(m_jail, pkg);
}

void ApplicationsDialog::onInstallFromFile() {
    const QString file = QFileDialog::getOpenFileName(this,
        tr("Choose an installer or archive"),
        QDir::homePath(),
        tr("Installers and archives (*.pkg *.txz *.exe *.msi *.run *.bin "
        "*.zip *.tar.gz *.tgz *.tar.xz *.tar.bz2 *.rar);;All files (*)"));
    if (file.isEmpty()) return;
    QMessageBox::information(this, tr("Not implemented"), tr("File install dialog is pending."));
}

void ApplicationsDialog::onLaunchSelected()
{
    const PackageInfo p = selectedPackage();
    if (!p.isValid()) return;
    QMessageBox::information(this, tr("Launch"), tr("Launching '%1' is implemented in F4.").arg(p.name));
}

void ApplicationsDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
    if (!ok) {
        QMessageBox::warning(this, tr("Operation failed"), desc + "\n\n" + err);
    }
    refresh();
}