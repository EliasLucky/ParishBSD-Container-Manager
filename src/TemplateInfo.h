#pragma once
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

struct TemplateInfo {
    QString name;
    QString state;       // ready, building, failed, not-built
    QString tier;        // app, vault, unknown
    QString kind;        // application, file-vault, database-vault
    QString description;
    QString currentBase;
    QStringList clusters;
    int buildSizeMb = 0;
    bool hidden = false;
    bool shipped = false;

    QString osVersion;
    QString osArch;
    QString hostOsVersion;
    QString hostOsArch;
    QString osStatus;
    bool needsRecovery = false;

    bool isValid() const { return !name.isEmpty(); }
    bool isReady() const { return state == QLatin1String("ready"); }

    bool isOsOutdated() const {
        return osStatus == QLatin1String("patch-behind") ||
               osStatus == QLatin1String("minor-behind") ||
               osStatus == QLatin1String("major-behind");
    }

    QString humanState() const {
        if (needsRecovery)                      return QObject::tr("Needs Recovery");
        if (state == QLatin1String("ready"))    return QObject::tr("Ready");
        if (state == QLatin1String("building")) return QObject::tr("Building...");
        if (state == QLatin1String("failed"))   return QObject::tr("Failed");
        return QObject::tr("Not Built");
    }

    QString humanOsStatus() const {
        if (needsRecovery)                          return QObject::tr("Needs recovery (interrupted update)");
        if (osStatus == QLatin1String("current"))   return QObject::tr("Matches host OS");
        if (osStatus == QLatin1String("patch-behind")) return QObject::tr("Security / patch update available");
        if (osStatus == QLatin1String("minor-behind") || osStatus == QLatin1String("release-behind"))
            return QObject::tr("FreeBSD release update available");
        if (osStatus == QLatin1String("major-behind")) return QObject::tr("Major OS upgrade available");
        if (osStatus == QLatin1String("not-built")) return QObject::tr("Not built yet");
        return QObject::tr("Unknown");
    }
};

Q_DECLARE_METATYPE(TemplateInfo)
