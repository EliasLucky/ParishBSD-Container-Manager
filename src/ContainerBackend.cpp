#include "ContainerBackend.h"
#include "TerminalLauncher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <memory>

ContainerBackend::ContainerBackend(QObject *parent)
    : QObject(parent), m_pkexecPath(QStringLiteral("/usr/local/bin/pkexec"))
{
}

bool ContainerBackend::isBusy() const
{
    return m_userActionCount.load() > 0;
}

bool ContainerBackend::isValidCompartmentName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9-]{1,31}$"));
    if (!re.match(name).hasMatch()) return false;
    if (name.startsWith(QLatin1Char('-')) || name.endsWith(QLatin1Char('-')) ||
        name.contains(QStringLiteral("--"))) {
        return false;
    }
    return true;
}

bool ContainerBackend::isValidRiskLevel(const QString &risk)
{
    return risk == QLatin1String("low") ||
           risk == QLatin1String("medium") ||
           risk == QLatin1String("high") ||
           risk == QLatin1String("critical");
}

void ContainerBackend::enqueueTask(const PendingTask &task)
{
    if (task.isPoll()) {
        // Never overlap or queue duplicate background polls while user actions are active
        if (m_userActionCount.load() > 0) return;
        if (m_taskRunning && m_currentTask.kind == task.kind &&
            m_currentTask.targetJail == task.targetJail) {
            return;
        }
        for (const PendingTask &queued : m_queue) {
            if (queued.kind == task.kind && queued.targetJail == task.targetJail) {
                return;
            }
        }
        m_queue.enqueue(task);
    } else {
        // User actions take priority ahead of pending background polls
        m_userActionCount.fetch_add(1);
        int insertAt = m_queue.size();
        for (int i = 0; i < m_queue.size(); ++i) {
            if (m_queue.at(i).isPoll()) {
                insertAt = i;
                break;
            }
        }
        m_queue.insert(insertAt, task);
    }

    pumpQueue();
}

void ContainerBackend::pumpQueue()
{
    if (m_taskRunning || m_queue.isEmpty()) return;

    m_currentTask = m_queue.dequeue();
    m_taskRunning = true;

    switch (m_currentTask.kind) {
    case PendingTask::Kind::PollContainers:
    case PendingTask::Kind::PollTemplates:
    case PendingTask::Kind::PollPackages:
    case PendingTask::Kind::PollExchangeFiles:
    case PendingTask::Kind::PollUsbDevices:
        startPollTask(m_currentTask);
        break;
    case PendingTask::Kind::UserAction:
        startUserActionTask(m_currentTask);
        break;
    case PendingTask::Kind::StreamingAction:
        startStreamingTask(m_currentTask);
        break;
    case PendingTask::Kind::TerminalAction:
        startTerminalTask(m_currentTask);
        break;
    }
}

void ContainerBackend::finishActiveTask()
{
    if (!m_currentTask.isPoll()) {
        m_userActionCount.fetch_sub(1);
    }
    m_activeProcess = nullptr;
    m_taskRunning = false;
    pumpQueue();
}

