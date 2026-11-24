#include "UsbDialog.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

UsbDialog::UsbDialog(ContainerBackend *backend,
                     const QList<ContainerInfo> &containers,
                     const QString &initialJail,
                     QWidget *parent)
    : QDialog(parent),
      m_backend(backend),
      m_containers(containers),
      m_initialJail(initialJail)
{
    setWindowTitle(tr("USB & Removable Media"));
    resize(680, 480);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto *hdr = new QHBoxLayout();
    auto *descLbl = new QLabel(
        tr("Connect removable USB drives or CDs/DVDs to a compartment (under /media/). "
           "Your main system disks are automatically protected and hidden."),
        this);
    descLbl->setWordWrap(true);
    m_refreshBtn = new QPushButton(tr("Refresh Drives"), this);
    hdr->addWidget(descLbl, 1);
    hdr->addWidget(m_refreshBtn);
    root->addLayout(hdr);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({ tr("Device"), tr("Filesystem"), tr("Size"), tr("Mode"), tr("Attached To") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAccessibleName(tr("Detected removable drives"));
    root->addWidget(m_table, 1);

    auto *optsBox = new QGroupBox(tr("Attachment Settings"), this);
    auto *optsForm = new QFormLayout(optsBox);

    m_jailCombo = new QComboBox(optsBox);
    m_jailCombo->setAccessibleName(tr("Target compartment"));
    for (const ContainerInfo &c : m_containers) {
        if (c.isContained()) continue;
        m_jailCombo->addItem(QStringLiteral("%1 (%2)").arg(c.name, c.humanRiskLevel()), c.name);
    }
    if (!m_initialJail.isEmpty()) {
        const int idx = m_jailCombo->findData(m_initialJail);
        if (idx >= 0) m_jailCombo->setCurrentIndex(idx);
    }
    auto *jailLbl = new QLabel(tr("&Compartment:"), optsBox);
    jailLbl->setBuddy(m_jailCombo);
    optsForm->addRow(jailLbl, m_jailCombo);

    auto *modeLay = new QVBoxLayout();
    m_roRadio = new QRadioButton(tr("Read-Only (Recommended — protects drive from tampering/ransomware)"), optsBox);
    m_rwRadio = new QRadioButton(tr("Read-Write (Requires explicit permission — allows saving files to USB)"), optsBox);
    m_roRadio->setChecked(true);
    auto *bg = new QButtonGroup(optsBox);
    bg->addButton(m_roRadio);
    bg->addButton(m_rwRadio);
    modeLay->addWidget(m_roRadio);
    modeLay->addWidget(m_rwRadio);
    optsForm->addRow(tr("Access Mode:"), modeLay);
    root->addWidget(optsBox);

    m_policyNote = new QLabel(
        tr("For safety, programs cannot be run directly from attached removable drives, even when Read-Write access is enabled."),
        this);
    m_policyNote->setWordWrap(true);
    root->addWidget(m_policyNote);

    auto *btnRow = new QHBoxLayout();
    m_attachBtn = new QPushButton(tr("Attach Device"), this);
    m_detachBtn = new QPushButton(tr("Detach from Compartment"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(m_attachBtn);
    btnRow->addWidget(m_detachBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(m_refreshBtn, &QPushButton::clicked, this, &UsbDialog::onRefresh);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &UsbDialog::onSelectionChanged);
    connect(m_jailCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UsbDialog::updateButtonStates);
    connect(m_roRadio, &QRadioButton::toggled, this, &UsbDialog::updateButtonStates);
    connect(m_attachBtn, &QPushButton::clicked, this, &UsbDialog::onAttach);
    connect(m_detachBtn, &QPushButton::clicked, this, &UsbDialog::onDetach);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_backend, &ContainerBackend::usbDevicesReady, this, &UsbDialog::onDevicesReady);
    connect(m_backend, &ContainerBackend::operationFinished, this, &UsbDialog::onOperationFinished);

    onRefresh();
    updateButtonStates();
}

void UsbDialog::onRefresh()
{
    m_backend->requestUsbDevices();
}

void UsbDialog::onDevicesReady(const QList<UsbDeviceInfo> &devices, const QString &error)
{
    m_devices = devices;
    m_table->setRowCount(0);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("USB Scan Error"), error);
        return;
    }

    m_table->setRowCount(devices.size());
    for (int i = 0; i < devices.size(); ++i) {
        const UsbDeviceInfo &d = devices.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(d.device));
        m_table->setItem(i, 1, new QTableWidgetItem(d.fstype.isEmpty() ? tr("unknown") : d.fstype));
        m_table->setItem(i, 2, new QTableWidgetItem(formatDiskUsage(d.sizeBytes)));
        m_table->setItem(i, 3, new QTableWidgetItem(d.mode));
        m_table->setItem(i, 4, new QTableWidgetItem(d.attachedJails.isEmpty() ? QStringLiteral("-") : d.attachedJails));
    }
    updateButtonStates();
}

UsbDeviceInfo UsbDialog::selectedDevice() const
{
    const auto ranges = m_table->selectedRanges();
    if (ranges.isEmpty()) return UsbDeviceInfo();
    const int row = ranges.first().topRow();
    if (row < 0 || row >= m_devices.size()) return UsbDeviceInfo();
    return m_devices.at(row);
}

void UsbDialog::onSelectionChanged()
{
    updateButtonStates();
}

void UsbDialog::updateButtonStates()
{
    const UsbDeviceInfo d = selectedDevice();
    const bool hasDev = !d.device.isEmpty();
    const bool hasJail = (m_jailCombo->count() > 0);
    const bool isOptical = d.device.startsWith(QLatin1String("cd")) || d.fstype == QLatin1String("cd9660");

    if (isOptical && m_rwRadio->isChecked()) {
        m_roRadio->setChecked(true);
    }
    m_rwRadio->setEnabled(!isOptical);

    m_attachBtn->setEnabled(hasDev && hasJail);
    m_detachBtn->setEnabled(hasDev && hasJail && !d.attachedJails.isEmpty());
}

void UsbDialog::onAttach()
{
    const UsbDeviceInfo d = selectedDevice();
    const QString jail = m_jailCombo->currentData().toString();
    if (d.device.isEmpty() || jail.isEmpty()) return;

    const bool rw = m_rwRadio->isChecked();
    if (rw) {
        const auto ans = QMessageBox::warning(
            this,
            tr("Confirm Read-Write USB Access"),
            tr("You are granting READ-WRITE permission on '/dev/%1' to compartment '%2'.\n\n"
               "Software inside '%2' will be able to modify, delete, or write files onto this drive "
               "(which may affect other computers you plug it into later).\n\n"
               "Proceed with Read-Write attachment?")
                .arg(d.device, jail),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (ans != QMessageBox::Yes) return;
    }

    m_backend->attachUsb(jail, d.device, d.fstype, rw, rw);
}

void UsbDialog::onDetach()
{
    const UsbDeviceInfo d = selectedDevice();
    const QString jail = m_jailCombo->currentData().toString();
    if (d.device.isEmpty() || jail.isEmpty()) return;
    m_backend->detachUsb(jail, d.device);
}

void UsbDialog::onOperationFinished(const QString &desc, bool ok, const QString &err)
{
    if (!desc.startsWith(QStringLiteral("Attaching USB")) &&
        !desc.startsWith(QStringLiteral("Detaching USB"))) {
        return;
    }
    if (ok) {
        onRefresh();
        emit usbChanged();
    } else {
        QMessageBox::warning(this, tr("USB Operation Failed"), err);
    }
}
