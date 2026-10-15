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
}

void ApplicationsDialog::refresh()
{
	populateInstalled();
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

	m_backend->installPackage(m_jail, pkg);
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
	det.start("/usr/local/parishbsd/scripts/parish-detect-installer.sh", { file });
	det.waitForFinished(5000);
	const QString detected = QString::fromUtf8(det.readAllStandardOutput()).trimmed();
	const QString typeKey = detected.section(':',0,0);

	if (typeKey == QLatin1String("linux-deb") || typeKey == QLatin1Sting("linux-rpm")) {
		QMessageBox::warning(this, tr("Unsupported package"),
			tr("This is a package for a different operating system.\n\n"
			"ParishBSD runs FreeBSD software. Look in the catalog "
			"for a FreeBSD version, or search the FReeBSD ports tree "
			"at freshprots.org."));
		return;
	}
	if (typeKey.startsWith(QLatin1String("source-"))) {
		QMessageBox::warning(this, tr("Source code"),
			tr("This is a source code, not a ready-install program.\n\n"
			"Building from source requires a compiler and specific arguments,commands,flags that vary by project. Open a console in this "
			"container (Actions -> Open Console) and build it there."));
		return;
	}

	const QString message = tr("Install this file in '%1'?\n\n"
			"File: %2\n"
			"Detected type: %3").arg(m_jail, QFileInfo(file).fileName(), detected);

	if (QMessageBox::question(this, tr("Install from file"), message) != QMessageBox::Yes)
		return;
	
	m_backend->installFromFile(m_jail, file, detected);
}

void ApplicationsDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
	if (!ok) {
		QMessageBox::warning(this, tr("Operation failed"), desc + "\n\n" + err);
	}
	refresh();
}
