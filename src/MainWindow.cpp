#include "MainWindow.h"
#include "ContainerBackend.h"
#include "ContainerModel.h"
#include "ActionsDialog.h"
#include "ProcessLogDialog.h"

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
#include <QRegularExpression>
#include <QIcon>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_backend = new ContainerBackend(this);
    m_model = new ContainerModel(m_backend, this);

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(ContainerModel::NameColumn);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(10000); // 10s poll

    buildMenus();
    buildToolBar();
    buildCentralWidget();
    buildStatusBar();

    connect(m_backend, &ContainerBackend::operationStarted, this, &MainWindow::onOperationStarted);
    connect(m_backend, &ContainerBackend::operationFinished, this, &MainWindow::onOperationFinished);
    connect(m_backend, &ContainerBackend::terminalLaunched, this, &MainWindow::onTerminalLaunched);
    connect(m_backend, &ContainerBackend::terminalClosed, this, &MainWindow::onTerminalClosed);

    connect(m_pollTimer, &QTimer::timeout, this, [this]() {
        m_model->refresh();
        updateActionStates();
    });

    m_pollTimer->start();
    onRefresh();
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&Compartment"));
    m_actCreate = fileMenu->addAction(tr("&New compartment..."));
    m_actCreate->setIcon(QIcon::fromTheme("list-add"));
    m_actCreate->setShortcut(QKeySequence::New);

    m_actDestroy = fileMenu->addAction(tr("&Destroy..."));
    m_actDestroy->setIcon(QIcon::fromTheme("edit-delete"));
    fileMenu->addSeparator();

    m_actConsole = fileMenu->addAction(tr("Open &Console"));
    m_actConsole->setIcon(QIcon::fromTheme("utilities-terminal"));
    fileMenu->addSeparator();

    m_actRefresh = fileMenu->addAction(tr("&Refresh"));
    m_actRefresh->setIcon(QIcon::fromTheme("view-refresh"));
    m_actRefresh->setShortcut(QKeySequence::Refresh);

    fileMenu->addSeparator();
    QAction *quit = fileMenu->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);

    QMenu *runMenu = menuBar()->addMenu(tr("&Run"));
    m_actStart = runMenu->addAction(tr("&Start"));
    m_actStart->setIcon(QIcon::fromTheme("media-playback-start"));
    m_actStop = runMenu->addAction(tr("Sto&p"));

    runMenu->addSeparator();

    m_actUnlock = runMenu->addAction(tr("&Unlock vault"));
    m_actUnlock->setIcon(QIcon::fromTheme("security-medium"));
    m_actLock = runMenu->addAction(tr("&Lock Vault"));
    m_actLock->setIcon(QIcon::fromTheme("security-high"));
    runMenu->addSeparator();
    m_actActions = runMenu->addAction(tr("&Actions..."));
    m_actActions->setIcon(QIcon::fromTheme("configure"));

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    m_actAbout = helpMenu->addAction(tr("&About ParishBSD"));
    m_actAbout->setIcon(QIcon::fromTheme("help-about"));

    connect(quit, &QAction::triggered, this, &QWidget::close);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);
    connect(m_actCreate, &QAction::triggered, this, &MainWindow::onCreate);
    connect(m_actDestroy, &QAction::triggered, this, &MainWindow::onDestroy);
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::onRefresh);
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStart);
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStop);
    connect(m_actUnlock, &QAction::triggered, this, &MainWindow::onUnlock);
    connect(m_actLock, &QAction::triggered, this, &MainWindow::onLock);
    connect(m_actConsole, &QAction::triggered, this, &MainWindow::onConsole);
    connect(m_actActions, &QAction::triggered, this, &MainWindow::onActions);
}

