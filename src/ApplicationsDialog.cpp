#include "ApplicationsDialog.h"
#include "AppIcons.h"
#include "CatalogTableModel.h"
#include "ContainerBackend.h"
#include "PackageTableModel.h"
#include "ProcessLogDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

ApplicationsDialog::ApplicationsDialog(ContainerBackend *backend,
                                       const ContainerInfo &container,
                                       QWidget *parent)
    : QDialog(parent), m_backend(backend), m_container(container)
{
    setWindowTitle(tr("Applications in '%1'").arg(m_container.name));
    setMinimumSize(820, 540);
    buildUi();

    connect(m_backend, &ContainerBackend::packagesReady,
            this, &ApplicationsDialog::onPackagesReady);
    connect(m_backend, &ContainerBackend::operationFinished,
            this, &ApplicationsDialog::onOperationFinished);

    refresh();
    populateCatalog();
}

void ApplicationsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *capBanner = new QLabel(
        tr("Features: %1 · Risk level: %2")
            .arg(m_container.humanCapabilities(), m_container.humanRiskLevel()),
        this);
    capBanner->setWordWrap(true);
    capBanner->setAccessibleName(tr("Compartment features summary"));
    root->addWidget(capBanner);

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
    m_installedSummary = new QLabel(tr("Loading installed applications..."), parent);
    m_installedSummary->setWordWrap(true);
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
    m_installedView->setAccessibleName(tr("Installed applications and packages"));
    layout->addWidget(m_installedView, 1);

    auto *primaryButtons = new QHBoxLayout();
    m_launchBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("media-playback-start"), QStringLiteral("start")),
        tr("&Launch"), parent);
    m_addToMenuBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("application-menu"), QStringLiteral("menu-shortcut")),
        tr("Add to Start &Menu"), parent);
    m_addToMenuBtn->setToolTip(
        tr("Adds a shortcut to your desktop Start Menu (~/.local/share/applications/) for KDE Plasma, MATE, XFCE, and GNOME"));
    m_addToDesktopBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("user-desktop"), QStringLiteral("desktop-shortcut")),
        tr("Add &Desktop Shortcut"), parent);
    m_addToDesktopBtn->setToolTip(
        tr("Places a shortcut on your Desktop (~/Desktop) so you can open this app directly"));

    primaryButtons->addWidget(m_launchBtn);
    primaryButtons->addWidget(m_addToMenuBtn);
    primaryButtons->addWidget(m_addToDesktopBtn);
    primaryButtons->addStretch();
    layout->addLayout(primaryButtons);

    auto *manageButtons = new QHBoxLayout();
    m_updateBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("view-refresh"), QStringLiteral("refresh")),
        tr("&Update All"), parent);
    m_removeBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("edit-delete"), QStringLiteral("destroy")),
        tr("&Remove"), parent);
    auto *installNewBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("list-add"), QStringLiteral("new-compartment")),
        tr("&Install New..."), parent);
    manageButtons->addWidget(m_updateBtn);
    manageButtons->addWidget(m_removeBtn);
    manageButtons->addStretch();
    manageButtons->addWidget(installNewBtn);
    layout->addLayout(manageButtons);

    connect(m_updateBtn, &QPushButton::clicked, this, &ApplicationsDialog::onUpdateSelected);
    connect(m_removeBtn, &QPushButton::clicked, this, &ApplicationsDialog::onRemoveSelected);
    connect(m_launchBtn, &QPushButton::clicked, this, &ApplicationsDialog::onLaunchSelected);
    connect(m_addToMenuBtn, &QPushButton::clicked, this, &ApplicationsDialog::onAddMenuShortcut);
    connect(m_addToDesktopBtn, &QPushButton::clicked, this, &ApplicationsDialog::onAddDesktopShortcut);
    connect(installNewBtn, &QPushButton::clicked, this, [this]() {
        m_tabs->setCurrentIndex(1);
    });

    connect(m_installedView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ApplicationsDialog::onSelectionChanged);
    connect(m_installedView, &QTableView::doubleClicked,
            this, &ApplicationsDialog::onLaunchSelected);
}