void ContainerBackend::startPollTask(const PendingTask &task)
{
    auto *proc = new QProcess(this);
    m_activeProcess = proc;
    proc->setProgram(task.program);
    proc->setArguments(task.args);
    auto completed = std::make_shared<bool>(false);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, task, completed](int exitCode, QProcess::ExitStatus status) {
        if (*completed) return;
        *completed = true;

        const QByteArray rawOut = proc->readAllStandardOutput();
        const QByteArray rawErr = proc->readAllStandardError();
        const bool ok = (status == QProcess::NormalExit && exitCode == 0);

        if (task.kind == PendingTask::Kind::PollContainers) {
            QList<ContainerInfo> result;
            QString err;
            if (!ok) {
                err = tr("Failed to run parishctl-list");
            } else {
                const QJsonDocument doc = QJsonDocument::fromJson(rawOut);
                if (!doc.isArray()) {
                    err = tr("JSON parse error from parishctl-list");
                } else {
                    for (const QJsonValue &val : doc.array()) {
                        if (!val.isObject()) continue;
                        const QJsonObject obj = val.toObject();
                        ContainerInfo info;
                        info.name = obj[QStringLiteral("name")].toString();
                        info.jailState = obj[QStringLiteral("jail_state")].toString();
                        info.keyState = obj[QStringLiteral("key_state")].isNull()
                            ? QString()
                            : obj[QStringLiteral("key_state")].toString();
                        info.sourceTemplate = obj[QStringLiteral("source_template")].toString();
                        info.kind = obj[QStringLiteral("kind")].toString();
                        info.tier = obj[QStringLiteral("tier")].toString();
                        info.riskLevel = obj[QStringLiteral("risk_level")].toString();
                        info.diskUsage = static_cast<quint64>(obj[QStringLiteral("disk_usage")].toDouble());
                        info.isVault = obj[QStringLiteral("is_vault")].toBool();
                        info.isCompatibility = obj[QStringLiteral("is_compatibility")].toBool();
                        info.audioOutput = !info.isVault && obj[QStringLiteral("audio_output")].toBool(false);
                        info.audioInput = !info.isVault && obj[QStringLiteral("audio_input")].toBool(false);
                        info.internalIp = obj[QStringLiteral("internal_ip")].toString();
                        info.path = QStringLiteral("/usr/local/jails/containers/%1").arg(info.name);

                        const QJsonArray caps = obj[QStringLiteral("capabilities")].toArray();
                        for (const QJsonValue &cv : caps) {
                            const QString c = cv.toString().trimmed();
                            if (!c.isEmpty()) info.capabilities.append(c);
                        }
                        if (info.capabilities.isEmpty()) {
                            if (!info.isVault) info.capabilities << QStringLiteral("gui") << QStringLiteral("audio");
                            if (info.isCompatibility) {
                                info.capabilities << QStringLiteral("compat-windows")
                                                  << QStringLiteral("compat-linux");
                            }
                            if (info.isVault) info.capabilities << QStringLiteral("vault");
                        }
                        if (!info.name.isEmpty()) result.append(info);
                    }
                }
            }
            emit containersReady(result, err);
        } else if (task.kind == PendingTask::Kind::PollTemplates) {
            QList<TemplateInfo> result;
            QString err;
            if (!ok) {
                err = tr("Failed to run parishctl-template-list");
            } else {
                const QJsonDocument doc = QJsonDocument::fromJson(rawOut);
                if (!doc.isArray()) {
                    err = tr("JSON parse error from parishctl-template-list");
                } else {
                    for (const QJsonValue &val : doc.array()) {
                        if (!val.isObject()) continue;
                        const QJsonObject obj = val.toObject();
                        TemplateInfo info;
                        info.name = obj[QStringLiteral("name")].toString();
                        info.state = obj[QStringLiteral("state")].toString();
                        info.tier = obj[QStringLiteral("tier")].toString();
                        info.kind = obj[QStringLiteral("kind")].toString();
                        info.description = obj[QStringLiteral("description_en")].toString();
                        info.currentBase = obj[QStringLiteral("current_base")].toString();
                        info.buildSizeMb = obj[QStringLiteral("build_size_mb")].toInt();
                        info.hidden = obj[QStringLiteral("hidden")].toBool();
                        info.shipped = obj[QStringLiteral("shipped")].toBool();
                        const QString clustersCsv = obj[QStringLiteral("clusters")].toString();
                        if (!clustersCsv.isEmpty()) {
                            info.clusters = clustersCsv.split(QLatin1Char(','), Qt::SkipEmptyParts);
                        }
                        if (!info.name.isEmpty()) result.append(info);
                    }
                }
            }
            emit templatesReady(result, err);
        } else if (task.kind == PendingTask::Kind::PollPackages) {
            QList<PackageInfo> result;
            QString err;
            if (!ok) {
                err = tr("parishctl-apps-list failed");
            } else {
                const QJsonDocument doc = QJsonDocument::fromJson(rawOut);
                if (doc.isArray()) {
                    for (const QJsonValue &val : doc.array()) {
                        if (!val.isObject()) continue;
                        const QJsonObject obj = val.toObject();
                        PackageInfo p;
                        const QString kindStr = obj[QStringLiteral("kind")].toString();
                        p.kind = (kindStr == QLatin1String("custom"))
                            ? PackageInfo::CustomApp
                            : PackageInfo::NativePackage;
                        p.name = obj[QStringLiteral("name")].toString();
                        p.version = obj[QStringLiteral("version")].toString();
                        p.latestVersion = obj[QStringLiteral("latest_version")].toString();
                        p.comment = obj[QStringLiteral("comment")].toString();
                        p.installMethod = obj[QStringLiteral("install_method")].toString();
                        p.installDate = obj[QStringLiteral("install_date")].toString();
                        p.launcherCommand = obj[QStringLiteral("launcher")].toString();
                        if (!p.name.isEmpty()) result.append(p);
                    }
                } else {
                    const QString text = QString::fromUtf8(rawOut);
                    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                        if (line.startsWith(QStringLiteral("Compartment "))) continue;
                        const QStringList parts = line.split(QLatin1Char('\t'));
                        if (parts.size() >= 3) {
                            PackageInfo p;
                            if (parts[0] == QLatin1String("native")) {
                                p.kind = PackageInfo::NativePackage;
                                p.name = parts[1];
                                p.version = parts[2];
                                if (parts.size() > 3) p.latestVersion = parts[3];
                                result.append(p);
                            } else if (parts[0] == QLatin1String("custom")) {
                                p.kind = PackageInfo::CustomApp;
                                p.name = parts[1];
                                p.version = parts[2];
                                if (parts.size() > 3) p.installMethod = parts[3];
                                if (parts.size() > 4) p.installDate = parts[4];
                                result.append(p);
                            }
                        }
                    }
                }
            }
            emit packagesReady(task.targetJail, result, err);
        } else if (task.kind == PendingTask::Kind::PollExchangeFiles) {
            QList<ExchangeFileEntry> result;
            QString err;
            if (!ok) {
                err = QString::fromUtf8(rawErr).trimmed();
                if (err.isEmpty()) err = tr("Failed to list exchange files");
            } else {
                const QJsonDocument doc = QJsonDocument::fromJson(rawOut);
                if (!doc.isArray()) {
                    err = tr("JSON parse error from parishctl-exchange files");
                } else {
                    for (const QJsonValue &val : doc.array()) {
                        if (!val.isObject()) continue;
                        const QJsonObject obj = val.toObject();
                        ExchangeFileEntry e;
                        e.name = obj[QStringLiteral("name")].toString();
                        e.type = obj[QStringLiteral("type")].toString();
                        e.size = static_cast<quint64>(obj[QStringLiteral("size")].toDouble());
                        if (!e.name.isEmpty()) result.append(e);
                    }
                }
            }
            emit exchangeFilesReady(task.targetJail, result, err);
        } else if (task.kind == PendingTask::Kind::PollUsbDevices) {
            QList<UsbDeviceInfo> result;
            QString err;
            if (!ok) {
                err = QString::fromUtf8(rawErr).trimmed();
                if (err.isEmpty()) err = tr("Failed to list removable USB devices");
            } else {
                const QJsonDocument doc = QJsonDocument::fromJson(rawOut);
                if (!doc.isArray()) {
                    err = tr("JSON parse error from parish-usb devices");
                } else {
                    for (const QJsonValue &val : doc.array()) {
                        if (!val.isObject()) continue;
                        const QJsonObject obj = val.toObject();
                        UsbDeviceInfo d;
                        d.device = obj[QStringLiteral("device")].toString();
                        d.fstype = obj[QStringLiteral("fstype")].toString();
                        d.sizeBytes = static_cast<quint64>(obj[QStringLiteral("size")].toDouble());
                        d.mode = obj[QStringLiteral("mode")].toString();
                        d.attachedJails = obj[QStringLiteral("attached_jails")].toString();
                        if (!d.device.isEmpty()) result.append(d);
                    }
                }
            }
            emit usbDevicesReady(result, err);
        }

        proc->deleteLater();
        finishActiveTask();
    });

    connect(proc, &QProcess::errorOccurred, this, [this, proc, task, completed](QProcess::ProcessError) {
        if (*completed) return;
        *completed = true;
        const QString errMsg = tr("Failed to run process: %1").arg(proc->errorString());
        if (task.kind == PendingTask::Kind::PollContainers) {
            emit containersReady({}, errMsg);
        } else if (task.kind == PendingTask::Kind::PollTemplates) {
            emit templatesReady({}, errMsg);
        } else if (task.kind == PendingTask::Kind::PollPackages) {
            emit packagesReady(task.targetJail, {}, errMsg);
        } else if (task.kind == PendingTask::Kind::PollExchangeFiles) {
            emit exchangeFilesReady(task.targetJail, {}, errMsg);
        } else if (task.kind == PendingTask::Kind::PollUsbDevices) {
            emit usbDevicesReady({}, errMsg);
        }
        proc->deleteLater();
        finishActiveTask();
    });

    proc->start();
}

