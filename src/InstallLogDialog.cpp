#include "InstallLogDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QCloseEvent>
#include <QTimer>

InstallLogDialog::InstallLogDialog(const QString &title, const QString &command, const QStringList &args, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(title);
    setMinimumSize(640,420);

    auto *root = new QVBoxLayout(this);
    m_status = new QLabel(tr("Working..."),this);
    root->addWidget(m_status);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0,0);
    root->addWidget(m_progress);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setFont(QFont("monospace"));
    root->addWidget(m_log,1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"),this);
    buttons->addWidget(m_cancelBtn);
    root->addLayout(buttons);

    m_process = new QProcess(this);
    
    connect(m_process, &QProcess::readyReadStandardOutput, this, &InstallLogDialog::onReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &InstallLogDialog::onProcessFinished);
    connect(m_cancelBtn, &QPushButton::clicked, this, &InstallLogDialog::onCancel);

    m_process->setProgram(command);
    m_process->setArguments(args);
    m_process->start();
}

void InstallLogDialog::onReadyRead()
{
    const QByteArray chunk = m_process->readAllStandardOutput();
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(QString::fromUtf8(chunk));
    m_log->moveCursor(QTextCursor::End);
}

void InstallLogDialog::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_progress->setRange(0,1);
    const bool ok = (status == QProcess::NormalExit && exitCode == 0);
    m_progress->setValue(ok ? 1 : 0);

    if (ok) {
        m_status->setText(tr("Installation complete."));
        m_cancelBtn->setText(tr("Close"));
        m_cancelBtn->disconnect();
        connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::accept);
        QTimer::singleShot(2000,this,&QDialog::accept);
    }
    else {
        m_status->setText(tr("Installation failed. See log above."));
        m_cancelBtn->setText(tr("Close"));
        m_cancelBtn->disconnect();
        connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

    emit finished(ok);
}

void InstallLogDialog::onCancel()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000))
            m_process->kill();
    }
    reject();
}

void InstallLogDialog::closeEvent(QCloseEvent *e)
{
    onCancel();
    e->accept();
}