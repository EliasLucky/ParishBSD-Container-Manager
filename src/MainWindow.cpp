#include "MainWindow.h"
#include "AppIcons.h"
#include "ApplicationsDialog.h"
#include "ContainerBackend.h"
#include "ContainerInfo.h"
#include "ContainerModel.h"
#include "ExchangeDialog.h"
#include "ProcessLogDialog.h"
#include "TemplateInfo.h"
#include "TemplateModel.h"
#include "TreeAssemblyModel.h"
#include "UsbDialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("ParishBSD Compartment Manager"));
    setWindowIcon(AppIcons::appWindowIcon());
    resize(940, 600);

    m_backend = new ContainerBackend(this);
    m_model = new ContainerModel(m_backend, this);
    m_templateModel = new TemplateModel(m_backend, this);
    m_treeModel = new TreeAssemblyModel(m_model, m_templateModel, this);

    QSettings settings;
    const int pollSecs = qBound(5, settings.value(QStringLiteral("polling/intervalSecs"), 10).toInt(), 300);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(pollSecs * 1000);

    buildMenus();
    buildToolBar();
    buildCentralWidget();
    buildStatusBar();

    connect(m_backend, &ContainerBackend::operationStarted,
            this, &MainWindow::onOperationStarted);
    connect(m_backend, &ContainerBackend::operationFinished,
            this, &MainWindow::onOperationFinished);
    connect(m_backend, &ContainerBackend::terminalLaunched,
            this, &MainWindow::onTerminalLaunched);
    connect(m_backend, &ContainerBackend::terminalClosed,
            this, &MainWindow::onTerminalClosed);

    // Clear "Refreshing..." status when background polls complete or warn
    connect(m_backend, &ContainerBackend::containersReady,
            this, [this](const QList<ContainerInfo> &, const QString &err) {
        if (!m_statusLabel) return;
        if (!err.isEmpty()) {
            m_statusLabel->setText(tr("Refresh error: %1").arg(err));
        } else if (!m_backend->isBusy()) {
            m_statusLabel->setText(tr("Ready"));
        }
    });
    connect(m_backend, &ContainerBackend::templatesReady,
            this, [this](const QList<TemplateInfo> &, const QString &err) {
        if (!m_statusLabel) return;
        if (!err.isEmpty()) {
            m_statusLabel->setText(tr("Template refresh error: %1").arg(err));
        } else if (!m_backend->isBusy()) {
            m_statusLabel->setText(tr("Ready"));
        }
    });

    // Preserve user expansion state of tree groups across periodic model resets
    connect(m_treeModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        if (!m_initialExpandDone) return;
        for (int g = 0; g < 3; ++g) {
            const QModelIndex idx = m_treeModel->index(g, 0);
            if (idx.isValid()) {
                m_groupExpanded[g] = m_tree->isExpanded(idx);
            }
        }
    });

    connect(m_treeModel, &QAbstractItemModel::modelReset, this, [this]() {
        if (!m_initialExpandDone) {
            m_groupExpanded[0] = true;
            m_groupExpanded[1] = false;
            m_groupExpanded[2] = false;
            m_initialExpandDone = true;
        }
        {
            const QSignalBlocker blocker(m_tree);
            for (int g = 0; g < 3; ++g) {
                const QModelIndex groupIdx = m_treeModel->index(g, 0);
                if (groupIdx.isValid()) {
                    m_tree->setExpanded(groupIdx, m_groupExpanded[g]);
                }
            }
        }
        if (!m_selectedName.isEmpty()) {
            for (int g = 0; g < 3; ++g) {
                const QModelIndex groupIdx = m_treeModel->index(g, 0);
                for (int r = 0; r < m_treeModel->rowCount(groupIdx); ++r) {
                    const QModelIndex idx = m_treeModel->index(r, 0, groupIdx);
                    if (idx.data(Qt::DisplayRole).toString() == m_selectedName) {
                        m_tree->setCurrentIndex(idx);
                        break;
                    }
                }
            }
        }
        updateActionStates();
        updateInspector();
    });

    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onRefresh);

    m_pollTimer->start();
    onRefresh();
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&Compartment"));
    m_actCreate = fileMenu->addAction(tr("&New Compartment..."));
    m_actCreate->setIcon(AppIcons::themedOrBundled(QStringLiteral("list-add"), QStringLiteral("new-compartment")));
    m_actCreate->setShortcut(QKeySequence::New);

    m_actNewTemplate = fileMenu->addAction(tr("New &Template..."));
    m_actNewTemplate->setIcon(AppIcons::themedOrBundled(QStringLiteral("document-new"), QStringLiteral("new-template")));

    m_actDestroy = fileMenu->addAction(tr("&Destroy..."));
    m_actDestroy->setIcon(AppIcons::themedOrBundled(QStringLiteral("edit-delete"), QStringLiteral("destroy")));
    fileMenu->addSeparator();

    m_actApps = fileMenu->addAction(tr("&Applications && Packages..."));
    m_actApps->setIcon(AppIcons::themedOrBundled(QStringLiteral("system-software-install"), QStringLiteral("apps")));

    m_actConsole = fileMenu->addAction(tr("Open &Console"));
    m_actConsole->setIcon(AppIcons::themedOrBundled(QStringLiteral("utilities-terminal"), QStringLiteral("console")));
    fileMenu->addSeparator();

    m_actRefresh = fileMenu->addAction(tr("&Refresh"));
    m_actRefresh->setIcon(AppIcons::themedOrBundled(QStringLiteral("view-refresh"), QStringLiteral("refresh")));
    m_actRefresh->setShortcut(QKeySequence::Refresh);

    fileMenu->addSeparator();
    QAction *quit = fileMenu->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);

    QMenu *runMenu = menuBar()->addMenu(tr("&Run"));
    m_actStart = runMenu->addAction(tr("&Start"));
    m_actStart->setIcon(AppIcons::themedOrBundled(QStringLiteral("media-playback-start"), QStringLiteral("start")));
    m_actStop = runMenu->addAction(tr("Sto&p"));
    m_actStop->setIcon(AppIcons::themedOrBundled(QStringLiteral("media-playback-stop"), QStringLiteral("stop")));
    runMenu->addSeparator();
    m_actUnlock = runMenu->addAction(tr("&Unlock Vault"));
    m_actUnlock->setIcon(AppIcons::themedOrBundled(QStringLiteral("security-medium"), QStringLiteral("unlock")));
    m_actLock = runMenu->addAction(tr("&Lock Vault"));
    m_actLock->setIcon(AppIcons::themedOrBundled(QStringLiteral("security-high"), QStringLiteral("lock")));

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    m_actExchange = toolsMenu->addAction(tr("File &Exchange..."));
    m_actExchange->setIcon(AppIcons::themedOrBundled(QStringLiteral("folder-sync"), QStringLiteral("exchange")));
    m_actExchange->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_actUsb = toolsMenu->addAction(tr("&USB && Removable Media..."));
    m_actUsb->setIcon(AppIcons::themedOrBundled(QStringLiteral("drive-removable-media"), QStringLiteral("usb")));
    m_actUsb->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    toolsMenu->addSeparator();
    m_actSettings = toolsMenu->addAction(tr("&Settings..."));
    m_actSettings->setIcon(AppIcons::themedOrBundled(QStringLiteral("preferences-system"), QStringLiteral("settings")));

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    m_actAbout = helpMenu->addAction(tr("&About ParishBSD"));
    m_actAbout->setIcon(AppIcons::themedOrBundled(QStringLiteral("help-about"), QStringLiteral("about")));

    connect(quit, &QAction::triggered, this, &QWidget::close);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);
    connect(m_actSettings, &QAction::triggered, this, &MainWindow::onSettings);
    connect(m_actCreate, &QAction::triggered, this, &MainWindow::onCreate);
    connect(m_actNewTemplate, &QAction::triggered, this, &MainWindow::onCreateTemplate);
    connect(m_actDestroy, &QAction::triggered, this, &MainWindow::onDestroy);
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::onRefresh);
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStart);
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStop);
    connect(m_actUnlock, &QAction::triggered, this, &MainWindow::onUnlock);
    connect(m_actLock, &QAction::triggered, this, &MainWindow::onLock);
    connect(m_actConsole, &QAction::triggered, this, &MainWindow::onConsole);
    connect(m_actApps, &QAction::triggered, this, &MainWindow::onApplications);
    connect(m_actExchange, &QAction::triggered, this, &MainWindow::onExchange);
    connect(m_actUsb, &QAction::triggered, this, &MainWindow::onUsb);
}

