#include "ExchangeDialog.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

static const char *kHostEndpointTag = "__HOST__";

ExchangeDialog::ExchangeDialog(ContainerBackend *backend,
                               const QList<ContainerInfo> &containers,
                               const QString &initialSourceJail,
                               QWidget *parent)
    : QDialog(parent),
      m_backend(backend),
      m_containers(containers),
      m_initialSource(initialSourceJail)
{
    setWindowTitle(tr("Compartment File Exchange"));
    resize(720, 560);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto *intro = new QLabel(
        tr("Transfer files securely through the host-brokered /exchange staging area. "
           "Direct compartment-to-compartment mounts never occur, symlinks and special device "
           "files are rejected, and setuid/setgid permissions are stripped automatically."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    // Source & Destination selectors
    auto *routeBox = new QGroupBox(tr("Transfer Route"), this);
    auto *routeForm = new QFormLayout(routeBox);

    m_srcCombo = new QComboBox(routeBox);
    m_srcCombo->setAccessibleName(tr("Source location"));
    auto *srcLbl = new QLabel(tr("&From:"), routeBox);
    srcLbl->setBuddy(m_srcCombo);

    m_dstCombo = new QComboBox(routeBox);
    m_dstCombo->setAccessibleName(tr("Destination location"));
    auto *dstLbl = new QLabel(tr("&To:"), routeBox);
    dstLbl->setBuddy(m_dstCombo);

    auto *modeRow = new QHBoxLayout();
    m_copyRadio = new QRadioButton(tr("Copy (preserve source)"), routeBox);
    m_moveRadio = new QRadioButton(tr("Move (delete source after copy)"), routeBox);
    m_copyRadio->setChecked(true);
    auto *modeGroup = new QButtonGroup(routeBox);
    modeGroup->addButton(m_copyRadio);
    modeGroup->addButton(m_moveRadio);
    modeRow->addWidget(m_copyRadio);
    modeRow->addWidget(m_moveRadio);
    modeRow->addStretch();

    routeForm->addRow(srcLbl, m_srcCombo);
    routeForm->addRow(dstLbl, m_dstCombo);
    routeForm->addRow(tr("Mode:"), modeRow);
    root->addWidget(routeBox);

    // Host file chooser (visible when source == Host)
    auto *hostRow = new QHBoxLayout();
    m_hostFileEdit = new QLineEdit(this);
    m_hostFileEdit->setPlaceholderText(tr("Choose a file from the host desktop to send to /exchange..."));
    m_hostFileEdit->setAccessibleName(tr("Host file path"));
    auto *hostLbl = new QLabel(tr("Host &File:"), this);
    hostLbl->setBuddy(m_hostFileEdit);
    m_browseHostBtn = new QPushButton(tr("Browse..."), this);
    hostRow->addWidget(hostLbl);
    hostRow->addWidget(m_hostFileEdit, 1);
    hostRow->addWidget(m_browseHostBtn);
    root->addLayout(hostRow);

    // Compartment exchange file list (visible when source == Compartment/Vault)
    auto *filesHeader = new QHBoxLayout();
    auto *filesLbl = new QLabel(tr("Files in source compartment &/exchange:"), this);
    m_refreshBtn = new QPushButton(tr("Refresh List"), this);
    filesHeader->addWidget(filesLbl);
    filesHeader->addStretch();
    filesHeader->addWidget(m_refreshBtn);
    root->addLayout(filesHeader);

    m_srcFilesTable = new QTableWidget(0, 3, this);
    filesLbl->setBuddy(m_srcFilesTable);
    m_srcFilesTable->setHorizontalHeaderLabels({ tr("Name"), tr("Type"), tr("Size") });
    m_srcFilesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_srcFilesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_srcFilesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_srcFilesTable->horizontalHeader()->setStretchLastSection(true);
    m_srcFilesTable->horizontalHeader()->resizeSection(0, 320);
    m_srcFilesTable->setAccessibleName(tr("Exchange files table"));
    root->addWidget(m_srcFilesTable, 1);

    // Target filename
    auto *destForm = new QFormLayout();
    m_destNameEdit = new QLineEdit(this);
    m_destNameEdit->setPlaceholderText(tr("Target filename (relative to destination /exchange)"));
    m_destNameEdit->setAccessibleName(tr("Destination filename"));
    auto *destNameLbl = new QLabel(tr("Target &Filename:"), this);
    destNameLbl->setBuddy(m_destNameEdit);
    destForm->addRow(destNameLbl, m_destNameEdit);
    root->addLayout(destForm);

    // Trust-boundary warning & explicit consent controls
    m_riskBanner = new QLabel(this);
    m_riskBanner->setWordWrap(true);
    m_riskBanner->setVisible(false);
    root->addWidget(m_riskBanner);

    m_consentCheck = new QCheckBox(this);
    m_consentCheck->setVisible(false);
    root->addWidget(m_consentCheck);

    m_sanitizeCheck = new QCheckBox(
        tr("Strip metadata & re-encode raster images (.png / .jpg) when importing to Host Quarantine"),
        this);
    m_sanitizeCheck->setChecked(true);
    m_sanitizeCheck->setVisible(false);
    root->addWidget(m_sanitizeCheck);

    // Buttons
    auto *btnRow = new QHBoxLayout();
    m_transferBtn = new QPushButton(tr("Transfer File"), this);
    m_transferBtn->setDefault(true);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addStretch();
    btnRow->addWidget(m_transferBtn);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    populateCompartmentCombos();

    connect(m_srcCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExchangeDialog::onSourceChanged);
    connect(m_dstCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExchangeDialog::onDestChanged);
    connect(m_browseHostBtn, &QPushButton::clicked,
            this, &ExchangeDialog::onBrowseHostFile);
    connect(m_refreshBtn, &QPushButton::clicked,
            this, &ExchangeDialog::onRefreshFiles);
    connect(m_srcFilesTable, &QTableWidget::itemSelectionChanged,
            this, &ExchangeDialog::onFileSelected);
    connect(m_hostFileEdit, &QLineEdit::textChanged,
            this, [this](const QString &path) {
        if (!path.isEmpty() && m_srcCombo->currentData().toString() == QLatin1String(kHostEndpointTag)) {
            m_destNameEdit->setText(QFileInfo(path).fileName());
        }
        updateRiskBanner();
    });
    connect(m_consentCheck, &QCheckBox::toggled, this, &ExchangeDialog::updateRiskBanner);
    connect(m_transferBtn, &QPushButton::clicked, this, &ExchangeDialog::onTransfer);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_backend, &ContainerBackend::exchangeFilesReady,
            this, &ExchangeDialog::onExchangeFilesReady);
    connect(m_backend, &ContainerBackend::operationFinished,
            this, &ExchangeDialog::onOperationFinished);

    onSourceChanged();
    onDestChanged();
}

ContainerInfo ExchangeDialog::findContainer(const QString &name) const
{
    for (const ContainerInfo &c : m_containers) {
        if (c.name == name) return c;
    }
    return ContainerInfo();
}

void ExchangeDialog::populateCompartmentCombos()
{
    m_srcCombo->clear();
    m_dstCombo->clear();

    for (const ContainerInfo &c : m_containers) {
        if (c.isContained()) continue;
        const QString label = QStringLiteral("%1 (%2 — %3)").arg(c.name, c.kind, c.humanRiskLevel());
        m_srcCombo->addItem(label, c.name);
        m_dstCombo->addItem(label, c.name);
    }

    m_srcCombo->addItem(tr("Host Desktop (Send file into compartment)"), QString::fromLatin1(kHostEndpointTag));
    m_dstCombo->addItem(tr("Host Quarantine (~/ParishBSD-Quarantine/)"), QString::fromLatin1(kHostEndpointTag));

    if (!m_initialSource.isEmpty()) {
        const int idx = m_srcCombo->findData(m_initialSource);
        if (idx >= 0) m_srcCombo->setCurrentIndex(idx);
    }
    if (m_dstCombo->count() > 1 && m_dstCombo->currentIndex() == m_srcCombo->currentIndex()) {
        m_dstCombo->setCurrentIndex((m_srcCombo->currentIndex() + 1) % m_dstCombo->count());
    }
}

void ExchangeDialog::onSourceChanged()
{
    const QString src = m_srcCombo->currentData().toString();
    const bool fromHost = (src == QLatin1String(kHostEndpointTag));

    m_hostFileEdit->setEnabled(fromHost);
    m_browseHostBtn->setEnabled(fromHost);
    m_srcFilesTable->setEnabled(!fromHost);
    m_refreshBtn->setEnabled(!fromHost);

    if (fromHost) {
        m_copyRadio->setChecked(true);
        m_moveRadio->setEnabled(false);
        m_srcFilesTable->setRowCount(0);
    } else {
        m_moveRadio->setEnabled(true);
        onRefreshFiles();
    }

    updateRiskBanner();
}

void ExchangeDialog::onDestChanged()
{
    const QString dst = m_dstCombo->currentData().toString();
    const bool toHost = (dst == QLatin1String(kHostEndpointTag));
    if (toHost) {
        m_copyRadio->setChecked(true);
        m_moveRadio->setEnabled(false);
        m_destNameEdit->setPlaceholderText(tr("Leave empty to place inside ~/ParishBSD-Quarantine/<jail>/"));
    } else {
        const bool fromHost = (m_srcCombo->currentData().toString() == QLatin1String(kHostEndpointTag));
        m_moveRadio->setEnabled(!fromHost);
        m_destNameEdit->setPlaceholderText(tr("Target filename (relative to destination /exchange)"));
    }
    updateRiskBanner();
}

void ExchangeDialog::onBrowseHostFile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Select Host File to Send"));
    if (!file.isEmpty()) {
        m_hostFileEdit->setText(file);
    }
}

