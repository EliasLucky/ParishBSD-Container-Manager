#pragma once

#include <QDialog>

#include "ContainerInfo.h"

class ContainerBackend;

class ActionsDialog : public QDialog
{
	Q_OBJECT
public:
	ActionsDialog(ContainerBackend *backend, const ContainerInfo & info, QWidget *parent = nullptr);

signals:
	void containerChanged();

private slots:
	void onStart();
	void onStop();
	void onUnlock();
	void onLock();
	void onConsole();
	void onOpenFiles();
	void onApplications();
	void onRefresh();
	void onDestroy();
	void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
	void rebuildButtons();

	ContainerBackend *m_backend;
	ContainerInfo m_info;
	class QVBoxLayout *m_buttonLayout = nullptr;
};
