#pragma once

#include <QString>

namespace dm {

// Who logged in last and with which session, kept for the login screen to
// preselect. The file is world-readable: the greeter reads it as its own user.
class State
{
public:
    explicit State(QString path);

    void remember(const QString& user, const QString& session);

private:
    QString m_path;
};

} // namespace dm
