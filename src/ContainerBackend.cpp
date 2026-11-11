#include "ContainerBackend.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>

ContainerBackend::ContainerBackend(QObject *parent) : QObject(parent)
{
    m_pkexecPath = QStringLiteral("/usr/local/bin/pkexec");
    m_terminalPath = QStringLiteral("/usr/local/bin/xfce4-terminal");
}

QString ContainerBackend::runSync(const QString &program, const QStringList &args, int *exitCode) const
{
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(5000)) {
        if (exitCode) *exitCode = -1;
        return QString();
    }
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        if (exitCode) *exitCode = -1;
        return QString();
    }
    if (exitCode) *exitCode = proc.exitCode();
    return QString::fromUtf8(proc.readAllStandardOutput());
}

QList<ContainerInfo> ContainerBackend::listContainers(QString *errorOut) const
{
    QList<ContainerInfo> result;
    int exitCode = 0;
    const QString out = runSync(m_pkexecPath, { QStringLiteral("/usr/local/sbin/parishctl-list"), QStringLiteral("--json") }, &exitCode);
    
    if (exitCode != 0) {
        if (errorOut) *errorOut = tr("Failed to run parishctl-list");
        return result;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (errorOut) *errorOut = tr("JSON parse error");
        return result;
    }

    for (const QJsonValue &val : doc.array()) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();
        
        ContainerInfo info;
        info.name = obj["name"].toString();
        info.jailState = obj["jail_state"].toString();
        info.keyState = obj["key_state"].isNull() ? QString() : obj["key_state"].toString();
        info.sourceTemplate = obj["source_template"].toString();
        info.kind = obj["kind"].toString();
        info.tier = obj["tier"].toString();
        info.diskUsage = static_cast<quint64>(obj["disk_usage"].toDouble());
        info.isVault = obj["is_vault"].toBool();
        info.isCompatibility = obj["is_compatibility"].toBool();
        info.path = QStringLiteral("/usr/local/jails/containers/%1").arg(info.name);

        if (!info.name.isEmpty()) result.append(info);
    }

    if (errorOut) errorOut->clear();
    return result;
}

QStringList ContainerBackend::listTemplatesNames() const
{
    QStringList names;
    int exitCode = 0;
    const QString out = runSync(m_pkexecPath, { QStringLiteral("/usr/local/sbin/parishctl-template-list"), QStringLiteral("--json") }, &exitCode);
    if (exitCode != 0) return names;

    QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8());
    if (!doc.isArray()) return names;

    for (const QJsonValue &val : doc.array()) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();
        if (obj["state"].toString() == "ready") {
            names.append(obj["name"].toString());
        }
    }
    return names;
}

void ContainerBackend::runAsync(const QString &description, const QString &program, const QStringList &args)
{
    emit operationStarted(description);
    auto *proc = new QProcess(this);
    proc->setProgram(program);
    proc->setArguments(args);

    auto completed = std::make_shared<bool>(false);

    connect(proc, &QProcess::finished, this, [this, proc, description, completed] (int exitCode, QProcess::ExitStatus status) {
        if (*completed) return;
        *completed = true;
        const bool ok = (status == QProcess::NormalExit && exitCode == 0);
        QString err;
        if (!ok) {
            err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (err.isEmpty()) err = tr("Command failed (exit %1)").arg(exitCode);
        }
        emit operationFinished(description, ok, err);
        proc->deleteLater();
    });

    connect(proc, &QProcess::errorOccurred, this, [this, proc, description, completed] (QProcess::ProcessError) {
        if (*completed) return;
        *completed = true;
        emit operationFinished(description, false, tr("Failed to run %1: %2").arg(proc->program(), proc->errorString()));
        proc->deleteLater();
    });

    proc->start();
}

void ContainerBackend::runParishctl(const QString &description, const QStringList &args)
{
    QString bin = args.first();
    QStringList pkArgs;
    pkArgs << QStringLiteral("/usr/local/sbin/") + bin;
    for (int i = 1; i < args.size(); ++i) pkArgs << args[i];
    runAsync(description, m_pkexecPath, pkArgs);
}

void ContainerBackend::runInTerminal(const QString &title, const QString &shellCommand)
{
    emit operationStarted(title);
    emit terminalLaunched(title);

    const QString wrapped = QStringLiteral("%1; _rc=$?; echo; echo '[press Enter to close]'; read _; exit $_rc").arg(shellCommand);

    auto *proc = new QProcess(this);
    proc->setProgram(m_terminalPath);
    proc->setArguments({ QStringLiteral("--title"), title, QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), wrapped });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, proc, title](int code, QProcess::ExitStatus status) {
        const bool ok = (status == QProcess::NormalExit && code == 0);
        QString err;
        if (!ok) {
            err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (err.isEmpty()) err = tr("Command failed (exit %1)").arg(code);
        }
        emit operationFinished(title, ok, err);
        emit terminalClosed(title);
        proc->deleteLater();
    });

    connect(proc, &QProcess::errorOccurred, this, [this, proc, title](QProcess::ProcessError) {
        emit operationFinished(title, false, proc->errorString());
        emit terminalClosed(title);
        proc->deleteLater();
    });

    proc->start();
}

