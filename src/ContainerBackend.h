#pragma once

#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <atomic>

#include "ContainerInfo.h"
#include "PackageInfo.h"
#include "TemplateInfo.h"

class QProcess;

class ContainerBackend : public QObject {
    Q_OBJECT
public:
    explicit ContainerBackend(QObject *parent = nullptr);

    // Non-blocking async queries (serialized through the single pkexec queue)
    void requestContainers();
    void requestTemplates();
    void requestPackages(const QString &jail);
    bool isBusy() const;

    // Lifecycle & vault operations
    void startContainer(const QString &name);
    void stopContainer(const QString &name);
    void unlockVault(const QString &name);
    void lockVault(const QString &name);
    void setRiskLevel(const QString &name, const QString &riskLevel);

    void destroyContainer(const QString &name, const QString &confirmationToken);
    void createContainer(const QString &name, const QString &templateName);

    // Interactive terminal actions
    void openConsole(const QString &name);
    void launchTemplateWizard();
    void updateTemplateAsync(const QString &name, bool shipped);

    // Application / package management (all async, serialized)
    void upgradePackages(const QString &jail);
    void installPackageAsync(const QString &jail, const QString &package);
    void installFromFileAsync(const QString &jail, const QString &filePath);
    void removePackages(const QString &jail,
                        const QStringList &nativePackages,
                        const QStringList &customApps);
    void launchApplication(const QString &jail, const QString &appName, bool isCustom);
    void cancelInstall();

signals:
    void operationStarted(const QString &description);
    void operationFinished(const QString &description, bool success, const QString &errorMessage);
    void terminalLaunched(const QString &description);
    void terminalClosed(const QString &description);

    void containersReady(QList<ContainerInfo> list, QString error);
    void templatesReady(QList<TemplateInfo> list, QString error);
    void packagesReady(QString jail, QList<PackageInfo> list, QString error);

    void installOutputReady(const QString &text);
    void installFinished(bool success, const QString &errorMessage);

private:
    struct PendingTask {
        enum class Kind {
            PollContainers,
            PollTemplates,
            PollPackages,
            UserAction,
            StreamingAction,
            TerminalAction
        };

        Kind kind = Kind::UserAction;
        QString description;
        QString program;
        QStringList args;
        QString targetJail;
        bool pauseTerminalOnExit = true;

        bool isPoll() const {
            return kind == Kind::PollContainers ||
                   kind == Kind::PollTemplates ||
                   kind == Kind::PollPackages;
        }
    };

    void enqueueTask(const PendingTask &task);
    void pumpQueue();
    void finishActiveTask();

    void startPollTask(const PendingTask &task);
    void startUserActionTask(const PendingTask &task);
    void startStreamingTask(const PendingTask &task);
    void startTerminalTask(const PendingTask &task);

    void enqueueParishctl(const QString &description, const QStringList &args);
    void enqueueStreamingPkexec(const QString &title, const QStringList &sbinArgs);
    void enqueueTerminal(const QString &title, const QStringList &execArgv, bool pauseOnExit = true);

    static bool isValidCompartmentName(const QString &name);
    static bool isValidRiskLevel(const QString &risk);

    QString m_pkexecPath;
    QQueue<PendingTask> m_queue;
    QProcess *m_activeProcess = nullptr;
    PendingTask m_currentTask;
    bool m_taskRunning = false;
    std::atomic<int> m_userActionCount{0};
};