void MainWindow::buildToolBar()
{
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));

    // Keep the toolbar short and focused on high-frequency lifecycle actions;
    // detailed compartment controls live in the Inspector pane and right-click menu.
    tb->addAction(m_actCreate);
    tb->addAction(m_actNewTemplate);

    m_tbSepLifecycle = tb->addSeparator();
    tb->addAction(m_actStart);
    tb->addAction(m_actStop);
    tb->addAction(m_actUnlock);
    tb->addAction(m_actLock);
    tb->addAction(m_actDestroy);

    tb->addSeparator();
    tb->addAction(m_actRefresh);
}

void MainWindow::buildCentralWidget()
{
    auto *central = new QWidget(this);
    auto *outerLayout = new QVBoxLayout(central);
    outerLayout->setContentsMargins(10, 10, 10, 10);
    outerLayout->setSpacing(8);

    auto *searchRow = new QHBoxLayout();
    m_search = new QLineEdit(central);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(tr("Filter compartments and templates by name or kind..."));
    m_search->setAccessibleName(tr("Search compartments and templates"));
    auto *searchLabel = new QLabel(tr("&Search:"), central);
    searchLabel->setBuddy(m_search);
    searchRow->addWidget(searchLabel);
    searchRow->addWidget(m_search, 1);
    outerLayout->addLayout(searchRow);

    auto *splitter = new QSplitter(Qt::Horizontal, central);

    m_tree = new QTreeView(splitter);
    m_tree->setModel(m_treeModel);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setAccessibleName(tr("Compartments and Templates tree"));

    QHeaderView *h = m_tree->header();
    h->setStretchLastSection(true);
    h->resizeSection(TreeAssemblyModel::NameColumn, 190);
    h->resizeSection(TreeAssemblyModel::SourceColumn, 130);
    h->resizeSection(TreeAssemblyModel::StateColumn, 115);
    h->resizeSection(TreeAssemblyModel::RiskColumn, 130);

    auto *inspectorContainer = new QWidget(splitter);
    inspectorContainer->setMinimumWidth(290);
    buildInspectorPane(inspectorContainer);

    splitter->addWidget(m_tree);
    splitter->addWidget(inspectorContainer);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    outerLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_model->setFilter(t);
        m_templateModel->setFilter(t);
    });

    connect(m_tree, &QTreeView::expanded, this, [this](const QModelIndex &idx) {
        if (!idx.parent().isValid() && idx.row() >= 0 && idx.row() < 3) {
            m_groupExpanded[idx.row()] = true;
        }
    });
    connect(m_tree, &QTreeView::collapsed, this, [this](const QModelIndex &idx) {
        if (!idx.parent().isValid() && idx.row() >= 0 && idx.row() < 3) {
            m_groupExpanded[idx.row()] = false;
        }
    });

    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_tree, &QTreeView::customContextMenuRequested,
            this, &MainWindow::onTreeContextMenu);
    connect(m_tree, &QTreeView::doubleClicked, this, [this]() {
        const ContainerInfo c = selectedContainer();
        if (c.isValid()) {
            if (c.isVault && c.isLocked()) {
                onUnlock();
            } else {
                onApplications();
            }
        } else if (isTemplateSelected()) {
            onCreate();
        }
    });
}

