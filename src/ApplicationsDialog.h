#pragma once

#include <QDialog>
#include <QString>
#include <QSet>

class QTabWidget;
class QTableView;
class QLineEdit;
class QLabel;
class QPushButton;

class ContainerBacked;
class PackageTableModel;
class CatalogTableModel;

class ApplicationsDialog : public QDialog {
	Q_OBJECT
public:
	ApplicationsDialog(ContainerBacked *backend, const QString &jailName, QWidget *parent = nullptr);

signals:
	void applicationsChanged();

private slots:
	void refresh();
	void onSelectionChanged();
	void onUpdateSelected();
	void onRemoveSelected();
	void onInstallCatalog();
	void onInstallFromFile();
	void onLaunchSelected();
	void onManageLauncher();
	void onEditLauncher();
	void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
	enum class InstallMode { Installer, Portable };
	enum class LauncherMode { None, Menu, Desktop, Both };

	void buildUi()
	void buildInstalledTab(QWidget *parent);
	void buildInstalNewTab(QWidget *parent);
	void populateInstalled();
	void populateCatalog();

	void handleFileInstall(const QString &file);
	void confirmAndInstall(const QString &file, const QString &detected, InstallMode mode);
	void askInstallerOrPortable(const QString &file, const QString &detected);
	LauncherMode askLauncherPreference() const;

	bool launcherExists(const QString &jail, const QString &name, bool onDesktop) const;
	LauncherMode currentLauncherState(const QString &name) const;
	void launchCustomApp(const QString &appName);
	void launchNativePackage(const QString &packageName);
	void createLauncher(const QString &name, LauncherMode mode);
	void removeLauncher(const QString &name);

	bool selectedIsCustom() const;
	QString selectedNames() const;
	PackaeInfo selectedPackage() const;
	void updateActionStates();

	ContainerBackend *m_backend;
	QString m_jail;

	QTabWidget *m_tabs = nullptr;

	QTableView *m_installedView = nullptr;
	PackageTableModel *m_installedModel = nullptr;
	QPushButton *m_updateBtn = nullptr;
	QPushButton *m_removeBtn = nullptr;
	QPushButton *m_launchBtn = nullptr;
	QPushButton *m_launcherBtn = nullptr;
	QPushButton *m_editLauncherBtn = nullptr;
	QLabel *m_installedSummary = nullptr;

	QLineEdit *m_catalogSearch = nullptr;
	QTableView *m_catalogView = nullptr;
	CatalogTableModel *m_catalogModel = nullptr;
	QPushButton *m_installFromFileBtn = nullptr;

	QSet<String> m_customAppNames;
};
