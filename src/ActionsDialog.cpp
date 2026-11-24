#include "ActionsDialog.h"
#include "ApplicationsDialog.h"
#include "ContainerBackend.h"
#include "ProcessLogDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

ActionsDialog::ActionsDialog(ContainerBackend *backend, const ContainerInfo &info, QWidget *parent)
    : QDialog(parent), m_backend(backend), m_info(info)
{
    setWindowTitle(tr("Actions: %1").arg(info.name));
    setMinimumWidth(440);

    auto *root = new QVBoxLayout(this);

    auto *summary = new QLabel(this);
    summary->setTextFormat(Qt::RichText);
    summary->setWordWrap(true);
    const QString caps = info.capabilities.isEmpty()
        ? tr("None")
        : info.capabilities.join(QStringLiteral(", "));
    summary->setText(tr(
        "<b>%1</b><br>"
        "Kind: %2<br>"
        "State: %3<br>"
        "Risk Level: %4<br>"
        "Capabilities: %5<br>"
        "Template: %6<br>"
        "Disk usage: %7")
        .arg(info.name.toHtmlEscaped(),
             info.kind.toHtmlEscaped(),
             info.humanState().toHtmlEscaped(),
             info.humanRiskLevel().toHtmlEscaped(),
             caps.toHtmlEscaped(),
             info.sourceTemplate.toHtmlEscaped(),
             formatDiskUsage(info.diskUsage).toHtmlEscaped()));
    root->addWidget(summary);

    auto *riskForm = new QFormLayout();
    m_riskCombo = new QComboBox(this);
    m_riskCombo->addItem(tr("Low (Trusted)"), QStringLiteral("low"));
    m_riskCombo->addItem(tr("Medium (Work)"), QStringLiteral("medium"));
    m_riskCombo->addItem(tr("High (Untrusted)"), QStringLiteral("high"));
    m_riskCombo->addItem(tr("Critical (Vault)"), QStringLiteral("critical"));
    const int riskIdx = m_riskCombo->findData(info.normalizedRiskLevel());
    if (riskIdx >= 0) m_riskCombo->setCurrentIndex(riskIdx);
    m_riskCombo->setEnabled(!info.isContained());
    m_riskCombo->setAccessibleName(tr("Compartment risk level"));

    auto *riskLabel = new QLabel(tr("&Risk Level:"), this);
    riskLabel->setBuddy(m_riskCombo);
    riskForm->addRow(riskLabel, m_riskCombo);
    root->addLayout(riskForm);

    connect(m_riskCombo, QOverload<int>::of(&QComboBox::activated),
            this, &ActionsDialog::onRiskLevelChanged);

    root->addSpacing(8);

    m_buttonLayout = new QVBoxLayout();
    root->addLayout(m_buttonLayout);

    rebuildButtons();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    connect(m_backend, &ContainerBackend::operationFinished,
            this, &ActionsDialog::onOperationFinished);
}

void ActionsDialog::rebuildButtons()
{
    while (QLayoutItem *item = m_buttonLayout->takeAt(0)) {
        if (auto *w = item->widget()) w->deleteLater();
        delete item;
    }

    auto addButton = [this](const QString &text, const QString &iconName, auto onClick) -> QPushButton * {
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
        auto *banner = new QLabel(
            tr("<b>CONTAINED BY GUARDIAN</b><br>Investigate the incident before taking recovery or destruction actions."),
            this);
        banner->setWordWrap(true);
        banner->setAlignment(Qt::AlignCenter);
        banner->setStyleSheet(QStringLiteral(
            "QLabel { color: #ffffff; background-color: #b91c1c; padding: 8px; border-radius: 4px; }"));
        banner->setAccessibleName(tr("Guardian containment warning"));
        m_buttonLayout->addWidget(banner);
        return;
    }

    if (m_info.isVault) {
        if (m_info.isLocked()) {
            addButton(tr("Unlock Vault"), QStringLiteral("security-medium"), &ActionsDialog::onUnlock);
        } else {
            addButton(tr("Open Shell / Files"), QStringLiteral("utilities-terminal"), &ActionsDialog::onConsole);
            addButton(tr("Lock Vault"), QStringLiteral("security-high"), &ActionsDialog::onLock);
        }
    } else {
        if (m_info.isRunning()) {
            addButton(tr("Open Console"), QStringLiteral("utilities-terminal"), &ActionsDialog::onConsole);
        } else {
            addButton(tr("Start"), QStringLiteral("media-playback-start"), &ActionsDialog::onStart);
        }
    }

    addSeparator();

    addButton(tr("Applications && Packages..."), QStringLiteral("system-software-install"), &ActionsDialog::onApplications);

    if (!m_info.isVault && m_info.isRunning()) {
        addButton(tr("Stop Container"), QStringLiteral("media-playback-stop"), &ActionsDialog::onStop);
    }

    addButton(tr("Refresh Status"), QStringLiteral("view-refresh"), &ActionsDialog::onRefresh);

    addSeparator();

    addButton(m_info.isVault ? tr("Destroy Vault...") : tr("Destroy Container..."),
              QStringLiteral("edit-delete"), &ActionsDialog::onDestroy);
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

void ActionsDialog::onApplications()
{
    ApplicationsDialog dlg(m_backend, m_info, this);
    dlg.exec();
}

void ActionsDialog::onRiskLevelChanged(int index)
{
    if (index < 0) return;
    const QString level = m_riskCombo->itemData(index).toString();
    if (!level.isEmpty() && level != m_info.normalizedRiskLevel()) {
        m_backend->setRiskLevel(m_info.name, level);
    }
}

void ActionsDialog::onRefresh()
{
    emit containerChanged();
    accept();
}

void ActionsDialog::onDestroy()
{
    if (m_info.isContained()) return;

    bool ok = false;
    const QString token = QInputDialog::getText(this, tr("Confirm Destruction"),
        tr("This will destroy compartment '%1'.\nType the name to confirm:").arg(m_info.name),
        QLineEdit::Normal, QString(), &ok);

    if (!ok || token != m_info.name) {
        if (ok) QMessageBox::warning(this, tr("Aborted"), tr("Confirmation token did not match."));
        return;
    }

    m_backend->destroyContainer(m_info.name, token);
    auto *logDlg = new ProcessLogDialog(m_backend, tr("Destroying %1").arg(m_info.name), this);
    logDlg->setAttribute(Qt::WA_DeleteOnClose);
    logDlg->show();
}

void ActionsDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
    Q_UNUSED(err);
    emit containerChanged();
    if (ok && (desc.startsWith(QStringLiteral("Destroy:")) || desc.startsWith(QStringLiteral("Create:")))) {
        accept();
    }
}
