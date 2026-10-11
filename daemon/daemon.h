#pragma once

#include <QDBusMessage>
#include <QList>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

#include "config.h"
#include "greeterserver.h"
#include "helperprocess.h"
#include "state.h"

namespace dm {

// Runs the login screen and the session on VTs of their own; see README.md.
class Daemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.DisplayManager")
public:
    explicit Daemon(Config config, QObject *parent = nullptr);
    ~Daemon() override;

    bool start();

public Q_SLOTS:
    // Called by the session before it ends; replies once the login screen is up.
    Q_SCRIPTABLE void SwitchToGreeter(const QDBusMessage &message);

private:
    struct PendingStart {
        QStringList command;
        QStringList environment;
    };

    void startGreeter();
    void showGreeter();
    void stopGreeter();
    void returnToGreeter();
    void onGreeterFinished();
    void replySwitchRequests(bool switched);

    void createSession(const QString &username);
    void postAuthMessageResponse(const QJsonValue &response);
    void startSession(const QStringList &command, const QStringList &environment,
                      const QString &session);
    void cancelSession();
    void launchSession(PendingStart start);
    void onSessionFinished();

    // then(true) once logind has taken `vt` for the helper's session, then(false)
    // after a timeout; nothing runs once the helper is released.
    void whenControlled(HelperProcess *helper, int vt, std::function<void(bool)> then);
    // HelperProcess objects end in their own signals; they are released later.
    static void release(std::unique_ptr<HelperProcess> &helper);

    Config m_config;
    State m_state;
    GreeterServer m_server;
    std::unique_ptr<HelperProcess> m_greeter;
    std::unique_ptr<HelperProcess> m_login;
    std::unique_ptr<HelperProcess> m_session;
    std::optional<PendingStart> m_pendingStart;
    QList<QDBusMessage> m_switchRequests;
    QTimer m_greeterRestart;
    int m_greeterFailures = 0;
    bool m_greeterExpected = false;
    // The login screen has handed a login over and cannot take another.
    bool m_greeterSpent = false;
    bool m_restartGreeter = false;
    bool m_bootSplash = false;
    bool m_stopping = false;
};

} // namespace dm