void ApplicationsDialog::buildInstallNewTab(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);

    auto *searchRow = new QHBoxLayout();
    m_catalogSearch = new QLineEdit(parent);
    m_catalogSearch->setClearButtonEnabled(true);
    m_catalogSearch->setPlaceholderText(tr("Filter by application name, category, or description"));
    auto *searchLabel = new QLabel(tr("&Search:"), parent);
    searchLabel->setBuddy(m_catalogSearch);
    searchRow->addWidget(searchLabel);
    searchRow->addWidget(m_catalogSearch, 1);
    layout->addLayout(searchRow);

    m_catalogModel = new CatalogTableModel(this);
    m_catalogModel->setTargetContainer(m_container);

    m_catalogView = new QTableView(parent);
    m_catalogView->setModel(m_catalogModel);
    m_catalogView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_catalogView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_catalogView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_catalogView->setAlternatingRowColors(true);
    m_catalogView->horizontalHeader()->setStretchLastSection(true);
    m_catalogView->verticalHeader()->setVisible(false);
    m_catalogView->setSortingEnabled(true);
    m_catalogView->setAccessibleName(tr("Application catalog"));
    layout->addWidget(m_catalogView, 1);

    connect(m_catalogSearch, &QLineEdit::textChanged,
            m_catalogModel, &CatalogTableModel::setFilter);
    connect(m_catalogView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ApplicationsDialog::onCatalogSelectionChanged);

    auto *buttons = new QHBoxLayout();
    m_installFromFileBtn = new QPushButton(tr("Install from &File..."), parent);
    m_installCatalogBtn = new QPushButton(tr("Install &Selected from Catalog"), parent);
    m_installCatalogBtn->setEnabled(false);
    buttons->addWidget(m_installFromFileBtn);
    buttons->addStretch();
    buttons->addWidget(m_installCatalogBtn);
    layout->addLayout(buttons);

    connect(m_installFromFileBtn, &QPushButton::clicked,
            this, &ApplicationsDialog::onInstallFromFile);
    connect(m_installCatalogBtn, &QPushButton::clicked,
            this, &ApplicationsDialog::onInstallCatalog);
}

void ApplicationsDialog::refresh()
{
    m_backend->requestPackages(m_container.name);
}

void ApplicationsDialog::onPackagesReady(const QString &jail,
                                           const QList<PackageInfo> &pkgs,
                                           const QString &err)
{
    if (jail != m_container.name) return;

    if (!err.isEmpty()) {
        m_installedSummary->setText(tr("Could not load installed applications: %1").arg(err));
        updateActionStates();
        return;
    }

    m_installedModel->setPackages(pkgs);

    if (pkgs.isEmpty()) {
        if (m_container.isRunning()) {
            m_installedSummary->setText(
                tr("No applications installed yet. Open the 'Install New' tab to install an app."));
        } else {
            m_installedSummary->setText(
                tr("No applications listed yet (this compartment is stopped). "
                   "Start it first, then open 'Install New' to install apps."));
        }
        updateActionStates();
        return;
    }

    int native = 0, custom = 0, updatable = 0;
    for (const auto &p : pkgs) {
        if (p.isCustom()) ++custom;
        else ++native;
        if (p.hasUpdate()) ++updatable;
    }
    m_installedSummary->setText(
        tr("%1 installed package(s), %2 custom app(s), %3 update(s) available")
            .arg(native)
            .arg(custom)
            .arg(updatable));

    updateActionStates();
}

PackageInfo ApplicationsDialog::selectedPackage() const
{
    const auto sel = m_installedView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return PackageInfo();
    return m_installedModel->packageAt(sel.first().row());
}

void ApplicationsDialog::onSelectionChanged()
{
    updateActionStates();
}

void ApplicationsDialog::onCatalogSelectionChanged()
{
    const auto sel = m_catalogView->selectionModel()->selectedRows();
    if (sel.isEmpty() || m_container.isContained()) {
        m_installCatalogBtn->setEnabled(false);
        return;
    }
    m_installCatalogBtn->setEnabled(m_catalogModel->isRowCompatible(sel.first().row()));
}

void ApplicationsDialog::updateActionStates()
{
    const PackageInfo p = selectedPackage();
    const bool valid = p.isValid();
    const bool contained = m_container.isContained();
    const bool canLaunch = valid && !contained && !m_container.isVault && m_container.supportsGui();

    bool anyUpdatable = false;
    for (int r = 0; r < m_installedModel->rowCount(); ++r) {
        if (m_installedModel->packageAt(r).hasUpdate()) {
            anyUpdatable = true;
            break;
        }
    }

    m_updateBtn->setEnabled(anyUpdatable && !contained);
    m_removeBtn->setEnabled(valid && !contained);
    m_launchBtn->setEnabled(canLaunch);
    m_addToMenuBtn->setEnabled(canLaunch);
    m_addToDesktopBtn->setEnabled(canLaunch);
    m_installFromFileBtn->setEnabled(!contained);
}

void ApplicationsDialog::populateCatalog()
{
    m_catalogModel->reload();
}

void ApplicationsDialog::onUpdateSelected()
{
    m_backend->upgradePackages(m_container.name);
    auto *logDlg = new ProcessLogDialog(m_backend, tr("Upgrading packages in %1").arg(m_container.name), this);
    logDlg->setAttribute(Qt::WA_DeleteOnClose);
    logDlg->show();
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

    QString msg = tr("Remove the following from '%1'?\n").arg(m_container.name);
    if (!native.isEmpty())
        msg += tr("\nPackages:\n  %1").arg(native.join(QStringLiteral("\n  ")));
    if (!custom.isEmpty())
        msg += tr("\nCustom apps:\n  %1\n\n"
                  "Custom apps will only have their shortcut information removed; "
                  "their program files stay inside the compartment.").arg(custom.join(QStringLiteral("\n  ")));

    if (QMessageBox::question(this, tr("Remove"), msg) != QMessageBox::Yes) return;

    m_backend->removePackages(m_container.name, native, custom);
}