void ContainerBackend::startUserActionTask(const PendingTask &task)
{
    emit operationStarted(task.description);

    auto *proc = new QProcess(this);
    m_activeProcess = proc;
    proc->setProgram(task.program);
    proc->setArguments(task.args);
    auto completed = std::make_shared<bool>(false);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, task, completed](int exitCode, QProcess::ExitStatus status) {
        if (*completed) return;
        *completed = true;
        const bool ok = (status == QProcess::NormalExit && exitCode == 0);
        QString err;
        if (!ok) {
            err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (err.isEmpty()) err = tr("Command failed (exit %1)").arg(exitCode);
        } else if (!task.attachAfterJail.isEmpty()) {
            QProcess::startDetached(QStringLiteral("/usr/local/bin/parish-xpra"),
                                    { QStringLiteral("attach"), task.attachAfterJail });
        }
        emit operationFinished(task.description, ok, err);
        proc->deleteLater();
        finishActiveTask();
    });

    connect(proc, &QProcess::errorOccurred,
            this, [this, proc, task, completed](QProcess::ProcessError) {
        if (*completed) return;
        *completed = true;
        emit operationFinished(task.description, false,
                               tr("Failed to run %1: %2").arg(proc->program(), proc->errorString()));
        proc->deleteLater();
        finishActiveTask();
    });

    proc->start();
}