void ExchangeDialog::onRefreshFiles()
{
    const QString src = m_srcCombo->currentData().toString();
    if (src.isEmpty() || src == QLatin1String(kHostEndpointTag)) return;
    m_backend->requestExchangeFiles(src);
}

void ExchangeDialog::onExchangeFilesReady(const QString &jail,
                                          const QList<ExchangeFileEntry> &files,
                                          const QString &error)
{
    if (jail != m_srcCombo->currentData().toString()) return;
    m_srcFilesTable->setRowCount(0);
    if (!error.isEmpty()) return;

    m_srcFilesTable->setRowCount(files.size());
    for (int i = 0; i < files.size(); ++i) {
        const ExchangeFileEntry &e = files.at(i);
        m_srcFilesTable->setItem(i, 0, new QTableWidgetItem(e.name));
        m_srcFilesTable->setItem(i, 1, new QTableWidgetItem(e.type == QLatin1String("dir") ? tr("Directory") : tr("File")));
        m_srcFilesTable->setItem(i, 2, new QTableWidgetItem(e.type == QLatin1String("dir") ? QStringLiteral("-") : formatDiskUsage(e.size)));
    }
}

void ExchangeDialog::onFileSelected()
{
    const auto ranges = m_srcFilesTable->selectedRanges();
    if (ranges.isEmpty()) {
        updateRiskBanner();
        return;
    }
    const int row = ranges.first().topRow();
    auto *item = m_srcFilesTable->item(row, 0);
    if (item && m_dstCombo->currentData().toString() != QLatin1String(kHostEndpointTag)) {
        m_destNameEdit->setText(item->text());
    }
    updateRiskBanner();
}

