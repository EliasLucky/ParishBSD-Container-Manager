#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>
#include <QStringList>

struct ContainerInfo {
    QString name;
    QString kind;         // Application, Compatibility, File Vault, Database Vault
    QString jailState;    // running, stopped, containing, contained, etc.
    QString keyState;     // locked, unlocked, "" (empty for non-vaults)
    QString sourceTemplate;
    QString tier;         // app, vault, legacy
    QString riskLevel;    // low, medium, high, critical (or empty -> default)
    QStringList capabilities; // gui, audio, compat-linux, compat-windows, vault
    quint64 diskUsage = 0;
    bool isVault = false;
    bool isCompatibility = false;
    bool audioOutput = false;
    bool audioInput = false;
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

    bool hasCapability(const QString &cap) const;
    bool supportsGui() const { return hasCapability(QStringLiteral("gui")) || (!isVault && capabilities.isEmpty()); }
    bool supportsLinuxCompat() const { return hasCapability(QStringLiteral("compat-linux")); }
    bool supportsWindowsCompat() const { return hasCapability(QStringLiteral("compat-windows")); }
    bool supportsAudio() const { return hasCapability(QStringLiteral("audio")); }

    QString normalizedRiskLevel() const;
    QString humanRiskLevel() const;
    QColor riskColor() const;

    QString humanState() const;
    QString humanCapabilities() const;
    QString accessibleText() const;
    QString accessibleDescription() const;
};

Q_DECLARE_METATYPE(ContainerInfo)

QString formatDiskUsage(quint64 bytes);
