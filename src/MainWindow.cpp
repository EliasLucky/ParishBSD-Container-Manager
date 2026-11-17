#include "MainWindow.h"
#include "ContainerBackend.h"
#include "ContainerModel.h"
#include "TemplateModel.h"
#include "TreeAssemblyModel.h"
#include "ActionsDialog.h"
#include "ProcessLogDialog.h"

#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLineEdit>
#include <QTreeView>
#include <QHeaderView>
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
    m_templateModel = new TemplateModel(m_backend, this);
    m_treeModel = new TreeAssemblyModel(m_model, m_templateModel, this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(10000); 

    buildMenus();
    buildToolBar();
    buildCentralWidget();
    buildStatusBar();

    connect(m_backend, &ContainerBackend::operationStarted, this, &MainWindow::onOperationStarted);
    connect(m_backend, &ContainerBackend::operationFinished, this, &MainWindow::onOperationFinished);
    connect(m_backend, &ContainerBackend::terminalLaunched, this, &MainWindow::onTerminalLaunched);
    connect(m_backend, &ContainerBackend::terminalClosed, this, &MainWindow::onTerminalClosed);

    connect(m_pollTimer, &QTimer::timeout, this, [this]() { onRefresh(); });

    m_pollTimer->start();
    onRefresh();

    // Default expand the Compartments group on first launch
    QTimer::singleShot(100, this, [this]() {
        m_tree->expand(m_treeModel->index(0, 0));
    });
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
    
    m_actCreateFromTemplate = new QAction(tr("Create From Template"), this);
    add(m_actCreateFromTemplate, QStringLiteral("document-new"));
    connect(m_actCreateFromTemplate, &QAction::triggered, this, &MainWindow::onCreate);

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
    m_search->clear();
    searchRow->addWidget(searchLabel);
    searchRow->addWidget(m_search,1);
    layout->addLayout(searchRow);

    m_tree = new QTreeView(central);
    m_tree->setModel(m_treeModel);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    
    QHeaderView *h = m_tree->header();
    h->setStretchLastSection(true);
    h->resizeSection(TreeAssemblyModel::NameColumn, 220);
    h->resizeSection(TreeAssemblyModel::SourceColumn, 180);
    h->resizeSection(TreeAssemblyModel::StateColumn, 100);

    layout->addWidget(m_tree, 1);
    setCentralWidget(central);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_model->setFilter(t);
        m_templateModel->setFilter(t);
    });

    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_tree, &QTreeView::doubleClicked, this, &MainWindow::onActions);
}

void MainWindow::buildStatusBar()
{
    m_statusLabel = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(m_statusLabel, 1);
}

ContainerInfo MainWindow::selectedContainer() const
{
    const QModelIndexList sel = m_tree->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return ContainerInfo();
    return m_treeModel->getContainer(sel.first());
}

bool MainWindow::isTemplateSelected() const
{
    const QModelIndexList sel = m_tree->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return false;
    quintptr id = sel.first().internalId();
    return (id == 2 || id == 3);
}

void MainWindow::updateActionStates()
{
    const ContainerInfo c = selectedContainer();
    const bool haveContainer = c.isValid();
    const bool haveTemplate = isTemplateSelected();
    
    const bool running = haveContainer && c.isRunning();
    const bool locked = haveContainer && c.isVault && c.isLocked();
    const bool contained = haveContainer && c.isContained();

    // Contextual Visibility
    bool isApp = haveContainer && !c.isVault;
    bool isVault = haveContainer && c.isVault;

    m_actStart->setVisible(isApp);
    m_actStop->setVisible(isApp);
    m_actConsole->setVisible(isApp);
    m_actUnlock->setVisible(isVault);
    m_actLock->setVisible(isVault);
    
    m_actDestroy->setVisible(haveContainer);
    m_actActions->setVisible(haveContainer);
    m_actCreateFromTemplate->setVisible(haveTemplate);

    // Enabled States
    m_actStart->setEnabled(haveContainer && !running && !contained);
    m_actStop->setEnabled(haveContainer && running && !contained);
    m_actUnlock->setEnabled(haveContainer && locked && !contained);
    m_actLock->setEnabled(haveContainer && !locked && !contained);
    m_actConsole->setEnabled(haveContainer && running && !contained);
    m_actDestroy->setEnabled(haveContainer && !contained);
    m_actActions->setEnabled(haveContainer);
}

void MainWindow::onSelectionChanged()
{
    updateActionStates();
}

void MainWindow::onRefresh()
{
    // Save selection
    QModelIndexList sel = m_tree->selectionModel()->selectedIndexes();
    if (!sel.isEmpty()) {
        m_selectedName = sel.first().data(Qt::DisplayRole).toString();
        m_selectedIsTemplate = (sel.first().internalId() > 1);
    } else {
        m_selectedName.clear();
    }

    // Save expanded state
    QList<bool> expandedState;
    for (int i=0; i<3; ++i) {
        expandedState.append(m_tree->isExpanded(m_treeModel->index(i, 0)));
    }

    m_model->refresh();
    m_templateModel->refresh();
    
    // Restore expanded state
    for (int i=0; i<3; ++i) {
        m_tree->setExpanded(m_treeModel->index(i, 0), expandedState[i]);
    }

    // Restore selection
    if (!m_selectedName.isEmpty()) {
        for (int g = 0; g < 3; ++g) {
            QModelIndex groupIdx = m_treeModel->index(g, 0);
            for (int r = 0; r < m_treeModel->rowCount(groupIdx); ++r) {
                QModelIndex idx = m_treeModel->index(r, 0, groupIdx);
                if (idx.data(Qt::DisplayRole).toString() == m_selectedName) {
                    m_tree->setCurrentIndex(idx);
                    break;
                }
            }
        }
    }
    
    updateActionStates();
    if (m_statusLabel) m_statusLabel->setText(tr("Refreshed."));
}

void MainWindow::onCreate()
{
    m_templateModel->refresh();
    const QStringList templates = m_backend->listTemplatesNames();
    
    if (templates.isEmpty()) {
        QMessageBox::warning(this, tr("No templates"), tr("No ready templates found. Build one first via CLI."));
        return;
    }

    QString preselected = "";
    const QModelIndexList sel = m_tree->selectionModel()->selectedIndexes();
    if (!sel.isEmpty() && sel.first().internalId() > 1) {
        preselected = sel.first().siblingAtColumn(0).data().toString();
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Create Compartment"));
    auto *form = new QFormLayout(&dlg);

    auto *nameEdit = new QLineEdit(&dlg);
    auto *templateBox = new QComboBox(&dlg);
    templateBox->addItems(templates);
    if (!preselected.isEmpty()) templateBox->setCurrentText(preselected);

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
    if (!c.isValid()) return;
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