#pragma once
#include <QString>
#include <QMetaType>

struct TemplateInfo {
    QString name;
    QString state;       // ready, building, failed, not-built
    QString tier;        // app, vault, unknown
    QString kind;        // application, file-vault, database-vault
    QString description;
    QString currentBase;
    int buildSizeMb = 0;
    bool hidden = false;
    bool shipped = false;

    bool isValid() const { return !name.isEmpty(); }
    bool isReady() const { return state == QLatin1String("ready"); }
    
    QString humanState() const {
        if (state == "ready") return "Ready";
        if (state == "building") return "Building...";
        if (state == "failed") return "Failed";
        return "Not Built";
    }
};
Q_DECLARE_METATYPE(TemplateInfo)