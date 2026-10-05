#include "sessions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

namespace dm {

// The escapes the Desktop Entry specification defines for string values.
static QString unescape(const QString& value)
{
    QString result;
    result.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != QLatin1Char('\\') || i + 1 == value.size()) {
            result.append(c);
            continue;
        }
        const QChar next = value.at(++i);
        switch (next.unicode()) {
        case 's': result.append(QLatin1Char(' ')); break;
        case 'n': result.append(QLatin1Char('\n')); break;
        case 't': result.append(QLatin1Char('\t')); break;
        case 'r': result.append(QLatin1Char('\r')); break;
        default: result.append(next); break;
        }
    }
    return result;
}

static bool isTrue(const QString& value)
{
    return value.trimmed() == QLatin1String("true");
}

static bool executableExists(const QString& program)
{
    if (QFileInfo(program).isAbsolute())
        return QFileInfo(program).isExecutable();
    return !QStandardPaths::findExecutable(program).isEmpty();
}

std::optional<SessionEntry> readSessionFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    QHash<QString, QString> keys;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (!inEntry || equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        if (!keys.contains(key))
            keys.insert(key, line.mid(equals + 1).trimmed());
    }

    if (isTrue(keys.value(QStringLiteral("Hidden")))
        || isTrue(keys.value(QStringLiteral("NoDisplay"))))
        return std::nullopt;
    const QString tryExec = unescape(keys.value(QStringLiteral("TryExec")));
    if (!tryExec.isEmpty() && !executableExists(tryExec))
        return std::nullopt;

    SessionEntry entry;
    entry.id = QFileInfo(path).completeBaseName();
    entry.name = unescape(keys.value(QStringLiteral("Name"), entry.id));
    // Field codes have no meaning for a session and are dropped.
    for (const QString& argument : QProcess::splitCommand(unescape(keys.value(QStringLiteral("Exec"))))) {
        if (!(argument.size() == 2 && argument.startsWith(QLatin1Char('%'))))
            entry.exec.append(argument);
    }
    for (const QString& name : keys.value(QStringLiteral("DesktopNames")).split(QLatin1Char(';'))) {
        if (!name.trimmed().isEmpty())
            entry.desktopNames.append(unescape(name.trimmed()));
    }
    if (entry.exec.isEmpty())
        return std::nullopt;
    return entry;
}

QList<SessionEntry> findSessions(const QStringList& directories)
{
    QList<SessionEntry> sessions;
    QSet<QString> seen;
    for (const QString& directory : directories) {
        const auto files = QDir(directory).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files,
                                                         QDir::Name);
        for (const QFileInfo& file : files) {
            const QString id = file.completeBaseName();
            if (seen.contains(id))
                continue;
            // A hidden entry masks the same id further down, as with autostart.
            seen.insert(id);
            if (auto entry = readSessionFile(file.absoluteFilePath()))
                sessions.append(*entry);
        }
    }
    return sessions;
}

std::optional<SessionEntry> findSession(const QStringList& directories, const QString& id)
{
    // An id comes from the greeter; it must name a file, not a path.
    if (id.isEmpty() || id.contains(QLatin1Char('/')) || id.startsWith(QLatin1Char('.')))
        return std::nullopt;
    for (const SessionEntry& session : findSessions(directories)) {
        if (session.id == id)
            return session;
    }
    return std::nullopt;
}

QStringList sessionEnvironment(const SessionEntry& session, int vt)
{
    QStringList environment {
            QStringLiteral("XDG_SESSION_TYPE=wayland"),
            QStringLiteral("XDG_SESSION_CLASS=user"),
            QStringLiteral("XDG_SEAT=seat0"),
            QStringLiteral("XDG_VTNR=%1").arg(vt),
    };
    // A bare command, as a greetd greeter sends, names no session.
    if (!session.id.isEmpty()) {
        environment.append(QStringLiteral("DESKTOP_SESSION=%1").arg(session.id));
        environment.append(QStringLiteral("XDG_SESSION_DESKTOP=%1")
                                   .arg(session.desktopNames.isEmpty() ? session.id
                                                                       : session.desktopNames.first()));
    }
    if (!session.desktopNames.isEmpty())
        environment.append(
                QStringLiteral("XDG_CURRENT_DESKTOP=%1").arg(session.desktopNames.join(QLatin1Char(':'))));
    return environment;
}

QStringList greeterEnvironment(int vt, const QString& socket)
{
    return {
            QStringLiteral("XDG_SESSION_TYPE=wayland"),
            QStringLiteral("XDG_SESSION_CLASS=greeter"),
            QStringLiteral("XDG_SEAT=seat0"),
            QStringLiteral("XDG_VTNR=%1").arg(vt),
            QStringLiteral("GREETD_SOCK=%1").arg(socket),
    };
}

} // namespace dm
