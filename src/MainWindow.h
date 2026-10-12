#pragma once

#include <QMainWindow>

class QLineEdit;
class QTableView;
class QSortFilterProxyModel;
class QAction;
class QLabel;

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
	void onActions();
	void onAbout();

	void onOperationStarted(const QString &desc);
	void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
	void buildMenus();
	void buildToolbar();
	void buildCentralWidget();
	void buildStatusBar();

	struct ContainerInfo selectedCotainer() const;

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
	QAction *m_actActions = nullptr;
};
