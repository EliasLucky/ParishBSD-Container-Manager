#include "ActionsDialog.h"
#include "ContainerBackend.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QInputDialog>
#include <QLineEdit>

ActionsDialog::ActionsDialog(ContainerBackend *backend, const ContainerInfo &info, QWidget *parent) : QDialog(parent), m_backend(backend), m_info(info)
{
    setWindowTitle(tr("Actions: %1").arg(info.name));
    setMinimumWidth(420);

    auto *root = new QVBoxLayout(this);

    auto *summary = new QLabel(this);
    summary->setTextFormat(Qt::RichText);
    summary->setText(tr(
        "<b>%1</b><br>"
        "Kind: %2<br>"
        "State: %3<br>"
        "Template: %4<br>"
        "Disk usage: %5")
        .arg(info.name.toHtmlEscaped(), info.kind.toHtmlEscaped(), info.humanState().toHtmlEscaped(), info.sourceTemplate.toHtmlEscaped(), formatDiskUsage(info.diskUsage).toHtmlEscaped()));
    root->addWidget(summary);
    root->addSpacing(8);

    m_buttonLayout = new QVBoxLayout();
    root->addLayout(m_buttonLayout);

    rebuildButtons();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    connect(m_backend, &ContainerBackend::operationFinished, this, &ActionsDialog::onOperationFinished);
}

void ActionsDialog::rebuildButtons()
{
    while (QLayoutItem *item = m_buttonLayout->takeAt(0)) {
        if (auto *w = item->widget()) w->deleteLater();
        delete item;
    }

    auto addButton = [this](const QString &text, const QString &iconName, auto onClick) -> QPushButton* {
        auto *b = new QPushButton(text, this);
        if (!iconName.isEmpty()) {
            const QIcon icon = QIcon::fromTheme(iconName);
            if (!icon.isNull()) b->setIcon(icon);
        }
        connect(b, &QPushButton::clicked, this, onClick);
        m_buttonLayout->addWidget(b);
        return b;
    };

    auto addSeparator = [this]() {
        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        m_buttonLayout->addWidget(sep);
    };

    if (m_info.isContained()) {
        auto *banner = new QLabel(tr("<b style='color:white;background-color:red;padding:4px;'>CONTAINED BY GUARDIAN</b>"), this);
        banner->setAlignment(Qt::AlignCenter);
        m_buttonLayout->addWidget(banner);
        addButton(tr("Open Console"), "utilities-terminal", &ActionsDialog::onConsole);
        return; // Do not show standard actions
    }

    if (m_info.isVault) {
        if (m_info.isLocked()) {
            addButton(tr("Unlock vault"), "security-medium", &ActionsDialog::onUnlock);
        }
        else {
            addButton(tr("Open files"), "system-file-manager", &ActionsDialog::onOpenFiles);
            addButton(tr("Lock vault"), "security-high", &ActionsDialog::onLock);
        }
    }
    else {
        if (m_info.isRunning()) {
            addButton(tr("Open console"), "utilities-terminal", &ActionsDialog::onConsole);
        }   
        else {
            addButton(tr("Start"), "media-play-start", &ActionsDialog::onStart);
        }
    }

    addSeparator();

    if (!m_info.isVault && m_info.isRunning()) {
        addButton(tr("Stop Container"), "media-playback-stop", &ActionsDialog::onStop);
    }

    addButton(tr("Refresh Status"), "view-refresh", &ActionsDialog::onRefresh);

    addSeparator();

    addButton(m_info.isVault ? tr("Destroy Vault...") : tr("Destroy Container..."), "edit-delete", &ActionsDialog::onDestroy);
}

void ActionsDialog::onStart() { m_backend->startContainer(m_info.name); }

void ActionsDialog::onStop()
{
    if (QMessageBox::question(this, tr("Stop container"),
        tr("Stop '%1'?\n\nAny unsaved work will be lost.").arg(m_info.name)) != QMessageBox::Yes) return;
    m_backend->stopContainer(m_info.name);
}

void ActionsDialog::onUnlock() { m_backend->unlockVault(m_info.name); }

void ActionsDialog::onLock()
{
    if (QMessageBox::question(this, tr("Lock vault"),
        tr("Lock vault '%1'?\n\nThe container will stop and its encryption key will be unloaded.").arg(m_info.name)) != QMessageBox::Yes) return;
    m_backend->lockVault(m_info.name);
}

void ActionsDialog::onConsole() { m_backend->openConsole(m_info.name); }

void ActionsDialog::onOpenFiles()
{
    // Placeholder: open file manager if vault unlocked
}

void ActionsDialog::onRefresh()
{
    emit containerChanged();
    accept();
}

void ActionsDialog::onDestroy() {
    if (m_info.isContained()) {
        QMessageBox::critical(this, tr("Cannot Destroy"),
            tr("This compartment is CONTAINED by Guardian. Investigate the incident first."));
        return;
    }

    bool ok = false;
    const QString token = QInputDialog::getText(this, tr("Confirm Destruction"),
        tr("This will destroy compartment '%1'.\nType the name to confirm:").arg(m_info.name),
        QLineEdit::Normal, QString(), &ok);

    if (!ok || token != m_info.name) {
        if (ok) QMessageBox::warning(this, tr("Aborted"), tr("Confirmation token did not match."));
        return;
    }
    
    m_backend->destroyContainer(m_info.name, token);
}

void ActionsDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
    if (!ok) {
        QMessageBox::warning(this, tr("Operation failed"), desc + "\n\n" + err);
    }
    emit containerChanged();
    accept();
}