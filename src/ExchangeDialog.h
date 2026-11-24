#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include "ContainerBackend.h"
#include "ContainerInfo.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;

class ExchangeDialog : public QDialog {
    Q_OBJECT
public:
    ExchangeDialog(ContainerBackend *backend,
                   const QList<ContainerInfo> &containers,
                   const QString &initialSourceJail = QString(),
                   QWidget *parent = nullptr);

signals:
    void exchangeCompleted();

private slots:
    void onSourceChanged();
    void onDestChanged();
    void onBrowseHostFile();
    void onRefreshFiles();
    void onExchangeFilesReady(const QString &jail, const QList<ExchangeFileEntry> &files, const QString &error);
    void onFileSelected();
    void onTransfer();
    void onOperationFinished(const QString &desc, bool ok, const QString &err);

private:
    void populateCompartmentCombos();
    void updateRiskBanner();
    ContainerInfo findContainer(const QString &name) const;

    ContainerBackend *m_backend;
    QList<ContainerInfo> m_containers;

    QComboBox *m_srcCombo = nullptr;
    QComboBox *m_dstCombo = nullptr;
    QTableWidget *m_srcFilesTable = nullptr;
    QLineEdit *m_hostFileEdit = nullptr;
    QPushButton *m_browseHostBtn = nullptr;
    QPushButton *m_refreshBtn = nullptr;

    QLineEdit *m_destNameEdit = nullptr;
    QRadioButton *m_copyRadio = nullptr;
    QRadioButton *m_moveRadio = nullptr;

    QLabel *m_riskBanner = nullptr;
    QCheckBox *m_consentCheck = nullptr;
    QCheckBox *m_sanitizeCheck = nullptr;
    QPushButton *m_transferBtn = nullptr;

    QString m_initialSource;
};
