#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

const char *kStateFile   = "/var/db/parishbsd/firstboot-state.conf";
const char *kPendingFile = "/var/db/parishbsd/firstboot-pending";
const char *kLogFile     = "/var/log/parishbsd/firstboot.log";
const char *kWorkerBin   = "/usr/local/sbin/parishbsd-firstboot-worker";

QMap<QString, QString> readStateFile()
{
    QMap<QString, QString> map;
    QFile f(QString::fromLatin1(kStateFile));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return map;
    }
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0) {
            map.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
    }
    return map;
}

} // namespace

class FirstBootWindow : public QWidget {
    Q_OBJECT
public:
    explicit FirstBootWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle(QObject::tr("Welcome to ParishBSD — First-Time Setup"));
        resize(740, 540);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 18, 18, 18);
        root->setSpacing(12);

        auto *title = new QLabel(
            QObject::tr("<b>Preparing Your ParishBSD Workspace</b>"), this);
        QFont tf = title->font();
        tf.setPointSize(tf.pointSize() + 4);
        title->setFont(tf);
        root->addWidget(title);

        m_warningBanner = new QLabel(
            QObject::tr("<b>IMPORTANT: Do NOT turn off or restart your computer while templates are being prepared.</b><br>"
                        "ParishBSD is downloading and verifying FreeBSD base files for your isolated compartments and vaults."),
            this);
        m_warningBanner->setWordWrap(true);
        m_warningBanner->setStyleSheet(QStringLiteral(
            "QLabel { background-color: #fef3c7; border: 1px solid #f59e0b; border-radius: 6px; "
            "padding: 10px 12px; color: #78350f; }"));
        root->addWidget(m_warningBanner);

        auto *intro = new QLabel(
            QObject::tr("You can keep using your desktop normally while this runs. If a power loss or unexpected "
                        "restart happens, ParishBSD automatically cleans partial files and resumes cleanly on next boot."),
            this);
        intro->setWordWrap(true);
        root->addWidget(intro);

        m_statusLabel = new QLabel(QObject::tr("Checking setup status..."), this);
        m_statusLabel->setWordWrap(true);
        QFont sf = m_statusLabel->font();
        sf.setBold(true);
        m_statusLabel->setFont(sf);
        root->addWidget(m_statusLabel);

        m_progress = new QProgressBar(this);
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        root->addWidget(m_progress);

        m_logView = new QPlainTextEdit(this);
        m_logView->setReadOnly(true);
        m_logView->setMaximumBlockCount(1500);
        m_logView->setFont(QFont(QStringLiteral("monospace")));
        root->addWidget(m_logView, 1);

        auto *btnRow = new QHBoxLayout();
        m_resumeBtn = new QPushButton(QObject::tr("Run / Resume Now"), this);
        m_openMgrBtn = new QPushButton(QObject::tr("Open Compartment Manager"), this);
        m_closeBtn = new QPushButton(QObject::tr("Hide Window"), this);

        m_openMgrBtn->setVisible(false);

        btnRow->addWidget(m_resumeBtn);
        btnRow->addStretch();
        btnRow->addWidget(m_openMgrBtn);
        btnRow->addWidget(m_closeBtn);
        root->addLayout(btnRow);

        connect(m_resumeBtn, &QPushButton::clicked, this, &FirstBootWindow::onResumeClicked);
        connect(m_openMgrBtn, &QPushButton::clicked, this, []() {
            QProcess::startDetached(QStringLiteral("/usr/local/bin/parish-compartments"), {});
        });
        connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);

        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(1500);
        connect(m_pollTimer, &QTimer::timeout, this, &FirstBootWindow::refreshState);
        m_pollTimer->start();

        refreshState();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (m_lastStatus == QLatin1String("running")) {
            const auto ans = QMessageBox::question(
                this,
                QObject::tr("First-Time Setup Still Running"),
                QObject::tr("Compartment templates are still being prepared.\n\n"
                            "Closing this window keeps setup running safely in the background — "
                            "please do not shut down or power off your computer until you see the completion notification.\n\n"
                            "Close this window anyway?"));
            if (ans != QMessageBox::Yes) {
                event->ignore();
                return;
            }
        }
        event->accept();
    }

