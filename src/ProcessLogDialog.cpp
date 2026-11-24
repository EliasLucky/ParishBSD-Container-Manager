
#include "ProcessLogDialog.h"
#include "ProcessLogDialog.h"
#include "ContainerBackend.h"

#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

ProcessLogDialog::ProcessLogDialog(ContainerBackend *backend, const QString &title, QWidget *parent)
    : QDialog(parent), m_backend(backend)
{
    setWindowTitle(title);
    setMinimumSize(640, 420);

    auto *root = new QVBoxLayout(this);
    m_status = new QLabel(tr("Working..."), this);
    m_status->setAccessibleName(tr("Operation status"));
    root->addWidget(m_status);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setAccessibleName(tr("Operation progress"));
    root->addWidget(m_progress);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setFont(QFont(QStringLiteral("monospace")));
    m_log->setAccessibleName(tr("Command output log"));
    root->addWidget(m_log, 1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    buttons->addWidget(m_cancelBtn);
    root->addLayout(buttons);

    connect(m_backend, &ContainerBackend::installOutputReady, this, &ProcessLogDialog::onOutput);
    connect(m_backend, &ContainerBackend::installFinished, this, &ProcessLogDialog::onFinished);
    connect(m_cancelBtn, &QPushButton::clicked, this, &ProcessLogDialog::onCancel);
}

void ProcessLogDialog::onOutput(const QString &text)
{
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(text);
    m_log->moveCursor(QTextCursor::End);
}

void ProcessLogDialog::onFinished(bool ok, const QString &err)
{
    if (m_finished) return;
    m_finished = true;
    m_succeeded = ok;

    m_progress->setRange(0, 1);
    m_progress->setValue(ok ? 1 : 0);

    if (ok) {
        m_status->setText(tr("Operation complete."));
        m_cancelBtn->setText(tr("Close"));
        m_cancelBtn->disconnect();
        connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::accept);
        QTimer::singleShot(1500, this, &QDialog::accept);
    } else {
        m_status->setText(tr("Operation failed."));
        if (!err.isEmpty()) onOutput(QStringLiteral("\nERROR: ") + err + QLatin1Char('\n'));
        m_cancelBtn->setText(tr("Close"));
        m_cancelBtn->disconnect();
        connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

    emit finished(ok);
}

void ProcessLogDialog::onCancel()
{
    if (m_finished) {
        if (m_succeeded) accept();
        else reject();
        return;
    }
    m_backend->cancelInstall();
    reject();
}

void ProcessLogDialog::closeEvent(QCloseEvent *e)
{
    if (!m_finished) {
        m_backend->cancelInstall();
    }
    e->accept();
}

