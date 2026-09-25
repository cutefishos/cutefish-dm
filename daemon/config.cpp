#include "config.h"

#include <QFileInfo>
#include <QProcess>
#include <QSettings>

namespace dm {

Config Config::load(const QString& path)
{
    Config config;
    if (!QFileInfo::exists(path))
        return config;
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Greeter"));
    config.greeterVt = settings.value(QStringLiteral("Vt"), config.greeterVt).toInt();
    config.greeterUser = settings.value(QStringLiteral("User"), config.greeterUser).toString();
    if (settings.contains(QStringLiteral("Command")))
        config.greeterCommand =
                QProcess::splitCommand(settings.value(QStringLiteral("Command")).toString());
    config.greeterService = settings.value(QStringLiteral("Service"), config.greeterService).toString();
    settings.endGroup();
    settings.beginGroup(QStringLiteral("Session"));
    config.sessionVt = settings.value(QStringLiteral("Vt"), config.sessionVt).toInt();
    config.sessionService = settings.value(QStringLiteral("Service"), config.sessionService).toString();
    if (settings.contains(QStringLiteral("Directories")))
        config.sessionDirectories =
                settings.value(QStringLiteral("Directories")).toString().split(QLatin1Char(':'), Qt::SkipEmptyParts);
    config.defaultSession =
            settings.value(QStringLiteral("Default"), config.defaultSession).toString();
    settings.endGroup();
    settings.beginGroup(QStringLiteral("Daemon"));
    config.runtimeDirectory =
            settings.value(QStringLiteral("RuntimeDirectory"), config.runtimeDirectory).toString();
    config.statePath = settings.value(QStringLiteral("StatePath"), config.statePath).toString();
    settings.endGroup();
    return config;
}

} // namespace dm
