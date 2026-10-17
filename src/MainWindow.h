#pragma once

#include <QMainWindow>

class QLineEdit;
class QTableView;
class QSortFilterProxyModel;
class QAction;
class QLabel;
class Qtimer;
class QSocketNotifier;

class ContainerBackend;
class ContainerModel;

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);

private slots:
	void onRefresh();
	void onSelectionChanged();
	void onCreate();
	void onDestroy();
	void onStart();
	void onStop();
	void onUnlock();
	void onLock();
	void onConsole();
	void onApplications();
	void onActions();
	void onAbout();

	void onOperationStarted(const QString &desc);
	void onOperationFinished(const QString &desc, bool ok, const QString &err);
	void onUsbEvent();
	void onTerminalLaunched(const QString &desc);
	void onTerminalClosed(const QString &desc);

private:
	void buildMenus();
	void buildToolBar();
	void buildCentralWidget();
	void buildStatusBar();
	void setupUsbListener();
	void showUsbAttachDialog(const QString &device);

	struct ContainerInfo selectedContainer() const;

	void updateActionStates();

	QString selectedName() const;

	ContainerBackend *m_backend = nullptr;
	ContainerModel *m_model = nullptr;
	QSortFilterProxyModel *m_proxy = nullptr;

	QTableView *m_table = nullptr;
	QLineEdit *m_search = nullptr;
	QLabel *m_statusLabel = nullptr;

	QAction *m_actRefresh = nullptr;
	QAction *m_actCreate = nullptr;
	QAction *m_actDestroy = nullptr;
	QAction *m_actStart = nullptr;
	QAction *m_actStop = nullptr;
	QAction *m_actUnlock = nullptr;
	QAction *m_actLock = nullptr;
	QAction *m_actConsole = nullptr;
	QAction *m_actApplications = nullptr;
	QAction *m_actActions = nullptr;
	QAction *m_actAbout = nullptr;

	QSocketNotifier *m_usbNotifier = nullptr;

	QTimer *m_pollTimer = nullptr;
};
