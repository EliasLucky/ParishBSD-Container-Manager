#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "ContainerInfo.h"
#include "PackageInfo.h"

class QProcess;

class ContainerBackend : public QObject {
    Q_OBJECT
public:
    explicit ContainerBackend(QObject *parent = nullptr);

    QList<ContainerInfo> listContainers(QString *errorOut = nullptr) const;
    QStringList listTemplatesNames() const;

    void startContainer(const QString &name);
    void stopContainer(const QString &name);
    void unlockVault(const QString &name);
    void lockVault(const QString &name);
    
    void destroyContainer(const QString &name, const QString &confirmationToken);
    void createContainer(const QString &name, const QString &templateName);
    
    void openConsole(const QString &name);

    QList<PackageInfo> listPackages(const QString &jail, QString *errorOut = nullptr) const;
    void upgradePackages(const QString &jail);
    void installPackage(const QString &jail, const QString &package);
    void installPackageAsync(const QString &jail, const QString &package);
    void cancelInstall();
    void removePackage(const QString &jail, const QString &package);
    void removeCustomApp(const QString &jail, const QString &appName);

    void runCommandAsync(const QString &title, const QStringList &args);

signals:
    void operationStarted(const QString &description);
    void operationFinished(const QString &description, bool success, const QString &errorMessage);
    void terminalLaunched(const QString &description);
    void terminalClosed(const QString &description);
    void installOutputReady(const QString &text);
    void installFinished(bool success, const QString &errorMessage);

private:
    void runParishctl(const QString &description, const QStringList &args);
    void runInTerminal(const QString &title, const QString &shellCommand);
    void runAsync(const QString &description, const QString &program, const QStringList &args);
    QString runSync(const QString &program, const QStringList &args, int *exitCode = nullptr) const;

    QString m_pkexecPath;
    QString m_terminalPath;
    QProcess *m_installProcess = nullptr;
};