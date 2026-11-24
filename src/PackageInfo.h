#pragma once
#include <QString>
#include <QMetaType>

struct PackageInfo {
    enum Kind {
        NativePackage, // from pkg
        CustomApp      // installed from file
    };

    Kind kind = NativePackage;
    QString name;
    QString version;
    QString comment;
    QString latestVersion; // if empty, up to date
    QString origin;        // port origin for native
    QString installDate;   // for custom apps
    QString installMethod; // pkg, local-pkg, wine-msi, linux-elf, archive, etc.
    QString launcherCommand;
    bool requiresGui = false;
    bool requiresWindows = false;
    bool requiresLinux = false;

    bool hasUpdate() const { return !latestVersion.isEmpty(); }
    bool isCustom() const { return kind == CustomApp; }
    bool isValid() const { return !name.isEmpty(); }
};

Q_DECLARE_METATYPE(PackageInfo)
Q_DECLARE_METATYPE(QList<PackageInfo>)