void MainWindow::buildInspectorPane(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(6, 0, 0, 0);

    m_inspectorStack = new QStackedWidget(parent);

    // Page 0: Nothing selected
    auto *emptyPage = new QWidget(parent);
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    auto *welcomeBox = new QGroupBox(tr("Getting Started"), emptyPage);
    auto *welcomeLay = new QVBoxLayout(welcomeBox);
    auto *welcomeLbl = new QLabel(
        tr("Select a compartment or template on the left to inspect its status, "
           "install or launch applications, manage sound and safety levels, or open a console.\n\n"
           "Tip: Right-click any item for quick actions, or choose 'New Template...' "
           "to create a custom setup for desktop, Linux, or Windows apps."),
        welcomeBox);
    welcomeLbl->setWordWrap(true);
    welcomeLay->addWidget(welcomeLbl);
    welcomeLay->addStretch();
    emptyLayout->addWidget(welcomeBox);
    m_inspectorStack->addWidget(emptyPage);

    // Page 1: Compartment Inspector
    auto *compPage = new QWidget(parent);
    auto *compLay = new QVBoxLayout(compPage);
    auto *compBox = new QGroupBox(tr("Selected Compartment"), compPage);
    auto *compBoxLay = new QVBoxLayout(compBox);

    m_inspName = new QLabel(compBox);
    m_inspName->setTextFormat(Qt::PlainText);
    QFont boldFont = m_inspName->font();
    boldFont.setBold(true);
    boldFont.setPointSize(boldFont.pointSize() + 1);
    m_inspName->setFont(boldFont);
    compBoxLay->addWidget(m_inspName);

    m_inspGuardianBanner = new QLabel(
        tr("BLOCKED FOR SAFETY (GUARDIAN): Investigate this compartment before restoring or destroying it."),
        compBox);
    m_inspGuardianBanner->setWordWrap(true);
    m_inspGuardianBanner->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; background-color: #b91c1c; padding: 8px; border-radius: 4px; font-weight: bold; }"));
    m_inspGuardianBanner->setVisible(false);
    compBoxLay->addWidget(m_inspGuardianBanner);

    m_inspDetails = new QLabel(compBox);
    m_inspDetails->setWordWrap(true);
    m_inspDetails->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    compBoxLay->addWidget(m_inspDetails);

    auto *riskRow = new QFormLayout();
    m_inspRiskCombo = new QComboBox(compBox);
    m_inspRiskCombo->addItem(tr("Low (Trusted - Green)"), QStringLiteral("low"));
    m_inspRiskCombo->addItem(tr("Medium (Work - Blue)"), QStringLiteral("medium"));
    m_inspRiskCombo->addItem(tr("High (Untrusted - Red)"), QStringLiteral("high"));
    m_inspRiskCombo->addItem(tr("Critical (Vault - Purple)"), QStringLiteral("critical"));
    m_inspRiskCombo->setAccessibleName(tr("Compartment color and risk level"));
    auto *riskLbl = new QLabel(tr("&Risk Level:"), compBox);
    riskLbl->setBuddy(m_inspRiskCombo);
    riskRow->addRow(riskLbl, m_inspRiskCombo);
    compBoxLay->addLayout(riskRow);

    connect(m_inspRiskCombo, QOverload<int>::of(&QComboBox::activated),
            this, &MainWindow::onRiskChanged);

    m_inspAudioBox = new QGroupBox(tr("Sound & Microphone"), compBox);
    auto *audioBoxLay = new QVBoxLayout(m_inspAudioBox);
    audioBoxLay->setContentsMargins(8, 6, 8, 6);
    m_inspAudioOutCheck = new QCheckBox(tr("Allow Sound &Output (Speakers / Headphones)"), m_inspAudioBox);
    m_inspAudioOutCheck->setAccessibleName(tr("Allow sound output for this compartment"));
    m_inspAudioInCheck = new QCheckBox(tr("Allow &Microphone Input"), m_inspAudioBox);
    m_inspAudioInCheck->setAccessibleName(tr("Allow microphone access for this compartment"));
    m_inspVaultAudioNote = new QLabel(tr("Sound and microphone are turned off for Vaults to protect sensitive data."), m_inspAudioBox);
    m_inspVaultAudioNote->setWordWrap(true);
    m_inspVaultAudioNote->setVisible(false);
    audioBoxLay->addWidget(m_inspAudioOutCheck);
    audioBoxLay->addWidget(m_inspAudioInCheck);
    audioBoxLay->addWidget(m_inspVaultAudioNote);
    compBoxLay->addWidget(m_inspAudioBox);

    connect(m_inspAudioOutCheck, &QCheckBox::clicked, this, &MainWindow::onAudioChanged);
    connect(m_inspAudioInCheck, &QCheckBox::clicked, this, &MainWindow::onAudioChanged);

    m_inspStartStopBtn = new QPushButton(tr("Start"), compBox);
    m_inspUnlockLockBtn = new QPushButton(tr("Unlock Vault"), compBox);
    m_inspConsoleBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("utilities-terminal"), QStringLiteral("console")),
        tr("Open Console"), compBox);

    m_inspAppsBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("system-software-install"), QStringLiteral("apps")),
        tr("Applications && Packages..."), compBox);

    auto *ioRow = new QHBoxLayout();
    m_inspExchangeBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("folder-sync"), QStringLiteral("exchange")),
        tr("File Exchange..."), compBox);
    m_inspUsbBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("drive-removable-media"), QStringLiteral("usb")),
        tr("USB && Media..."), compBox);
    ioRow->addWidget(m_inspExchangeBtn);
    ioRow->addWidget(m_inspUsbBtn);

    m_inspDestroyBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("edit-delete"), QStringLiteral("destroy")),
        tr("Destroy..."), compBox);

    compBoxLay->addWidget(m_inspStartStopBtn);
    compBoxLay->addWidget(m_inspUnlockLockBtn);
    compBoxLay->addWidget(m_inspAppsBtn);
    compBoxLay->addWidget(m_inspConsoleBtn);
    compBoxLay->addLayout(ioRow);
    compBoxLay->addSpacing(6);
    compBoxLay->addWidget(m_inspDestroyBtn);
    compBoxLay->addStretch();

    connect(m_inspStartStopBtn, &QPushButton::clicked, this, [this]() {
        const ContainerInfo c = selectedContainer();
        if (!c.isValid()) return;
        if (c.isRunning()) onStop();
        else onStart();
    });
    connect(m_inspUnlockLockBtn, &QPushButton::clicked, this, [this]() {
        const ContainerInfo c = selectedContainer();
        if (!c.isValid()) return;
        if (c.isLocked()) onUnlock();
        else onLock();
    });
    connect(m_inspConsoleBtn, &QPushButton::clicked, this, &MainWindow::onConsole);
    connect(m_inspAppsBtn, &QPushButton::clicked, this, &MainWindow::onApplications);
    connect(m_inspExchangeBtn, &QPushButton::clicked, this, &MainWindow::onExchange);
    connect(m_inspUsbBtn, &QPushButton::clicked, this, &MainWindow::onUsb);
    connect(m_inspDestroyBtn, &QPushButton::clicked, this, &MainWindow::onDestroy);

    compLay->addWidget(compBox);
    m_inspectorStack->addWidget(compPage);

    // Page 2: Template Inspector
    auto *tmplPage = new QWidget(parent);
    auto *tmplLay = new QVBoxLayout(tmplPage);
    auto *tmplBox = new QGroupBox(tr("Selected Template"), tmplPage);
    auto *tmplBoxLay = new QVBoxLayout(tmplBox);

    m_tmplName = new QLabel(tmplBox);
    m_tmplName->setFont(boldFont);
    tmplBoxLay->addWidget(m_tmplName);

    m_tmplDetails = new QLabel(tmplBox);
    m_tmplDetails->setWordWrap(true);
    m_tmplDetails->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    tmplBoxLay->addWidget(m_tmplDetails);

    m_tmplCreateBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("list-add"), QStringLiteral("new-compartment")),
        tr("Create Compartment from Template..."), tmplBox);
    m_tmplUpdateBtn = new QPushButton(
        AppIcons::themedOrBundled(QStringLiteral("view-refresh"), QStringLiteral("refresh")),
        tr("Update Template Packages"), tmplBox);
    tmplBoxLay->addWidget(m_tmplCreateBtn);
    tmplBoxLay->addWidget(m_tmplUpdateBtn);
    tmplBoxLay->addStretch();

    connect(m_tmplCreateBtn, &QPushButton::clicked, this, &MainWindow::onCreate);
    connect(m_tmplUpdateBtn, &QPushButton::clicked, this, &MainWindow::onUpdateTemplate);

    tmplLay->addWidget(tmplBox);
    m_inspectorStack->addWidget(tmplPage);

    layout->addWidget(m_inspectorStack);
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

