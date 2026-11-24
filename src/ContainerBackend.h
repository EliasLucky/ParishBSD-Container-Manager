#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <atomic>

#include "ContainerInfo.h"
#include "PackageInfo.h"
#include "TemplateInfo.h"

struct ExchangeFileEntry {
    QString name;
    QString type; // file | dir
    quint64 size = 0;
};

struct UsbDeviceInfo {
    QString device;        // da0p1, cd0, ...
    QString fstype;        // msdosfs, exfat, ufs, ntfs, cd9660
    quint64 sizeBytes = 0;
    QString mode;          // unmounted | ro | rw
    QString attachedJails; // comma-separated
};

class ContainerBackend : public QObject {
    Q_OBJECT
public:
    explicit ContainerBackend(QObject *parent = nullptr);

    // Non-blocking async queries (serialized through the pkexec queue)
    void requestContainers();
    void requestTemplates();
    void requestPackages(const QString &jail);
    void requestExchangeFiles(const QString &jail);
    void requestUsbDevices();
    bool isBusy() const;

    // Lifecycle & vault operations
    void startContainer(const QString &name);
    void stopContainer(const QString &name);
    void unlockVault(const QString &name);
    void lockVault(const QString &name);
    void setRiskLevel(const QString &name, const QString &riskLevel);

    // Audio policy (output & input independent; excluded for vaults)
    void setAudioConfig(const QString &name, bool outputEnabled, bool inputEnabled);

    // File Exchange (op: copy | move | put | get)
    void exchangeTransfer(const QString &op,
                          const QString &srcJail,
                          const QString &srcPath,
                          const QString &dstJail,
                          const QString &dstPath,
                          bool confirmVaultExport,
                          bool confirmHostImport,
                          bool sanitize);

    // USB / Removable media
    void attachUsb(const QString &jail,
                   const QString &device,
                   const QString &fstype,
                   bool readWrite,
                   bool confirmRw);
    void detachUsb(const QString &jail, const QString &device);

    void destroyContainer(const QString &name, const QString &confirmationToken);
    void createContainer(const QString &name, const QString &templateName);

    // Interactive terminal actions (run independently of backend queue)
    void openConsole(const QString &name);
    void launchTemplateWizard();
    void updateTemplateAsync(const QString &name, bool shipped);

    // Application / package management
    void upgradePackages(const QString &jail);
    void installPackageAsync(const QString &jail, const QString &package);
    void installFromFileAsync(const QString &jail, const QString &filePath);
    void removePackages(const QString &jail,
                        const QStringList &nativePackages,
                        const QStringList &customApps);
    void launchApplication(const QString &jail, const QString &appName, bool isCustom);
    void createLauncher(const QString &jail, const QString &appName, const QString &where);
    void cancelInstall();

signals:
    void operationStarted(const QString &description);
    void operationFinished(const QString &description, bool success, const QString &errorMessage);
    void terminalLaunched(const QString &description);
    void terminalClosed(const QString &description);

    void containersReady(QList<ContainerInfo> list, QString error);
    void templatesReady(QList<TemplateInfo> list, QString error);
    void packagesReady(QString jail, QList<PackageInfo> list, QString error);
    void exchangeFilesReady(QString jail, QList<ExchangeFileEntry> files, QString error);
    void usbDevicesReady(QList<UsbDeviceInfo> devices, QString error);

    void installOutputReady(const QString &text);
    void installFinished(bool success, const QString &errorMessage);

private:
    struct PendingTask {
        enum class Kind {
            PollContainers,
            PollTemplates,
            PollPackages,
            PollExchangeFiles,
            PollUsbDevices,
            UserAction,
            StreamingAction
        };

        Kind kind = Kind::UserAction;
        QString description;
        QString program;
        QStringList args;
        QString targetJail;

        bool isPoll() const {
            return kind == Kind::PollContainers ||
                   kind == Kind::PollTemplates ||
                   kind == Kind::PollPackages ||
                   kind == Kind::PollExchangeFiles ||
                   kind == Kind::PollUsbDevices;
        }
    };

    void enqueueTask(const PendingTask &task);
    void pumpQueue();
    void finishActiveTask();

    void startPollTask(const PendingTask &task);
    void startUserActionTask(const PendingTask &task);
    void startStreamingTask(const PendingTask &task);

    void launchIndependentTerminal(const QString &sessionKey,
                                   const QString &title,
                                   const QStringList &execArgv,
                                   bool pauseOnExit);

    void enqueueParishctl(const QString &description, const QStringList &args);
    void enqueueStreamingPkexec(const QString &title, const QStringList &sbinArgs);

    static bool isValidCompartmentName(const QString &name);
    static bool isValidRiskLevel(const QString &risk);
    static QString formatProcessFailure(int exitCode,
                                        QProcess::ExitStatus status,
                                        const QString &stderrText);

    QString m_pkexecPath;
    QQueue<PendingTask> m_queue;
    QProcess *m_activeProcess = nullptr;
    PendingTask m_currentTask;
    bool m_taskRunning = false;
    std::atomic<int> m_userActionCount{0};
    QMap<QString, QProcess*> m_activeTerminals;
};