void ExchangeDialog::updateRiskBanner()
{
    const QString src = m_srcCombo->currentData().toString();
    const QString dst = m_dstCombo->currentData().toString();
    const bool fromHost = (src == QLatin1String(kHostEndpointTag));
    const bool toHost = (dst == QLatin1String(kHostEndpointTag));

    const ContainerInfo srcInfo = findContainer(src);
    const ContainerInfo dstInfo = findContainer(dst);

    QStringList warnings;
    bool needsConsent = false;
    bool criticalStyle = false;

    if (fromHost && toHost) {
        warnings << tr("Source and destination are both the Host. Choose a compartment.");
    } else if (!fromHost && !toHost && src == dst) {
        warnings << tr("Source and destination compartments are the same.");
    }

    if (!fromHost && srcInfo.isVault) {
        needsConsent = true;
        criticalStyle = true;
        warnings << tr("VAULT EXPORT WARNING: \"%1\" is an encrypted Vault. Exporting files "
                       "removes ZFS encryption protection from this copy.")
                        .arg(srcInfo.name);
        if (srcInfo.isLocked()) {
            warnings << tr("Vault \"%1\" is currently locked. Unlock it first.").arg(srcInfo.name);
        }
    }

    if (toHost) {
        needsConsent = true;
        criticalStyle = true;
        warnings << tr("HOST IMPORT WARNING: Files originating from a compartment (such as PNG images, "
                       "PDFs, or archives) may contain malicious payloads targeting host viewers or "
                       "thumbnailers. Prefer transferring to another viewer compartment instead. "
                       "Host imports are placed read-only (0444) inside ~/ParishBSD-Quarantine/.");
    }

    if (!fromHost && !toHost &&
        srcInfo.normalizedRiskLevel() == QLatin1String("high") &&
        (dstInfo.normalizedRiskLevel() == QLatin1String("low") || dstInfo.isVault)) {
        warnings << tr("TRUST BOUNDARY NOTICE: Transferring from High-Risk Untrusted compartment \"%1\" "
                       "into \"%2\". Open unknown documents inside an untrusted viewer rather than a Vault.")
                        .arg(srcInfo.name, dstInfo.name);
    }

    m_sanitizeCheck->setVisible(toHost);

    if (warnings.isEmpty()) {
        m_riskBanner->setVisible(false);
        m_consentCheck->setVisible(false);
        m_consentCheck->setChecked(false);
    } else {
        m_riskBanner->setText(warnings.join(QStringLiteral("\n\n")));
        m_riskBanner->setStyleSheet(criticalStyle
            ? QStringLiteral("QLabel { color: #ffffff; background-color: #991b1b; padding: 8px; border-radius: 4px; }")
            : QStringLiteral("QLabel { color: #1e293b; background-color: #fef3c7; border: 1px solid #f59e0b; padding: 8px; border-radius: 4px; }"));
        m_riskBanner->setVisible(true);
        m_consentCheck->setVisible(needsConsent);
        if (needsConsent) {
            m_consentCheck->setText(toHost
                ? tr("I understand the Host security risk and want to import into Quarantine")
                : tr("I confirm exporting plaintext files out of encrypted Vault \"%1\"").arg(srcInfo.name));
        }
    }

    const bool sameEndpoint = (src == dst);
    bool havePayload = false;
    if (fromHost) {
        havePayload = !m_hostFileEdit->text().trimmed().isEmpty();
    } else {
        havePayload = !m_srcFilesTable->selectedRanges().isEmpty();
    }
    const bool consentSatisfied = !needsConsent || m_consentCheck->isChecked();
    m_transferBtn->setEnabled(!sameEndpoint && havePayload && consentSatisfied);
}

