#include "ContainerInfo.h"

QString formatDiskUsage(quint64 bytes) {
    static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QString("%1 B").arg(bytes);
    return QString("%1 %2").arg(value, 0, 'f', 2).arg(units[unit]);
}

QString ContainerInfo::humanState() const {
    if (jailState == "running") return "Running";
    if (jailState == "stopped") return "Stopped";
    if (jailState == "contained") return "CONTAINED by Guardian";
    if (jailState == "containment-failed") return "CONTAINMENT FAILED";
    if (jailState == "contained-but-running") return "CONTAINED (running)";
    if (jailState == "stopped-maybe-contained") return "May be contained";
    if (jailState == "containing") return "Containing...";
    if (jailState == "running-other-contained") return "Running (another contained)";
    return "Unknown";
}

QString ContainerInfo::accessibleText() const {
    return QString("%1, %2, %3").arg(name, kind, humanState());
}

QString ContainerInfo::accessibleDescription() const {
    QString desc = QString("%1 is a %2 compartment from template %3. State: %4.")
        .arg(name, kind, sourceTemplate, humanState());
    if (isVault) {
        desc += QString(" Vault is %1.").arg(keyState == "locked" ? "locked" : "unlocked");
    }
    if (diskUsage > 0) {
        desc += QString(" Disk usage: %1.").arg(formatDiskUsage(diskUsage));
    }
    return desc;
}