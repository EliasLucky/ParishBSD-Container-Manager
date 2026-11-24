#include "MainWindow.h"
#include "ActionsDialog.h"
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
    resize(1080, 660);

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

    // Re-sync tree selection and inspector whenever async model resets complete
    connect(m_treeModel, &QAbstractItemModel::modelReset, this, [this]() {
        if (!m_initialExpandDone) {
            m_tree->expand(m_treeModel->index(0, 0));
            m_initialExpandDone = true;
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
    m_actCreate->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_actCreate->setShortcut(QKeySequence::New);

    m_actNewTemplate = fileMenu->addAction(tr("New &Template (Wizard)..."));
    m_actNewTemplate->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));

    m_actDestroy = fileMenu->addAction(tr("&Destroy..."));
    m_actDestroy->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    fileMenu->addSeparator();

    m_actApps = fileMenu->addAction(tr("&Applications && Packages..."));
    m_actApps->setIcon(QIcon::fromTheme(QStringLiteral("system-software-install")));

    m_actConsole = fileMenu->addAction(tr("Open &Console"));
    m_actConsole->setIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));
    fileMenu->addSeparator();

    m_actRefresh = fileMenu->addAction(tr("&Refresh"));
    m_actRefresh->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    m_actRefresh->setShortcut(QKeySequence::Refresh);

    fileMenu->addSeparator();
    QAction *quit = fileMenu->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);

    QMenu *runMenu = menuBar()->addMenu(tr("&Run"));
    m_actStart = runMenu->addAction(tr("&Start"));
    m_actStart->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    m_actStop = runMenu->addAction(tr("Sto&p"));
    m_actStop->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
    m_actAttachXpra = runMenu->addAction(tr("Attach &Display (Xpra)"));
    m_actAttachXpra->setIcon(QIcon::fromTheme(QStringLiteral("video-display")));
    m_actDetachXpra = runMenu->addAction(tr("Detach Dis&play"));
    m_actDetachXpra->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    runMenu->addSeparator();
    m_actUnlock = runMenu->addAction(tr("&Unlock Vault"));
    m_actUnlock->setIcon(QIcon::fromTheme(QStringLiteral("security-medium")));
    m_actLock = runMenu->addAction(tr("&Lock Vault"));
    m_actLock->setIcon(QIcon::fromTheme(QStringLiteral("security-high")));
    runMenu->addSeparator();
    m_actActions = runMenu->addAction(tr("&Actions..."));
    m_actActions->setIcon(QIcon::fromTheme(QStringLiteral("configure")));

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    m_actExchange = toolsMenu->addAction(tr("File &Exchange..."));
    m_actExchange->setIcon(QIcon::fromTheme(QStringLiteral("folder-sync")));
    m_actExchange->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_actUsb = toolsMenu->addAction(tr("&USB && Removable Media..."));
    m_actUsb->setIcon(QIcon::fromTheme(QStringLiteral("drive-removable-media")));
    m_actUsb->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    toolsMenu->addSeparator();
    m_actSettings = toolsMenu->addAction(tr("&Settings..."));
    m_actSettings->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system")));

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    m_actAbout = helpMenu->addAction(tr("&About ParishBSD"));
    m_actAbout->setIcon(QIcon::fromTheme(QStringLiteral("help-about")));

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
    connect(m_actAttachXpra, &QAction::triggered, this, &MainWindow::onAttachXpra);
    connect(m_actDetachXpra, &QAction::triggered, this, &MainWindow::onDetachXpra);
    connect(m_actApps, &QAction::triggered, this, &MainWindow::onApplications);
    connect(m_actExchange, &QAction::triggered, this, &MainWindow::onExchange);
    connect(m_actUsb, &QAction::triggered, this, &MainWindow::onUsb);
    connect(m_actActions, &QAction::triggered, this, &MainWindow::onActions);
}

