#include "daemon.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDir>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>

#include <pwd.h>

#include "sessions.h"
#include "terminal.h"

namespace dm {

namespace {

// How long the login screen or a session may take to open its display.
constexpr int kControlTimeoutMs = 10000;
constexpr int kControlPollMs = 20;
// How long the login screen outlives the switch to a session: the kernel takes
// its cursor down with it, and the session's replaces it once its displays settle.
constexpr int kGreeterLingerMs = 3000;

std::optional<uid_t> uidOf(const QString& user)
{
    const passwd* entry = getpwnam(user.toLocal8Bit().constData());
    if (!entry)
        return std::nullopt;
    return entry->pw_uid;
}

// As GDM does: plymouth gives up the display with its splash left on screen.
bool bootSplashRunning()
{
    const QString plymouth = QStandardPaths::findExecutable(QStringLiteral("plymouth"));
    return !plymouth.isEmpty() && QProcess::execute(plymouth, {QStringLiteral("--ping")}) == 0;
}

} // namespace

Daemon::Daemon(Config config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_state(m_config.statePath)
{
    m_greeterRestart.setSingleShot(true);
    connect(&m_greeterRestart, &QTimer::timeout, this, &Daemon::startGreeter);
    connect(&m_server, &GreeterServer::createSession, this, &Daemon::createSession);
    connect(&m_server, &GreeterServer::postAuthMessageResponse, this,
            &Daemon::postAuthMessageResponse);
    connect(&m_server, &GreeterServer::startSession, this, &Daemon::startSession);
    connect(&m_server, &GreeterServer::cancelSession, this, &Daemon::cancelSession);
    connect(&m_server, &GreeterServer::disconnected, this, [this] {
        if (m_login && !m_login->hasStarted())
            release(m_login);
    });
}

Daemon::~Daemon()
{
    m_stopping = true;
    replySwitchRequests(false);
    m_login.reset();
    m_session.reset();
    m_greeter.reset();
    restoreTextMode(m_config.sessionVt);
    restoreTextMode(m_config.greeterVt);
}

bool Daemon::start()
{
    const auto greeter = uidOf(m_config.greeterUser);
    if (!greeter) {
        qCritical() << "No login screen user" << m_config.greeterUser;
        return false;
    }
    m_greeterUid = *greeter;
    QDir().mkpath(m_config.runtimeDirectory);
    const QString socket = m_config.runtimeDirectory + QStringLiteral("/greeter.sock");
    if (!m_server.listen(socket, m_greeterUid)) {
        qCritical() << "Cannot listen on" << socket;
        return false;
    }
    auto bus = QDBusConnection::systemBus();
    if (!bus.registerService(QStringLiteral("com.cutefish.DisplayManager"))
        || !bus.registerObject(QStringLiteral("/com/cutefish/DisplayManager"), this,
                               QDBusConnection::ExportScriptableSlots))
        qWarning() << "Cannot register com.cutefish.DisplayManager; sessions will end on screen";
    if (bootSplashRunning()) {
        m_bootSplash = true;
        QProcess::execute(QStringLiteral("plymouth"), {QStringLiteral("deactivate")});
    }
    // A run that ended mid-login may have left it in KD_GRAPHICS with nobody on it.
    restoreTextMode(m_config.sessionVt);
    startGreeter();
    return true;
}

void Daemon::startGreeter()
{
    if (m_greeter || m_stopping)
        return;
    // Until the compositor draws, VT1 keeps the previous frame, not the console.
    setGraphicsMode(m_config.greeterVt);
    m_greeterExpected = false;
    m_greeterSpent = false;
    m_greeter = std::make_unique<HelperProcess>();
    connect(m_greeter.get(), &HelperProcess::authenticated, this, [this] {
        m_greeter->start(m_config.greeterCommand,
                         greeterEnvironment(m_config.greeterVt, m_server.path()));
    });
    connect(m_greeter.get(), &HelperProcess::started, this, [this](const QString& id) {
        m_greeterSessionId = id;
        if (std::exchange(m_bootSplash, false))
            QProcess::startDetached(QStringLiteral("plymouth"),
                                    {QStringLiteral("quit"), QStringLiteral("--retain-splash")});
        showGreeter();
    });
    connect(m_greeter.get(), &HelperProcess::failed, this, [](bool, const QString& message) {
        qWarning() << "Login screen:" << message;
    });
    connect(m_greeter.get(), &HelperProcess::finished, this, &Daemon::onGreeterFinished);
    m_greeter->login(m_config.greeterService, m_config.greeterUser, false, m_config.greeterVt);
}

void Daemon::showGreeter()
{
    if (!m_greeter || !m_greeter->hasStarted())
        return;
    // Created on the VT in front, as at boot, the login screen is active already.
    if (activeVt() == m_config.greeterVt) {
        replySwitchRequests(true);
        return;
    }
    HelperProcess* greeter = m_greeter.get();
    whenControlled(greeter, m_config.greeterVt, [this, greeter](bool controlled) {
        if (m_greeter.get() != greeter)
            return;
        if (controlled && activateSession(m_greeterSessionId)) {
            replySwitchRequests(true);
            return;
        }
        // Never switch to a VT nobody took; restart the login screen instead.
        qWarning() << "Cannot switch to the login screen on VT" << m_config.greeterVt;
        greeter->terminate();
    });
}

void Daemon::stopGreeter()
{
    m_greeterExpected = true;
    if (m_greeter)
        m_greeter->terminate();
}

void Daemon::onGreeterFinished()
{
    release(m_greeter);
    m_greeterSessionId.clear();
    restoreTextMode(m_config.greeterVt);
    if (m_stopping)
        return;
    if (std::exchange(m_restartGreeter, false)) {
        startGreeter();
        return;
    }
    if (m_greeterExpected) {
        m_greeterFailures = 0;
        return;
    }
    // Restart a login screen that died, with backoff, unless a session is up.
    replySwitchRequests(false);
    if (m_session)
        return;
    ++m_greeterFailures;
    m_greeterRestart.start(qMin(m_greeterFailures, 5) * 1000);
}

void Daemon::replySwitchRequests(bool switched)
{
    auto bus = QDBusConnection::systemBus();
    for (const QDBusMessage& request : std::exchange(m_switchRequests, {})) {
        bus.send(switched ? request.createReply()
                          : request.createErrorReply(QDBusError::Failed,
                                                     QStringLiteral("The login screen is not up")));
    }
}

void Daemon::SwitchToGreeter(const QDBusMessage& message)
{
    auto bus = QDBusConnection::systemBus();
    // A caller that is gone before it is looked up is refused, not taken for root.
    const QDBusReply<uint> caller = bus.interface()->serviceUid(message.service());
    const auto owner = m_session ? uidOf(m_session->user()) : std::nullopt;
    if (!caller.isValid() || (caller.value() != 0 && (!owner || caller.value() != *owner))) {
        bus.send(message.createErrorReply(QDBusError::AccessDenied,
                                          QStringLiteral("Only the session's own user can end it")));
        return;
    }
    message.setDelayedReply(true);
    m_switchRequests.append(message);
    // A login screen that already handed a login over is replaced, not shown.
    if (m_greeter && m_greeterSpent) {
        m_restartGreeter = true;
        stopGreeter();
    } else if (m_greeter) {
        showGreeter();
    } else {
        startGreeter();
    }
}

void Daemon::createSession(const QString& username)
{
    if (m_login)
        release(m_login);
    m_login = std::make_unique<HelperProcess>();
    auto* login = m_login.get();
    connect(login, &HelperProcess::question, this, [this](const QString& style, const QString& text) {
        m_server.respond(GreeterServer::authMessage(style, text));
    });
    connect(login, &HelperProcess::authenticated, this, [this] {
        m_server.respond(GreeterServer::success());
    });
    connect(login, &HelperProcess::failed, this, [this, login](bool authentication,
                                                             const QString& message) {
        m_server.respond(GreeterServer::error(authentication, message));
        if (m_login.get() == login)
            release(m_login);
    });
    connect(login, &HelperProcess::finished, this, [this, login] {
        if (m_login.get() == login)
            release(m_login);
    });
    login->login(m_config.sessionService, username, true, m_config.sessionVt);
}

void Daemon::postAuthMessageResponse(const QJsonValue& response)
{
    if (!m_login)
        m_server.respond(GreeterServer::error(false, QStringLiteral("No session is being created")));
    else
        m_login->answer(response);
}

void Daemon::startSession(const QStringList& command, const QStringList& environment,
                          const QString& session)
{
    if (!m_login || !m_login->isAuthenticated()) {
        m_server.respond(GreeterServer::error(false, QStringLiteral("Not authenticated")));
        return;
    }
    PendingStart start;
    // The client's variables first: the session's own, set after, win.
    start.environment = environment;
    if (!session.isEmpty()) {
        const auto entry = findSession(m_config.sessionDirectories, session);
        if (!entry) {
            m_server.respond(GreeterServer::error(false, QStringLiteral("No such session")));
            return;
        }
        start.command = QStringList {QStringLiteral(DM_SESSION_WRAPPER)} + entry->exec;
        start.environment += sessionEnvironment(*entry, m_config.sessionVt);
        m_state.remember(m_login->user(), session);
    } else if (!command.isEmpty()) {
        start.command = QStringList {QStringLiteral(DM_SESSION_WRAPPER)} + command;
        start.environment += sessionEnvironment(SessionEntry {}, m_config.sessionVt);
    } else {
        m_server.respond(GreeterServer::error(false, QStringLiteral("Nothing to start")));
        return;
    }
    m_server.respond(GreeterServer::success());

    // The previous session is still ending; the next one follows on its VT.
    if (m_session) {
        m_pendingStart = std::move(start);
        m_session->terminate();
        return;
    }
    launchSession(std::move(start));
}

void Daemon::cancelSession()
{
    if (m_login && !m_login->hasStarted())
        release(m_login);
    m_server.respond(GreeterServer::success());
}

void Daemon::launchSession(PendingStart start)
{
    if (!m_login)
        return;
    setGraphicsMode(m_config.sessionVt);
    m_greeterSpent = true;
    m_session = std::move(m_login);
    HelperProcess* session = m_session.get();
    connect(session, &HelperProcess::started, this, [this, session](const QString& id) {
        whenControlled(session, m_config.sessionVt, [this, session, id](bool controlled) {
            if (m_session.get() != session)
                return;
            // Never switch to a VT nobody took; end the session instead.
            if (!controlled || !activateSession(id)) {
                qWarning() << "Cannot switch to the session on VT" << m_config.sessionVt
                           << "- ending it";
                session->terminate();
                return;
            }
            QTimer::singleShot(kGreeterLingerMs, this,
                               [this, greeter = QPointer<HelperProcess>(m_greeter.get())] {
                                   if (greeter && greeter == m_greeter.get())
                                       stopGreeter();
                               });
        });
    });
    connect(m_session.get(), &HelperProcess::finished, this, &Daemon::onSessionFinished);
    m_session->start(start.command, start.environment);
}

void Daemon::onSessionFinished()
{
    release(m_session);
    restoreTextMode(m_config.sessionVt);
    if (m_stopping)
        return;
    // A login screen that went away meanwhile took its login along.
    if (auto pending = std::exchange(m_pendingStart, std::nullopt); pending && m_login) {
        launchSession(std::move(*pending));
        return;
    }
    // A session that failed before it was shown leaves a spent login screen.
    if (m_greeter && m_greeterSpent) {
        m_restartGreeter = true;
        stopGreeter();
        return;
    }
    // A crash, or another desktop's own logout: the login screen comes back.
    if (m_greeter)
        showGreeter();
    else
        startGreeter();
}

void Daemon::whenControlled(HelperProcess* helper, int vt, std::function<void(bool)> then)
{
    // The helper's child, so the wait ends with it.
    auto* timer = new QTimer(helper);
    auto waited = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [timer, waited, vt, then = std::move(then)] {
        *waited += kControlPollMs;
        const bool controlled = isControlled(vt);
        if (!controlled && *waited < kControlTimeoutMs)
            return;
        timer->stop();
        timer->deleteLater();
        then(controlled);
    });
    timer->start(kControlPollMs);
}

void Daemon::release(std::unique_ptr<HelperProcess>& helper)
{
    if (!helper)
        return;
    helper->disconnect();
    helper.release()->deleteLater();
}

} // namespace dm