void ExchangeDialog::onTransfer()
{
    const QString src = m_srcCombo->currentData().toString();
    const QString dst = m_dstCombo->currentData().toString();
    const bool fromHost = (src == QLatin1String(kHostEndpointTag));
    const bool toHost = (dst == QLatin1String(kHostEndpointTag));

    if (src == dst) return;

    const ContainerInfo srcInfo = findContainer(src);
    const bool vaultExport = (!fromHost && srcInfo.isVault);

    if (fromHost) {
        const QString hostFile = m_hostFileEdit->text().trimmed();
        const QString dstName = m_destNameEdit->text().trimmed().isEmpty()
            ? QFileInfo(hostFile).fileName()
            : m_destNameEdit->text().trimmed();
        m_backend->exchangeTransfer(QStringLiteral("put"), QString(), hostFile, dst, dstName,
                                    false, false, false);
        return;
    }

    const auto ranges = m_srcFilesTable->selectedRanges();
    if (ranges.isEmpty()) return;
    const QString srcFile = m_srcFilesTable->item(ranges.first().topRow(), 0)->text();

    if (toHost) {
        m_backend->exchangeTransfer(QStringLiteral("get"), src, srcFile, QString(), QString(),
                                    vaultExport, true, m_sanitizeCheck->isChecked());
        return;
    }

    const QString dstName = m_destNameEdit->text().trimmed().isEmpty()
        ? srcFile
        : m_destNameEdit->text().trimmed();
    const QString op = m_moveRadio->isChecked() ? QStringLiteral("move") : QStringLiteral("copy");
    m_backend->exchangeTransfer(op, src, srcFile, dst, dstName, vaultExport, false, false);
}

void ExchangeDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
    if (!desc.startsWith(QStringLiteral("File Exchange"))) return;
    if (ok) {
        QMessageBox::information(this, tr("Transfer Complete"), tr("%1 succeeded.").arg(desc));
        onRefreshFiles();
        emit exchangeCompleted();
    } else {
        QMessageBox::warning(this, tr("Transfer Failed"), err);
    }
}
