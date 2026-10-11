#pragma once

#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

namespace dm {

// The daemon's side of one cutefish-dm-helper: a login, then its session.
class HelperProcess : public QObject
{
    Q_OBJECT
public:
    explicit HelperProcess(QObject *parent = nullptr);
    ~HelperProcess() override;

    void login(const QString &service, const QString &user, bool authenticate, int vt);
    // A string for a prompt, null for an informational message.
    void answer(const QJsonValue &response);
    void start(const QStringList &command, const QStringList &environment);
    // SIGTERM to the helper, which passes it on to its session, or stops the
    // login while none runs yet. A helper still running after the stop timeout
    // is killed, so `finished` always follows.
    void terminate();
    void setStopTimeout(int milliseconds) { m_stopDeadline.setInterval(milliseconds); }

    QString user() const { return m_user; }
    bool isAuthenticated() const { return m_authenticated; }
    bool hasStarted() const { return m_sessionPid > 0; }
    QString sessionId() const { return m_sessionId; }

signals:
    void question(const QString &style, const QString &text);
    void authenticated();
    void started(const QString &sessionId);
    // `authentication`: the credentials were wrong, rather than something broke.
    void failed(bool authentication, const QString &message);
    // The helper exited; after `started`, the session ended and PAM closed it.
    void finished();

private:
    void send(const QJsonObject &message);
    void readMessages();

    QProcess m_process;
    QTimer m_stopDeadline;
    QByteArray m_buffer;
    QString m_user;
    QString m_sessionId;
    qint64 m_sessionPid = 0;
    bool m_authenticated = false;
    bool m_failed = false;
};

} // namespace dm
