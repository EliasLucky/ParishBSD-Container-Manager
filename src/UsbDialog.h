#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include "ContainerBackend.h"
#include "ContainerInfo.h"

class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTableWidget;

class UsbDialog : public QDialog {
    Q_OBJECT
public:
    UsbDialog(ContainerBackend *backend,
              const QList<ContainerInfo> &containers,
              const QString &initialJail = QString(),
              QWidget *parent = nullptr);

signals:
    void usbChanged();

private slots:
    void onRefresh();
    void onDevicesReady(const QList<UsbDeviceInfo> &devices, const QString &error);
    void onSelectionChanged();
    void onAttach();
    void onDetach();
    void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
    UsbDeviceInfo selectedDevice() const;
    void updateButtonStates();

    ContainerBackend *m_backend;
    QList<ContainerInfo> m_containers;
    QList<UsbDeviceInfo> m_devices;
    QString m_initialJail;

    QTableWidget *m_table = nullptr;
    QComboBox *m_jailCombo = nullptr;
    QRadioButton *m_roRadio = nullptr;
    QRadioButton *m_rwRadio = nullptr;
    QLabel *m_policyNote = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_attachBtn = nullptr;
    QPushButton *m_detachBtn = nullptr;
};
