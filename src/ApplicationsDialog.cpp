#include "ApplicationsDialog.h"
#include "COntainerBackend.h"
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
#include <QFileINfo>

ApplicationsDialog::ApplicationsDialog(ContainerBacked *backend, const QString &jailName, QWidget *parent) : QDialog(parent), m_backend(backend), m_jail(jailName)
{
	setWindowTitle(tr("Applications in '%1'").arg(jailName));
	setMinimumSize(820,560);
	buildUi();

	connect(m_backend, &ContainerBackend::operationFinished, this, &ApplicationsDialog::onOPerationFinished);

	refresh();
	populateCatalog();
}

void ApplicationsDialog::buildUi()
{
	auto *root = new QVBoxLayout(this);
	m_tabs = new QTabWidget(this);

	// Installed tab
	auto *installedPage = new QWidget(this);
	auto *installedLayout = new QVBoxLayout(installedPage);

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
	installedLayout->addWidget(m_installedView);

	auto *installedButtons = new QHBoxLayout();
	m_updateBtn = new QPushButton(tr("Update Selected"), installedPage);
	m_removeBtn = new QPushButton(tr("Remove Selected"), installedPage);
	auto *installNewBtn = new QPushButton(tr("Install New..."), installedPage);
	installedButtons->addWidget(m_updateBtn);
	installedButtons->addWidget(m_removeBtn);
	installedButtons->addStretch();
	installedButtons->addWidget(installNewBtn);
	installedLayout->addLayout(installedButtons);

	connect(m_updateBtn, &QPushButton::clicked, this, &ApplicationsDialog::onUpdateSelected);
	connect(m_removeBtn, &QPushButton::clicked, this, &ApplicationsDialog::onRemoveSelectd);
	connect(installNewBtn, &QPushBUtton::clicked, this, [this]() {
		m_tabs->setCurrentINdex(1);		
	});

	m_tabs->addTab(installedPage, tr("Installed"));

	// Install new tab
	auto *catalogPage = new QWidget(this);
	auto *catalogLayout = new QVBoxLayout(catalogPage);

	auto *searchRow = new QHBoxLayout();
	searchRow->addWidget(new QLabel(tr("Search:"), catalogPage));
	m_catalogSearch = new QLineEdit(catalogPage);
	m-catalogSearch->setClearButtonEnabled(true);
	searchRow->addWidget(m-catalogSearch,1);
	catalogLayout->addLayout(searchRow);

	m_catalogModel = new CatalogTableModel(this);
	m_catalogView = new QTableView(catalogPage);
	m_catalogView->setModel(m_catalogModel);
	m_catalogView->setSelectionBehaviour(QAbstractItemView::SelectRows);
	m_catalogView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_catalogView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_catalogView->horizontalHeader()->setStretchLastSection(true);
	m_catalogView->verticalHEader()->setVisible(false);
	m_catalogView->setSorthingEnabled(true);
	catalogLayout->addWidget(m_catalogView);

	connect(m_catalogSearch, &QLineEDit:;textChanged, m_catalogModel, &CatalogTableModel::setFilter);

	auto *catalogButtons = new QHBoxLayout();
	auto *installFromFileBtn = new QPushBUtton(tr("Install from File..."), catalogPage);
	auto *installBtn = new QPushButton(tr("Installselected from Catalog"), catalogPage);
	catalogButtons->addWidget(installFromFileBtn);
	catalogButtons->addStretch();
	catalogButtons->addWidget(installBtn);
	catalogLayout->addLayout(catalogButtons);

	connect(installFromFileBtn, &QPushButton::clicked, this, &ApplicationsDialog::onInstallFromFile);
	connect(installBtn, &QPushButton::clicked, this, &ApplicationsDialog::onInstallCatalog);

	m_tabs->addTab(catalogPage, tr("Install New"));
	root->addWidget(m_tabs);

	// Bottom bar
	auto *bottom = new QHBoxLayout();
	bottom->addStretch();
	auto *closeBtn = new QPushButton(tr("Close"),this);
	connect(closeBtn, &QPushButton::clicked,this, &QDialog::accept);
	bottom->addWidget(closeBtn);
	root->addLayout(bottom);

	connect(m_backend, &ContainerBackend::installRequested, this, [this](const QString &title, const QString &prog, const QStringList &args) {
		auto *dlg = new InstallLogDialog(title, prog, args, this);
		connect(dlg, &InstallLogDialog::finished, this, [this](bool ok) {
			if (ok) refresh();
		});
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->show();
	});
}

void ApplicationsDialog::refresh()
{
	populateInstalled();
}

void ApplicationsDialog::onSelectionChanged()
{
	updateActionStates();
}