TemplateInfo MainWindow::selectedTemplate() const
{
    const QModelIndexList sel = m_tree->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return TemplateInfo();
    return m_treeModel->getTemplate(sel.first());
}

bool MainWindow::isTemplateSelected() const
{
    return selectedTemplate().isValid();
}

void MainWindow::updateActionStates()
{
    const ContainerInfo c = selectedContainer();
    const TemplateInfo t = selectedTemplate();
    const bool haveContainer = c.isValid();
    const bool haveReadyTemplate = t.isValid() && t.isReady();

    const bool running = haveContainer && c.isRunning();
    const bool locked = haveContainer && c.isVault && c.isLocked();
    const bool contained = haveContainer && c.isContained();

    const bool isApp = haveContainer && !c.isVault;
    const bool isVault = haveContainer && c.isVault;

    // Single create button adjusts label cleanly when a ready template is selected
    m_actCreate->setText(haveReadyTemplate ? tr("Create from Template") : tr("New Compartment"));

    m_tbSepLifecycle->setVisible(haveContainer);
    m_actStart->setVisible(isApp && !running);
    m_actStop->setVisible(isApp && running);
    m_actUnlock->setVisible(isVault && locked);
    m_actLock->setVisible(isVault && !locked);
    m_actDestroy->setVisible(haveContainer);
    m_actConsole->setVisible(haveContainer);
    m_actApps->setVisible(haveContainer);

    m_actStart->setEnabled(haveContainer && !running && !contained);
    m_actStop->setEnabled(haveContainer && running && !contained);
    m_actUnlock->setEnabled(haveContainer && locked && !contained);
    m_actLock->setEnabled(haveContainer && !locked && !contained);
    m_actConsole->setEnabled(haveContainer && running && !contained);
    m_actApps->setEnabled(haveContainer);
    m_actExchange->setEnabled(!contained);
    m_actUsb->setEnabled(!contained);
    m_actDestroy->setEnabled(haveContainer && !contained);
}

