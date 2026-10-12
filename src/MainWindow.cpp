#include "MainWindow.h"
#include "ContainerBackend.h"
#include "ContainerModel.h"
#include "ActionsDialog.h"

#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLineEdit>
#include <QTableView>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QAction>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QItemSelectionModel>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
	m_backend = new ContainerBackend(this);
	m_model = new ContainerModel(m_backend, this);

	m_proxy = new QSortFilterProxyModel(this);
	m_proxy->setSourceModel(m_model);
	m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_proxy->setFilterKeyColumn(ContainerModel::NameColumn);
	m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

	buildMenus();
	buildToolBar();
	buildCentralWidget();
	buildStatusBar();

	connect(m_backend, &ContainerBackend::operationStarted, this, &MainWindow::onOperationStarted);
	connect(m_backend, &ContainerBackend::operationFinished, this, &MainWindow::onOperationFinished);

	onRefresh();
}

//////////////////

void MainWindow::buildMenus()
{
	QMenu *fileMenu = menuBar()->addMenu(tr("&Container"));
	m_actCreate = fileMenu->addAction(tr("&New container..."));
	m_actDestroy = fileMenu->addAction(tr("&Destroy..."));
	fileMenu->addSeparator();
	m_actRefresh = fileMenu->addAction(tr("&Refresh"));
	fileMenu->addSeparator();
	QAction *quit = fileMenu->addAction(tr("&Quit"));
	quit->setShortcut(QKeySequence::Quit);

	QMenu *runMenu = menuBar()->addMenu(tr("&Run"));
	m_actStart = runMenu->addAction(tr("&Start"));
	m_actStop = runMenu->addAction(tr("Sto&p"));
	m_actUnlock = runMenu->addAction(tr("&Unlock vault"));
	m_actLock = runMenu->addAction(tr("&Lock vault"));
	runMenu->addSeparator();
	m_actConsole = runMenu->addAction(tr("Open &console"));
	m_actActions = runMenu->addAction(tr("&Actions..."));

	QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
	QAction *about = helpMenu->addAction(tr("&About ParishBSD"));

	connect(quit, &QAction::triggered, this, &QWidget::close);
	connect(about, &QAction::triggered, this, &MainWindow::onAbout);
	connect(m_actCreate, &QAction::triggered, this, &MainWindow::onCreate);
	connect(m_actDestroy, &QAction::triggered, this, &MainWindow::onDestroy);
	connect(m_actRefresh, &QAction::triggered, this, &MainWindow::onRefresh);
	connect(m_actStart, &QAction::triggered, this, &MainWindow::onStart);
	connect(m_actStop, &QAction::triggered, this, &MainWindow::onStop);
	connect(m_actUnlock, &QAction::triggered, this, &MainWindow::onUnlock);
	connect(m_actLock, &QAction::triggered, this, &MainWindow::onLock);
	connect(m_actConsole, &QAction::triggered, this, &MainWindow::onConsole);
}

void MainWindow::buildToolBar()
{
	QToolBar *tb = addToolBar(tr("Main"));
	tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	tb->setMovable(false);

	auto add = [tb](QAction *a, const QString &iconName) {
		QIcon icon = QIcon::fromTheme(iconName);
		if (!icon.isNull())
			a->setIcon(icon);
		tb->addAction(a);
	};

	add(m_actCreate, QStringLiteral("list-add"));
	add(m_actDestroy, QStringLiteral("edit-delete"));
	tb->addSeparator();
	add(m_actStart, QStringLiteral("media-playback-start"));
	add(m_actStop, QStringLiteral("media-playback-stop"));
	tb->addSeparator();
	add(m_actUnlock, QStringLiteral("security-medium"));
	add(m_actLock, QStringLiteral("security-high"));
	tb->addSeparator();
	add(m_actConsole, QStringLiteral("utilities-terminal"));
	add(m_actActions, QStringLiteral("configure"));
	tb->addSeparator();
	add(m_actRefresh, QStringLiteral("view-refresh"));
}