void ContainerBackend::startStreamingTask(const PendingTask &task)
{
    emit operationStarted(task.description);

    auto *proc = new QProcess(this);
    m_activeProcess = proc;
    proc->setProgram(task.program);
    proc->setArguments(task.args);
    auto completed = std::make_shared<bool>(false);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        emit installOutputReady(QString::fromUtf8(proc->readAllStandardOutput()));
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        emit installOutputReady(QString::fromUtf8(proc->readAllStandardError()));
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, task, completed](int exitCode, QProcess::ExitStatus status) {
        if (*completed) return;
        *completed = true;
        const bool ok = (status == QProcess::NormalExit && exitCode == 0);
        QString err;
        if (!ok) {
            err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (err.isEmpty()) err = tr("Exit code %1").arg(exitCode);
        }
        emit installFinished(ok, err);
        emit operationFinished(task.description, ok, err);
        proc->deleteLater();
        finishActiveTask();
    });

    connect(proc, &QProcess::errorOccurred,
            this, [this, proc, task, completed](QProcess::ProcessError) {
        if (*completed) return;
        *completed = true;
        const QString err = tr("Failed to start %1: %2").arg(proc->program(), proc->errorString());
        emit installFinished(false, err);
        emit operationFinished(task.description, false, err);
        proc->deleteLater();
        finishActiveTask();
    });

    proc->start();
}

void ContainerBackend::startTerminalTask(const PendingTask &task)
{
    QString termPath;
    QStringList termArgs;
    QString err;

    if (!TerminalLauncher::findTerminal(task.description, task.args, termPath, termArgs, &err,
                                        QString(), task.pauseTerminalOnExit)) {
        emit terminalLaunched(task.description);
        emit operationFinished(task.description, false, err);
        emit terminalClosed(task.description);
        finishActiveTask();
        return;
    }

    emit operationStarted(task.description);
    emit terminalLaunched(task.description);

    auto *proc = new QProcess(this);
    m_activeProcess = proc;
    proc->setProgram(termPath);
    proc->setArguments(termArgs);
    auto completed = std::make_shared<bool>(false);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, task, completed](int code, QProcess::ExitStatus status) {
        if (*completed) return;
        *completed = true;
        const bool ok = (status == QProcess::NormalExit && code == 0);
        QString errMsg;
        if (!ok) {
            errMsg = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (errMsg.isEmpty()) errMsg = tr("Command failed (exit %1)").arg(code);
        }
        emit operationFinished(task.description, ok, errMsg);
        emit terminalClosed(task.description);
        proc->deleteLater();
        finishActiveTask();
    });

    connect(proc, &QProcess::errorOccurred,
            this, [this, proc, task, completed](QProcess::ProcessError) {
        if (*completed) return;
        *completed = true;
        emit operationFinished(task.description, false, proc->errorString());
        emit terminalClosed(task.description);
        proc->deleteLater();
        finishActiveTask();
    });

    proc->start();
}