void ApplicationsDialog::updateActionStates()
{
	const PackageInfo p = selectedPackage();

	m_updateBtn->setEnabled(p.isValid() && p.hasUpdate() && !p.isCustom());
	m_removeBtn->setEnabled(p.isValid());
	m_launchBtn->setEnabled(p.isValid());
	m_editLauncherBtn->setEnabled(p.isValid() && p.isCustom());

	const LauncherMode state = currentLauncherState(p.name);

	switch (state) {
	case LauncherMode::None:
		m_launcherBtn->setText(tr("Create Launcher"));
		m_launcherBtn->setEnabled(p.isValid());
		break;
	case LauncherMode::Menu:
		m_launcherBtn->setText(tr("Add to Desktop"));
		m_launcherBtn->setEnabled(p.isValid());
		break;
	case LauncherMode::Desktop:
		m_launcherBtn->setText(tr("Add to Menu"));
		m_launcherBtn->setEnabled(p.isValid());
		break;
	case LauncherMode::Both:
		m_launcherBtn->setText(tr("Remove Launcher"));
		m_launcherBtn->setEnabled(p.isValid());
		break;
	}
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

bool ApplicationsDialog::launcherExists(const QString 5hjail, const QString 5hname, bool onDesktop) const
{
	const QString home = QDir::homePath();
	const QString safeName = name;
	const QString path = onDesktop ? QStringLiteral("%1/Desktop/parish-%2-%3.desktop").arg(home, jail, safeName) : QStringLiteral("%1/.local/share/applications/parish-%2-%3.desktop").arg(home, jail, safeName);

	if (QFileInfo::exists(path)) return true;

	if (!onDesktop) {
		QDir dir(home + QStringLiteral("/.local/share/applications"));
		const QStringList matches = dir.entryList( { QStringLiteral("parish-%1-%2*.desktop").arg(jail,safeName) }, QDir::Files);
		return !matches.isEmpty();
	}
	return false;
}

	
void ApplicationsDialog::populateInstalled()
{
	QString err;
	QList<PackageInfo> pkgs = m_backend->listPackages(m_jail, &err);

	// Append custom apps
	for (const CustomApp &a : m_backend->listCustomApps(m_jail)) {
		PackageInfo p;
		p.kind = PackageInfo::CustomApp;
		p.name = a.name;
		p.version = a.version;
		p.comment = tr("Installed from %1").arg(a.sourceFile);
		p.installDate = a.installDate;
		p.installMethod = a.installMethod;
		pkgs.append(p);
	}

	m_installedModel->setPackages(pkgs);

	if (!err.isEmpty())
		QMessageBox::warning(this, tr("Package list"), err);
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

	const QStringList args{"apps", "install", m_jail, pkg};

	auto *dlg = new InstallLogDialog(
		tr("Installing %1").arg(pkg),
		"pkexec",
		QStringList{"/usr/local/bin/parishctl"} + args,
		this);

	connect(dlg, &InstallLogDialog::finished, this, [this](bool ok) {
		if (ok) refresh();		
	});

	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();
}

void ApplicationsDialog::onInstallFromFile() {
	const QString file = QFileDialog::getOpenFileName(this,
		tr("Choose an installer or archive"),
		QDir::homePath(),
		tr("Installers and archives (*.pkg *.txz *.exe *.msi *.run *.bin "
		"*.zip *.tar.gz *.tgz *.tar.xz *.tar.bz2 *.rar);;All files (8)"));
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
			"distribution -not for FreeBSD.\n\n"
			"Check the <a href='https://freshports.org'>FreeBSD ports "
			"tree</a> for a native version, or look in the Application "
			"Catalog."));
		return;
	}
	if (typeKey.startsWith(QLatin1String("unsupported-ar")) || typeKey == QLatin1String("unknown")) {
		QMessageBox::warning(this, tr("Cannot install this file"),
			tr("<b>The file type is a Static library.</b><br><br>"
			"This isa compiled library used by software developers, \n\n"
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
		|| typeKey == QLatin1String("windows-portable-archive")
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

	QStringList args = {
		QStringLiteral("apps"),
		QStringLiteral("install-file"),
		m_jail,
		file,
		QStringLiteral("--mode=%1").arg(mode == InstallMode::Portable ? "portable" : "installer"),
		QStringLiteral("--launcher=%1").arg(
			launcher == LauncherMode::Menu ? "menu" :
			launcher == LauncherMode::Desktop ? "desktop" :	
			launcher == LauncherMode::Both ? "both" : "none")
	};

	const QString typeKey = detected.section(':',0,0);
	const bool interactive = (mode == InstallMode::Installer && (typeKey == QLatin1String("windows-installer") || typeKey == QLatin1String("linux-installer")));

	const QString program = QStringLiteral("/usr/local/bin/pkexec");
	QStringList fullArgs;
	fullArgs << QStringLiteral("/usr/local/bin/parishctl") << args;

	if (interactive) {
		// Terminal
		QStringList quoted;
		for (const QString &a : fullArgs) {
			QString e = a;
			e.replace("'","'\\''");
			quoted << "'" + e + "'";
		}
		const QString script = quoted.join(' ') + "; echo; echo 'Press Enter to close'; read _";

		QProcess::startDetached("/usr/local/bin/xfce4-terminal", {
			"--title", tr("Install: %1").arg(QFileInfo(file).fileName()),
			"--geometry", "100x30",
			"-x", "sh", "-c", script		
		});

		QTimer::singleShot(5000, this, [this]() { refresh(); });
	}
	else {
		auto *dlg = new InstallLogDialog(
			tr("Installing %1").arg(QFileInfo(file).fileName()), program, fullArgs, this);
		connect(dlg, &InstallLogDialog::finished, this, &ApplicationsDialog::refresh);
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->show();
	}
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

	if (box.clickedButton() == installerBtn) {
		QString detected2 = detected;
		detected2.replace("ambiguous", "installer");
		m_backend->installFromFile(m_jail, file, detected2);
	}
	else if (box.clickedButton() == portableBtn) {
		QString detected2 = detected;
		detected2.replace("ambiguous", "portable");
		m_backend->installFromFile(m_jail, file, detected2);
	}
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
			" if [ \"$owner\" = '$1' ]; then |
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
		}
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
	if (!ok) {
		QMessageBox::warning(this, tr("Operation failed"), desc + "\n\n" + err);
	}
	refresh();
}