void MainWindow::buildToolBar()
{
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));

    auto add = [tb](QAction *a, const QString &iconName) {
        const QIcon icon = QIcon::fromTheme(iconName);
        if (!icon.isNull()) a->setIcon(icon);
        tb->addAction(a);
    };

    add(m_actCreate, QStringLiteral("list-add"));

    m_actCreateFromTemplate = new QAction(tr("Create From Template"), this);
    add(m_actCreateFromTemplate, QStringLiteral("document-new"));
    connect(m_actCreateFromTemplate, &QAction::triggered, this, &MainWindow::onCreate);

    add(m_actNewTemplate, QStringLiteral("list-add"));
    add(m_actDestroy, QStringLiteral("edit-delete"));
    tb->addSeparator();
    add(m_actStart, QStringLiteral("media-playback-start"));
    add(m_actStop, QStringLiteral("media-playback-stop"));
    add(m_actAttachXpra, QStringLiteral("video-display"));
    tb->addSeparator();
    add(m_actUnlock, QStringLiteral("security-medium"));
    add(m_actLock, QStringLiteral("security-high"));
    tb->addSeparator();
    add(m_actApps, QStringLiteral("system-software-install"));
    add(m_actConsole, QStringLiteral("utilities-terminal"));
    add(m_actExchange, QStringLiteral("folder-sync"));
    add(m_actUsb, QStringLiteral("drive-removable-media"));
    add(m_actActions, QStringLiteral("configure"));
    tb->addSeparator();
    add(m_actRefresh, QStringLiteral("view-refresh"));
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
    m_tree->setAccessibleName(tr("Compartments and Templates tree"));

    QHeaderView *h = m_tree->header();
    h->setStretchLastSection(true);
    h->resizeSection(TreeAssemblyModel::NameColumn, 210);
    h->resizeSection(TreeAssemblyModel::SourceColumn, 150);
    h->resizeSection(TreeAssemblyModel::StateColumn, 130);
    h->resizeSection(TreeAssemblyModel::RiskColumn, 140);

    auto *inspectorContainer = new QWidget(splitter);
    inspectorContainer->setMinimumWidth(300);
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

    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_tree, &QTreeView::doubleClicked, this, [this]() {
        if (selectedContainer().isValid()) onActions();
        else if (isTemplateSelected()) onCreate();
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
        tr("Select a compartment or template on the left to inspect its state, "
           "manage installed applications, set Qubes-style risk levels, or open a console.\n\n"
           "Tip: Use 'New Template (Wizard)...' to build custom compartments with "
           "graphical display (T-xpra), Linux, or Windows (Wine) support."),
        welcomeBox);
    welcomeLbl->setWordWrap(true);
    welcomeLay->addWidget(welcomeLbl);
    welcomeLay->addStretch();
    emptyLayout->addWidget(welcomeBox);
    m_inspectorStack->addWidget(emptyPage);

    // Page 1: Compartment Inspector
    auto *compPage = new QWidget(parent);
    auto *compLay = new QVBoxLayout(compPage);
    auto *compBox = new QGroupBox(tr("Compartment Inspector"), compPage);
    auto *compBoxLay = new QVBoxLayout(compBox);

    m_inspName = new QLabel(compBox);
    m_inspName->setTextFormat(Qt::PlainText);
    QFont boldFont = m_inspName->font();
    boldFont.setBold(true);
    boldFont.setPointSize(boldFont.pointSize() + 1);
    m_inspName->setFont(boldFont);
    compBoxLay->addWidget(m_inspName);

    m_inspGuardianBanner = new QLabel(
        tr("CONTAINED BY GUARDIAN: Investigate the incident before restoring or destroying."),
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
    m_inspRiskCombo->setAccessibleName(tr("Compartment visual risk level"));
    auto *riskLbl = new QLabel(tr("&Risk Level:"), compBox);
    riskLbl->setBuddy(m_inspRiskCombo);
    riskRow->addRow(riskLbl, m_inspRiskCombo);
    compBoxLay->addLayout(riskRow);

    connect(m_inspRiskCombo, QOverload<int>::of(&QComboBox::activated),
            this, &MainWindow::onRiskChanged);

    m_inspAudioBox = new QGroupBox(tr("Audio"), compBox);
    auto *audioBoxLay = new QVBoxLayout(m_inspAudioBox);
    audioBoxLay->setContentsMargins(8, 6, 8, 6);
    m_inspAudioOutCheck = new QCheckBox(tr("Audio &Output (Playback)"), m_inspAudioBox);
    m_inspAudioOutCheck->setAccessibleName(tr("Enable compartment audio output"));
    m_inspAudioInCheck = new QCheckBox(tr("Audio &Input (Microphone)"), m_inspAudioBox);
    m_inspAudioInCheck->setAccessibleName(tr("Enable compartment audio input"));
    m_inspVaultAudioNote = new QLabel(tr("Vaults exclude audio devices for security."), m_inspAudioBox);
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
    m_inspConsoleBtn = new QPushButton(tr("Open Console"), compBox);

    auto *xpraRow = new QHBoxLayout();
    m_inspAttachXpraBtn = new QPushButton(tr("Attach Display"), compBox);
    m_inspDetachXpraBtn = new QPushButton(tr("Detach Display"), compBox);
    xpraRow->addWidget(m_inspAttachXpraBtn);
    xpraRow->addWidget(m_inspDetachXpraBtn);

    m_inspAppsBtn = new QPushButton(tr("Applications && Packages..."), compBox);

    auto *ioRow = new QHBoxLayout();
    m_inspExchangeBtn = new QPushButton(tr("File Exchange..."), compBox);
    m_inspUsbBtn = new QPushButton(tr("USB && Media..."), compBox);
    ioRow->addWidget(m_inspExchangeBtn);
    ioRow->addWidget(m_inspUsbBtn);

    m_inspActionsBtn = new QPushButton(tr("More Actions..."), compBox);
    m_inspDestroyBtn = new QPushButton(tr("Destroy..."), compBox);

    compBoxLay->addWidget(m_inspStartStopBtn);
    compBoxLay->addWidget(m_inspUnlockLockBtn);
    compBoxLay->addWidget(m_inspConsoleBtn);
    compBoxLay->addLayout(xpraRow);
    compBoxLay->addWidget(m_inspAppsBtn);
    compBoxLay->addLayout(ioRow);
    compBoxLay->addWidget(m_inspActionsBtn);
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
    connect(m_inspAttachXpraBtn, &QPushButton::clicked, this, &MainWindow::onAttachXpra);
    connect(m_inspDetachXpraBtn, &QPushButton::clicked, this, &MainWindow::onDetachXpra);
    connect(m_inspAppsBtn, &QPushButton::clicked, this, &MainWindow::onApplications);
    connect(m_inspExchangeBtn, &QPushButton::clicked, this, &MainWindow::onExchange);
    connect(m_inspUsbBtn, &QPushButton::clicked, this, &MainWindow::onUsb);
    connect(m_inspActionsBtn, &QPushButton::clicked, this, &MainWindow::onActions);
    connect(m_inspDestroyBtn, &QPushButton::clicked, this, &MainWindow::onDestroy);

    compLay->addWidget(compBox);
    m_inspectorStack->addWidget(compPage);

    // Page 2: Template Inspector
    auto *tmplPage = new QWidget(parent);
    auto *tmplLay = new QVBoxLayout(tmplPage);
    auto *tmplBox = new QGroupBox(tr("Template Inspector"), tmplPage);
    auto *tmplBoxLay = new QVBoxLayout(tmplBox);

    m_tmplName = new QLabel(tmplBox);
    m_tmplName->setFont(boldFont);
    tmplBoxLay->addWidget(m_tmplName);

    m_tmplDetails = new QLabel(tmplBox);
    m_tmplDetails->setWordWrap(true);
    m_tmplDetails->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    tmplBoxLay->addWidget(m_tmplDetails);

    m_tmplCreateBtn = new QPushButton(tr("Create Compartment from Template..."), tmplBox);
    m_tmplUpdateBtn = new QPushButton(tr("Update Template Packages"), tmplBox);
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
    const bool haveContainer = c.isValid();
    const bool haveTemplate = isTemplateSelected();

    const bool running = haveContainer && c.isRunning();
    const bool locked = haveContainer && c.isVault && c.isLocked();
    const bool contained = haveContainer && c.isContained();

    const bool isApp = haveContainer && !c.isVault;
    const bool isVault = haveContainer && c.isVault;
    const bool hasGui = isApp && c.supportsGui();

    m_actStart->setVisible(isApp);
    m_actStop->setVisible(isApp);
    m_actAttachXpra->setVisible(hasGui);
    m_actDetachXpra->setVisible(hasGui);
    m_actConsole->setVisible(haveContainer);
    m_actUnlock->setVisible(isVault);
    m_actLock->setVisible(isVault);

    m_actApps->setVisible(haveContainer);
    m_actDestroy->setVisible(haveContainer);
    m_actActions->setVisible(haveContainer);
    m_actCreateFromTemplate->setVisible(haveTemplate);

    m_actStart->setEnabled(haveContainer && !running && !contained);
    m_actStop->setEnabled(haveContainer && running && !contained);
    m_actAttachXpra->setEnabled(hasGui && running && !contained);
    m_actDetachXpra->setEnabled(hasGui && running && !contained);
    m_actUnlock->setEnabled(haveContainer && locked && !contained);
    m_actLock->setEnabled(haveContainer && !locked && !contained);
    m_actConsole->setEnabled(haveContainer && running && !contained);
    m_actApps->setEnabled(haveContainer);
    m_actExchange->setEnabled(!contained);
    m_actUsb->setEnabled(!contained);
    m_actDestroy->setEnabled(haveContainer && !contained);
    m_actActions->setEnabled(haveContainer);
}