void ContainerBackend::requestContainers()
{
    PendingTask t;
    t.kind = PendingTask::Kind::PollContainers;
    t.description = QStringLiteral("List compartments");
    t.program = m_pkexecPath;
    t.args = { QStringLiteral("/usr/local/sbin/parishctl-list"), QStringLiteral("--json") };
    enqueueTask(t);
}

void ContainerBackend::requestTemplates()
{
    PendingTask t;
    t.kind = PendingTask::Kind::PollTemplates;
    t.description = QStringLiteral("List templates");
    t.program = m_pkexecPath;
    t.args = { QStringLiteral("/usr/local/sbin/parishctl-template-list"), QStringLiteral("--json") };
    enqueueTask(t);
}

void ContainerBackend::requestPackages(const QString &jail)
{
    if (!isValidCompartmentName(jail)) {
        emit packagesReady(jail, {}, tr("Invalid compartment name."));
        return;
    }
    PendingTask t;
    t.kind = PendingTask::Kind::PollPackages;
    t.description = tr("Listing applications in %1").arg(jail);
    t.program = m_pkexecPath;
    t.args = { QStringLiteral("/usr/local/sbin/parishctl-apps-list"), jail };
    t.targetJail = jail;
    enqueueTask(t);
}

void ContainerBackend::requestExchangeFiles(const QString &jail)
{
    if (!isValidCompartmentName(jail)) {
        emit exchangeFilesReady(jail, {}, tr("Invalid compartment name."));
        return;
    }
    PendingTask t;
    t.kind = PendingTask::Kind::PollExchangeFiles;
    t.description = tr("Listing exchange files in %1").arg(jail);
    t.program = m_pkexecPath;
    t.args = { QStringLiteral("/usr/local/sbin/parishctl-exchange"),
               QStringLiteral("files"), jail, QStringLiteral("--json") };
    t.targetJail = jail;
    enqueueTask(t);
}

void ContainerBackend::requestUsbDevices()
{
    PendingTask t;
    t.kind = PendingTask::Kind::PollUsbDevices;
    t.description = QStringLiteral("List removable USB devices");
    t.program = QStringLiteral("/usr/local/bin/parish-usb");
    t.args = { QStringLiteral("devices"), QStringLiteral("--json") };
    enqueueTask(t);
}

void ContainerBackend::enqueueParishctl(const QString &description, const QStringList &args)
{
    PendingTask t;
    t.kind = PendingTask::Kind::UserAction;
    t.description = description;
    t.program = m_pkexecPath;
    t.args << QStringLiteral("/usr/local/sbin/") + args.first();
    for (int i = 1; i < args.size(); ++i) t.args << args[i];
    enqueueTask(t);
}

void ContainerBackend::enqueueDirectAction(const QString &description, const QString &program, const QStringList &args)
{
    PendingTask t;
    t.kind = PendingTask::Kind::UserAction;
    t.description = description;
    t.program = program;
    t.args = args;
    enqueueTask(t);
}

void ContainerBackend::enqueueStreamingPkexec(const QString &title, const QStringList &sbinArgs)
{
    PendingTask t;
    t.kind = PendingTask::Kind::StreamingAction;
    t.description = title;
    t.program = m_pkexecPath;
    t.args = sbinArgs;
    enqueueTask(t);
}

void ContainerBackend::enqueueTerminal(const QString &title, const QStringList &execArgv, bool pauseOnExit)
{
    PendingTask t;
    t.kind = PendingTask::Kind::TerminalAction;
    t.description = title;
    t.args = execArgv;
    t.pauseTerminalOnExit = pauseOnExit;
    enqueueTask(t);
}

void ContainerBackend::startContainer(const QString &name)
{
    if (!isValidCompartmentName(name)) return;
    enqueueParishctl(tr("Starting '%1'").arg(name), { QStringLiteral("parishctl-start"), name });
}

void ContainerBackend::stopContainer(const QString &name)
{
    if (!isValidCompartmentName(name)) return;
    enqueueParishctl(tr("Stopping '%1'").arg(name), { QStringLiteral("parishctl-stop"), name });
}

void ContainerBackend::unlockVault(const QString &name)
{
    if (!isValidCompartmentName(name)) return;
    enqueueTerminal(tr("Unlock Vault: %1").arg(name),
                    { m_pkexecPath, QStringLiteral("/usr/local/sbin/parishctl-unlock"), name },
                    true);
}

