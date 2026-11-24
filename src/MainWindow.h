#pragma once

#include <QList>
#include <QMainWindow>
#include <QString>
#include <QTimer>

class QAction;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeView;

class ContainerBackend;
class ContainerModel;
class TemplateModel;
class TreeAssemblyModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onRefresh();
    void onSelectionChanged();
    void onCreate();
    void onCreateTemplate();
    void onUpdateTemplate();
    void onDestroy();
    void onStart();
    void onStop();
    void onUnlock();
    void onLock();
    void onConsole();
    void onAttachXpra();
    void onDetachXpra();
    void onApplications();
    void onExchange();
    void onUsb();
    void onActions();
    void onRiskChanged(int index);
    void onAudioChanged();
    void onSettings();
    void onAbout();

    void onOperationStarted(const QString &desc);
    void onOperationFinished(const QString &desc, bool ok, const QString &err);
    void onTerminalLaunched(const QString &desc);
    void onTerminalClosed(const QString &desc);

private:
    void buildMenus();
    void buildToolBar();
    void buildCentralWidget();
    void buildInspectorPane(QWidget *parent);
    void buildStatusBar();

    struct ContainerInfo selectedContainer() const;
    struct TemplateInfo selectedTemplate() const;
    bool isTemplateSelected() const;
    void updateActionStates();
    void updateInspector();

    ContainerBackend *m_backend = nullptr;
    ContainerModel *m_model = nullptr;
    TemplateModel *m_templateModel = nullptr;
    TreeAssemblyModel *m_treeModel = nullptr;

    QTreeView *m_tree = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_statusLabel = nullptr;

    // Right-hand inspector widgets
    QStackedWidget *m_inspectorStack = nullptr;
    QLabel *m_inspName = nullptr;
    QLabel *m_inspDetails = nullptr;
    QLabel *m_inspGuardianBanner = nullptr;
    QComboBox *m_inspRiskCombo = nullptr;
    QGroupBox *m_inspAudioBox = nullptr;
    QCheckBox *m_inspAudioOutCheck = nullptr;
    QCheckBox *m_inspAudioInCheck = nullptr;
    QLabel *m_inspVaultAudioNote = nullptr;
    QPushButton *m_inspStartStopBtn = nullptr;
    QPushButton *m_inspUnlockLockBtn = nullptr;
    QPushButton *m_inspConsoleBtn = nullptr;
    QPushButton *m_inspAttachXpraBtn = nullptr;
    QPushButton *m_inspDetachXpraBtn = nullptr;
    QPushButton *m_inspAppsBtn = nullptr;
    QPushButton *m_inspExchangeBtn = nullptr;
    QPushButton *m_inspUsbBtn = nullptr;
    QPushButton *m_inspActionsBtn = nullptr;
    QPushButton *m_inspDestroyBtn = nullptr;

    QLabel *m_tmplName = nullptr;
    QLabel *m_tmplDetails = nullptr;
    QPushButton *m_tmplCreateBtn = nullptr;
    QPushButton *m_tmplUpdateBtn = nullptr;

    QAction *m_actRefresh = nullptr;
    QAction *m_actCreate = nullptr;
    QAction *m_actCreateFromTemplate = nullptr;
    QAction *m_actNewTemplate = nullptr;
    QAction *m_actDestroy = nullptr;
    QAction *m_actStart = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actUnlock = nullptr;
    QAction *m_actLock = nullptr;
    QAction *m_actConsole = nullptr;
    QAction *m_actAttachXpra = nullptr;
    QAction *m_actDetachXpra = nullptr;
    QAction *m_actApps = nullptr;
    QAction *m_actExchange = nullptr;
    QAction *m_actUsb = nullptr;
    QAction *m_actActions = nullptr;
    QAction *m_actSettings = nullptr;
    QAction *m_actAbout = nullptr;

    QTimer *m_pollTimer = nullptr;
    QString m_selectedName;
    bool m_selectedIsTemplate = false;
    bool m_initialExpandDone = false;
};
