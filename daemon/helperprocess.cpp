#include "helperprocess.h"

#include <QJsonArray>
#include <QJsonObject>

#include "../common/wire.h"

namespace dm {

namespace {

QString helperPath()
{
    const QString overridden = qEnvironmentVariable("CUTEFISH_DM_HELPER");
    return overridden.isEmpty() ? QStringLiteral(DM_HELPER_PATH) : overridden;
}

} // namespace

HelperProcess::HelperProcess(QObject* parent)
    : QObject(parent)
{
    m_process.setProgram(helperPath());
    // The session's output shares the helper's stderr: the daemon's journal.
    m_process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &HelperProcess::readMessages);
    connect(&m_process, &QProcess::finished, this, [this] {
        readMessages();
        if (!m_authenticated && !m_failed) {
            m_failed = true;
            emit failed(false, QStringLiteral("The login helper exited"));
        }
        emit finished();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_failed = true;
            emit failed(false, QStringLiteral("The login helper did not start"));
            emit finished();
        }
    });
}

HelperProcess::~HelperProcess()
{
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        terminate();
        if (!m_process.waitForFinished(3000))
            m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void HelperProcess::login(const QString& service, const QString& user, bool authenticate, int vt)
{
    m_user = user;
    m_process.start();
    send({{QStringLiteral("type"), QStringLiteral("login")},
          {QStringLiteral("service"), service},
          {QStringLiteral("user"), user},
          {QStringLiteral("authenticate"), authenticate},
          {QStringLiteral("vt"), vt}});
}

void HelperProcess::answer(const QJsonValue& response)
{
    send({{QStringLiteral("type"), QStringLiteral("answer")},
          {QStringLiteral("response"), response}});
}

void HelperProcess::start(const QStringList& command, const QStringList& environment)
{
    send({{QStringLiteral("type"), QStringLiteral("start")},
          {QStringLiteral("cmd"), QJsonArray::fromStringList(command)},
          {QStringLiteral("env"), QJsonArray::fromStringList(environment)}});
}

void HelperProcess::terminate()
{
    // Never the session's pid itself: once reaped, another process may have it.
    if (m_process.state() != QProcess::NotRunning)
        m_process.terminate();
}

void HelperProcess::send(const QJsonObject& message)
{
    QByteArray line = encodeLine(message);
    m_process.write(line);
    wipe(line);
}

void HelperProcess::readMessages()
{
    m_buffer.append(m_process.readAllStandardOutput());
    qsizetype end = 0;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const auto message = decodeLine(m_buffer.left(end));
        m_buffer.remove(0, end + 1);
        if (!message)
            continue;
        const QString type = message->value(QStringLiteral("type")).toString();
        if (type == QLatin1String("question")) {
            emit question(message->value(QStringLiteral("style")).toString(),
                          message->value(QStringLiteral("text")).toString());
        } else if (type == QLatin1String("authenticated")) {
            m_authenticated = true;
            emit authenticated();
        } else if (type == QLatin1String("started")) {
            m_sessionPid = message->value(QStringLiteral("pid")).toInteger();
            m_sessionId = message->value(QStringLiteral("session")).toString();
            emit started(m_sessionId);
        } else if (type == QLatin1String("error")) {
            m_failed = true;
            emit failed(message->value(QStringLiteral("auth")).toBool(),
                        message->value(QStringLiteral("message")).toString());
        } else if (type == QLatin1String("finished")) {
            m_sessionPid = 0;
        }
    }
}

} // namespace dm
