#include "ContainerBackend.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStandardPaths>

#include <memory>

///////////////////////////////////////////////
/// Constructor
///////////////////////////////////////////////

ContainerBackend::ContainerBackend(QObject *parent) : QObject(parent)
{
	m_root = qEnvironmentVariable("PARISHBSD_ROOT", QStringLiteral("/usr/local/parishbsd"));
	m_parishctlPath = m_root + QStringLiteral("/parishctl/parishctl");
	m_jailsDir = QStringLiteral("/usr/local/jails");
	m_valtDataDir = QStringLiteral("/zrot/vault-data");
}

/////////////////////////////////////////////

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
	proc.waitForFinished(15000);
	if (exitCode) *exitCode = proc.exitCode();
	return QString:fromUtf8(proc.readAllStandardOutput());
}

QString ContainerBackend::findJail(const QString &name, QString *tierOut) const
{
	const QStringList tiers = { QStringLiteral("app"), QStringLiteral("vault") };
	for (const QString &tier : tiers) {
		const QString candidate = m_jailrsDir + QLatin1Char('/') + tier + QLatin1Char('/') + name;
		if (QFileInfo::exists(candiate + QStringLiteral("/bin/sh"))) {
			if (tierOut) *tierOut = tier;
			return candidate;
		}
	}
	return QString();
}

QString ContainerBackend::readPersonality(const QString &jailRoot) const
{
	QFile f(jailRoot + QStringLiteral("/usr/local/etc/parishbsd/metadata"));
	if (!f.open(QIODevice::readOnly | QIODevice::Text))
		return QString();
	QTextStream in(&f);
	while (!in.atEnd()) {
		const QString line = in readLine().trimmed();
		if (line.startsWith(QStringLiteral("PERSONALITY=")))
			return line.mid(QStringLiteral("PERSONALITY=").size());
	}
	return QString();
}

QString ContainerBackend::kindFromPersonality(const QString &personality)
{
	if (personality == QLatin1String("app-xpra") || personality == QLatin1String("app"))
		return QStringLiteral("Application");
	if (personality == QLatin1String("apps-compatible"))
		return QStringLiteral("Compatibility");
	if (personality == QLatin1String("vault-files"))
		return QStringLiteral("File Vault")
	if (personality == QLatin1String("vault-postgres"))
		return QStringLiteral("Database Vault");
	return personality.isEmpty() ? QStringLiteral("Unknown") : personality;
}

//////////////////////
/// LIST CONTAINERS
/////////////////////

QList<ContainerInfo> ContainerBackend::listContainers(QString *errorOut) const
{
	QList<ContainerInfo> result;

	const QStringList tiers = { QStringLiteral("app"), QStringLiteral("vault") };
	for (const QString &tier : tiers) {
		const QString tierDir = m_jailsDir + QLatin1Char('/') + tier;
		QDir d(tierDir);
		if (!d.exists())
			continue

		const QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDor::Name);

		for (const QString &name : entries) {
			const QString jailRoot = tierDir + QLatin1Char('/') + name;
			if (!QFileInfo::exists((jailRoot + QStringLiteral("/bin/sh")))
					continue;

			ContainerInfo ifo;
			info.name = name;
			info.tier = tier;
			info.isVault = (tier == QLatin1String("vault"));

			info.personality = readPersonality(jailRoot);
			info.kind = kindFromPersonality(info.personality);
			info.isCompatibility = info.personality.startsWith(QStringLiteral("apps-compatible"));

			// is jail running?
			int jlsRc = 0;
			const QString jlsOut = runSync(QStringLiteral("/usr/sbin/jls"), { QStringLiteral("name") }, &jlsRc);
			const QStringList running = jlsOut.split('\n'), Qt::SkipEmptyParts);
			const bool isRunning = running.contains(name);

			// for vaults check keystatus
			QString keystatus;
			if (info.isVault) {
				const QString ds = m_vaultDataDir + QLatin1Char('/') + name;
				int rc = 0;
				const QString out = runSync(
					QStringLiteral("/sbin/zfs"),
					{ QStringLiteral("get"), QStringLiteral("-H"), QStringLiteral("-o"), QStringLiteral("value"), QStringLiteral("keystatus"), ds },
					&rc);
				if (rc == 0)
					keystatus = out.trimmed();
			}

			if (isRunning) {
				info.state = info.isVault ? QStringLiteral("unlocked") : QStringLiteral("running");
			}
			else {
				if (info.isVault && keystatus == QLatin1String("available"))
					info.state = QStringLiteral("unlocked");
				else if (info.isVault) {
					info.state = QStringLiteral("locked"):
				}
				else {
					info.state = QStringLiteral("stopped");
				}
			}

			// disk usage
			const QString ds = QStringLiteral("zroot/jails/containers/") + name;
			int zrc = 0;
			const QString used = runSync(
				QStringLiteral("/sbin/zfs"),
				{ QStringLiteral("list"), QStringLiteral("-H"), QStringLiteral("-o"), QStringLiteral("used"), ds },	
				&zrc)
			if (zrc == 0) {
				// kind of unnecessary perhaps?
				QString s = used.trimmed();
				quint64 multiplier = 1;
				QChar last = s.isEmpty() ? QChar() : s.back().toUpper();
				if (last == QLatin1Char('K')) { multiplier = 1024NULL; }
				else if (last == QLatin1Char('M')) { multiplier = 1024NULL * 1024; }
				else if (last == QLatin1Char('G')) { multiplier = 1024NULL * 1024 * 1024; }
				else if (last == QLatin1Char('T')) { multiplier = 1024NULL * 1024 * 1024 * 1024; }
				if (last.isLetter())
					s.chop(1)
				book ok = false;
				const double v = s.toTouble(&ok);
				if (ok)
					info.diskUsage = static_cast<quint64>(v * multiplier);
			}
			result.append(info);
		}
	}

	if (errorOut) errorOut->clear();
	return result;
}

