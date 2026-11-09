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
    proc.waitForFinished(30000);
    if (exitCode) *exitCode = proc.exitCode();
    return QString::fromUtf8(proc.readAllStandardOutput());
}

QList<ContainerInfo> ContainerBackend::listContainers(QString *errorOut) const
{
    QList<ContainerInfo> result;
    int exitCode = 0;
    
    // The GUI backend uses the JSON flag
    const QString out = runSync(m_pkexecPath, { QStringLiteral("/usr/local/sbin/parishctl-list"), QStringLiteral("--json") }, &exitCode);
    
    if (exitCode != 0) {
        if (errorOut) *errorOut = tr("Failed to run parishctl-list");
        return result;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorOut) *errorOut = parseError.errorString();
        return result;
    }

    if (!doc.isArray()) {
        if (errorOut) *errorOut = tr("JSON root is not an array");
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

        if (!info.name.isEmpty()) {
            result.append(info);
        }
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

void ContainerBackend::runInTerminal(const QString &title, const QString &program, const QStringList &args)
{
    emit operationStarted(title);
    emit terminalLaunched(title);

    QStringList quoted;
    for (const QString &a : args) {
        QString esc = a;
        esc.replace("'", "'\\''");
        quoted.append("'" + esc + "'");
    }
    const QString shellLine = quoted.join(' ');
    const QString wrapped = QStringLiteral("%1; echo; echo '[press Enter to close]'; read _").arg(shellLine);

    auto *proc = new QProcess(this);
    proc->setProgram(program);
    proc->setArguments({ QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), wrapped });

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
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-unlock' '%2'; echo; echo 'Press Enter to close...'; read _").arg(m_pkexecPath, name);
    runInTerminal(tr("Unlocking '%1'").arg(name), m_terminalPath,
        { QStringLiteral("--title"), tr("Unlock Vault: %1").arg(name),
          QStringLiteral("--geometry"), QStringLiteral("90x20"),
          QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}

void ContainerBackend::lockVault(const QString &name)
{
    runParishctl(tr("Locking '%1'").arg(name), { QStringLiteral("parishctl-lock"), name });
}

void ContainerBackend::destroyContainer(const QString &name, const QString &confirmationToken)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-destroy' '%2' --confirmation-token='%3'; echo; echo 'Press Enter to close...'; read _")
        .arg(m_pkexecPath, name, confirmationToken);
    runInTerminal(tr("Destroying '%1'").arg(name), m_terminalPath,
        { QStringLiteral("--title"), tr("Destroy: %1").arg(name),
          QStringLiteral("--geometry"), QStringLiteral("90x24"),
          QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}

void ContainerBackend::createContainer(const QString &name, const QString &templateName)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-create' '%2' --template='%3'; echo; echo 'Press Enter to close...'; read _")
        .arg(m_pkexecPath, name, templateName);
    runInTerminal(tr("Creating '%1'").arg(name), m_terminalPath,
        { QStringLiteral("--title"), tr("Create: %1").arg(name),
          QStringLiteral("--geometry"), QStringLiteral("90x24"),
          QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}

void ContainerBackend::openConsole(const QString &name)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-console' '%2'; echo; echo '[console closed]'; read _")
        .arg(m_pkexecPath, name);
    runInTerminal(tr("Console: '%1'").arg(name), m_terminalPath,
        { QStringLiteral("--title"), tr("Console: %1").arg(name),
          QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}

QList<PackageInfo> ContainerBackend::listPackages(const QString &jail, QString *errorOut) const
{
    QList<PackageInfo> result;
    int rc = 0;
    const QString infoOut = runSync(m_pkexecPath, { QStringLiteral("/usr/sbin/pkg"), QStringLiteral("-j"), jail, QStringLiteral("info") }, &rc);
    if (rc != 0) {
        if (errorOut) *errorOut = tr("pkg info failed");
        return result;
    }

    for (const QString &line : infoOut.split('\n', Qt::SkipEmptyParts)) {
        const int sp = line.indexOf(' ');
        if (sp <= 0) continue;
        const QString nameVer = line.left(sp).trimmed();
        const QString comment = line.mid(sp).trimmed();

        int dashPos = -1;
        for (int i = nameVer.size()-1; i > 0; --i) {
            if (nameVer.at(i) == '-' && i+1 < nameVer.size() && nameVer.at(i+1).isDigit()) {
                dashPos = i;
                break;
            }
        }
        if (dashPos < 0) continue;

        PackageInfo p;
        p.kind = PackageInfo::NativePackage;
        p.name = nameVer.left(dashPos);
        p.version = nameVer.mid(dashPos+1);
        p.comment = comment;
        result.append(p);
    }

    // Custom apps from host-side metadata
    const QString launcherDir = QStringLiteral("/var/db/parishbsd/launchers/%1").arg(jail);
    QDir d(launcherDir);
    if (d.exists()) {
        for (const QString &fname : d.entryList({ "*.conf" }, QDir::Files)) {
            QFile f(d.filePath(fname));
            if (!f.open(QIODevice::ReadOnly)) continue;
            QTextStream in(&f);
            PackageInfo p;
            p.kind = PackageInfo::CustomApp;
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.startsWith("name=")) p.name = line.mid(5);
                else if (line.startsWith("version=")) p.version = line.mid(8);
                else if (line.startsWith("method=")) p.installMethod = line.mid(7);
                else if (line.startsWith("install_path=")) p.origin = line.mid(13);
                else if (line.startsWith("written_at=")) p.installDate = line.mid(11);
            }
            if (!p.name.isEmpty()) result.append(p);
        }
    }

    if (errorOut) errorOut->clear();
    return result;
}

void ContainerBackend::upgradePackages(const QString &jail)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-apps-upgrade' '%2'; echo; echo 'Press Enter to close...'; read _").arg(m_pkexecPath, jail);
    runInTerminal(tr("Upgrading packages in %1").arg(jail), m_terminalPath,
        { QStringLiteral("--title"), tr("Upgrade: %1").arg(jail),
          QStringLiteral("--geometry"), QStringLiteral("100x28"),
          QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}

void ContainerBackend::installPackage(const QString &jail, const QString &package)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-apps-install' '%2' '%3'; echo; echo 'Press Enter to close...'; read _").arg(m_pkexecPath, jail, package);
    runInTerminal(tr("Installing %1 in %2").arg(package, jail), m_terminalPath,
        { QStringLiteral("--title"), tr("Install: %1").arg(jail),
          QStringLiteral("--geometry"), QStringLiteral("100x28"),
          QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}

void ContainerBackend::removePackage(const QString &jail, const QString &package)
{
    const QString cmd = QStringLiteral("'%1' '/usr/local/sbin/parishctl-apps-remove' '%2' '%3'; echo; echo 'Press Enter to close...'; read _").arg(m_pkexecPath, jail, package);
    runInTerminal(tr("Removing %1 from %2").arg(package, jail), m_terminalPath,
        { QStringLiteral("--title"), tr("Remove: %1").arg(jail),
          QStringLiteral("--geometry"), QStringLiteral("100x28"),
          QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}