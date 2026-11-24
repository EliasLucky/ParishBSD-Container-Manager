#include "ActionsDialog.h"
#include "ApplicationsDialog.h"
#include "ContainerBackend.h"
#include "ExchangeDialog.h"
#include "ProcessLogDialog.h"
#include "UsbDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

ActionsDialog::ActionsDialog(ContainerBackend *backend,
                             const ContainerInfo &info,
                             const QList<ContainerInfo> &allContainers,
                             QWidget *parent)
    : QDialog(parent), m_backend(backend), m_info(info), m_allContainers(allContainers)
{
    setWindowTitle(tr("Actions: %1").arg(info.name));
    setMinimumWidth(460);

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

    // Audio Policy Group (host-brokered PulseAudio/PipeWire socket; excluded for Vaults)
    auto *audioBox = new QGroupBox(tr("Audio Isolation Policy"), this);
    auto *audioLay = new QVBoxLayout(audioBox);
    if (info.isVault) {
        m_vaultAudioNote = new QLabel(
            tr("Vaults exclude audio output and microphone devices by security policy."),
            audioBox);
        m_vaultAudioNote->setWordWrap(true);
        m_vaultAudioNote->setAccessibleName(tr("Vault audio policy notice"));
        audioLay->addWidget(m_vaultAudioNote);
    } else {
        m_audioOutCheck = new QCheckBox(tr("Enable Audio &Output (Playback)"), audioBox);
        m_audioOutCheck->setChecked(info.audioOutput);
        m_audioOutCheck->setEnabled(!info.isContained());
        m_audioOutCheck->setAccessibleName(tr("Enable compartment audio output"));

        m_audioInCheck = new QCheckBox(tr("Enable Audio &Input (Microphone)"), audioBox);
        m_audioInCheck->setChecked(info.audioInput);
        m_audioInCheck->setEnabled(!info.isContained());
        m_audioInCheck->setAccessibleName(tr("Enable compartment audio input"));

        auto *audioHint = new QLabel(
            tr("Uses an isolated host PulseAudio/PipeWire socket per compartment; no raw /dev/dsp access."),
            audioBox);
        audioHint->setWordWrap(true);

        audioLay->addWidget(m_audioOutCheck);
        audioLay->addWidget(m_audioInCheck);
        audioLay->addWidget(audioHint);

        connect(m_audioOutCheck, &QCheckBox::clicked, this, &ActionsDialog::onAudioPolicyChanged);
        connect(m_audioInCheck, &QCheckBox::clicked, this, &ActionsDialog::onAudioPolicyChanged);
    }
    root->addWidget(audioBox);

    root->addSpacing(6);

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
            if (m_info.supportsGui()) {
                auto *xpraRow = new QHBoxLayout();
                auto *attachBtn = new QPushButton(tr("Attach Display (Xpra)"), this);
                attachBtn->setIcon(QIcon::fromTheme(QStringLiteral("video-display")));
                auto *detachBtn = new QPushButton(tr("Detach Display"), this);
                detachBtn->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
                connect(attachBtn, &QPushButton::clicked, this, &ActionsDialog::onAttachXpra);
                connect(detachBtn, &QPushButton::clicked, this, &ActionsDialog::onDetachXpra);
                xpraRow->addWidget(attachBtn);
                xpraRow->addWidget(detachBtn);
                m_buttonLayout->addLayout(xpraRow);
            }
        } else {
            addButton(tr("Start"), QStringLiteral("media-playback-start"), &ActionsDialog::onStart);
        }
    }

    addSeparator();

    addButton(tr("Applications && Packages..."), QStringLiteral("system-software-install"), &ActionsDialog::onApplications);
    addButton(tr("File Exchange..."), QStringLiteral("folder-sync"), &ActionsDialog::onExchange);
    addButton(tr("USB && Removable Media..."), QStringLiteral("drive-removable-media"), &ActionsDialog::onUsb);

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

void ActionsDialog::onAttachXpra() { m_backend->attachXpra(m_info.name); }

void ActionsDialog::onDetachXpra() { m_backend->detachXpra(m_info.name); }

void ActionsDialog::onApplications()
{
    ApplicationsDialog dlg(m_backend, m_info, this);
    dlg.exec();
}

void ActionsDialog::onExchange()
{
    const QList<ContainerInfo> list = m_allContainers.isEmpty()
        ? QList<ContainerInfo>{ m_info }
        : m_allContainers;
    ExchangeDialog dlg(m_backend, list, m_info.name, this);
    connect(&dlg, &ExchangeDialog::exchangeCompleted, this, &ActionsDialog::containerChanged);
    dlg.exec();
}

void ActionsDialog::onUsb()
{
    const QList<ContainerInfo> list = m_allContainers.isEmpty()
        ? QList<ContainerInfo>{ m_info }
        : m_allContainers;
    UsbDialog dlg(m_backend, list, m_info.name, this);
    connect(&dlg, &UsbDialog::usbChanged, this, &ActionsDialog::containerChanged);
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

void ActionsDialog::onAudioPolicyChanged()
{
    if (m_info.isVault || m_info.isContained() || !m_audioOutCheck || !m_audioInCheck) return;
    m_backend->setAudioConfig(m_info.name, m_audioOutCheck->isChecked(), m_audioInCheck->isChecked());
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
    if (ok && desc.startsWith(QStringLiteral("Updating audio policy")) && m_audioOutCheck && m_audioInCheck) {
        m_info.audioOutput = m_audioOutCheck->isChecked();
        m_info.audioInput = m_audioInCheck->isChecked();
    } else if (!ok && desc.startsWith(QStringLiteral("Updating audio policy")) && m_audioOutCheck && m_audioInCheck) {
        const QSignalBlocker b1(m_audioOutCheck);
        const QSignalBlocker b2(m_audioInCheck);
        m_audioOutCheck->setChecked(m_info.audioOutput);
        m_audioInCheck->setChecked(m_info.audioInput);
    }
    emit containerChanged();
    if (ok && (desc.startsWith(QStringLiteral("Destroy:")) || desc.startsWith(QStringLiteral("Create:")))) {
        accept();
    }
}