void ContainerBackend::startContainer(const QString &name) {
    runParishctl(tr("Starting '%1'").arg(name), { QStringLiteral("parishctl-start"), name });
}

void ContainerBackend::stopContainer(const QString &name) {
    runParishctl(tr("Stopping '%1'").arg(name), { QStringLiteral("parishctl-stop"), name });
}

void ContainerBackend::unlockVault(const QString &name)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-unlock' '%2'").arg(m_pkexecPath, name);
    runInTerminal(tr("Unlock Vault: %1").arg(name), cmd);
}

void ContainerBackend::lockVault(const QString &name)
{
    runParishctl(tr("Locking '%1'").arg(name), { QStringLiteral("parishctl-lock"), name });
}

void ContainerBackend::destroyContainer(const QString &name, const QString &confirmationToken)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-destroy' '%2' --confirmation-token='%3'")
        .arg(m_pkexecPath, name, confirmationToken);
    runInTerminal(tr("Destroy: %1").arg(name), cmd);
}

void ContainerBackend::createContainer(const QString &name, const QString &templateName)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-create' '%2' --template='%3'")
        .arg(m_pkexecPath, name, templateName);
    runInTerminal(tr("Create: %1").arg(name), cmd);
}

void ContainerBackend::openConsole(const QString &name)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-console' '%2'").arg(m_pkexecPath, name);
    runInTerminal(tr("Console: %1").arg(name), cmd);
}

QList<PackageInfo> ContainerBackend::listPackages(const QString &jail, QString *errorOut) const
{
    QList<PackageInfo> result;
    int rc = 0;
    
    const QString infoOut = runSync(m_pkexecPath, { QStringLiteral("/usr/local/sbin/parishctl-apps-list"), jail }, &rc);
    if (rc != 0) {
        if (errorOut) *errorOut = tr("parishctl-apps-list failed");
        return result;
    }

    for (const QString &line : infoOut.split('\n', Qt::SkipEmptyParts)) {
        if (line.startsWith("Compartment ")) continue;
        
        const QStringList parts = line.split('\t');
        if (parts.size() >= 3) {
            PackageInfo p;
            if (parts[0] == "native") {
                p.kind = PackageInfo::NativePackage;
                p.name = parts[1];
                p.version = parts[2];
                result.append(p);
            } else if (parts[0] == "custom") {
                p.kind = PackageInfo::CustomApp;
                p.name = parts[1];
                p.version = parts[2];
                if (parts.size() > 3) p.installMethod = parts[3];
                result.append(p);
            }
        }
    }

    if (errorOut) errorOut->clear();
    return result;
}

void ContainerBackend::upgradePackages(const QString &jail)
{
    runParishctl(tr("Upgrading packages in %1").arg(jail), { QStringLiteral("parishctl-apps-upgrade"), jail });
}

void ContainerBackend::installPackage(const QString &jail, const QString &package)
{
    runParishctl(tr("Installing %1 in %2").arg(package, jail), { QStringLiteral("parishctl-apps-install"), jail, package });
}

void ContainerBackend::installPackageAsync(const QString &jail, const QString &package)
{
    emit operationStarted(tr("Installing %1").arg(package));
    
    if (m_installProcess) {
        m_installProcess->kill();
        m_installProcess->deleteLater();
    }

    m_installProcess = new QProcess(this);
    m_installProcess->setProgram(m_pkexecPath);
    m_installProcess->setArguments({ QStringLiteral("/usr/local/sbin/parishctl-apps-install"), jail, package });

    connect(m_installProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        emit installOutputReady(QString::fromUtf8(m_installProcess->readAllStandardOutput()));
    });
    connect(m_installProcess, &QProcess::readyReadStandardError, this, [this]() {
        emit installOutputReady(QString::fromUtf8(m_installProcess->readAllStandardError()));
    });

    connect(m_installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, 
        [this, package](int exitCode, QProcess::ExitStatus status) {
            const bool ok = (status == QProcess::NormalExit && exitCode == 0);
            QString err;
            if (!ok) {
                err = QString::fromUtf8(m_installProcess->readAllStandardError()).trimmed();
                if (err.isEmpty()) err = tr("Exit code %1").arg(exitCode);
            }
            emit installFinished(ok, err);
            emit operationFinished(tr("Installing %1").arg(package), ok, err);
            m_installProcess->deleteLater();
            m_installProcess = nullptr;
    });

    m_installProcess->start();
}

void ContainerBackend::cancelInstall()
{
    if (m_installProcess && m_installProcess->state() == QProcess::Running) {
        m_installProcess->terminate();
        if (!m_installProcess->waitForFinished(3000)) {
            m_installProcess->kill();
        }
    }
}

void ContainerBackend::removePackage(const QString &jail, const QString &package)
{
    runParishctl(tr("Removing %1 from %2").arg(package, jail), { QStringLiteral("parishctl-apps-remove"), jail, package });
}

void ContainerBackend::removeCustomApp(const QString &jail, const QString &appName)
{
    runParishctl(tr("Removing %1 from %2").arg(appName, jail), { QStringLiteral("parishctl-apps-remove-custom"), jail, appName });
}