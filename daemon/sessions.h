#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace dm {

// One entry of wayland-sessions, as sddm and GDM offer it.
struct SessionEntry {
    // The file name without ".desktop", e.g. "cutefish-wayland".
    QString id;
    QString name;
    QString comment;
    QStringList exec;
    QStringList desktopNames;
};

// Hidden, NoDisplay, a missing TryExec or no Exec: not a session.
std::optional<SessionEntry> readSessionFile(const QString& path);

// An id in an earlier directory hides the same id in later ones.
QList<SessionEntry> findSessions(const QStringList& directories);

std::optional<SessionEntry> findSession(const QStringList& directories, const QString& id);

// For pam_systemd, set before pam_open_session (sddm's Display::startAuth).
QStringList sessionEnvironment(const SessionEntry& session, int vt);
QStringList greeterEnvironment(int vt, const QString& socket);

} // namespace dm