void MainWindow::buildCentralWidget()
{
	auto *central = new QWidget(this);
	auto *layout = new QVBoxLayout(central);
	layout->setContentsMargins(8,8,8,8);

	// search bar
	auto *searchRow = new QHBoxLayout();
	auto *searchLabel = new QLabel(tr("Search:"),central);
	m_search = new QLineEdit(central);
	m_search->setClearButtonEnabled(true);
	searchRow->addWidget(searchLabel);
	searchRow->addWidget(m_search,1);
	layout->addLayout(searchRow);

	// tittle
	m_table = new QTableView(central);
	m_table->setModel(m_proxy);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSortingEnabled(true);
	m_table->sortByColumn(ContainerModel::NameColumn, Qt::AscendingOrder);
	m_table->setAlternatingRowColors(true);
	m_table->setShowGrid(false);
	m_table->verticalHeader()->setVisible(false);

	QHeaderView *h = m_table->horizontalHeader();
	h->setSectionResizeMode(ContainerModel::NameColumn, QHeaderView::Interactive);
	h->setSectionResizeMode(ContainerModel::KindColumn, QHeaderView::ResizeToContents);
	h->setSectionResizeMode(ContainerModel::StateColumn, QHeaderView::ResizeToContents);
	h->setSectionResizeMode(ContainerModel::TemplateColumn, QHeaderView::Interactive);
	h->setSectionResizeMode(ContainerModel::DiskUsageColumn, QHeaderView::ResizeToContents);
	h->setStretchLastSection(false);
	h->resizeSection(ContainerModel::NameColumn, 220);
	h->resizeSection(ContainerModel::TemplateColumn, 180);

	layout->addWidget(m_table, 1);
	
	setCentralWidget(central);

	// connects
	connect(m_search, &QLineEdit::textChanged,
		this, [this](const QString &t) {
			m_proxy->setFilterFixedString(t);
		});

	connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
		this, &MainWindow::onSelectionChanged);

	// double-click opens ActionsDialog
	connect(m_table, &QTableView::doubleClicked,
		this, &MainWindow::onActions);
}

void MainWindow::buildStatusBar()
{
	m_statusLabel = new QLabel(tr("Ready"), this);
	statusBar()->addWidget(m_statusLabel, 1);
}

//////////////
/// Selection
//////////////
QString MainWindow::selectedName() const
{
	const QModelIndexList sel = m_table->selectionModel()->selectedRows();
	if (sel.isEmpty())
		return QString();
	const QModelIndex sourceIdx = m_proxy->mapToSource(sel.first());
	if (!sourceIdx.isValid())
		return QString();
	return m_model->containerAt(sourceIdx.row()).name;
}

ContainerInfo MainWindow::selectedContainer() const
{
	const QModelIndexList sel = m_table->selectionModel()->selectedRows();
	if (sel.isEmpty())
		return ContainerInfo();
	const QModelIndex sourceIdx = m_proxy->mapToSource(sel.first());
	if (!sourceIdx.isValid())
		return ContainerInfo();
	return m_model->containerAt(sourceIdx.row());
}

void MainWindow::updateActionStates()
{
	const ContainerInfo c = selectedContainer();
	const bool have = c.isValid();
	const bool running = have && c.isRunning();
	const bool locked = have && c.isLocked();

	m_actStart ->setEnabled(have && !running && c.isVault);
	m_actStop->setEnabled(have && running && !c.isVault);
	m_actUnlock->setEnabled(have && c.isVault && locked);
	m_actLock->setEnabled(have && c.isVault && !locked);
	m_actConsole->setEnabled(have && running);
	m_actDestroy->setEnabled(have);
	m_actActions->setEnabled(have);
}

void MainWindow::onSelectionChanged()
{
	updateActionStates();
}

//////////////
/// Actions
//////////////
void MainWindow::onRefresh()
{
	m_model->refresh();
	updateActionStates();
	if (m_statusLabel)
		m_statusLabel->setText(tr("Refreshed."));
}

