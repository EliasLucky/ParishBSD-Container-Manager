#include "ContainerInfo.h"
#include <QObject>

QString formatDiskUsage(quint64 bytes) {
    static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 2).arg(QLatin1String(units[unit]));
}

bool ContainerInfo::hasCapability(const QString &cap) const {
    return capabilities.contains(cap, Qt::CaseInsensitive);
}

QString ContainerInfo::normalizedRiskLevel() const {
    const QString r = riskLevel.trimmed().toLower();
    if (r == QLatin1String("low") || r == QLatin1String("trusted"))
        return QStringLiteral("low");
    if (r == QLatin1String("medium") || r == QLatin1String("work"))
        return QStringLiteral("medium");
    if (r == QLatin1String("high") || r == QLatin1String("untrusted"))
        return QStringLiteral("high");
    if (r == QLatin1String("critical") || r == QLatin1String("vault"))
        return QStringLiteral("critical");
    return isVault ? QStringLiteral("critical") : QStringLiteral("medium");
}

QString ContainerInfo::humanRiskLevel() const {
    const QString r = normalizedRiskLevel();
    if (r == QLatin1String("low"))      return QObject::tr("Low (Trusted)");
    if (r == QLatin1String("medium"))   return QObject::tr("Medium (Work)");
    if (r == QLatin1String("high"))     return QObject::tr("High (Untrusted)");
    if (r == QLatin1String("critical")) return QObject::tr("Critical (Vault)");
    return QObject::tr("Medium (Work)");
}

QColor ContainerInfo::riskColor() const {
    const QString r = normalizedRiskLevel();
    if (r == QLatin1String("low"))      return QColor(0x2e, 0x7d, 0x32); // Green
    if (r == QLatin1String("medium"))   return QColor(0x1d, 0x4e, 0xd8); // Blue
    if (r == QLatin1String("high"))     return QColor(0xc6, 0x28, 0x28); // Red
    return QColor(0x6a, 0x1b, 0x9a);                                     // Purple (Vault/Critical)
}

QString ContainerInfo::humanState() const {
    if (jailState == QLatin1String("running"))                 return QObject::tr("Running");
    if (jailState == QLatin1String("stopped"))                 return QObject::tr("Stopped");
    if (jailState == QLatin1String("contained"))               return QObject::tr("CONTAINED by Guardian");
    if (jailState == QLatin1String("containment-failed"))      return QObject::tr("CONTAINMENT FAILED");
    if (jailState == QLatin1String("contained-but-running"))   return QObject::tr("CONTAINED (running)");
    if (jailState == QLatin1String("stopped-maybe-contained")) return QObject::tr("May be contained");
    if (jailState == QLatin1String("containing"))              return QObject::tr("Containing...");
    if (jailState == QLatin1String("running-other-contained")) return QObject::tr("Running (another contained)");
    return QObject::tr("Unknown");
}

QString ContainerInfo::humanCapabilities() const {
    if (capabilities.isEmpty()) {
        return isVault ? QObject::tr("Encrypted storage") : QObject::tr("Standard apps");
    }
    QStringList friendly;
    for (const QString &cap : capabilities) {
        if (cap == QLatin1String("gui")) {
            friendly << QObject::tr("Graphical apps");
        } else if (cap == QLatin1String("audio")) {
            friendly << QObject::tr("Sound");
        } else if (cap == QLatin1String("compat-windows")) {
            friendly << QObject::tr("Windows apps");
        } else if (cap == QLatin1String("compat-linux")) {
            friendly << QObject::tr("Linux apps");
        } else if (cap == QLatin1String("vault")) {
            friendly << QObject::tr("Encrypted storage");
        } else {
            friendly << cap;
        }
    }
    return friendly.join(QStringLiteral(", "));
}

QString ContainerInfo::accessibleText() const {
    return QObject::tr("%1, %2, %3, risk level %4")
        .arg(name, kind, humanState(), humanRiskLevel());
}

QString ContainerInfo::accessibleDescription() const {
    QString desc = QObject::tr("%1 is a %2 compartment based on template %3. Status: %4. Risk level: %5.")
        .arg(name, kind, sourceTemplate, humanState(), humanRiskLevel());
    if (isVault) {
        desc += QLatin1Char(' ') + (keyState == QLatin1String("locked")
            ? QObject::tr("Vault is locked.")
            : QObject::tr("Vault is unlocked."));
    } else {
        desc += QLatin1Char(' ') + QObject::tr("Sound output: %1. Microphone: %2.")
            .arg(audioOutput ? QObject::tr("enabled") : QObject::tr("disabled"),
                 audioInput ? QObject::tr("enabled") : QObject::tr("disabled"));
    }
    desc += QLatin1Char(' ') + QObject::tr("Features: %1.").arg(humanCapabilities());
    if (diskUsage > 0) {
        desc += QLatin1Char(' ') + QObject::tr("Space used: %1.").arg(formatDiskUsage(diskUsage));
    }
    return desc;
}
