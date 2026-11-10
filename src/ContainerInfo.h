#pragma once

#include <QString>
#include <QMetaType>

struct ContainerInfo {
    QString name;
    QString kind;         // Application, Compatibility, File Vault, Database Vault
    QString jailState;    // running, stopped, containing, contained, etc.
    QString keyState;     // locked, unlocked, "" (empty for non-vaults)
    QString sourceTemplate;
    QString tier;         // app, vault, legacy
    quint64 diskUsage = 0;
    bool isVault = false;
    bool isCompatibility = false;
    QString path;         // /usr/local/jails/containers/<name>
    QString internalIp;

    bool isValid() const { return !name.isEmpty(); }
    
    bool isRunning() const {
        return jailState == QLatin1String("running") ||
               jailState == QLatin1String("contained-but-running") ||
               jailState == QLatin1String("running-other-contained");
    }

    bool isLocked() const {
        return keyState == QLatin1String("locked");
    }

    bool isContained() const {
        return jailState == QLatin1String("contained") ||
               jailState == QLatin1String("containment-failed") ||
               jailState == QLatin1String("contained-but-running") ||
               jailState == QLatin1String("stopped-maybe-contained") ||
               jailState == QLatin1String("containing");
    }

    QString humanState() const;
    QString accessibleText() const;
    QString accessibleDescription() const;
};

Q_DECLARE_METATYPE(ContainerInfo)

QString formatDiskUsage(quint64 bytes);