void ApplicationsDialog::onInstallCatalog()
{
    const auto sel = m_catalogView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;

    QString reason;
    if (!m_catalogModel->isRowCompatible(sel.first().row(), &reason)) {
        QMessageBox::warning(this, tr("Incompatible Application"), reason);
        return;
    }

    const QString pkg = m_catalogModel->packageAt(sel.first().row());
    if (pkg.isEmpty()) return;

    if (QMessageBox::question(this, tr("Install"),
        tr("Install '%1' in '%2'?").arg(pkg, m_container.name)) != QMessageBox::Yes) return;

    m_backend->installPackageAsync(m_container.name, pkg);
    auto *logDlg = new ProcessLogDialog(m_backend, tr("Installing %1").arg(pkg), this);
    logDlg->setAttribute(Qt::WA_DeleteOnClose);
    logDlg->show();
    m_tabs->setCurrentIndex(0);
}

bool ApplicationsDialog::checkFileCompatibility(const QString &filePath, QString *errorOut) const
{
    const QString lower = QFileInfo(filePath).fileName().toLower();
    if ((lower.endsWith(QStringLiteral(".exe")) || lower.endsWith(QStringLiteral(".msi"))) &&
        !m_container.supportsWindowsCompat()) {
        if (errorOut) {
            *errorOut = tr("'%1' does not have Windows app support enabled.\n\n"
                           "Create a compartment from a template with Windows support to run .exe or .msi files.")
                .arg(m_container.name);
        }
        return false;
    }
    if ((lower.endsWith(QStringLiteral(".deb")) || lower.endsWith(QStringLiteral(".rpm")) ||
         lower.endsWith(QStringLiteral(".appimage"))) &&
        !m_container.supportsLinuxCompat()) {
        if (errorOut) {
            *errorOut = tr("'%1' does not have Linux app support enabled.\n\n"
                           "Create a compartment from a template with Linux support for this file.")
                .arg(m_container.name);
        }
        return false;
    }
    return true;
}

void ApplicationsDialog::onInstallFromFile()
{
    const QString file = QFileDialog::getOpenFileName(this,
        tr("Choose an installer or archive"),
        QDir::homePath(),
        tr("Installers and archives (*.pkg *.txz *.exe *.msi *.run *.bin *.AppImage "
           "*.deb *.rpm *.zip *.tar.gz *.tgz *.tar.xz *.tar.bz2 *.rar);;All files (*)"));
    if (file.isEmpty()) return;

    QString compatErr;
    if (!checkFileCompatibility(file, &compatErr)) {
        QMessageBox::warning(this, tr("Incompatible Compartment"), compatErr);
        return;
    }

    m_backend->installFromFileAsync(m_container.name, file);
    auto *logDlg = new ProcessLogDialog(
        m_backend, tr("Installing %1").arg(QFileInfo(file).fileName()), this);
    logDlg->setAttribute(Qt::WA_DeleteOnClose);
    logDlg->show();
    m_tabs->setCurrentIndex(0);
}

void ApplicationsDialog::onLaunchSelected()
{
    const PackageInfo p = selectedPackage();
    if (!p.isValid()) return;

    if (m_container.isContained()) {
        QMessageBox::critical(this, tr("Compartment Blocked"),
            tr("Cannot launch applications from a Guardian-contained compartment."));
        return;
    }
    if (!m_container.supportsGui()) {
        QMessageBox::warning(this, tr("No Graphical Support"),
            tr("Compartment '%1' does not support graphical desktop apps.").arg(m_container.name));
        return;
    }

    m_backend->launchApplication(m_container.name, p.name, p.isCustom());
}

void ApplicationsDialog::onAddMenuShortcut()
{
    const PackageInfo p = selectedPackage();
    if (!p.isValid() || m_container.isContained() || m_container.isVault) return;
    m_backend->createLauncher(m_container.name, p.name, QStringLiteral("menu"));
}

void ApplicationsDialog::onAddDesktopShortcut()
{
    const PackageInfo p = selectedPackage();
    if (!p.isValid() || m_container.isContained() || m_container.isVault) return;
    m_backend->createLauncher(m_container.name, p.name, QStringLiteral("desktop"));
}

void ApplicationsDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
    Q_UNUSED(err);
    if (ok && (desc.startsWith(QStringLiteral("Adding ")) ||
               desc.startsWith(QStringLiteral("Creating Desktop shortcut")))) {
        QMessageBox::information(this, tr("Shortcut Created"),
            tr("%1 completed successfully.\n\n"
               "The shortcut will automatically start this compartment and open the app window when clicked.")
                .arg(desc));
        return;
    }
    if (ok) {
        emit applicationsChanged();
    }
    refresh();
}
