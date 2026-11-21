#pragma once
#include <QString>
#include <QStringList>

class TerminalLauncher {
public:
    // Discovers the user's default terminal emulator and builds the correct arguments.
    // Does NOT launch the process (the caller must do that to manage the semaphore).
    static bool findTerminal(const QString &title, const QString &command, 
                             QString &outPath, QStringList &outArgs, QString *errorOut);
};