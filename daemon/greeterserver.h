#pragma once

#include <QJsonObject>
#include <QList>
#include <QLocalServer>
#include <QPointer>
#include <sys/types.h>

#include "../common/wire.h"

class QLocalSocket;

namespace dm {

// GREETD_SOCK with greetd's protocol; one request is answered at a time.
class GreeterServer : public QObject
{
    Q_OBJECT
public:
    explicit GreeterServer(QObject* parent = nullptr);
    ~GreeterServer() override;

    // Only `owner` may connect; the socket is chowned to it with mode 0600.
    bool listen(const QString& path, uid_t owner);
    QString path() const { return m_path; }

    // Answers the request in flight.
    void respond(const QJsonObject& response);
    static QJsonObject success();
    static QJsonObject error(bool authentication, const QString& description);
    static QJsonObject authMessage(const QString& style, const QString& text);

signals:
    void createSession(const QString& username);
    void postAuthMessageResponse(const QJsonValue& response);
    // `session`, a wayland-sessions id, is our extension; greetd greeters send cmd.
    void startSession(const QStringList& command, const QStringList& environment,
                      const QString& session);
    void cancelSession();
    // The client went away; a new greeter starts from scratch.
    void disconnected();

private:
    void accept();
    void readRequests();
    void dispatch(const QJsonObject& request);

    QLocalServer m_server;
    QPointer<QLocalSocket> m_client;
    FrameReader m_reader;
    QList<QJsonObject> m_queued;
    QString m_path;
    uid_t m_owner = 0;
    bool m_waiting = false;
};

} // namespace dm