void MainWindow::buildToolBar()
{
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setMovable(false);
    tb->setIconSize(QSize(20,20));

    auto add = [tb](QAction *a, const QString &iconName) {
        QIcon icon = QIcon::fromTheme(iconName);
        if (!icon.isNull()) a->setIcon(icon);
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

    auto *searchRow = new QHBoxLayout();
    auto *searchLabel = new QLabel(tr("Search:"),central);
    m_search = new QLineEdit(central);
    m_search->setClearButtonEnabled(true);
    searchRow->addWidget(searchLabel);
    searchRow->addWidget(m_search,1);
    layout->addLayout(searchRow);

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
    h->setSectionResizeMode(ContainerModel::IconColumn, QHeaderView::Fixed);
    h->setSectionResizeMode(ContainerModel::NameColumn, QHeaderView::Interactive);
    h->setSectionResizeMode(ContainerModel::KindColumn, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(ContainerModel::StateColumn, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(ContainerModel::TemplateColumn, QHeaderView::Interactive);
    h->setSectionResizeMode(ContainerModel::DiskUsageColumn, QHeaderView::ResizeToContents);
    h->setStretchLastSection(false);
    h->resizeSection(ContainerModel::IconColumn, 36);
    h->resizeSection(ContainerModel::NameColumn, 220);
    h->resizeSection(ContainerModel::TemplateColumn, 180);

    layout->addWidget(m_table, 1);
    
    setCentralWidget(central);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_proxy->setFilterFixedString(t);
    });

    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, &MainWindow::onSelectionChanged);

    connect(m_table, &QTableView::doubleClicked,
        this, &MainWindow::onActions);
}

void MainWindow::buildStatusBar()
{
    m_statusLabel = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(m_statusLabel, 1);
}

ContainerInfo MainWindow::selectedContainer() const
{
    const QModelIndexList sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty()) return ContainerInfo();
    const QModelIndex sourceIdx = m_proxy->mapToSource(sel.first());
    if (!sourceIdx.isValid()) return ContainerInfo();
    return m_model->containerAt(sourceIdx.row());
}

void MainWindow::updateActionStates()
{
    const ContainerInfo c = selectedContainer();
    const bool have = c.isValid();
    const bool running = have && c.isRunning();
    const bool locked = have && c.isLocked();
    const bool contained = have && c.isContained();

    m_actStart->setEnabled(have && !running && !c.isVault && !contained);
    m_actStop->setEnabled(have && running && !contained);
    m_actUnlock->setEnabled(have && c.isVault && locked && !contained);
    m_actLock->setEnabled(have && c.isVault && !locked && !contained);
    m_actConsole->setEnabled(have && running && !contained);
    m_actDestroy->setEnabled(have && !contained);
    m_actActions->setEnabled(have);
}

void MainWindow::onSelectionChanged()
{
    updateActionStates();
}

void MainWindow::onRefresh()
{
    m_model->refresh();
    updateActionStates();
    if (m_statusLabel) m_statusLabel->setText(tr("Refreshed."));
}

