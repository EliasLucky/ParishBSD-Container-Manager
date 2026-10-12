#include "ActionsDialog.h"
#include "ContainerBackend.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDialogButtonBox>

ActionsDialog::ActionsDialog(ContainerBackend *backend, const ContainerInfo &info, QWidget *parent) : QDialog(parent), m_backend(backend, m_info(info)
{
	setWindowTitle(tr("Actions: %1").arg(info.name));
	setMinimumWidth(420);

	auto *root = new QVboxLayout(this);

	auto *summary = new QLabel(this);
	summary->setTextFormat(Qt::RichText);
	summary->setText(tr(
		"<b>%1</b><br>"
		"Kind: %2<br>"
		"State: %3<br>"
		"Personality: %4<br>"
		"Disk usage: %5")
		.arg(info.name.toHtmlEscaped(), info.kind.toHtmlEscaped(), info.state.toHtmlEscaped(), info.personality.toHtmlEscaped(), formatDiskUsage(info.diskUSage).toHtmlEscaped()));
	root->addWidget(summary);
	root->addSpacing(8);

	m_buttonLayout = new QVBoxLayout();
	root->addLayout(m_buttonLayout);

	rebuildButtons();

	auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(box, &DialogButtonBox::rejected, this, &QDialog::reject);
	root->addWidget(box);

	connect(m_backend, &ContainerBackend::operationFinished, this, &ActionsDialog::onOperationFinished);
}

void ActionsDialog::rebuildButtons()
{
	while (QLayoutItem *item = m_buttonLayout->takeAt(0)) {
		if (auto *w = item->widget()) w->deleteLater();
		delete item;
	}

	auto addButton = [this](const QString &text, auto slot) {
		auto *b = new QPushButton(text, this);
		connect(b, &QPushButton::clicked, this, slot);
		m_buttonLayout->addWidget(b);
		return b;
	};

	if (m_info.isVault) {
		if (m_info.isLocked()) {
			addButton(tr("Unlock vault"), &ActionsDialog::onUnlock);
		}
		else {
			addButton(tr("Open files"), &ActionsDialog::onOpenFiles);
			addButton(tr("Lock vault"), &ActionsDialog::onLock);
		}
	}
	else {
		if (m_info.isRunning()) {
			addButton(tr("Open console"), &ActionsDialog::onConsole);
			addButton(tr("Stop"), &ActionsDialog::onStop);
		}	
		else {
			addButton(tr("Start"), &ActionsDialog::onStart);
		}
	}

	addButton(tr("Destroy..."), &ActionsDialog::onDestroy);
}

void ActionsDialog::onStart()
{
	m_backend->startContainer(m_info.name);
}

void ActionsDialog::onStop()
{
	if (QMessageBox::question(this, tr("Stop container"),
		tr("Stop '%1'?\n\nAny unsaved work will be lost.").arg(m_info.name)) != QMessageBox::Yes)
		return;
	m_backend->stopContainer(m_info.name);
}

void ActionsDialog::onUnlock()
{
	m_backend->unlockVault(m_info.name);
}

void ActionsDialog::onLock()
{
	if (QMessageBox::question(this, tr("Lock vault"),
		tr("Lock vault '%1'?\n\nThe container will stop and its encryption key will be unloaded.").arg(m_info.name)) != QMessageBox::Yes)
		return;
	m_backend->lockVault(m_info.name);
}

void ActionsDialog::onConsole()
{
	m_backend->openConsole(m_info.name);
}

void ActionsDialog::onOpenFiles()
{
	m_backend->openVaultFiles(m_info.name);
}

void ActionsDialog::onDestroy() {
	const QString warning = m_info.isVault
		? tr("Destroy vault '%1'?\n\nThe container AND its encrypted data "
		     "will be destroyed. The data cannot be recovered, even with the passphrase.").arg(m_info.name)
		: tr("Destroy container '%1'?\n\nThis cannot be undone.").arg(m_info.name);

	if (QMessageBox::warning(this, tr("Destroy"), warning, QMessage::Yes | QMessage::No) != QMessageBox::Yes
		return;
	
	m_backend->destroyContainer(m_info.name);
}

void ActionsDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
	if (!ok) {
		QMessageBox::warning(this, tr("Operation failed"), desc + "\n\n" + err);
	}

	emit containerChanged();
	accept();
}
