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
	m_vaultDataDir = QStringLiteral("/zroot/vault-data");
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
	return QString::fromUtf8(proc.readAllStandardOutput());
}

QString ContainerBackend::findJailRoot(const QString &name, QString *tierOut) const
{
	const QStringList tiers = { QStringLiteral("app"), QStringLiteral("vault") };
	for (const QString &tier : tiers) {
		const QString candidate = m_jailsDir + QLatin1Char('/') + tier + QLatin1Char('/') + name;
		if (QFileInfo::exists(candidate + QStringLiteral("/bin/sh"))) {
			if (tierOut) *tierOut = tier;
			return candidate;
		}
	}
	return QString();
}

QString ContainerBackend::readPersonality(const QString &jailRoot) const
{
	QFile f(jailRoot + QStringLiteral("/usr/local/etc/parishbsd/metadata"));
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	QTextStream in(&f);
	while (!in.atEnd()) {
		const QString line = in.readLine().trimmed();
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
		return QStringLiteral("File Vault");
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
int jlsRc = 0;
	const QString jlsOut = runAsync(QStringLiteral("/usr/sbin/jls"), { QStringLiteral("name") }, &jlsRc);
	const QStringList runningJails = jlsOut.split('\n', Qt::SkipEmptyParts);

	const QStringList tiers = { QStringLiteral("app"), QStringLiteral("vault") };

	for (const QString &tier : tiers) {
		const QString tierDir = m_jailsDir + QLatin1Char('/') + tier;
		QDir d(tierDir);
		if (!d.exists())
			continue;

		const QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

		for (const QString &name : entries) {
			const QString jailRoot = tierDir + QLatin1Char('/') + name;
			if (!QFileInfo::exists(jailRoot + QStringLiteral("/bin/sh"))) { continue; }

			ContainerInfo info;
			info.name = name;
			info.tier = tier;
			info.isVault = (tier == QLatin1String("vault"));

			info.personality = readPersonality(jailRoot);
			info.kind = kindFromPersonality(info.personality);
			info.isCompatibility = info.personality.startsWith(QStringLiteral("apps-compatible"));

			int jlsRc = 0;
			const QString jlsOut = runSync(QStringLiteral("/usr/sbin/jls"), { QStringLiteral("name") }, &jlsRc);
			const QStringList running = jlsOut.split('\n', Qt::SkipEmptyParts);
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
			else if (info.isVault) {
				if (keystatus == QLatin1String("available"))
					info.state = QStringLiteral("unlocked");
				else {
					info.state = QStringLiteral("locked");
				}
			}
			else {
				info.state = QStringLiteral("stopped");
			}

			// disk usage
			const QString ds = QStringLiteral("zroot/jails/containers/") + name;
			int zrc = 0;
			const QString used = runSync(
				QStringLiteral("/sbin/zfs"),
				{ QStringLiteral("list"), QStringLiteral("-H"), QStringLiteral("-o"), QStringLiteral("used"), ds },	
				&zrc);
			if (zrc == 0) {
				QString s = used.trimmed();
				quint64 multiplier = 1;
				QChar last = s.isEmpty() ? QChar() : s.back().toUpper();
				if (last == QLatin1Char('K')) { multiplier = 1024ULL; }
				else if (last == QLatin1Char('M')) { multiplier = 1024ULL * 1024; }
				else if (last == QLatin1Char('G')) { multiplier = 1024ULL * 1024 * 1024; }
				else if (last == QLatin1Char('T')) { multiplier = 1024ULL * 1024 * 1024 * 1024; }
				if (last.isLetter())
					s.chop(1);
				bool ok = false;
				const double v = s.toDouble(&ok);
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

	connect(proc, &QProcess::finished, this, [this,proc,description,completed] (int exitCode, QProcess::ExitStatus status) {
		if (*completed) return;
		*completed = true;	

		const bool ok = (status == QProcess::NormalExit && exitCode == 0);
		QString err;
		if (!ok) {
			err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
			if (err.isEmpty())
				err = tr("Command failed (exit %1)").arg(exitCode);
		}
		emit operationFinished(description, ok, err);
		proc->deleteLater();
	});

	connect(proc, &QProcess::errorOccurred, this, [this, proc, description, completed] (QProcess::ProcessError) {
		if (*completed) return;
		*completed = true;

		emit operationFinished(description, false, tr("Failed to run %1: %2").arg(proc->program(),proc->errorString()));
		proc->deleteLater();		
	});

	proc->start();
}

void ContainerBackend::runParishctl(const QString &description, const QStringList &args)
{
	QStringList pkArgs;
	pkArgs << m_parishctlPath;
	pkArgs << args;
	runAsync(description, QStringLiteral("/usr/local/bin/pkexec"), pkArgs);
}

void ContainerBackend::runInTerminal(const QString &title, const QString &program, const QStringList &args)
{
	emit operationStarted(title);
	emit terminalLaunched(title);

	// Build a shell line by single-quoting every argument, so that
	// paths and user data cannot inject shell metacharacters
	QStringList quoted;
	for (const QString &a : args) {
		QString esc = a;
		esc.replace("'", "'\\''");
		quoted.append("'" + esc + "'");
	}
	const QString shellLine = quoted.join(' ');

	// run the command, then wait for Enter for visible output
	const QString wrapped = QStringLiteral("%1; echo; echo '[press Enter to close]'; read _").arg(shellLine);

	auto *proc = new QProcess(this);
	proc->setProgram(program);
	proc->setArguments({ QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), wrapped });

	connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, proc, title](int code, QProcess::ExitStatus status) {
		const bool ok = (status == QProcess::NormalExit && code == 0);
		QString err;
		if (!ok) {
			err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
			if (err.isEmpty())
				err = tr("Command failed (exit %1)").arg(code);
		}
		emit operationFinished(title, ok, err);
		emit terminalClosed(title);
		proc->deleteLater();
	});

	connect(proc, &QProcess::errorOccurred, this, [this,proc,title](QProcess::ProcessError) {
		emit operationFinished(title,false,proc->errorString());
		emit terminalClosed(title);
		proc->deleteLater();
	});

	proc->start();
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
	const QString cmd = QStringLiteral("/usr/local/bin/pkexec '%1' unlock '%2'; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name);

	runInTerminal(tr("Unlocking '%1'").arg(name),
		QStringLiteral("/usr/local/bin/xfce4-terminal"),
		{ QStringLiteral("--title"), tr("Unlock Vault: %1").arg(name),
		QStringLiteral("--geometry"), QStringLiteral("90x20"),
		QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd
		});
}

void ContainerBackend::lockVault(const QString &name)
{
	runParishctl(tr("Locking '%1'").arg(name), { QStringLiteral("lock"), name });
}

void ContainerBackend::destroyContainer(const QString &name)
{
	// parishctl performs its own typed confirmation.
	const QString cmd = QStringLiteral("/usr/local/bin/pkexec '%1' destroy '%2'; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name);

	runInTerminal(tr("Destroying '%1'").arg(name),
		QStringLiteral("/usr/local/bin/xfce4-terminal"),
		{ QStringLiteral("--title"), tr("Destroy: %1").arg(name),
	       	QStringLiteral("--geometry"), QStringLiteral("90x24"),
		QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}

void ContainerBackend::createContainer(const QString &name, const QString &personality)
{
	// vault creation requires prompt for a passphrase.
	const bool isVault = personality.startsWith(QStringLiteral("vault-"));

	if (isVault) {
		const QString cmd = QStringLiteral("/usr/local/bin/pkexec '%1' create '%2' '%3'; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name, personality);

		runInTerminal(tr("Creating vault '%1'").arg(name),
			QStringLiteral("/usr/local/bin/xfce4-terminal"),
			{ QStringLiteral("--title"), tr("Create Vault: %1").arg(name),
			QStringLiteral("--geometry"), QStringLiteral("90x24"),
		        QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), cmd });
	}
	else {
		runParishctl(tr("Creating '%1'").arg(name), { QStringLiteral("create"), name, personality });
	}
}

void ContainerBackend::openConsole(const QString &name)
{
	const QString cmd = QStringLiteral("/usr/local/bin/pkexec /usr/sbin/jexec '%1' /bin/sh; echo; echo '[console closed]'; read _").arg(name);
	runInTerminal(tr("Console: '%1'").arg(name),
		QStringLiteral("/usr/local/bin/xfce4-terminal"),
		{ QStringLiteral("--title"), tr("Console: %1").arg(name),
	        QStringLiteral("-x"),
		QStringLiteral("sh"), QStringLiteral("-c"), cmd });
}

void ContainerBackend::openVaultFiles(const QString &name) {
	runAsync(tr("Opening files for '%1'").arg(name),
		QStringLiteral("/usr/local/bin/parish-vault-files"),
		{ name });
}

QList<PackageInfo> ContainerBackend::listPackages(const QString &jail, QString *errorOut) const
{
	QList<PackageInfo> result;

	QProcess info;
	info.start("/usr/sbin/pkg", { "-j", jail, "info" });
	if (!info.waitForFinished(20000)) {
		if (errorOut) *errorOut = tr("pkg info timed out");
		return result;
	}
	if (info.exitCode() != 0) {
		if (errorOut)
			*errorOut = QString::fromUtf8(info.readAllStandardError()).trimmed();
		return result;
	}

	const QString infoOut = QString::fromUtf8(info.readAllStandardOutput());
	for (const QString &line : infoOut.split('\n', Qt::SkipEmptyParts)) {
		const int sp = line.indexOf(' ');
		if (sp <= 0) continue;
		const QString nameVer = line.left(sp).trimmed();
		const QString comment = line.mid(sp).trimmed();

		int dashPos = -1;
		for (int i = nameVer.size()-1; i > 0; --i) {
			if (nameVer.at(i) == '-' && i+1 < nameVer.size()
				&& nameVer.at(i+1).isDigit()) {
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

//
	QProcess up;
	up.start("/usr/sbin/pkg", { "-j", jail, "upgrade", "-n" });
	if (up.waitForFinished(20000) && up.exitCode() == 0) {
		const QString upOut = QString::fromUtf8(up.readAllStandardOutput());
		for (const QString &line : upOut.split('\n', Qt::SkipEmptyParts)) {
			const int arrow = line.indexOf("->");
			if (arrow < 0) continue;
			const QString left = line.left(arrow).trimmed();
			const QString right = line.mid(arrow+2).trimmed();

			int dash = -1;
			for (int i = left.size()-1; i > 0; --i) {
				if (left.at(i) == '-' && i+1 < left.size() && left.at(i+1).isDigit()) {
					dash = i;
					break;
				}
			}
			if (dash < 0) continue;
			const QString pkgName = left.left(dash);
			for (auto &p : result) {
				if (p.name == pkgName) {
					p.latestVersion = right;
					break;
				}
			}
		}
	}

	for (const CustomApp &a : listCustomApps(jail)) {
		PackageInfo p;
		p.kind = PackageInfo::CustomApp;
		p.name = a.name;
		p.version = a.version;
		p.installDate = a.installDate;
		p.installMethod = a.installMethod;
		p.origin = a.installPath;
		result.append(p);
	}

	if (errorOut) errorOut->clear();
	return result;
}

QList<CustomApp> ContainerBackend::listCustomApps(const QString &jail) const
{
	QList<CustomApp> result;
	const QString dir = m_jailsDir + QStringLiteral("/app/") + jail + QStringLiteral("/usr/local/etc/parishbsd/custom-apps");

	QDir d(dir);
	if (!d.exists()) return result;

	for (const QString &fname : d.entryList({ "*.conf" }, QDir::Files)) {
		QFile f(d.filePath(fname));
		if (!f.open(QIODevice::ReadOnly)) continue;
	
		CustomApp app;
QTextStream in(&f);
		while (!in.atEnd()) {
			QString line = in.readLine().trimmed();
			if (line.isEmpty() || line.startsWith('#')) continue;
			const int eq = line.indexOf('=');
			if (eq <= 0) continue;
			const QString key = line.left(eq).trimmed();
			QString val = line.mid(eq+1).trimmed();
			if (val.size() >= 2 && val.startsWith('"') && val.endsWith('"'))
				 val = val.mid(1,val.size()-2);

			if (key == QLatin1String("NAME")) app.name = val;
			else if (key == QLatin1String("VERSION")) app.version = val;
			else if (key == QLatin1String("INSTALL_METHOD")) app.installMethod = val;
			else if (key == QLatin1String("INSTALL_PATH")) app.installPath = val;
			else if (key == QLatin1String("LAUNCHER_COMMAND")) app.launcherCommand = val;
			else if (key == QLatin1String("ICON_NAME")) app.iconName = val;
			else if (key == QLatin1String("INSTALL_DATE")) app.installDate = val;
		}

		app.sourceFile =fname; 
		app.metadataFile = d.filePath(fname);
		if (app.isValid())
			result.append(app);
	}
	return result;
}

void ContainerBackend::upgradePackages(const QString &jail)
{
	const QString script = QStringLiteral("/usr/local/bin/pkexec /usr/local/bin/parishctl apps upgrade '%1'; echo; echo 'Press Enter to close...'; read _").arg(jail);

	runInTerminal(tr("Upgrading packages in %1").arg(jail), QStringLiteral("/usr/local/bin/xfce4-terminal", { QStringLiteral("--title"), tr("Upgrade: %1").arg(jail),QStringLiteral("--geometry"), QStringLiteral("100x28"), QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), script });
}

void ContainerBackend::installPackage(const QString &jail, const QString &package)
{
	// FreeBSD packages names are alphanumeric plus a small set of punctuation.
	// Validate to prevent command injection.
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+\\-]*$"));
	if (!valid.match(package).hasMatch()) {
		emit operationFinished(tr("Install %1").arg(package), false, tr("Invalid package name: %1").arg(package));
		return;
	}

	const QString script = QStringLiteral("/usr/local/bin/pkexec /usr/local/bin/parishctl apps install '%1' '%2'; echo; echo 'Press Enter to close...'; read _").arg(jail, package);

	runInTerminal(tr("Installing %1 in %2").arg(package, jail), QStringLiteral("/usr/local/bin/xfce4-terminal"), { QStringLiteral("--title"), tr("Install: %1").arg(jail),QStringLiteral("--geometry"), QStringLiteral("100x28"), QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), script });
}

void ContainerBackend::installFromFile(const QString &jail, const QString &file, const QString &detectedType, const QString &launcherMode)
{
	const QString typeKey = detectedType.section(':', 0, 0);

	const bool interactive = (typeKey == QLatin1String("windows-installer") || typeKey == QLatin1String("linux-installer"));
	const QString modeStr = typeKey.contains(QLatin1String("portable")) ? QStringLiteral("portable") : QStringLiteral("installer");
	const QStringList pkArgs ={QStringLiteral("/usr/local/bin/parishctl"), QStringLiteral("apps"),QStringLiteral("install-file"),jail,file, QStringLiteral("--mode=%1").arg(eis), QStringLiteral("--launcher=%1").arg(launcherMode)};

	const QString fileName = QFileInfo(file).fileName();

	if (interactive) {
		const QString script = QStringLiteral(
			"/usr/local/bin/pkexec "
			"echo; echo 'Press Enter to close'; read _").arg(pkArgs.join(' '));
		runInTerminal(tr("Installing %1").arg(fileName),
			QStringLiteral("/usr/local/bin/xfce4-terminal", {QStringLiteral("--title"),tr("Install: %1").arg(fileName), QStringLiteral("--geometry"), QStringLiteral("100x30"),  QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), script });
	}
	else {
		emit installRequested(tr("Installing %1").arg(fileName), QStringLiteral("/usr/local/bin/pkexec"),pkArgs});
}

void ContainerBackend::removePackage(const QString &jail, const QString &package) {
	// FreeBSD packages names are alphanumeric plus a small set of punctuation.
	// Validate to prevent ocmmand injection.
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+\\-]*$"));
	if (!valid.match(package).hasMatch()) {
		emit operationFinished(tr("Remove %1").arg(package), false, tr("Invalid package name: %1").arg(package));
		return;
	}

	const QString script = QStringLiteral("/usr/local/bin/pkexec /usr/local/bin/parishctl apps remove '%1' '%2'").arg(jail, package);

	runInTerminal(tr("Removing %1 from %2").arg(package, jail), QStringLiteral("/usr/local/bin/xfce4-terminal"), { QStringLiteral("--title"),tr("Remove: %1").arg(jail), QStringLiteral("--geometry"), QStringLiteral("100x28"), QStringLiteral("-x"), QStringLiteral("sh"), QStringLiteral("-c"), script });
}

void ContainerBackend::removeCustomApp(const QString &jail, const QString &appName) {
	const QString dir = m_jailsDir + QStringLiteral("/app/") + jail + QStringLiteral("/usr/local/etc/parishbsd/custom-apps");

	QDir d(dir);
	if (!d.exists()) {
		emit operationFinished(tr("Remove %1").arg(appName), false, tr("No custom-apps directory in '%1'").arg(jail));
		return;
	}

	for (const QString &fname : d.entryList({ "*.conf" }, QDir::Files)) {
		QFile f(d.filePath(fname));
		if (!f.open(QIODevice::ReadOnly | QUIODevice::Text)) continue;
		QTextStream in(&f);
		while (!in.atEnd()) {
			QString line = in.readLine().trimmed();
			if (line.startsWith("NAME="))) {
				metaName = line.mid(5);
				if(metaName.startsWith('"') && metaName.endsWidth('"'))
					metaName = metaName.mid(1,metaName.size()-2);
				break;
				}
			}
		}
		f.close();

		if (metaName != appName)
			continue;
		if (QFIle::remove(d.filePath(fname))) {
			emit operationFinished(tr("Rmoved %1").arg(appName), true, QString());
		}
		else {
			emit operationFinished(tr("Remove %1").arg(appName), false, tr("Cannot delete %1").arg(fname));
		}
		return;
	}

	emit operationFinished(tr("Remove %1").arg(appName), false, tr("No metadata foound for %1").arg(appName));
}

void ContainerBackend::setLauncherCommand(const QString &jail, const QString &appName, const QStrig &command)
{
	runAsync(tr("Updating launcher for %1").arg(appName),
		QStringLiteral("/usr/local/bin/pkexec"),
		{ QStringLiteral("/usr/local/bin/parishctl"), QStringLiteral("apps"), QStringLiteral("set-launcher"), jail, appName, command });
}
