#pragma once

#include <QString>
#include <QStringList>

namespace dm {

struct Config {
    // Separate VTs keep a session's end off the screen; VT1 has the boot splash.
    int greeterVt = 1;
    int sessionVt = 2;
    QString greeterUser = QStringLiteral("cutefish-greeter");
    QStringList greeterCommand = {QStringLiteral("/usr/bin/cutefish-compositor"),
                                  QStringLiteral("--login")};
    QString greeterService = QStringLiteral("cutefish-dm-greeter");
    QString sessionService = QStringLiteral("cutefish-dm");
    QStringList sessionDirectories = {QStringLiteral("/usr/local/share/wayland-sessions"),
                                      QStringLiteral("/usr/share/wayland-sessions")};
    QString defaultSession = QStringLiteral("cutefish-wayland");
    QString statePath = QStringLiteral("/var/lib/cutefish-dm/state.conf");
    QString runtimeDirectory = QStringLiteral("/run/cutefish-dm");

    // Defaults, overridden by whatever `path` sets.
    static Config load(const QString &path = QStringLiteral("/etc/cutefish-dm.conf"));
};

} // namespace dm