void ContainerBackend::lockVault(const QString &name)
{
    if (!isValidCompartmentName(name)) return;
    enqueueParishctl(tr("Locking '%1'").arg(name), { QStringLiteral("parishctl-lock"), name });
}

void ContainerBackend::setRiskLevel(const QString &name, const QString &riskLevel)
{
    if (!isValidCompartmentName(name) || !isValidRiskLevel(riskLevel)) return;
    enqueueParishctl(tr("Setting risk level for '%1' to %2").arg(name, riskLevel),
                     { QStringLiteral("parishctl-set-risk"), name, riskLevel });
}

void ContainerBackend::setAudioConfig(const QString &name, bool outputEnabled, bool inputEnabled)
{
    if (!isValidCompartmentName(name)) return;
    const QString outFlag = outputEnabled ? QStringLiteral("--output=on") : QStringLiteral("--output=off");
    const QString inFlag = inputEnabled ? QStringLiteral("--input=on") : QStringLiteral("--input=off");
    enqueueParishctl(tr("Updating audio policy for '%1'").arg(name),
                     { QStringLiteral("parishctl-audio"), QStringLiteral("set"), name, outFlag, inFlag });
}

void ContainerBackend::attachXpra(const QString &name)
{
    if (!isValidCompartmentName(name)) return;
    PendingTask t;
    t.kind = PendingTask::Kind::UserAction;
    t.description = tr("Attaching display for '%1'").arg(name);
    t.program = m_pkexecPath;
    t.args = { QStringLiteral("/usr/local/sbin/parishctl-xpra-start"), name };
    t.attachAfterJail = name;
    enqueueTask(t);
}

void ContainerBackend::detachXpra(const QString &name)
{
    if (!isValidCompartmentName(name)) return;
    enqueueDirectAction(tr("Detaching display for '%1'").arg(name),
                        QStringLiteral("/usr/local/bin/parish-xpra"),
                        { QStringLiteral("detach"), name });
}

void ContainerBackend::exchangeTransfer(const QString &op,
                                        const QString &srcJail,
                                        const QString &srcPath,
                                        const QString &dstJail,
                                        const QString &dstPath,
                                        bool confirmVaultExport,
                                        bool confirmHostImport,
                                        bool sanitize)
{
    QStringList args{ QStringLiteral("parishctl-exchange"), op };

    if (op == QLatin1String("copy") || op == QLatin1String("move")) {
        if (!isValidCompartmentName(srcJail) || !isValidCompartmentName(dstJail)) return;
        args << srcJail << srcPath << dstJail << dstPath;
        if (confirmVaultExport) args << QStringLiteral("--confirm-vault-export");
    } else if (op == QLatin1String("put")) {
        if (!isValidCompartmentName(dstJail) || srcPath.trimmed().isEmpty()) return;
        args << srcPath << dstJail << dstPath;
    } else if (op == QLatin1String("get")) {
        if (!isValidCompartmentName(srcJail) || srcPath.trimmed().isEmpty()) return;
        args << srcJail << srcPath;
        if (!dstPath.trimmed().isEmpty()) args << dstPath;
        if (confirmVaultExport) args << QStringLiteral("--confirm-vault-export");
        if (confirmHostImport) args << QStringLiteral("--confirm-host-import");
        if (sanitize) args << QStringLiteral("--sanitize");
    } else {
        return;
    }

    enqueueParishctl(tr("File Exchange (%1)").arg(op), args);
}

void ContainerBackend::attachUsb(const QString &jail,
                                 const QString &device,
                                 const QString &fstype,
                                 bool readWrite,
                                 bool confirmRw)
{
    if (!isValidCompartmentName(jail) || device.trimmed().isEmpty()) return;
    QStringList args{ QStringLiteral("parishctl-usb-attach"), jail, device };
    if (!fstype.trimmed().isEmpty()) args << fstype.trimmed();
    if (readWrite) {
        args << QStringLiteral("--rw");
        if (confirmRw) args << QStringLiteral("--confirm-rw");
    } else {
        args << QStringLiteral("--ro");
    }
    enqueueParishctl(tr("Attaching USB %1 to '%2'").arg(device, jail), args);
}

