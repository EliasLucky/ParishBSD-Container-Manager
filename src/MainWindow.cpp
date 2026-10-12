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
	m_proxy->setFilterCaseSensitiviy(Qt::CaseInsensitive);
	m_proxy->setFilterKeyColumn(ContainerModel::NameColumn);
	m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

	buildMenus();
	buildToolbar();
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

void MainWindow::buildToolbar()
{
	QToolbar *tb = addToolBar(tr("Main"));
	tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	tb->setMovable(false);

	audo add = [tb](QAction *a, const QString &iconName) {
		QIcon icon = QIcon::fromTheme(iconNme);
		if (!icon.isNull())
			a->setIcon(icon);
		tb->addAction(a)
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
	auto *cenral = new QWidget(this);
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
	m_table->setSelectionBehaviour(QAbstractItemView::SelectRows);
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
	h->setSectionResizeMOde(ContainerModel::TemplateColumn, QHeaderView::Interactive);
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

	connect(m_table->selectionModel(), &QItemSelection::selectionChanged,
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
/// Actions
//////////////
