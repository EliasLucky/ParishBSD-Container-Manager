#include "TerminalLauncher.h"
#include <QStandardPaths>
#include <QProcessEnvironment>
#include <QObject>

bool TerminalLauncher::findTerminal(const QString &title, const QString &command, 
                                    QString &outPath, QStringList &outArgs, QString *errorOut)
{
    QString termPath;
    QStringList args;
    
    // Wrap the command to pause at the end, preserving exit code
    QString wrappedCmd = QStringLiteral("%1; _rc=$?; echo; echo '[press Enter to close]'; read _; exit $_rc").arg(command);

    // 1. Check $TERMINAL environment variable (User preference)
    QString envTerm = QProcessEnvironment::systemEnvironment().value("TERMINAL");
    if (!envTerm.isEmpty()) {
        termPath = QStandardPaths::findExecutable(envTerm);
        if (!termPath.isEmpty()) {
            args << "-e" << "sh" << "-c" << wrappedCmd;
        }
    }

    // 2. Fallback chain (DE-specific standard terminals)
    if (termPath.isEmpty()) {
        struct TermInfo {
            const char* exe;
            bool useTitleEq; // true: --title=T, false: --title T
            const char* execFlag;
        };
        
        // Order matters: KDE, MATE, GNOME, XFCE, generic
        TermInfo terminals[] = {
            {"konsole", false, "-e"},
            {"mate-terminal", true, "--"},
            {"gnome-terminal", true, "--"},
            {"xfce4-terminal", false, "-x"},
            {"alacritty", false, "-e"},
            {"xterm", false, "-e"}
        };

        for (const auto& t : terminals) {
            QString path = QStandardPaths::findExecutable(t.exe);
            if (!path.isEmpty()) {
                termPath = path;
                if (t.useTitleEq) {
                    args << QString("--title=%1").arg(title) << t.execFlag << "sh" << "-c" << wrappedCmd;
                } else {
                    if (QString(t.exe) == "xterm") {
                        args << "-T" << title << t.execFlag << "sh" << "-c" << wrappedCmd;
                    } else {
                        args << "--title" << title << t.execFlag << "sh" << "-c" << wrappedCmd;
                    }
                }
                break;
            }
        }
    }

    if (termPath.isEmpty()) {
        if (errorOut) {
            *errorOut = QObject::tr("No terminal emulator found. Please install one.");
        }
        return false;
    }

    outPath = termPath;
    outArgs = args;
    return true;
}