void MainWindow::updateInspector()
{
    const ContainerInfo c = selectedContainer();
    if (c.isValid()) {
        m_inspectorStack->setCurrentIndex(1);
        m_inspName->setText(c.name);
        m_inspGuardianBanner->setVisible(c.isContained());

        m_inspDetails->setText(
            tr("Type: %1\nStatus: %2\nTemplate: %3\nFeatures: %4\nSpace used: %5")
                .arg(c.kind, c.humanState(), c.sourceTemplate,
                     c.humanCapabilities(), formatDiskUsage(c.diskUsage)));

        {
            const QSignalBlocker b(m_inspRiskCombo);
            const int rIdx = m_inspRiskCombo->findData(c.normalizedRiskLevel());
            if (rIdx >= 0) m_inspRiskCombo->setCurrentIndex(rIdx);
        }
        m_inspRiskCombo->setEnabled(!c.isContained());

        const bool running = c.isRunning();
        const bool contained = c.isContained();

        m_inspAudioOutCheck->setVisible(!c.isVault);
        m_inspAudioInCheck->setVisible(!c.isVault);
        m_inspVaultAudioNote->setVisible(c.isVault);
        if (!c.isVault) {
            const QSignalBlocker bOut(m_inspAudioOutCheck);
            const QSignalBlocker bIn(m_inspAudioInCheck);
            m_inspAudioOutCheck->setChecked(c.audioOutput);
            m_inspAudioInCheck->setChecked(c.audioInput);
            m_inspAudioOutCheck->setEnabled(!contained);
            m_inspAudioInCheck->setEnabled(!contained);
        }

        m_inspStartStopBtn->setVisible(!c.isVault);
        m_inspStartStopBtn->setText(running ? tr("Stop Compartment") : tr("Start Compartment"));
        m_inspStartStopBtn->setIcon(running
            ? AppIcons::themedOrBundled(QStringLiteral("media-playback-stop"), QStringLiteral("stop"))
            : AppIcons::themedOrBundled(QStringLiteral("media-playback-start"), QStringLiteral("start")));
        m_inspStartStopBtn->setEnabled(!contained);

        m_inspUnlockLockBtn->setVisible(c.isVault);
        m_inspUnlockLockBtn->setText(c.isLocked() ? tr("Unlock Vault") : tr("Lock Vault"));
        m_inspUnlockLockBtn->setIcon(c.isLocked()
            ? AppIcons::themedOrBundled(QStringLiteral("security-medium"), QStringLiteral("unlock"))
            : AppIcons::themedOrBundled(QStringLiteral("security-high"), QStringLiteral("lock")));
        m_inspUnlockLockBtn->setEnabled(!contained);

        m_inspConsoleBtn->setEnabled(running && !contained);
        m_inspAppsBtn->setEnabled(true);
        m_inspExchangeBtn->setEnabled(!contained);
        m_inspUsbBtn->setEnabled(!contained);
        m_inspDestroyBtn->setEnabled(!contained);
        return;
    }

    const TemplateInfo t = selectedTemplate();
    if (t.isValid()) {
        m_inspectorStack->setCurrentIndex(2);
        m_tmplName->setText(t.name);
        m_tmplDetails->setText(
            tr("Origin: %1\nType: %2\nStatus: %3\nBase snapshot: %4\nBuild size: %5 MiB\n\n%6")
                .arg(t.shipped ? tr("Shipped") : tr("Custom"),
                     t.kind,
                     t.humanState(),
                     t.currentBase.isEmpty() ? tr("(none)") : t.currentBase)
                .arg(t.buildSizeMb)
                .arg(t.description));
        m_tmplCreateBtn->setEnabled(t.isReady());
        m_tmplUpdateBtn->setEnabled(t.isReady());
        return;
    }

    m_inspectorStack->setCurrentIndex(0);
}

