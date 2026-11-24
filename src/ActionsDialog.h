#pragma once

#include <QDialog>
#include <QList>
#include "ContainerInfo.h"

class ContainerBackend;
class QCheckBox;
class QComboBox;
class QLabel;
class QVBoxLayout;

class ActionsDialog : public QDialog
{
    Q_OBJECT
public:
    ActionsDialog(ContainerBackend *backend,
                  const ContainerInfo &info,
                  const QList<ContainerInfo> &allContainers = QList<ContainerInfo>(),
                  QWidget *parent = nullptr);

signals:
    void containerChanged();

private slots:
    void onStart();
    void onStop();
    void onUnlock();
    void onLock();
    void onConsole();
    void onAttachXpra();
    void onDetachXpra();
    void onApplications();
    void onExchange();
    void onUsb();
    void onRiskLevelChanged(int index);
    void onAudioPolicyChanged();
    void onRefresh();
    void onDestroy();
    void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
    void rebuildButtons();

    ContainerBackend *m_backend;
    ContainerInfo m_info;
    QList<ContainerInfo> m_allContainers;
    QVBoxLayout *m_buttonLayout = nullptr;
    QComboBox *m_riskCombo = nullptr;
    QCheckBox *m_audioOutCheck = nullptr;
    QCheckBox *m_audioInCheck = nullptr;
    QLabel *m_vaultAudioNote = nullptr;
};
