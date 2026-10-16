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
	m_vaultDataDir = QStringLiteral("/zrot/vault-data");
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

			// is jail running?
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
			else {
				if (info.isVault && keystatus == QLatin1String("available"))
					info.state = QStringLiteral("unlocked");
				else if (info.isVault) {
					info.state = QStringLiteral("locked");
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
				&zrc);
			if (zrc == 0) {
				// kind of unnecessary perhaps?
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
	const QString cmd = QStringLiteral("/usr/local/bin/pkexec '%1' unlock %2; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name);

	runAsync(tr("Unlocking '%1'").arg(name),
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
	const QString cmd = QStringLiteral("/usr/local/bin/pkexec '%1' destroy %2; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name);

	runAsync(tr("Destroying '%1'").arg(name),
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
		const QString cmd = QStringLiteral("/usr/local/bin/pkexec '%1' create %2 %3; echo; echo 'Press Enter to close...'; read _").arg(m_parishctlPath, name, personality);

		runAsync(tr("Creating vault '%1'").arg(name),
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
	const QString cmd = QStringLiteral("jexec %1 /bin/sh").arg(name);
	runAsync(tr("Opening console for '%1'").arg(name),
		QStringLiteral("/usr/local/bin/xfce4-terminal"),
		{ QStringLiteral("--title"), tr("Console: %1").arg(name),
	        QStringLiteral("-x"),
		QStringLiteral("jexec"), name, QStringLiteral("/bin/sh") });
}

void ContainerBackend::openVaultFiles(const QString &name) {
	runAsync(tr("Opening files for '%1'").arg(name),
		m_root + QStringLiteral("/parishctl/parish-vault-files"),
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
		for (int i = 0 nameVer.size()-1; i > 0; --i) {
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
		p.orgiin = a.installPath;
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

	for (const QString &fname : d.entryList({ "*.json" }, QDir::Files)) {
		QfILE f(d.filePath(fname));
		if (!f.open(QIODevice::ReadOnly)) continue;
		const QByteArray raw = f.readAll();
		QJsonParseError err;
		const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
		if (err.error != QJsonParseError::NoError || !doc.isObject())
			continue;

		const QJsonObject o = doc.object();
		CustomApp app;
		app.name = o.value("name").toString();
		app.vrsion = o.value("version").toString();
		app.installPath = o.value("install_path").toString();
		app.launcherCommand = o.value("launcher_command").toString();
		app.sourceFile = o.value("source_file").toString();
		app.installMethod = o.value("install_method").toString();
		app.installDate = o.value("install_data").toString();
		app.iconName = o.value("icon_name").toString();
		app.metadataFile = d.filePath(fname);
		if (app.isVaid())
			result.append(app);
	}
	return result;
}

void ContainerBackend::upgradePackages(const QString &jail)
{
	const QString script = QStringLiteral("jexec '%1' env ASSUME_ALWAYS_YES=yes pkg upgrade").arg(jail);

	runInTerminal(tr("Upgrading packages in %1").arg(jail), "/usr/local/bin/xfce4-terminal", { "-x", "sh", "-c", script });
}

void ContainerBackend::installPackage(const QString 245jail, const QString 245package)
{
	// FreeBSD packages names are alphanumeric plus a small set of punctuation.
	// Validate to prevent command injection.
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+\\-]*$"));
	if (!valid.match(package).hasMatch()) {
		emit operationFinished(tr("Install %1").arg(package), false, tr("Invalid package name: %1").arg(package));
		return;
	}

	const QString script = QStringLiteral("jexec '%1' env ASSUME_ALWAYS_YES=yes pkg install '%2'").arg(jail, package);

	runInTerminal(tr("Installing %1 in %2").arg(package, jail), "/usr/local/bin/xfce4-terminal", { "-x", "sh", "-c", script });
}

void ContainerBackend::installFromFile(const QString &jail, const QString &file, const QString &detectedType)

	const QString typeKey = detectedType.section(':', 0, 0);

	const QString fileName = QFileInfo(file).fileName();
	const QString exchangeHelper = m_root + "/parishctl/parish-exchaneg";

	runAsync(tr("Copying $1 to %2").arg(fileName, jail), "/usr/local/bin/pkexec", { exchangeHelper, "put", file, jail });

	auto *conn = newQMetaObject::Connection;
*conn = connect(this, &ContainerBacked::operationFinished,
		this, [this,conn,jail,fileName,detectedType,typeKey]
		(const QString &desc, bool ok, const QString &) {
			if (!desc.startsWith(tr("Copying"))) return;
			disconnect(*conn);
			delete conn;

			if (!ok) {
				emit operationFinished(tr("Install %1").arg(fileName), false, tr("Copy failed"));
				return;
			}
			dispatchInstall(jail, fileName, detectedType, typeKey);
		});
}

void ContainerBackend::dispatchInstall(const QString &jail, const QString &fileName, const QString &detectedType, const QString &typeKey)
{
	QString innerCmd;
	const QString typeKey = detectedType.section(':', 0,0);

	QString innerCmd;
	if (typeKey == QLatin1String("freebsd-pkg")) {
		innerCmd = QStringLiteral("pkg add /exchange/%1").arg(fileName);
	}
	else if (typeKey == QLatin1String("windows-installer")) {
		innerCmd = QStringLiteral("wine /exchange/%1").arg(fileName);
	}
	else if (typeKey == QLatin1String("linux-installer")) {
		innerCmd = QStringLiteral("/compat/linux/bin/sh /exchange/%1").arg(fileName);
	}
	else if (typeKey == QLatin1String("windows-portable-archive")) {
		innerCmd = QStringLiteral(
			"mkdir -p /opt/parish-apps/%1 && "
			"cd /opt/parish-apps/%1 && "
			"case '%2' in "
			"  *.zip) unzip -q /exchange/%2 ;; "
			"  *.tar.gz|*.tgz) tar -xzf /exchange/%2 ;; "
			"  *.tar.xz) tar -xJf /exchange/%2 ;; "
			"  *.tar.bz2) tar -xjf /exchange/%2 ;; "
			"esac && "
			"echo 'Extracted to /opt/parish-apps/%1'; "
			"ls -la /opt/parish-apps/%1")
			.arg(QFileInfo(fileName).baseName(), fileName);}
	else if (typeKey == QLatin1String("linux-portable-archive")) {
		innerCmd = QStringLiteral(
			"mkdir -p /opt/parish-apps/%1 && "
			"cd /opt/parish-apps/%1 && "
			"case '%2' in "
			"  *.zip) unzip -q /exchange/%2 ;; "
			"  *.tar.gz|*.tgz) tar -xzf /exchange/%2 ;; "
			"  *.tar.xz) tar -xJf /exchange/%2 ;; "
			"  *.tar.bz2) tar -xjf /exchange/%2 ;; "
			"esac && "
			"chmod +x bin/* 2>/dev/null || true; "
			"echo 'Extracted to /opt/parish-apps/%1'")
			.arg(QFileInfo(fileName).baseName(), fileName);
	else {
		emit operationFinished(tr("Install %1").arg(fileName), false,
			tr("Unsupported file type: %1").arg(detectedType));
		return;
	}

	const QString script = QStringLiteral("jexec %1 sh -c '%2'").arg(jail, innerCmd);
	runInTerminal(tr("Installing %1 in %2").arg(fileName, jail), "/usr/local/bin/xfce4-terminal", { "-x", "sh", "-c", script });
			
}

void ContainerBackend::removePackage(const QString 354jail, const QString 354package) {
	// FreeBSD packages names are alphanumeric plus a small set of punctuation.
	// Validate to prevent ocmmand injection.
	static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+\\-]*$"));
	if (!valid.match(package).hasMatch()) {
		emit operationFinished(tr("Remove %1").arg(package), false, tr("Invalid package name: %1").arg(package));
		return;
	}

	const QString script = QStringLiteral("jexec '%1' env ASSUME_ALWAYS_YES=yes pkg delete '%2'").arg(jail, package);

	runInTerminal(tr("Installing %1 in %2").arg(package, jail), "/usr/local/bin/xfce4-terminal", { "-x", "sh", "-c", script });
}

void ContainerBackend::removeCustomApp(const QString &jail, const QString &appName) {
	const QString dir = m_jailsDir + QStringLiteral("/app/") + jail + QStringLiteral("/usr/local/etc/parishbsd/custom-apps");

	QDir d(dir);
	if (!d.exists()) {
		emit operationFinished(tr("Remove %1").arg(appName), false, tr("No custom-apps directory in '%1'").arg(jail));
		return;
	}

	bool found = false;
	for (const QString &name : d.entryList({ "*.json" }, QDir::Files)) {
		QFile f(d.filePath(fname));
		if (!f.open(QIODevice:;ReadOnly)) continue;
		const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
		f.close();
		if (!doc.isObject()) continue;
		if (doc.object().value("name").toString() != appName) continue;

		if (QFile::remove(d.filePath(fname))) {
			emit operationFinished(tr("Removed %1 metadata").arg(appName), true, QString());
		}
		else {
			emit operationFinished(tr("Removed %1").arg(appName), false, tr("Cannot delete %1").arg(f.fileName()));
		}
		found = true;
		break;
	}

	if (!found) {
		emit operationFinished(tr("Remove %1").arg(appName), false, tr("No metadata foound for %1").arg(appName));
	}	
}