private slots:
    void refreshState()
    {
        const auto st = readStateFile();
        QString status = st.value(QStringLiteral("status"));
        if (status.isEmpty()) {
            status = QFile::exists(QString::fromLatin1(kPendingFile))
                ? QStringLiteral("pending")
                : QStringLiteral("completed");
        }
        m_lastStatus = status;

        const int total = st.value(QStringLiteral("total_templates"), QStringLiteral("4")).toInt();
        const int done  = st.value(QStringLiteral("done_count"), QStringLiteral("0")).toInt();
        const QString cur = st.value(QStringLiteral("current_template"));
        const QString err = st.value(QStringLiteral("last_error"));

        if (total > 0) {
            m_progress->setRange(0, total);
            m_progress->setValue(qBound(0, done, total));
        }

        if (status == QLatin1String("running")) {
            if (!cur.isEmpty()) {
                m_statusLabel->setText(
                    QObject::tr("Building template '%1' (%2 of %3 finished)...")
                        .arg(cur).arg(done).arg(total));
            } else {
                m_statusLabel->setText(QObject::tr("Preparing base system files and templates..."));
            }
            m_resumeBtn->setEnabled(false);
            m_openMgrBtn->setVisible(false);
        } else if (status == QLatin1String("completed")) {
            if (total > 0) {
                m_progress->setRange(0, total);
                m_progress->setValue(total);
            }
            m_warningBanner->setStyleSheet(QStringLiteral(
                "QLabel { background-color: #dcfce7; border: 1px solid #22c55e; border-radius: 6px; "
                "padding: 10px 12px; color: #14532d; }"));
            m_warningBanner->setText(QObject::tr("<b>Setup Complete!</b> You can safely restart, shut down, or start creating compartments anytime."));
            m_statusLabel->setText(
                QObject::tr("Setup Complete! All ParishBSD compartments and templates are ready to use."));
            m_resumeBtn->setVisible(false);
            m_openMgrBtn->setVisible(true);
            m_closeBtn->setText(QObject::tr("Close"));
        } else if (status == QLatin1String("waiting_network")) {
            m_statusLabel->setText(
                QObject::tr("Waiting for an internet connection to download FreeBSD base files for templates..."));
            m_resumeBtn->setEnabled(true);
            m_resumeBtn->setVisible(true);
        } else if (status == QLatin1String("failed")) {
            m_statusLabel->setText(
                err.isEmpty()
                    ? QObject::tr("First-time setup encountered an error (for example, no internet connection yet). Click 'Run / Resume Now' once connected.")
                    : QObject::tr("Paused after error: %1").arg(err));
            m_resumeBtn->setEnabled(true);
            m_resumeBtn->setVisible(true);
        } else {
            m_statusLabel->setText(
                QObject::tr("Waiting for background setup service to start, or click 'Run / Resume Now'."));
            m_resumeBtn->setEnabled(true);
            m_resumeBtn->setVisible(true);
        }

        QFile lf(QString::fromLatin1(kLogFile));
        if (lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (lf.size() > m_logOffset) {
                lf.seek(m_logOffset);
                const QString chunk = QString::fromUtf8(lf.readAll());
                m_logOffset = lf.pos();
                if (!chunk.isEmpty()) {
                    m_logView->moveCursor(QTextCursor::End);
                    m_logView->insertPlainText(chunk);
                    m_logView->moveCursor(QTextCursor::End);
                }
            } else if (lf.size() < m_logOffset) {
                m_logOffset = 0;
                m_logView->setPlainText(QString::fromUtf8(lf.readAll()));
                m_logOffset = lf.pos();
            }
        }
    }

    void onResumeClicked()
    {
        if (m_resumeProc && m_resumeProc->state() != QProcess::NotRunning) {
            return;
        }
        m_resumeBtn->setEnabled(false);
        m_statusLabel->setText(QObject::tr("Starting first-time setup worker..."));

        m_resumeProc = new QProcess(this);
        m_resumeProc->setProgram(QStringLiteral("/usr/local/bin/pkexec"));
        m_resumeProc->setArguments({ QString::fromLatin1(kWorkerBin), QStringLiteral("--resume") });
        connect(m_resumeProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) {
            if (code != 0) {
                QMessageBox::warning(this,
                    QObject::tr("Setup Interrupted"),
                    QObject::tr("First-time setup could not finish yet. Check network connectivity and click 'Run / Resume Now' to retry."));
            }
            m_resumeProc->deleteLater();
            m_resumeProc = nullptr;
            refreshState();
        });
        m_resumeProc->start();
    }

private:
    QLabel *m_warningBanner = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QPushButton *m_resumeBtn = nullptr;
    QPushButton *m_openMgrBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QTimer *m_pollTimer = nullptr;
    QProcess *m_resumeProc = nullptr;
    QString m_lastStatus;
    qint64 m_logOffset = 0;
};

#include "FirstBootMain.moc"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("parish-firstboot-gui"));
    app.setOrganizationName(QStringLiteral("ParishBSD"));

    bool autostart = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--autostart")) {
            autostart = true;
        }
    }
    if (autostart && !QFile::exists(QString::fromLatin1(kPendingFile))) {
        const auto st = readStateFile();
        if (st.value(QStringLiteral("status")) == QLatin1String("completed")) {
            return 0;
        }
    }

    FirstBootWindow win;
    win.show();
    return app.exec();
}
