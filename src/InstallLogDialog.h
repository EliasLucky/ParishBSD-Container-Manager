#pragma once

#include <QDialog>

class QPlainTextEdit;
class QProgressBar;
class QLabel;
class QPushButton;
class ContainerBackend;

class InstallLogDialog : public QDialog
{
    Q_OBJECT
public:
    InstallLogDialog(ContainerBackend *backend, const QString &title, const QString &jail, const QString &package, QWidget *parent = nullptr);

signals:
    void finished(bool success);

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onInstallOutput(const QString &text);
    void onInstallFinished(bool ok, const QString &err);
    void onCancel();

private:
    ContainerBackend *m_backend;
    QPlainTextEdit *m_log = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_cancelBtn = nullptr;
};