void MainWindow::onTreeContextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_tree->indexAt(pos);
    if (idx.isValid()) {
        m_tree->setCurrentIndex(idx);
    }

    QMenu menu(this);
    const ContainerInfo c = idx.isValid() ? m_treeModel->getContainer(idx) : ContainerInfo();
    const TemplateInfo t = idx.isValid() ? m_treeModel->getTemplate(idx) : TemplateInfo();

    if (c.isValid()) {
        if (c.isContained()) {
            QAction *hdr = menu.addAction(tr("Blocked by Guardian"));
            hdr->setEnabled(false);
            menu.addSeparator();
            menu.addAction(m_actApps);
        } else {
            if (c.isVault) {
                menu.addAction(c.isLocked() ? m_actUnlock : m_actLock);
            } else {
                menu.addAction(c.isRunning() ? m_actStop : m_actStart);
            }
            menu.addAction(m_actApps);
            menu.addAction(m_actConsole);
            menu.addSeparator();
            menu.addAction(m_actExchange);
            menu.addAction(m_actUsb);
            menu.addSeparator();
            menu.addAction(m_actDestroy);
        }
    } else if (t.isValid()) {
        QAction *createAct = menu.addAction(
            AppIcons::themedOrBundled(QStringLiteral("list-add"), QStringLiteral("new-compartment")),
            tr("Create Compartment from Template..."), this, &MainWindow::onCreate);
        createAct->setEnabled(t.isReady());
        QAction *updAct = menu.addAction(
            AppIcons::themedOrBundled(QStringLiteral("view-refresh"), QStringLiteral("refresh")),
            tr("Update Template Packages"), this, &MainWindow::onUpdateTemplate);
        updAct->setEnabled(t.isReady());
    } else {
        if (idx.isValid() && !idx.parent().isValid()) {
            const bool expanded = m_tree->isExpanded(idx);
            menu.addAction(expanded ? tr("Collapse Group") : tr("Expand Group"), this, [this, idx, expanded]() {
                m_tree->setExpanded(idx, !expanded);
            });
            menu.addSeparator();
        }
        menu.addAction(m_actCreate);
        menu.addAction(m_actNewTemplate);
        menu.addAction(m_actRefresh);
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void MainWindow::onSelectionChanged()
{
    const QModelIndexList sel = m_tree->selectionModel()->selectedIndexes();
    if (!sel.isEmpty()) {
        m_selectedName = sel.first().siblingAtColumn(0).data(Qt::DisplayRole).toString();
        m_selectedIsTemplate = (sel.first().internalId() > 1);
    } else {
        m_selectedName.clear();
        m_selectedIsTemplate = false;
    }
    updateActionStates();
    updateInspector();
}

void MainWindow::onRefresh()
{
    m_model->refresh();
    m_templateModel->refresh();
    if (m_statusLabel && !m_backend->isBusy()) {
        m_statusLabel->setText(tr("Refreshing..."));
    }
}

void MainWindow::onCreate()
{
    const QStringList templates = m_templateModel->readyTemplateNames();

    if (templates.isEmpty()) {
        QMessageBox::warning(this, tr("No Ready Templates"),
            tr("No ready templates found.\nUse 'New Template...' to build a template first."));
        return;
    }

    QString preselected;
    const TemplateInfo selTmpl = selectedTemplate();
    if (selTmpl.isValid() && selTmpl.isReady()) {
        preselected = selTmpl.name;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Create Compartment"));
    auto *form = new QFormLayout(&dlg);

    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(tr("lowercase-name"));
    auto *nameLabel = new QLabel(tr("&Name:"), &dlg);
    nameLabel->setBuddy(nameEdit);

    auto *templateBox = new QComboBox(&dlg);
    templateBox->addItems(templates);
    if (!preselected.isEmpty()) templateBox->setCurrentText(preselected);
    auto *tmplLabel = new QLabel(tr("&Template:"), &dlg);
    tmplLabel->setBuddy(templateBox);

    form->addRow(nameLabel, nameEdit);
    form->addRow(tmplLabel, templateBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString name = nameEdit->text().trimmed();
    const QString tmpl = templateBox->currentText();

    const QRegularExpression re(QStringLiteral("^[a-z0-9-]{1,31}$"));
    if (!re.match(name).hasMatch()) {
        QMessageBox::warning(this, tr("Invalid Name"),
            tr("Name may contain only lowercase letters, digits, and hyphens (max 31 characters)."));
        return;
    }
    if (name.startsWith(QLatin1Char('-')) || name.endsWith(QLatin1Char('-')) ||
        name.contains(QStringLiteral("--"))) {
        QMessageBox::warning(this, tr("Invalid Name"),
            tr("Name cannot start or end with a hyphen or contain consecutive hyphens."));
        return;
    }

    m_backend->createContainer(name, tmpl);

    auto *logDlg = new ProcessLogDialog(m_backend, tr("Creating %1").arg(name), this);
    logDlg->setAttribute(Qt::WA_DeleteOnClose);
    logDlg->show();
}

void MainWindow::onCreateTemplate()
{
    m_backend->launchTemplateWizard();
}

void MainWindow::onUpdateTemplate()
{
    const TemplateInfo t = selectedTemplate();
    if (!t.isValid()) return;
    if (QMessageBox::question(this, tr("Update Template"),
        tr("Update packages inside template '%1'?\n\nExisting compartments created from it remain unchanged.").arg(t.name)) != QMessageBox::Yes) {
        return;
    }
    m_backend->updateTemplateAsync(t.name, t.shipped);
    auto *logDlg = new ProcessLogDialog(m_backend, tr("Updating template %1").arg(t.name), this);
    logDlg->setAttribute(Qt::WA_DeleteOnClose);
    logDlg->show();
}

void MainWindow::onDestroy()
{
    const ContainerInfo c = selectedContainer();
    if (!c.isValid()) return;

    if (c.isContained()) {
        QMessageBox::critical(this, tr("Cannot Destroy"),
            tr("This compartment is blocked by Guardian. Investigate the incident first."));
        return;
    }

    bool ok = false;
    const QString token = QInputDialog::getText(this, tr("Confirm Destruction"),
        tr("This will permanently delete compartment '%1' and all files inside it.\nType the compartment name to confirm:").arg(c.name),
        QLineEdit::Normal, QString(), &ok);

    if (!ok || token != c.name) {
        if (ok) QMessageBox::warning(this, tr("Aborted"), tr("The typed name did not match."));
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
    if (QMessageBox::question(this, tr("Stop Compartment"),
        tr("Stop '%1'?\n\nAny unsaved work inside open apps will be lost.").arg(c.name)) != QMessageBox::Yes) return;
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
    if (QMessageBox::question(this, tr("Lock Vault"),
        tr("Lock vault '%1'?").arg(c.name)) != QMessageBox::Yes) return;
    m_backend->lockVault(c.name);
}

void MainWindow::onConsole()
{
    const ContainerInfo c = selectedContainer();
    if (c.isValid()) m_backend->openConsole(c.name);
}

void MainWindow::onApplications()
{
    const ContainerInfo c = selectedContainer();
    if (!c.isValid()) return;

    ApplicationsDialog dlg(m_backend, c, this);
    connect(&dlg, &ApplicationsDialog::applicationsChanged, this, &MainWindow::onRefresh);
    dlg.exec();
}

void MainWindow::onExchange()
{
    const ContainerInfo c = selectedContainer();
    ExchangeDialog dlg(m_backend, m_model->allContainers(), c.isValid() ? c.name : QString(), this);
    connect(&dlg, &ExchangeDialog::exchangeCompleted, this, &MainWindow::onRefresh);
    dlg.exec();
}

void MainWindow::onUsb()
{
    const ContainerInfo c = selectedContainer();
    UsbDialog dlg(m_backend, m_model->allContainers(), c.isValid() ? c.name : QString(), this);
    connect(&dlg, &UsbDialog::usbChanged, this, &MainWindow::onRefresh);
    dlg.exec();
}

void MainWindow::onRiskChanged(int index)
{
    if (index < 0) return;
    const ContainerInfo c = selectedContainer();
    if (!c.isValid() || c.isContained()) return;
    const QString level = m_inspRiskCombo->itemData(index).toString();
    if (!level.isEmpty() && level != c.normalizedRiskLevel()) {
        m_backend->setRiskLevel(c.name, level);
    }
}

void MainWindow::onAudioChanged()
{
    const ContainerInfo c = selectedContainer();
    if (!c.isValid() || c.isVault || c.isContained()) return;
    m_backend->setAudioConfig(c.name, m_inspAudioOutCheck->isChecked(), m_inspAudioInCheck->isChecked());
}

void MainWindow::onSettings()
{
    QSettings settings;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Settings"));
    auto *form = new QFormLayout(&dlg);

    auto *termEdit = new QLineEdit(settings.value(QStringLiteral("terminal/executable")).toString(), &dlg);
    termEdit->setPlaceholderText(tr("Auto-detect (konsole, mate-terminal, xfce4-terminal, ...)"));
    auto *termLabel = new QLabel(tr("Preferred &Terminal:"), &dlg);
    termLabel->setBuddy(termEdit);

    auto *pollSpin = new QSpinBox(&dlg);
    pollSpin->setRange(5, 300);
    pollSpin->setSuffix(tr(" s"));
    pollSpin->setValue(qBound(5, settings.value(QStringLiteral("polling/intervalSecs"), 10).toInt(), 300));
    auto *pollLabel = new QLabel(tr("&Refresh Interval:"), &dlg);
    pollLabel->setBuddy(pollSpin);

    form->addRow(termLabel, termEdit);
    form->addRow(pollLabel, pollSpin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        settings.setValue(QStringLiteral("terminal/executable"), termEdit->text().trimmed());
        settings.setValue(QStringLiteral("polling/intervalSecs"), pollSpin->value());
        m_pollTimer->setInterval(pollSpin->value() * 1000);
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About ParishBSD Compartment Manager"),
        tr("<b>ParishBSD Compartment Manager</b><br><br>"
           "Manage isolated compartments, encrypted vaults, installed applications, "
           "and templates on ParishBSD."));
}

void MainWindow::onOperationStarted(const QString &desc)
{
    if (m_statusLabel) m_statusLabel->setText(desc + QStringLiteral("..."));
}

void MainWindow::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
    if (m_statusLabel) {
        m_statusLabel->setText(ok ? tr("%1: done.").arg(desc) : tr("%1: failed.").arg(desc));
    }

    if (!ok) {
        QMessageBox::warning(this, tr("Operation Failed"), desc + QStringLiteral("\n\n") + err);
    }

    onRefresh();
}

void MainWindow::onTerminalLaunched(const QString &desc)
{
    if (m_statusLabel) m_statusLabel->setText(tr("%1 opened in terminal.").arg(desc));
}

void MainWindow::onTerminalClosed(const QString &desc)
{
    onRefresh();
    if (m_statusLabel) m_statusLabel->setText(tr("%1 closed.").arg(desc));
}
