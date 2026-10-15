#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "ContainerInfo.h"

class QProcess;

// For listing containers, ContainerBackend reads the filesystem and
// queries zfs(8) / jail(8) directly. For actions (start, stop, create,
// destroy, unlock, lock), it runs parishctl(1).
class ContainerBackend : public QObject {
	Q_OBJECT
public:
	explicit ContainerBackend(QObject *parent = nullptr);

	QList<ContainerInfo> listContainers(QString *errorOut = nullptr) const;

	QStringList listPersonalities() const;

	QString parishctlPath() const { return m_parishctlPath; }

	// -----------------------------
	
	void startContainer(const QString &name);
	void stopContainer(const QString &name);
	void unlockVault(const QString &name);
	void lockVault(const QString &name);
	void destroyContainer(const QString &name);
	void createContainer(const QString &name, const QString &personality);

	void openConsole(const QString &name);

	void openVaultFiles(const QString &name);

	QList<PackageInfo> listPackages(const QString &jail, QString *errorOut = nullptr) const;
	QList<CustomApp> listCustomApps(const QString &jail) const;
	void updatePackages(const QString &jail);
	void installPackage(const QString &jail, const QString &package);
	void installFromFile(const QString &jail, const QString &file, const QString &detectedType);
	void removePackage(const QString &jail, const QString &package);
	void removeCustomApp(const QString &jail, const QString &appName);
signals:

	void operationStarted(const QString &description);
	void operationFinished(const QString &description, bool success, const QString &errorMessage);
	void terminalLaunched(const QString &description);
	void terminalClosed(const QString &descrpition);

private:
	void runParishctl(const QString &description, const QStringList &args);

	void runAsync(const QString &description, const QString &program, const QStringList &args);

	QString runSync(const QString &program, const QStringList &args, int *exitCode = nullptr) const;

	QString findJailRoot(const QString &name, QString *tierOut = nullptr) const;
	
	QString readPersonality(const QString &jailRoot) const;

	static QString kindFromPersonality(const QString &personality);

	QString m_root; // /usr/local/parishbsd
	QString m_parishctlPath;
	QString m_jailsDir; // /usr/local/jails
	QString m_vaultDataDir; // /zroot/vault-data
};
