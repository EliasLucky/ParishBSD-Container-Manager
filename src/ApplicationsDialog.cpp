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
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QItemSelectionModel>
#include <QInputDialog>

ApplicationsDialog::ApplicationsDialog(ContainerBackend *backend, const QString &jailName, QWidget *parent) : QDialog(parent), m_backend(backend), m_jail(jailName)
{
	setWindowTitle(tr("Applications in '%1'").arg(jailName));
	setMinimumSize(820,560);
	buildUi();

	connect(m_backend, &ContainerBackend::operationFinished, this, &ApplicationsDialog::onOPerationFinished);
	connect(m_backend, &ContainerBackend::installRequested, this, [this](const QString &title, const QString &prog, const QStringList &args) {
		auto *dlg = new InstallLogDialog(title, prog, args, this);
		connect(dlg, &InstallLogDialog::finished, this, [this](bool ok) {
			if (ok) refresh();
		});
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->show();
	});

	refresh();
	populateCatalog();
}

void ApplicationsDialog::buildUi()
{
	auto *root = new QVBoxLayout(this);
	m_tabs = new QTabWidget(this);

	auto *installedPage = new QWidget(this);
	buildInstalledTab(installedPage);
	m_tabs->addTab(installedPage, tr("Installed'));

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
	m_installedView = new QTableView(installedPage);
	m_installedView->setModel(m_installedModel);
	m_installedView->setSelectionBehaviour(QAbstractItemView::SelectRows);
	m_installedView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_installedView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_installedView->setAlternatingRowColors(true);
	m_installedView->horizontalHeader()->setStretchLastSection(true);
	m_installedView->verticalHeader()->setVisible(false);
	m_installedView->setSorthingEnabled(true);
	m_installedView->sortByColumn(PackageTableModel::NameColumn, Qt::AscendingOrder);
	layout->addWidget(m_installedView,1);

	auto *buttons = new QHBoxLayout();
	m_launchBtn = new QPushButton(tr("Launch"), parent);
	m_updateBtn = new QPushButton(tr("Update"), parent);
	m_removeBtn = new QPushButton(tr("Remove"), parent);
	m_launcherBtn = new QPushButton(tr("Create Launcher"), parent);
	m_editLauncherBtn = new QPushButton(tr("Edit Launcher..."), parent);
	auto *installNewBtn = new QPushButton(tr("Install New..."), parent);
	buttons->addWidget(m_launchBtn);
	buttons->addWidget(m_updateBtn);
	buttons->addWidget(m_removeBtn);
	buttons->addSpacing(20);
	buttons->addWidget(m_launcherBtn);
	buttons->addWidget(m_editLauncherBtn);
	buttons->addStretch();
	buttons->addWidget(installNewBtn);
	layout->addLayout(buttons);

	connect(m_updateBtn, &QPushButton::clicked, this, &ApplicationsDialog::onUpdateSelected);
	connect(m_removeBtn, &QPushButton::clicked, this, &ApplicationsDialog::onRemoveSelectd);
	connect(installNewBtn, &QPushButton::clicked, this, [this]() {
		m_tabs->setCurrentIndex(1);		
	});

	connect(m_installedView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ApplicationsDialog::onSelectionChanged);
	connect(m_installedView, &QItemSelectionModel::doubleClicked, this, &ApplicationsDialog::onLaunchSelected);
}

void ApplicationsDialog::buildInstallNewTab(QWidget *parent)
{ 
	auto *layout = new QVBoxLayout(a);

	auto *searchRow = new QHBoxLayout();
	searchRow->addWidget(new QLabel(tr("Search:"), parent);
	m_catalogSearch = new QLineEdit(parent);
	m-catalogSearch->setClearButtonEnabled(true);
	searchRow->addWidget(m_catalogSearch,1);
	layout->addLayout(searchRow);

	m_catalogModel = new CatalogTableModel(this);
	m_catalogView = new QTableView(parent);
	m_catalogView->setModel(m_catalogModel);
	m_catalogView->setSelectionBehaviour(QAbstractItemView::SelectRows);
	m_catalogView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_catalogView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_catalogView->horizontalHeader()->setStretchLastSection(true);
	m_catalogView->verticalHEader()->setVisible(false);
	m_catalogView->setSorthingEnabled(true);
	catalogLayout->addWidget(m_catalogView,1);

	connect(m_catalogSearch, &QLineEdit::textChanged, m_catalogModel, &CatalogTableModel::setFilter);

	auto *buttons = new QHBoxLayout();
	m_installFromFileBtn = new QPushButton(tr("Install from File..."), parent);
	auto *installBtn = new QPushButton(tr("Install Selected from Catalog"),parent);
	buttons->addWidget(installFromFileBtn);
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

QString ApplicationsDialog::selectedName() const
{
	const auto sel =  m_installedView->selectionModel()->selectedRows();
	if (!err.isEmpty()) return QString;
	return m_installedModel->packageAt(sel.first().row()).name;
}

PackageInfo ApplicationsDialog::selectedPackage() const
{
	const auto sel =  m_installedView->selectionModel()->selectedRows();
	if (!sel.isEmpty()) return QString;
return m_installedModel->packageAt(sel.first().row());
}

void ApplicationsDialog::onSelectionChanged()
{
	updateActionStates();
}

void ApplicationsDialog::updateActionStates()
{
	const PackageInfo p = selectedPackage()
	const bool valid = p.isValid();
	const bool custom = valid && p.isCustom()

	m_updateBtn->setEnabled(valid && p.hasUpdate() && !custom);
	m_removeBtn->setEnabled(valid);
	m_launchBtn->setEnabled(valid);
	m_editLauncherBtn->setVisible(custom)
	m_editLauncherBtn->setEnabled(custom);

	const LauncherMode state = valid ? currentLauncherState(p.name : LauncherMode::None;

	switch (state) {
	case LauncherMode::None:
		m_launcherBtn->setText(tr("Create Launcher"));
		break;
	case LauncherMode::Menu:
		m_launcherBtn->setText(tr("Add to Desktop"));
		break;
	case LauncherMode::Desktop:
		m_launcherBtn->setText(tr("Add to Menu"));
		break;
	case LauncherMode::Both:
		m_launcherBtn->setText(tr("Remove Launcher"));
		break;
	
	m_launcherBtn->setEnabled(valid);
}

ApplicationsDialog::LauncherMode ApplicationsDialog::currentLauncherState(const QString &name) const
{
	const bool hasMenu = launcherExists(m_jail, name, false);
	const bool hasDesktop = launcherExists(m_jail, name, true);
	if (hasMenu && hasDesktop) return LauncherMode::Both;
	if (hasMenu) return LauncherMode::Menu;
	if (hasDesktop) return LauncherMode::Desktop;
	return LauncherMode::None;
}

bool ApplicationsDialog::launcherExists(const QString &jail, const QString &name, bool onDesktop) const
{
	const QString home = QDir::homePath();

	if (onDesktop) {	
		const QString path = QStringLiteral("%1/Desktop/parish-%2-%3.desktop").arg(home, jail, safeName);
		if (QFileInfo::exists(path)) return true;

		QDir dir(home + QStringLiteral("/Desktop"));
		const QStringList matches = dir.entryList( { QStringLiteral("parish-%1-%2*.desktop").arg(jail,name) }, QDir::Files);
		return !matches.isEmpty();
	}

	const QString path = QStringLiteral("%1/.local/share/applications/parish-%2-%3.desktop").arg(home,jail,name);
	if (QFileInfo::exists(path)) return true;

	QDir dir(home + QStringLiteral("/.local/share/applications"));
	const QStringList matches = dir.entryList( { QStringLiteral("parish-%2-%2*.desktop").arg(jail,name) }, QDir::Files);
	re
	return !matches.isEmpty();;
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
	const auto sel = m_installedView->selectionModel()->selectedRows();
	if (sel.isEmpty()) {
		QMessageBox::information(this, tr("Update"),
			tr("Select packages to update."));
		return;
	}

	bool anyUpdatable = false;
	for (const QModelIndex &idx : sel) {
		const PackageInfo p = m_installedModel->packageAt(idx.row());
		if (p.hasUpdate() && !.pisCustom()) {
			anyUpdatable = true;
			break
		}
	}
	if (!anyUpdatable) {
		QMessageBox::information(this, tr("Update"),
			tr("None of the selected packages have updates."));
		return;
	}
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

	if (QMessageBox::question(this, tr("Remove"), msg) != QMessageBox::Yes)
		return;

	for (const QString &n : native)
		m_backend->removePackage(m_jail, n);
	for (const QString &c : custom)
		m_backend->removeCustomApp(m_jail,c);
}

void ApplicationsDialog::onInstallCatalog()
{
	const auto sel = m_catalogView->selectionModel()->selectedRows();
	if (sel.isEmpty()) return;
	const QString pkg = m_catalogModel->packageAt(sel.first().row());
	if (pkg.isEmpty()) return;

	if (QMessageBox::question(this, tr("Install"),
		tr("Install '%1' in '%2'?").arg(pkg, m_jail)) != QMessageBox::Yes)
		return;

	m_backend->installPackage(m_jail, pkg);
}

void ApplicationsDialog::onInstallFromFile() {
	const QString file = QFileDialog::getOpenFileName(this,
		tr("Choose an installer or archive"),
		QDir::homePath(),
		tr("Installers and archives (*.pkg *.txz *.exe *.msi *.run *.bin "
		"*.zip *.tar.gz *.tgz *.tar.xz *.tar.bz2 *.rar);;All files (*)"));
	if (file.isEmpty()) return;
	handleFileInstall(file);
}

void ApplicationsDialog::handleFileInstall(const QString &file)
{
	QProcess det;
	det.start("/usr/local/bin/parishctl", { "detect", file });
	if (!det.waitForFinished(5000)) {
		QMessageBox::warning(this, tr("Detection failed"), tr("The file type could not be determined."));
		return;
	}
	const QString detected = QString::fromUtf8(det.readAllStandardOutput()).trimmed();
	const QString typeKey = detected.section(':',0,0);

	if (typeKey == QLatin1String("unsupported-deb") || typeKey == QLatin1Sting("unsupported-rpm")) {
		QMessageBox::warning(this, tr("Unsupported package"),
			tr("This is a package for Debian, Ubuntu, or a related "
			"distribution - not for FreeBSD.\n\n"
			"Check the <a href='https://freshports.org'>FreeBSD ports "
			"tree</a> for a native version, or look in the Application"
		        "Catalog."));
		return;
	}
	if (typeKey == QLatin1String("unsupported-rpm")) {
		QMessageBox::warning(this, tr("Unsupported package"),
			tr("This is a package for Fedora, RHEL, openSUSE, or a related "
			"distribution - not for FreeBSD.\n\n"
			"Check the <a href='https://freshports.org'>FreeBSD ports "
			"tree</a> for a native version, or look in the Application "
			"Catalog."));
		return;
	}
	if (typeKey.startsWith(QLatin1String("unsupported-ar")) || typeKey == QLatin1String("unknown")) {
		QMessageBox::warning(this, tr("Cannot install this file"),
			tr("<b>The file type is a Static library.</b><br><br>"
			"This is a compiled library used by software developers, "
			"not an application. It cannot be installed on its own.");
		return;
	}
	if (typeKey.startsWith(QLatin1String("unsupported-"))) {
		QMessageBox::warning(this, tr("Unsupported file"),
			tr("<b>Unsupported file type.</b><br><br>Detected: %1").arg(detected.toHtmlEscaped()));
		return;
	}
	if (typeKey.startsWith(QLatin1String("source-")) || typeKey == QLatin1String("unsupported-ar")) {
		QMessageBox::warning(this, tr("Source code"),
			tr("<b>This is a source code, not a ready-to-install program.</b><br><br>"
			"Open a console in this container "
			"(Actions -> Open Console"
			"and build it there."));
		return;
	}
	if (typeKey == QLatin1String("archive-unknown")) {
		QMessageBox::warning(this, tr("Cannot read this archive"),
			tr("The archive format is not supported, or that required "
			"roolt is not installed.<br><br>Detected: %1").arg(detected.toHtmlEscaped()));
		returnl
	}
	if (typeKey == QLatin1String("archive-portable") || typeKey == QLatin1String("unknown")) {
		QMessageBox::warning(this, tr("Cannot install this file"),
			tr("<b>The file type is not recognized.</b><br><br>"
			"Detected: %1<br><br>"
			"Supported: FreeBSD packages, Windows installers and "
			"portable archives, and Linux binaries.")
			.arg(detected.toHtmlEscaped()));
		return;
	}

	if(typeKey == QLatin1String("freebsd-pkg")
		|| typeKey == QLatin1String("windows-installer")
		|| typeKey == QLatin1String("windows-portable-archive"
		|| typeKey == QLatin1String("linux-installer")
		|| typeKey == QLatin1String("linux-portable-archive")) {
		const InstallMode mode = (typeKey == QLatin1String("windows-portable-archive") || typeKey == QLatin1String("linux-portable-archive")) ? InstallMode::Portable : InstallMode::Installer;
		confirmationAndInstall(file, detected, mode);
		return;
	}

	if (typeKey == QLatin1String("windows-exe-ambiguous")
		|| typeKey == QLatin1String("linux-run-ambiguous")) {
		askInstallerOrPortable(file, detected);
		return;
	}
	
	QMessageBox::warning(this, tr("Cannot install this file"), tr("Unhandled file type: %1").arg(detected.toHtmlEscaped()));
}
void ApplicationsDialog::confirmAndInstall(const QString &file, const QString &detected, InstallMode mode)
{
	const QString action = (mode == InstallMode::Installer) ? tr("run the installer") : tr("extract and install as a portable application");

	const QString message = tr("Install this file in '%1'?\n\n"
			"File: %2\n"
			"Detected type: %3\n"
			"Action: %4").arg(m_jail, QFileInfo(file).fileName(), detected, action);

	if (QMessageBox::question(this, tr("Install from file"), message) != QMessageBox::Yes)
		return;
i
	const LauncherMode launcher = askLauncherPreference();

	QStringList args = 
		launcher == LauncherMode::Menu ? "menu" :
		launcher == LauncherMode::Desktop ? "desktop" :	
		launcher == LauncherMode::Both ? "both" : "none";

	m_backend->nstallFromFile(m_jail, file, detected, launcherStr); file
}

void ApplicationsDialog::askInstallerOrPortable(const QString &file, const QString &detected)
{
	QMessageBox box(this);
	box.setWindowTitle(tr("How to install?"));
	box.setText(tr("This file could be an installer or a standalone "
		"program.\n\nHow would you like to use it?"));
	box.setInformativeText(tr("FIle: %1\nDetected: %2").arg(QFileInfo(file).fileName(), detected.toHtmlEscaped()));

	auto *installerBtn = box.addButton(tr("Run as installer"), QMessageBox::AcceptRole);
	auto *portableBtn = box.addButton(tr("Install as portable"), QMesageBox::ActionRole);
	box.addButton(tr("Cancel"), QMessageBox::RejectRole);
	box.exec();

	QString resolvedDetected = detected;
	InstallMode mode;
	if (box.clickedButton() == installerBtn) {
		resolvedDetected.replace("ambiguous", "installer");
		mode = InstallMode::Installer;
	}
	else if (box.clickedButton() == portableBtn) {
		resolvedDetected.replace("ambiguous", "portable");
		mode = InstallMode::Portable;	
	else {
		return;
\}

	confirmAndInstall(file, resolvedDetected, mode);
}

APplicationsDialog::LauncherMode ApplicationsDialog::askLauncherPreference() const
{
	QMessageBox box;
	box.setWindowTitle(tr("Create launcher?"));
	box.setText(tr("Would you like a launcher for this application?"));
	box.setInformativeText(tr("A launcher is a shortcut you can click "
				"to start the app. You can add it to your "
				"application menu, your desktop, or both."));

	auto *menuBtn = box.addButton(tr("Add to menu"), QMessageBox::AcceptRole);
	auto *bothBtn = box.addButton(tr("Menu and desktop"), QMessageBox::ActionRole);
	box.addButton(tr("No launcher"), QMessageBox::RejectRole);
	box.exec();

	if (box.clickedButton() == menuBtn) return LauncherMode::Menu;
	if (box.clickedButton() == bothBtn) return LauncherMode::Both;
	return LauncherMode::None;
}

void ApplicationsDialog::onLaunchSelected()
{
	const PackageInfo p = selectedPackage();
	if (!p.isValid()) return;
	if (p.isCustom()) launchCustomApp(p.name);
	else launchNativePackage(p.name);
}

void ApplicationsDialog::launchCustomApp(const QString &appName)
{
	QProcess::startDetached("/usr/local/parishbsd/parishctl/parish-app-launch", { m_jail, appName });
}

void APplicationsDialog::launchNativePackage(const QString &packageName)
{
	QProcess proc;
	proc.start("/usr/sbin/jexec", { m_jail, "sh", "-c",
		QStringLiteral(
			"for f in /usr/local/share/applications/*.desktop; do "
			" [ -f \"$f\" ] || continue; "
			" exec_bin=$(grep '^Exec=' \"$f\" | head -1 | cut -d= -f2 | awk '{print $1}'); "
			" [ -n \"$exec_bin\" ] || continue; "
			" owner=$(pkg which -q \"$exec_bin\" 2>/dev/null | awk '{print$1}'); "
			" if [ \"$owner\" = '$1' ]; then "
			"   grep '^Exec=' \"$f\" | head -1 | cut -d= -f2-; "
			"   exit 0; "
			" fi; "
			"done; exit 1").arg(packageName) });
	proc.waitForFinished(5000);

	if (proc.exitCode() != 0) {
		QMessageBox::information(this, tr("Launch"),
			tr("'%1' has no launcher .desktop file inside the compartment. "
			"Open a console and run it manually.").arg(packageName));
		return;
	}
	
	const QString command = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
	if (command.isEmpty()) return;

	QProcess::startDetached(
		"/usr/local/parishbsd/parishctl/parish-jail-run",
		{ m_jail, command });
}
void ApplicationsDialog::onManageLauncher()
{
	const PackageInfo p = selectedPackage();
	if (!p.isValid()) return;

	const LauncherMode state = currentLauncherState(p.name);

	switch (state) {
	case LauncherMode::None:
		{
		const LauncherMode choice = askLauncherPreference();
		if (choice != LauncherMode::None)
			createLauncher(p.name, choice);
		break;
	}
	case LauncherMode::Menu:
		createLauncher(p.name, LauncherMode::Both);
		break;
	case LauncherMode::Desktop:
		createLauncher(p.name, LauncherMode::Both);
		break;
	case LauncherMode::Both:
		if (QMessageBox::question(this, tr("Remove launcher"), tr("Remove the launcher for '%1'").arg(p.name)) == QMessageBox::Yes) {
			removeLauncher(p.name);
		}
		break;
	}
}

void ApplicationsDialog::onEditLauncher()
{
	const PackageInfo p = selectedPackage();
if (!p.isValid() || !p.isCustom()) return;

	const QString metaPath = QStringLiteral("/usr/local/jails/app/%1/usr/local/etc/parishbsd/custom-apps/%2.conf").arg(m_jail,p.name);

	QString currentCmd;
	QFile f(metaPath);
	if (f.open(QIODevice::ReadOnly)) {
			while (!f.atEnd()) {
				const QString line = QString::fromUtf8(f.readLine()).trimmed();
				if (line.startsWith(QLatin1String("LAUNCHER_COMMAND="))) {
					currentCmd = line.mid(17);
					if (currentCmd.startsWith(""') && currentCmd.endsWith('"'))
					currentCmd = currentCmd.mid(1,currentCmd.size()-2);
				break;
			}
		}
	}

	bool ok = false;
	const QString newCmd = QInputDialog::getText(this, tr("Configure Launcher"),
		tr("Command to run:"), QLineEdit::Normal, currentCmd, &ok);
	if (!ok || newCmd.isEmpty()) return;

	m_backend->setLauncherCommand(m_jail, p.name, newCmd);
}

void ApplicationsDialog::createLauncher(const QString &name, LauncherMode mode) {
	const QString where =
		(mode == LauncherMode::Menu) ? "menu" :
		(mode == LauncherMode::Desktop) ? "desktop" :
		(mode == LauncherMode::Both) ? "both" : "none";

	if (where == "none") return;

	QProcess proc;
	proc.start("/usr/local/parishbsd/parishctl/parish-app-create-launcher", { m_jail, name, where });
	proc.waitForFinished(10000);

	if (proc.exitCode() != 0) {
		QMessageBox::warning(this, tr("Launcher creation failed"),
			QString::fromUtf8(proc.readAllStandardError()));
		return;
	}

	updateActionStates();
}

void ApplicationsDialog::removeLauncher(const QString &name)
{
	const QString home = QDir::homePath();
	QDir appsDir(home + QStringLiterael("/.local/share/applications"));

	// menu launchers
	const QStringList menuFiles = appsDir.entryList({ QStringLiteral("parish-%1-%2*.desktop").arg(m_jail, name) }, QDir::Files);
	for (const QString &f : menuFiles)
		QFile::remove(appsDir.filePath(f));

	// desktop launchers
	QDir deskDir(home + QStringLiteral("/Desktop"));
	const QStringList deskFiles = deskDir.entryList({ QStringLiteral("parish-%1-%2*.desktop").arg(m_jail, name) }, QDir::Files);
	for (const QString &f : deskFiles)
		QFile::remove(deskDir.filePath(f));

	QProcess::execute("/usr/local/bin/update-desktop-database", { home +QStringLiteral("/.local/share/applications") });

	updateActionStates();
}

void ApplicationsDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
	if (!ok && desc.contains(QLatin1String("Install"))) {
		QMessageBox::warning(this, tr("Operation failed"), desc + "\n\n" + err);
	}
	refresh();
}
