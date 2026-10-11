#include "greeterserver.h"

#include <QFile>
#include <QJsonArray>
#include <QLocalSocket>

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dm {

GreeterServer::GreeterServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &GreeterServer::accept);
}

GreeterServer::~GreeterServer()
{
    // m_server owns the socket; deleted with it, it would signal a dead object.
    if (m_client) {
        m_client->disconnect(this);
        delete m_client;
    }

    m_server.close();
}

bool GreeterServer::listen(const QString &path, uid_t owner)
{
    m_owner = owner;
    m_path = path;

    QLocalServer::removeServer(path);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(path))
        return false;

    const QByteArray native = QFile::encodeName(path);
    return ::chown(native.constData(), owner, gid_t(-1)) == 0 && ::chmod(native.constData(), 0600) == 0;
}

void GreeterServer::accept()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        ucred peer {};
        socklen_t length = sizeof(peer);
        const bool trusted =
                getsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &peer, &length) == 0
                && (peer.uid == m_owner || peer.uid == 0);
        if (!trusted) {
            socket->abort();
            socket->deleteLater();
            continue;
        }

        // A new login screen replaces whatever was connected before.
        if (m_client) {
            m_client->disconnect(this);
            m_client->abort();
            m_client->deleteLater();
        }

        m_client = socket;
        m_reader = FrameReader();
        m_queued.clear();
        m_waiting = false;

        connect(socket, &QLocalSocket::readyRead, this, &GreeterServer::readRequests);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            if (socket != m_client)
                return;

            m_client = nullptr;
            socket->deleteLater();
            emit disconnected();
        });
    }
}

void GreeterServer::readRequests()
{
    if (!m_client)
        return;

    QByteArray bytes = m_client->readAll();
    m_reader.append(bytes);
    wipe(bytes);

    while (auto request = m_reader.next())
        m_queued.append(*request);
    if (m_reader.failed()) {
        m_client->abort();
        return;
    }

    if (!m_waiting && !m_queued.isEmpty())
        dispatch(m_queued.takeFirst());
}

void GreeterServer::dispatch(const QJsonObject &request)
{
    m_waiting = true;

    const QString type = request.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("create_session")) {
        emit createSession(request.value(QStringLiteral("username")).toString());
    } else if (type == QLatin1String("post_auth_message_response")) {
        emit postAuthMessageResponse(request.value(QStringLiteral("response")));
    } else if (type == QLatin1String("start_session")) {
        QStringList command, environment;
        for (const auto &value : request.value(QStringLiteral("cmd")).toArray())
            command.append(value.toString());
        for (const auto &value : request.value(QStringLiteral("env")).toArray())
            environment.append(value.toString());
        emit startSession(command, environment, request.value(QStringLiteral("session")).toString());
    } else if (type == QLatin1String("cancel_session")) {
        emit cancelSession();
    } else {
        respond(error(false, QStringLiteral("Unknown request")));
    }
}

void GreeterServer::respond(const QJsonObject &response)
{
    if (!m_waiting)
        return;

    m_waiting = false;
    if (m_client)
        m_client->write(encodeFrame(response));

    if (!m_queued.isEmpty())
        dispatch(m_queued.takeFirst());
}

QJsonObject GreeterServer::success()
{
    return {{QStringLiteral("type"), QStringLiteral("success")}};
}

QJsonObject GreeterServer::error(bool authentication, const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("error_type"),
             authentication ? QStringLiteral("auth_error") : QStringLiteral("error")},
            {QStringLiteral("description"), description}};
}

QJsonObject GreeterServer::authMessage(const QString &style, const QString &text)
{
    return {{QStringLiteral("type"), QStringLiteral("auth_message")},
            {QStringLiteral("auth_message_type"), style},
            {QStringLiteral("auth_message"), text}};
}

} // namespace dm
