#pragma once
#include <QString>
#include <QStringList>

class TerminalLauncher {
public:
    // Discovers the user's default terminal emulator (respecting optional
    // Settings override, then $TERMINAL, then standard DE terminals) and builds
    // safe foreground arguments so QProcess tracks the real lifetime of the window.
    // Does NOT launch the process (the caller manages the command queue).
    static bool findTerminal(const QString &title,
                             const QStringList &execArgv,
                             QString &outPath,
                             QStringList &outArgs,
                             QString *errorOut,
                             const QString &preferredTerminal = QString(),
                             bool pauseOnExit = true);
};
