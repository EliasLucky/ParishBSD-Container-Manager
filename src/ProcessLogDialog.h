#pragma once
#include <QDialog>

class QPlainTextEdit;
class QProgressBar;
class QLabel;
class QPushButton;
class ContainerBackend;
class QCloseEvent;

class ProcessLogDialog : public QDialog
{
    Q_OBJECT
public:
    ProcessLogDialog(ContainerBackend *backend, const QString &title, QWidget *parent = nullptr);

signals:
    void finished(bool success);

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onOutput(const QString &text);
    void onFinished(bool ok, const QString &err);
    void onCancel();

private:
    ContainerBackend *m_backend;
    QPlainTextEdit *m_log = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_cancelBtn = nullptr;
};