void ContainerBackend::detachUsb(const QString &jail, const QString &device)
{
    if (!isValidCompartmentName(jail) || device.trimmed().isEmpty()) return;
    enqueueParishctl(tr("Detaching USB %1 from '%2'").arg(device, jail),
                     { QStringLiteral("parishctl-usb-detach"), jail, device });
}

void ContainerBackend::destroyContainer(const QString &name, const QString &confirmationToken)
{
    if (!isValidCompartmentName(name)) return;
    enqueueStreamingPkexec(tr("Destroy: %1").arg(name),
                           { QStringLiteral("/usr/local/sbin/parishctl-destroy"),
                             name,
                             QStringLiteral("--confirmation-token=%1").arg(confirmationToken) });
}

void ContainerBackend::createContainer(const QString &name, const QString &templateName)
{
    if (!isValidCompartmentName(name)) return;
    enqueueStreamingPkexec(tr("Create: %1").arg(name),
                           { QStringLiteral("/usr/local/sbin/parishctl-create"),
                             name,
                             QStringLiteral("--template=%1").arg(templateName) });
}

void ContainerBackend::openConsole(const QString &name)
{
    if (!isValidCompartmentName(name)) return;
    enqueueTerminal(tr("Console: %1").arg(name),
                    { m_pkexecPath, QStringLiteral("/usr/local/sbin/parishctl-console"), name },
                    true);
}

void ContainerBackend::launchTemplateWizard()
{
    enqueueTerminal(tr("ParishBSD Template Wizard"),
                    { m_pkexecPath, QStringLiteral("/usr/local/bin/parish-template-wizard") },
                    false);
}

void ContainerBackend::updateTemplateAsync(const QString &name, bool shipped)
{
    enqueueStreamingPkexec(tr("Updating template '%1'").arg(name),
                           { QStringLiteral("/usr/local/sbin/parish-template-build"),
                             name,
                             QStringLiteral("--update"),
                             QStringLiteral("--source=%1").arg(shipped ? QStringLiteral("shipped")
                                                                       : QStringLiteral("user")) });
}

void ContainerBackend::upgradePackages(const QString &jail)
{
    if (!isValidCompartmentName(jail)) return;
    enqueueStreamingPkexec(tr("Upgrading packages in %1").arg(jail),
                           { QStringLiteral("/usr/local/sbin/parishctl-apps-upgrade"), jail });
}

void ContainerBackend::installPackageAsync(const QString &jail, const QString &package)
{
    if (!isValidCompartmentName(jail) || package.trimmed().isEmpty()) return;
    enqueueStreamingPkexec(tr("Installing %1 in %2").arg(package, jail),
                           { QStringLiteral("/usr/local/sbin/parishctl-apps-install"), jail, package });
}

void ContainerBackend::installFromFileAsync(const QString &jail, const QString &filePath)
{
    if (!isValidCompartmentName(jail) || filePath.trimmed().isEmpty()) return;
    enqueueStreamingPkexec(tr("Installing file into %1").arg(jail),
                           { QStringLiteral("/usr/local/sbin/parishctl-apps-install-file"), jail, filePath });
}

void ContainerBackend::removePackages(const QString &jail,
                                      const QStringList &nativePackages,
                                      const QStringList &customApps)
{
    if (!isValidCompartmentName(jail)) return;
    for (const QString &pkg : nativePackages) {
        enqueueParishctl(tr("Removing %1 from %2").arg(pkg, jail),
                         { QStringLiteral("parishctl-apps-remove"), jail, pkg });
    }
    for (const QString &app : customApps) {
        enqueueParishctl(tr("Removing custom app %1 from %2").arg(app, jail),
                         { QStringLiteral("parishctl-apps-remove-custom"), jail, app });
    }
}

void ContainerBackend::launchApplication(const QString &jail, const QString &appName, bool isCustom)
{
    Q_UNUSED(isCustom);
    if (!isValidCompartmentName(jail) || appName.trimmed().isEmpty()) return;
    enqueueDirectAction(tr("Launching '%1' in %2").arg(appName, jail),
                        QStringLiteral("/usr/local/bin/parish-app-launch"),
                        { jail, appName });
}

void ContainerBackend::cancelInstall()
{
    if (m_activeProcess &&
        m_currentTask.kind == PendingTask::Kind::StreamingAction &&
        m_activeProcess->state() == QProcess::Running) {
        m_activeProcess->terminate();
        if (!m_activeProcess->waitForFinished(3000)) {
            m_activeProcess->kill();
        }
    }
}
