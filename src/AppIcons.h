#pragma once

#include <QIcon>
#include <QString>

namespace AppIcons {

// Returns the bundled icon first for consistent UI appearance across FreeBSD
// desktop environments, with theme lookup fallback if explicitly requested.
inline QIcon bundled(const QString &resourceName, const QString &themeFallback = QString())
{
    const QIcon builtIn(QStringLiteral(":/icons/") + resourceName + QStringLiteral(".png"));
    if (!builtIn.isNull()) {
        return builtIn;
    }
    if (!themeFallback.isEmpty()) {
        return QIcon::fromTheme(themeFallback);
    }
    return QIcon();
}

// Prefers desktop theme icon if present, otherwise uses bundled PNG fallback so
// no toolbar button or menu item ever renders blank on minimal FreeBSD setups.
inline QIcon themedOrBundled(const QString &themeName, const QString &bundledName)
{
    if (!themeName.isEmpty()) {
        const QIcon themed = QIcon::fromTheme(themeName);
        if (!themed.isNull()) {
            return themed;
        }
    }
    return bundled(bundledName);
}

inline QIcon appWindowIcon()
{
    return QIcon(QStringLiteral(":/parishbsd.png"));
}

} // namespace AppIcons
