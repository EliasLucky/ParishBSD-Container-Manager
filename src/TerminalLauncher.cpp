#include "TerminalLauncher.h"
#include <QFileInfo>
#include <QObject>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

namespace {

// Builds arguments for a specific terminal binary so that:
// 1. Single-instance / factory daemons do not immediately exit 0.
// 2. Program & arguments are passed via positional parameters ("$@" inside sh)
//    so untrusted strings are never re-parsed by sh.
QStringList buildTerminalArgs(const QString &exePath,
                              const QString &title,
                              const QStringList &execArgv,
                              bool pauseOnExit)
{
    const QString base = QFileInfo(exePath).fileName().toLower();
    QStringList args;

    if (base == QLatin1String("konsole")) {
        // --nofork keeps konsole attached so QProcess tracks session lifetime.
        args << QStringLiteral("--nofork")
             << QStringLiteral("-p") << QStringLiteral("tabtitle=%1").arg(title)
             << QStringLiteral("-e");
    } else if (base == QLatin1String("gnome-terminal")) {
        // --wait blocks until the tab/window closes.
        args << QStringLiteral("--wait")
             << QStringLiteral("--title=%1").arg(title)
             << QStringLiteral("--");
    } else if (base == QLatin1String("mate-terminal")) {
        // --disable-factory prevents delegating to an existing factory process.
        args << QStringLiteral("--disable-factory")
             << QStringLiteral("--title=%1").arg(title)
             << QStringLiteral("--");
    } else if (base == QLatin1String("xfce4-terminal")) {
        // --disable-server keeps the launched process alive for QProcess.
        args << QStringLiteral("--disable-server")
             << QStringLiteral("--title=%1").arg(title)
             << QStringLiteral("-x");
    } else if (base == QLatin1String("alacritty")) {
        args << QStringLiteral("--title") << title
             << QStringLiteral("-e");
    } else if (base == QLatin1String("kitty")) {
        args << QStringLiteral("--title") << title
             << QStringLiteral("--");
    } else if (base == QLatin1String("foot")) {
        args << QStringLiteral("--title") << title;
    } else if (base == QLatin1String("xterm") || base == QLatin1String("uxterm")) {
        args << QStringLiteral("-T") << title
             << QStringLiteral("-e");
    } else {
        // Generic POSIX/xterm-compatible fallback
        args << QStringLiteral("-e");
    }

    if (pauseOnExit) {
        // "$@" expands each argument verbatim with zero shell splitting/injection.
        args << QStringLiteral("sh")
             << QStringLiteral("-c")
             << QStringLiteral("\"$@\"; _rc=$?; printf '\\n[press Enter to close]\\n'; read _dummy; exit $_rc")
             << QStringLiteral("parish-term-wrap")
             << execArgv;
    } else {
        args << execArgv;
    }

    return args;
}

} // namespace

bool TerminalLauncher::findTerminal(const QString &title,
                                    const QStringList &execArgv,
                                    QString &outPath,
                                    QStringList &outArgs,
                                    QString *errorOut,
                                    const QString &preferredTerminal,
                                    bool pauseOnExit)
{
    if (execArgv.isEmpty()) {
        if (errorOut) *errorOut = QObject::tr("No command specified for terminal.");
        return false;
    }

    QStringList candidates;
    if (!preferredTerminal.trimmed().isEmpty()) {
        candidates << preferredTerminal.trimmed();
    }

    QSettings settings;
    const QString savedTerm = settings.value(QStringLiteral("terminal/executable")).toString().trimmed();
    if (!savedTerm.isEmpty()) {
        candidates << savedTerm;
    }

    const QString envTerm = QProcessEnvironment::systemEnvironment().value(QStringLiteral("TERMINAL")).trimmed();
    if (!envTerm.isEmpty()) {
        candidates << envTerm;
    }

    // Standard FreeBSD desktop environment fallback chain
    candidates << QStringLiteral("konsole")
               << QStringLiteral("mate-terminal")
               << QStringLiteral("gnome-terminal")
               << QStringLiteral("xfce4-terminal")
               << QStringLiteral("alacritty")
               << QStringLiteral("kitty")
               << QStringLiteral("foot")
               << QStringLiteral("xterm");

    QString foundPath;
    for (const QString &candidate : candidates) {
        const QString resolved = QStandardPaths::findExecutable(candidate);
        if (!resolved.isEmpty()) {
            foundPath = resolved;
            break;
        }
    }

    if (foundPath.isEmpty()) {
        if (errorOut) {
            *errorOut = QObject::tr("No terminal emulator found. Install konsole, xfce4-terminal, mate-terminal, or xterm, or set one in Settings.");
        }
        return false;
    }

    outPath = foundPath;
    outArgs = buildTerminalArgs(foundPath, title, execArgv, pauseOnExit);
    return true;
}