void MainWindow::updateInspector()
{
    const ContainerInfo c = selectedContainer();
    if (c.isValid()) {
        m_inspectorStack->setCurrentIndex(1);
        m_inspName->setText(c.name);
        m_inspGuardianBanner->setVisible(c.isContained());

        const QString caps = c.capabilities.isEmpty()
            ? tr("Standard")
            : c.capabilities.join(QStringLiteral(", "));
        m_inspDetails->setText(
            tr("Kind: %1\nState: %2\nTemplate: %3\nCapabilities: %4\nDisk usage: %5")
                .arg(c.kind, c.humanState(), c.sourceTemplate, caps, formatDiskUsage(c.diskUsage)));

        {
            const QSignalBlocker b(m_inspRiskCombo);
            const int rIdx = m_inspRiskCombo->findData(c.normalizedRiskLevel());
            if (rIdx >= 0) m_inspRiskCombo->setCurrentIndex(rIdx);
        }
        m_inspRiskCombo->setEnabled(!c.isContained());

        const bool running = c.isRunning();
        const bool contained = c.isContained();
        const bool showGui = !c.isVault && c.supportsGui();

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
        m_inspStartStopBtn->setEnabled(!contained);

        m_inspUnlockLockBtn->setVisible(c.isVault);
        m_inspUnlockLockBtn->setText(c.isLocked() ? tr("Unlock Vault") : tr("Lock Vault"));
        m_inspUnlockLockBtn->setEnabled(!contained);

        m_inspConsoleBtn->setEnabled(running && !contained);
        m_inspAttachXpraBtn->setVisible(showGui);
        m_inspDetachXpraBtn->setVisible(showGui);
        m_inspAttachXpraBtn->setEnabled(showGui && running && !contained);
        m_inspDetachXpraBtn->setEnabled(showGui && running && !contained);
        m_inspAppsBtn->setEnabled(true);
        m_inspExchangeBtn->setEnabled(!contained);
        m_inspUsbBtn->setEnabled(!contained);
        m_inspActionsBtn->setEnabled(true);
        m_inspDestroyBtn->setEnabled(!contained);
        return;
    }

    const TemplateInfo t = selectedTemplate();
    if (t.isValid()) {
        m_inspectorStack->setCurrentIndex(2);
        m_tmplName->setText(t.name);
        m_tmplDetails->setText(
            tr("Origin: %1\nKind: %2\nState: %3\nBase snapshot: %4\nBuild size: %5 MiB\n\n%6")
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
    if (m_statusLabel) m_statusLabel->setText(tr("Refreshing..."));
}

void MainWindow::onCreate()
{
    const QStringList templates = m_templateModel->readyTemplateNames();

    if (templates.isEmpty()) {
        QMessageBox::warning(this, tr("No ready templates"),
            tr("No ready templates found.\nUse 'Compartment -> New Template (Wizard)...' to build one first."));
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
        QMessageBox::warning(this, tr("Invalid name"),
            tr("Name may contain only lowercase letters, digits, and hyphens (max 31 characters)."));
        return;
    }
    if (name.startsWith(QLatin1Char('-')) || name.endsWith(QLatin1Char('-')) ||
        name.contains(QStringLiteral("--"))) {
        QMessageBox::warning(this, tr("Invalid name"),
            tr("Name cannot start/end with a hyphen or contain consecutive hyphens."));
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
        tr("Update packages inside template '%1'?\n\nExisting compartments cloned from it remain unchanged.").arg(t.name)) != QMessageBox::Yes) {
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

void MainWindow::onAttachXpra()
{
    const ContainerInfo c = selectedContainer();
    if (c.isValid() && !c.isVault && !c.isContained()) {
        m_backend->attachXpra(c.name);
    }
}

void MainWindow::onDetachXpra()
{
    const ContainerInfo c = selectedContainer();
    if (c.isValid() && !c.isVault && !c.isContained()) {
        m_backend->detachXpra(c.name);
    }
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

void MainWindow::onActions()
{
    const ContainerInfo c = selectedContainer();
    if (!c.isValid()) return;

    ActionsDialog dlg(m_backend, c, m_model->allContainers(), this);
    connect(&dlg, &ActionsDialog::containerChanged, this, &MainWindow::onRefresh);
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
    termEdit->setPlaceholderText(tr("Auto-detect ($TERMINAL, konsole, xfce4-terminal, ...)"));
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
           "A graphical front-end for parishctl(1) managing FreeBSD jail compartments, "
           "encrypted vaults, applications, and templates.<br><br>"
           "Part of ParishBSD."));
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
        QMessageBox::warning(this, tr("Operation failed"), desc + QStringLiteral("\n\n") + err);
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