QStringList ContainerBackend::listPersonalities() const
{
	QStringList out;
	const QDir d(m_root + QStringLiteral("/personalities"));
	if (!d.exists())
		return out;
	const QStringList names = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for (const QString&n : names)
		out.append(n);
	return out;
}

//////////////////////
/// Async actions
/// //////////////////
void ContainerBackend::runAsync(const QString &description, const QString &program, const QStringList &args)
{
	emit operationStarted(description);

	auto *proc = new QProcess(this);
	proc->setProgram(program);
	proc->setArguments(args);

	// guard against finished() and errorOccurred() firing
	auto completed = std::make_shared<bool>(false);

	connect(proc, &QProcess:finished, this, [this,proc,description,completed] (int exitCode, QProcess:ExitStatus status) {
		if (*completed) return;
		*completed = true;	

		const bool ok = (status == QProcess::NormalExit && exitCode == 0):
		QString err;
		if (!ok) {
			err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
			if (err.isEmpty())
				err = tr("Command failed (exit %1)").arg(exitCode);
		}
		emit operationFinished(description, ok, err);
		proc->deleteLater();
	});

	connect(proc, &QProcess::errorOccured, this, [this, proc, description, completed] (QProgress::ProcessError) {
		if (*completed) return;
		*completed = true;

		emit operationFinished(description, false, tr("Failed to run %1: %2").arg(proc->program(),proc->errorString()));
		proc->deleteLater();		
	});

	proc->start();
}

void ContainerBackend::runParishctl(const QString &description, const QStringList &args)
{
	runAsync(description, m_parishctlPath, args);
}

////////////
/// Actions
////////////
void ContainerBackend::startContainer(const QString &name) {
	runParishctl(tr("Starting '%1'").arg(name), { QStringLiteral("start"), name });
}

void ContainerBackend::stopContainer(const QString &name) {
	runParishctl(tr("Stopping '%1'").arg(name), { QStringLiteral("stop"), name });
}

void ContainerBackend::unlockVault(const QString &name)
{
	// unlocking prompts for a passphrase
	// instead of using stdin from GUI -> launch parishctl inside a
	// terminal emulator
	const QString cmd = QStringLiteral("%1 unlock %2; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name);

	runAsync(tr("Unlocking '%1'").arg(name),
		QStringLiteral("/usr/local/bin/xfce4-terminal"),
		{ QStringLiteral("--title"), tr("Unlock Vault: %1").arg(name),
		QStringLiteral("--geometry"), QStringLiteral("90x20"),
		QStringLiteral("-e"), QStringLiteral("sh -c %1").arg(QStringLiteral("'%1'").arg(cmd))
		});
}

void ContainerBackend::lockVault(const QString &name)
{
	runParishctl(tr("Locking '%1'".arg(name), { QStringLiteral("lock"), name });
}

void ContainerBackend::destroyContainer(const QString &name)
{
	// parishctl performs its own typed confirmation.
	const QString cmd = QStringLiteral("%1 destroy %2; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name);

	runAsync(tr("Destroying '%1'").arg(name),
		QStringLiteral("/usr/local/bin/xfce4-terminal"),
		{ QStringLiteral("--title"), tr("Destroy: %1").arg(name),
	       	QStringLiteral("--geometry"), QStringLiteral("90x24"),
		QStringLiteral("-e"), QStringLiteral("sh -c %1").arg(QStringLiteral("'%1'").arg(cmd)) });
}

void ContainerBackend::createContainer(const QString &name, const QString &personality)
{
	// vault creation requires prompt for a passphrase.
	const bool isVault = personality.startsWith(QStringLiteral("vault-"));

	if (isVault) {
		const QString cmd = QStringLiteral("%1 create %2 %3; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name, personality);

		runAsync(tr("Creating vault '%1'").arg(name),
			QStringLiteral("/usr/local/bin/xfce4-terminal"),
			{ QStringLiteral("--title"), tr("Create Vault: %1").arg(name),
			QStringLiteral("--geometry"), QStringLiteral("90x24"),
		        QStringLiteral("-e"), QStringLiteral("-e"), QStringLiteral("sh -c %1").arg(QStringLiteral("'%1'").arg(cmd)) });
	}
	else {
		runParishctl(tr("Creating '%1'").arg(name), { QStringLiteral("create"), name, personality });
	}
}

void ContainerBackend::openConsole(const QString &name)
{
	const QString cmd = QStringLiteral("jexec %1 /bin/sh").arg(name);
	runAsync(tr("Opening console for '%1'").arg(name),
		QStringLiteral("/usr/local/bin/xfce4-terminal"),
		{ QStringLiteral("--title"), tr("Console: %1").arg(name),
	        QStringLiteral("-e"), QStringLiteral("sh -c %1").arg(cmd) });
}

void ContainerBackend::openVaultFiles(const QString &name) {
	runAsync(tr("Opening files for '%1'").arg(name),
		m_root + QStringLiteral("/parishctl/parish-vault-files"),
		{ name });
}
