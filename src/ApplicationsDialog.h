#pragma once
#include <QDialog>
#include "PackageInfo.h"
#include "CustomApp.h"

class QTabWidget;
class QTableView;
class QLineEdit;
class QPushButton;
class ContainerBacked;
class PackageTableModel;
class CatalogTableModel;

class ApplicationsDialog : public QDialog {
	Q_OBJECT
public:
	ApplicationsDialog(ContainerBacked *backend, const QString &jailName, QWidget *parent = nullptr);

private slots:
	void refresh();
	void onUpdateSelected();
	void onRemoveSelected();
	void onInstallCatalog();
	void onInstallFromFile();
	void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
	void buildUi();
	void populateInstalled();
	void populateCatalog();
	void handleFileInstall(const QString &file);

	ContainerBackend *m_backend;
	QString m_jail;

	QTabWidget *m_tabs = nullptr;

	QTableView *m_installedView = nullptr;
	PackageTableModel *m_installedModel = nullptr;
	QPushButton *m_updateBtn = nullptr;
	QPushButton *m_removeBtn = nullptr;

	QLineEdit *m_catalogSearch = nullptr;
	QTableView *m_catalogView = nullptr;
	CatalogTableModel *m_catalogModel = nullptr;
};
