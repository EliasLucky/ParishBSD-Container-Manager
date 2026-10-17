#pragma once

#include <QDialog>

class QPlaintTextEdit;
class QProgressBar;
class QLabel;
class QPushButton;
class QProcess;

class InstallLogDialog : public QDialog
{
	Q_OBJECT
public:
	InstallLogDialog(const QString &title, const QString &command, const QStringList &args, QWidget *parent = nullptr);

signals:
	void finished(bool success);

private slots:
	void onReadyRead();
	void onProcessFinished(int exitCode, QProcess::ExitStatus status);
	void onCancel();

private:
	QPlainTextEdit *m_log = nullptr;
	QProgressBar *m_progress = nullptr;
	QLabel *m_status = nullptr;
	QPushButton *m_cancelBtn = nullptr;
	QProcess *m_process = nullptr;
};