void MainWindow::onCreate()
{
	const QStringList personalities = m_backend->listPersonalities();
	if (personalities.isEmpty()) {
		QMessageBox::warning(this, tr("No personalities"),
				tr("No personalities found in %1/personalities/.").arg(m_backend->parishctlPath().section('/',0,-4)));
		return;
	}

	QDialog dlg(this);
	dlg.setWindowTitle(tr("Create Container"));
	auto *form = new QFormLayout(&dlg);

	auto *nameEdit = new QLineEdit(&dlg);
	auto *personalityBox = new QComboBox(&dlg);
	personalityBox->addItems(personalities);

	form->addRow(tr("Name:"),nameEdit);
	form->addRow(tr("personality:"),personalityBox);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	if (dlg.exec() != QDialog::Accepted)
		return;

	const QString name = nameEdit->text().trimmed();
	const QString personality = personalityBox->currentText();

	if (name.isEmpty()) {
		QMessageBox::warning(this, tr("Invalid name"), tr("Please enter a name."));
		return;
	}

	for (const QChar &ch : name) {
		if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
			QMessageBox::warning(this, tr("Invalid name"), tr("Name may contain only letters, digits, '-' and '_'."));
			return;
		}
	}

	m_backend->createContainer(name, personality);
}

void MainWindow::onDestroy()
{
	const ContainerInfo c = selectedContainer();
	if (!c.isValid())
		return;

	const QString question = c.isVault
		? tr("Destroy vault '%1'?\n\nThis will destroy the container AND "
		     "its encrypted data. The data cannot be recovered.").arg(c.name)
		: tr("Destroy container '%1'?\n\nThis cannot be undone.").arg(c.name);

	if (QMessageBox::warning(this, tr("Destroy"), question, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
		return;

	m_backend->destroyContainer(c.name);
}

void MainWindow::onStart()
{
	const ContainerInfo c = selectedContainer();
	if (c.isValid())
		m_backend->startContainer(c.name);
}

void MainWindow::onStop()
{
	const ContainerInfo c = selectedContainer();
	if (!c.isValid())
		return;
	if (QMessageBox::question(this, tr("Stop container"),
		tr("Stop '%1'?\n\nAny unsaved work will be lost.").arg(c.name)) != QMessageBox::Yes)
		return
	m_backend->stopContainer(c.name);
}

void MainWindow::onUnlock()
{
	const ContainerInfo c = selectedContainer();
	if (c.isValid())
		m_backend->unlockVault(c.name);
}

void MainWindow::onLock()
{
	const ContainerInfo c = selectedContainer();
	if (QMessageBox::question(this, tr("Lock vault"),
		tr("Lock vault '%1'?").arg(c.name)) != QMessageBox::Yes)
		return
	m_backend->lockVault(c.name);
}

void MainWindow::onConsole()
{
	const ContainerInfo c = selectedContainer();
	if (c.isValid())
		m_backend->openConsole(c.name);
}

void MainWindow::onActions()
{
	const ContainerInfo c = selectedContainer();
	if (!c.isValid())
		return;

	ActionsDialog dlg(m_backend, c, this);
	connect(&dlg, &ActionsDialog::containerChanged, this, &MainWindow::onRefresh);
	dlg.exec();
}

void MainWindow::onAbout()
{
	QMessageBox::about(this, tr("About ParishBSD Container Manager"),
		tr("<b>ParishBSD Container Manager</b><br><br>"
		   "A graphical front-end for parishctl(1).<br><br>"
		   "Part of ParishBSD."));
}

//////////////////////////////
/// Operation feedback status
//////////////////////////////
void MainWindow::onOperationStarted(const QString &desc)
{
	if (m_statusLabel)
		m_statusLabel->setText(desc + QStringLiteral("..."));
}

void MainWindow::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
	if (m_statusLabel) {
		if (ok)
			m_statusLabel->setText(tr("%1: done.").arg(desc));
		else
			m_statusLabel->setText(tr("%1: failed.").arg(desc));
	}

	if (!ok) {
		QMessageBox::warning(this, tr("Operation failed"),
				desc + "\n\n" + err);
	}

	onRefresh();
}
