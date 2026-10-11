#include "state.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace dm {

State::State(QString path)
    : m_path(std::move(path))
{
}

void State::remember(const QString &user, const QString &session)
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