void MainWindow::onCreate()
{
    const QStringList templates = m_backend->listTemplatesNames();
    if (templates.isEmpty()) {
        QMessageBox::warning(this, tr("No templates"),
                tr("No ready templates found. Build one first via CLI."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Create Compartment"));
    auto *form = new QFormLayout(&dlg);

    auto *nameEdit = new QLineEdit(&dlg);
    auto *templateBox = new QComboBox(&dlg);
    templateBox->addItems(templates);

    form->addRow(tr("Name:"),nameEdit);
    form->addRow(tr("Template:"),templateBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString name = nameEdit->text().trimmed();
    const QString tmpl = templateBox->currentText();

    QRegularExpression re("^[a-z0-9-]{1,31}$");
    if (!re.match(name).hasMatch()) {
        QMessageBox::warning(this, tr("Invalid name"), tr("Name may contain only lowercase letters, digits, and hyphens (max 31 characters)."));
        return;
    }
    if (name.startsWith('-') || name.endsWith('-') || name.contains("--")) {
        QMessageBox::warning(this, tr("Invalid name"), tr("Name cannot start/end with a hyphen or contain consecutive hyphens."));
        return;
    }

    m_backend->createContainer(name, tmpl);
    auto *logDlg = new ProcessLogDialog(m_backend, tr("Creating %1").arg(name), this);
    logDlg->setAttribute(Qt::WA_DeleteOnClose);
    logDlg->show();
}

void MainWindow::onDestroy()
{
    const ContainerInfo c = selectedContainer();
    if (!c.isValid()) return;

    if (c.isContained()) {
        QMessageBox::critical(this, tr("Cannot Destroy"),
            tr("This compartment is CONTAINED by Guardian. Investigate the incident first."));
        return;
    }

    bool ok = false;
    const QString token = QInputDialog::getText(this, tr("Confirm Destruction"),
        tr("This will destroy compartment '%1'.\nType the name to confirm:").arg(c.name),
        QLineEdit::Normal, QString(), &ok);

    if (!ok || token != c.name) {
        if (ok) QMessageBox::warning(this, tr("Aborted"), tr("Confirmation token did not match."));
        return;
    }

    m_backend->destroyContainer(c.name, token);
    auto *logDlg = new ProcessLogDialog(m_backend, tr("Destroying %1").arg(c.name), this);
    logDlg->setAttribute(Qt::WA_DeleteOnClose);
    logDlg->show();
}

void MainWindow::onStart()
{
    const ContainerInfo c = selectedContainer();
    if (c.isValid()) m_backend->startContainer(c.name);
}

void MainWindow::onStop()
{
    const ContainerInfo c = selectedContainer();
    if (!c.isValid()) return;
    if (QMessageBox::question(this, tr("Stop container"),
        tr("Stop '%1'?\n\nAny unsaved work will be lost.").arg(c.name)) != QMessageBox::Yes) return;
    m_backend->stopContainer(c.name);
}

void MainWindow::onUnlock()
{
    const ContainerInfo c = selectedContainer();
    if (c.isValid()) m_backend->unlockVault(c.name);
}

void MainWindow::onLock()
{
    const ContainerInfo c = selectedContainer();
    if (QMessageBox::question(this, tr("Lock vault"),
        tr("Lock vault '%1'?").arg(c.name)) != QMessageBox::Yes) return;
    m_backend->lockVault(c.name);
}

void MainWindow::onConsole()
{
    const ContainerInfo c = selectedContainer();
    if (c.isValid()) m_backend->openConsole(c.name);
}

void MainWindow::onActions()
{
    const ContainerInfo c = selectedContainer();
    if (!c.isValid()) return;

    ActionsDialog dlg(m_backend, c, this);
    connect(&dlg, &ActionsDialog::containerChanged, this, &MainWindow::onRefresh);
    dlg.exec();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About ParishBSD Compartment Manager"),
        tr("<b>ParishBSD Compartment Manager</b><br><br>"
           "A graphical front-end for parishctl(1).<br><br>"
           "Part of ParishBSD."));
}

void MainWindow::onOperationStarted(const QString &desc)
{
    if (m_statusLabel) m_statusLabel->setText(desc + QStringLiteral("..."));
}

void MainWindow::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
    if (m_statusLabel) {
        if (ok) m_statusLabel->setText(tr("%1: done.").arg(desc));
        else m_statusLabel->setText(tr("%1: failed.").arg(desc));
    }

    if (!ok) {
        QMessageBox::warning(this, tr("Operation failed"), desc + "\n\n" + err);
    }

    onRefresh();
}

void MainWindow::onTerminalLaunched(const QString &desc)
{
    if (m_statusLabel) m_statusLabel->setText(tr("%1 - waiting for terminal...").arg(desc));
}

void MainWindow::onTerminalClosed(const QString &desc)
{
    onRefresh();
    if (m_statusLabel) m_statusLabel->setText(tr("%1: done.").arg(desc));
}
