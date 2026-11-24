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

    bool isValid() const { return !name.isEmpty(); }
    bool isReady() const { return state == QLatin1String("ready"); }

    QString humanState() const {
        if (state == QLatin1String("ready"))     return QObject::tr("Ready");
        if (state == QLatin1String("building"))  return QObject::tr("Building...");
        if (state == QLatin1String("failed"))    return QObject::tr("Failed");
        return QObject::tr("Not Built");
    }
};

Q_DECLARE_METATYPE(TemplateInfo)
