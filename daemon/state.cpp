#include "state.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace dm {

State::State(QString path)
    : m_path(std::move(path))
{
}

QString State::lastUser() const
{
    return QSettings(m_path, QSettings::IniFormat).value(QStringLiteral("Last/User")).toString();
}

QString State::lastSession(const QString& user) const
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Sessions"));
    return settings.value(user).toString();
}

void State::remember(const QString& user, const QString& session)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Last/User"), user);
    settings.beginGroup(QStringLiteral("Sessions"));
    settings.setValue(user, session);
    settings.endGroup();
    settings.sync();
}

} // namespace dm
