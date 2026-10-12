#pragma once

#include <QString>
#include <QMetaType>

struct ContainerInfo {
	QString name;
	QString kind;         // Application, Compatibility, File Vault, Database Vault
	QString state;        // running, stopped, unlocked, locked
	QString personality;  // app-xpra, vault-postgres, ...
	QString tier;         // app or vault
	QString templateName; // app@base, ...
	quint64 diskUsage;    // reported by zfs
	bool isVault;
	bool isCompatibility; // apps-compatible
			      //
	ContainerInfo()
		: diskUsage(0), isVault(false), isCompatibility(false) {}

	bool isValid() const { return !name.isEmpty(); }
	bool isRunning() const {
		return state == QLatin1String("running") || state == QLatin1String("unlocked");
	}

	bool isLocked() const {
		return state == QLatin1String("locked");
	}
};

Q_DECLARE_METATYPE(ContainerInfo);

QString formatDiskUsage(quint64 bytes);
