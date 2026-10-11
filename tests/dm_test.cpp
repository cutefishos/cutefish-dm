#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QLocalSocket>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <unistd.h>

#include "common/wire.h"
#include "daemon/config.h"
#include "daemon/greeterserver.h"
#include "daemon/helperprocess.h"
#include "daemon/sessions.h"
#include "daemon/state.h"

// When set, the test binary is the helper: it accepts "right" and its session
// runs until terminated, or, set to "stubborn", ignores SIGTERM.
static constexpr char kFakeHelper[] = "DM_TEST_FAKE_HELPER";

static QByteArray readLine()
{
    QByteArray line;
    char c = 0;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n')
        line.append(c);
    return line;
}

static void writeLine(const QJsonObject &message)
{
    const QByteArray bytes = dm::encodeLine(message);
    if (write(STDOUT_FILENO, bytes.constData(), size_t(bytes.size())) != bytes.size())
        _exit(3);
}

static int fakeHelper()
{
    const auto login = dm::decodeLine(readLine());
    if (!login || login->value(QStringLiteral("user")).toString() != QLatin1String("alice"))
        return 2;

    writeLine({{QStringLiteral("type"), QStringLiteral("question")},
               {QStringLiteral("style"), QStringLiteral("secret")},
               {QStringLiteral("text"), QStringLiteral("Password:")}});
    const auto answer = dm::decodeLine(readLine());
    if (!answer || answer->value(QStringLiteral("response")).toString() != QLatin1String("right")) {
        writeLine({{QStringLiteral("type"), QStringLiteral("error")},
                   {QStringLiteral("auth"), true},
                   {QStringLiteral("message"), QStringLiteral("Authentication failure")}});
        return 1;
    }

    writeLine({{QStringLiteral("type"), QStringLiteral("authenticated")}});

    const auto start = dm::decodeLine(readLine());
    if (!start || start->value(QStringLiteral("cmd")).toArray().isEmpty())
        return 0;

    if (qgetenv(kFakeHelper) == "stubborn")
        signal(SIGTERM, SIG_IGN);

    writeLine({{QStringLiteral("type"), QStringLiteral("started")},
               {QStringLiteral("pid"), qint64(getpid())},
               {QStringLiteral("session"), QStringLiteral("42")}});

    for (;;)
        pause();
}

static QByteArray frame(const QJsonObject &message)
{
    return dm::encodeFrame(message);
}

static void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

class DisplayManagerTest : public QObject
{
    Q_OBJECT
private slots:

    void framesSurviveSplitsAndBatches()
    {
        const QByteArray first = frame({{QStringLiteral("type"), QStringLiteral("success")}});
        const QByteArray second = frame({{QStringLiteral("type"), QStringLiteral("cancel_session")}});
        dm::FrameReader reader;
        reader.append(first.left(3));
        QVERIFY(!reader.next());
        reader.append(first.mid(3) + second);
        const auto a = reader.next();
        const auto b = reader.next();
        QVERIFY(a && b);
        QCOMPARE(a->value(QStringLiteral("type")).toString(), QStringLiteral("success"));
        QCOMPARE(b->value(QStringLiteral("type")).toString(), QStringLiteral("cancel_session"));
        QVERIFY(!reader.next());
        QVERIFY(!reader.failed());
    }

    void oversizedOrMalformedFramesFailTheReader()
    {
        dm::FrameReader oversized;
        const quint32 length = dm::kMaxFrameSize + 1;
        oversized.append(QByteArray(reinterpret_cast<const char *>(&length), sizeof(length)));
        QVERIFY(!oversized.next());
        QVERIFY(oversized.failed());

        dm::FrameReader malformed;
        const QByteArray payload("[1,2]");
        const quint32 size = quint32(payload.size());
        malformed.append(QByteArray(reinterpret_cast<const char *>(&size), sizeof(size)) + payload);
        QVERIFY(!malformed.next());
        QVERIFY(malformed.failed());
    }

    void linesRoundTrip()
    {
        const QJsonObject message {{QStringLiteral("type"), QStringLiteral("answer")},
                                   {QStringLiteral("response"), QJsonValue()}};
        const QByteArray line = dm::encodeLine(message);
        QVERIFY(line.endsWith('\n'));
        QCOMPARE(line.count('\n'), 1);
        QCOMPARE(*dm::decodeLine(line), message);
        QVERIFY(!dm::decodeLine("not json"));
    }

    void sessionFilesParse()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath(QStringLiteral("plasma.desktop")),
                  "[Desktop Entry]\n"
                  "# a comment\n"
                  "Name=Plasma (Wayland)\n"
                  "Comment=Plasma by KDE\n"
                  "Exec=/usr/bin/startplasma-wayland --arg \"two words\" %f\n"
                  "DesktopNames=KDE;Plasma;\n"
                  "[Other Group]\n"
                  "Exec=ignored\n");
        const auto entry = dm::readSessionFile(directory.filePath(QStringLiteral("plasma.desktop")));
        QVERIFY(entry);
        QCOMPARE(entry->id, QStringLiteral("plasma"));
        QCOMPARE(entry->name, QStringLiteral("Plasma (Wayland)"));
        QCOMPARE(entry->exec, (QStringList {QStringLiteral("/usr/bin/startplasma-wayland"),
                                            QStringLiteral("--arg"), QStringLiteral("two words")}));
        QCOMPARE(entry->desktopNames, (QStringList {QStringLiteral("KDE"), QStringLiteral("Plasma")}));
    }

    void hiddenMissingOrCommandlessSessionsAreNotOffered()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath(QStringLiteral("hidden.desktop")),
                  "[Desktop Entry]\nName=Hidden\nExec=/bin/true\nHidden=true\n");
        writeFile(directory.filePath(QStringLiteral("nodisplay.desktop")),
                  "[Desktop Entry]\nName=NoDisplay\nExec=/bin/true\nNoDisplay=true\n");
        writeFile(directory.filePath(QStringLiteral("missing.desktop")),
                  "[Desktop Entry]\nName=Missing\nExec=/bin/true\nTryExec=/nonexistent/program\n");
        writeFile(directory.filePath(QStringLiteral("noexec.desktop")),
                  "[Desktop Entry]\nName=NoExec\n");
        writeFile(directory.filePath(QStringLiteral("good.desktop")),
                  "[Desktop Entry]\nName=Good\nExec=/bin/true\nTryExec=true\n");
        const auto sessions = dm::findSessions({directory.path()});
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first().id, QStringLiteral("good"));
    }

    void earlierDirectoriesWinAndIdsNameFilesOnly()
    {
        QTemporaryDir local, system;
        writeFile(local.filePath(QStringLiteral("cutefish.desktop")),
                  "[Desktop Entry]\nName=Local\nExec=/bin/true\n");
        writeFile(system.filePath(QStringLiteral("cutefish.desktop")),
                  "[Desktop Entry]\nName=System\nExec=/bin/false\n");
        writeFile(local.filePath(QStringLiteral("gnome.desktop")),
                  "[Desktop Entry]\nName=GNOME\nExec=/bin/true\nHidden=true\n");
        writeFile(system.filePath(QStringLiteral("gnome.desktop")),
                  "[Desktop Entry]\nName=GNOME\nExec=/bin/true\n");
        const QStringList directories {local.path(), system.path()};
        QCOMPARE(dm::findSession(directories, QStringLiteral("cutefish"))->name,
                 QStringLiteral("Local"));
        // A hidden local entry masks the system one.
        QVERIFY(!dm::findSession(directories, QStringLiteral("gnome")));
        QVERIFY(!dm::findSession(directories, QStringLiteral("../cutefish")));
        QVERIFY(!dm::findSession(directories, QStringLiteral(".hidden")));
        QVERIFY(!dm::findSession(directories, QString()));
    }

    void environmentsCarryWhatPamSystemdReads()
    {
        dm::SessionEntry gnome;
        gnome.id = QStringLiteral("gnome");
        gnome.desktopNames = {QStringLiteral("GNOME")};
        const QStringList session = dm::sessionEnvironment(gnome, 2);
        QVERIFY(session.contains(QStringLiteral("XDG_SESSION_TYPE=wayland")));
        QVERIFY(session.contains(QStringLiteral("XDG_SESSION_CLASS=user")));
        QVERIFY(session.contains(QStringLiteral("XDG_SEAT=seat0")));
        QVERIFY(session.contains(QStringLiteral("XDG_VTNR=2")));
        QVERIFY(session.contains(QStringLiteral("DESKTOP_SESSION=gnome")));
        QVERIFY(session.contains(QStringLiteral("XDG_SESSION_DESKTOP=GNOME")));
        QVERIFY(session.contains(QStringLiteral("XDG_CURRENT_DESKTOP=GNOME")));

        const QStringList bare = dm::sessionEnvironment(dm::SessionEntry {}, 2);
        QVERIFY(bare.contains(QStringLiteral("XDG_SESSION_TYPE=wayland")));
        for (const QString &variable : bare)
            QVERIFY(!variable.startsWith(QLatin1String("DESKTOP_SESSION=")));

        const QStringList greeter = dm::greeterEnvironment(1, QStringLiteral("/run/x.sock"));
        QVERIFY(greeter.contains(QStringLiteral("XDG_SESSION_CLASS=greeter")));
        QVERIFY(greeter.contains(QStringLiteral("XDG_VTNR=1")));
        QVERIFY(greeter.contains(QStringLiteral("GREETD_SOCK=/run/x.sock")));
    }

    void stateRemembersTheLastLogin()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("state/state.conf"));
        dm::State state(path);
        QVERIFY(!QFile::exists(path));
        state.remember(QStringLiteral("alice"), QStringLiteral("gnome"));
        state.remember(QStringLiteral("bob"), QStringLiteral("cutefish-wayland"));
        // The keys the login screen reads.
        const QSettings reread(path, QSettings::IniFormat);
        QCOMPARE(reread.value(QStringLiteral("Last/User")).toString(), QStringLiteral("bob"));
        QCOMPARE(reread.value(QStringLiteral("Sessions/alice")).toString(), QStringLiteral("gnome"));
        QCOMPARE(reread.value(QStringLiteral("Sessions/bob")).toString(),
                 QStringLiteral("cutefish-wayland"));
    }

    void configFallsBackToDefaults()
    {
        const dm::Config defaults = dm::Config::load(QStringLiteral("/nonexistent/dm.conf"));
        QCOMPARE(defaults.greeterVt, 1);
        QCOMPARE(defaults.sessionVt, 2);
        QCOMPARE(defaults.greeterUser, QStringLiteral("cutefish-greeter"));

        QTemporaryDir directory;
        writeFile(directory.filePath(QStringLiteral("dm.conf")),
                  "[Greeter]\nVt=8\nCommand=/usr/bin/cutefish-compositor --login --no-xwayland\n"
                  "[Session]\nVt=9\n[Daemon]\nRuntimeDirectory=/run/test\n");
        const dm::Config config = dm::Config::load(directory.filePath(QStringLiteral("dm.conf")));
        QCOMPARE(config.greeterVt, 8);
        QCOMPARE(config.sessionVt, 9);
        QCOMPARE(config.greeterCommand.last(), QStringLiteral("--no-xwayland"));
        QCOMPARE(config.runtimeDirectory, QStringLiteral("/run/test"));
        QCOMPARE(config.greeterUser, QStringLiteral("cutefish-greeter"));
    }

    void greeterServerSpeaksGreetd()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("greeter.sock"));
        dm::GreeterServer server;
        QVERIFY(server.listen(path, getuid()));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());

        QSignalSpy created(&server, &dm::GreeterServer::createSession);
        QSignalSpy started(&server, &dm::GreeterServer::startSession);
        QLocalSocket client;
        client.connectToServer(path);
        QVERIFY(client.waitForConnected(1000));

        // Two requests at once: the second waits for the first's answer.
        client.write(frame({{QStringLiteral("type"), QStringLiteral("create_session")},
                            {QStringLiteral("username"), QStringLiteral("alice")}}));
        client.write(frame({{QStringLiteral("type"), QStringLiteral("start_session")},
                            {QStringLiteral("cmd"), QJsonArray {}},
                            {QStringLiteral("env"), QJsonArray {QStringLiteral("LANG=C")}},
                            {QStringLiteral("session"), QStringLiteral("gnome")}}));
        client.flush();
        QTRY_COMPARE(created.count(), 1);
        QCOMPARE(created.first().first().toString(), QStringLiteral("alice"));
        QTest::qWait(50);
        QCOMPARE(started.count(), 0);

        server.respond(dm::GreeterServer::authMessage(QStringLiteral("secret"), QStringLiteral("Password:")));
        QTRY_COMPARE(started.count(), 1);
        QCOMPARE(started.first().at(1).toStringList(), QStringList {QStringLiteral("LANG=C")});
        QCOMPARE(started.first().at(2).toString(), QStringLiteral("gnome"));
        server.respond(dm::GreeterServer::success());

        dm::FrameReader reader;
        QList<QJsonObject> responses;
        QTRY_VERIFY([&] {
            reader.append(client.readAll());
            while (auto response = reader.next())
                responses.append(*response);
            return responses.size() == 2;
        }());
        QCOMPARE(responses[0].value(QStringLiteral("type")).toString(), QStringLiteral("auth_message"));
        QCOMPARE(responses[0].value(QStringLiteral("auth_message_type")).toString(),
                 QStringLiteral("secret"));
        QCOMPARE(responses[1].value(QStringLiteral("type")).toString(), QStringLiteral("success"));
    }

    void helperProtocolRunsALogin()
    {
        qputenv("CUTEFISH_DM_HELPER", QCoreApplication::applicationFilePath().toLocal8Bit());
        qputenv(kFakeHelper, "1");
        dm::HelperProcess helper;
        QSignalSpy questions(&helper, &dm::HelperProcess::question);
        QSignalSpy authenticated(&helper, &dm::HelperProcess::authenticated);
        QSignalSpy started(&helper, &dm::HelperProcess::started);
        QSignalSpy finished(&helper, &dm::HelperProcess::finished);

        helper.login(QStringLiteral("cutefish-dm"), QStringLiteral("alice"), true, 2);
        QTRY_COMPARE(questions.count(), 1);
        QCOMPARE(questions.first().first().toString(), QStringLiteral("secret"));
        helper.answer(QStringLiteral("right"));
        QTRY_COMPARE(authenticated.count(), 1);
        QVERIFY(helper.isAuthenticated());
        helper.start({QStringLiteral("/bin/true")}, {QStringLiteral("XDG_VTNR=2")});
        QTRY_COMPARE(started.count(), 1);
        QCOMPARE(helper.sessionId(), QStringLiteral("42"));
        QVERIFY(helper.hasStarted());
        helper.terminate();
        QTRY_COMPARE(finished.count(), 1);

        qunsetenv(kFakeHelper);
    }

    void aHelperIgnoringTerminationIsKilledAtTheDeadline()
    {
        qputenv("CUTEFISH_DM_HELPER", QCoreApplication::applicationFilePath().toLocal8Bit());
        qputenv(kFakeHelper, "stubborn");
        dm::HelperProcess helper;
        helper.setStopTimeout(300);
        QSignalSpy questions(&helper, &dm::HelperProcess::question);
        QSignalSpy authenticated(&helper, &dm::HelperProcess::authenticated);
        QSignalSpy started(&helper, &dm::HelperProcess::started);
        QSignalSpy finished(&helper, &dm::HelperProcess::finished);

        helper.login(QStringLiteral("cutefish-dm"), QStringLiteral("alice"), true, 2);
        QTRY_COMPARE(questions.count(), 1);
        helper.answer(QStringLiteral("right"));
        QTRY_COMPARE(authenticated.count(), 1);
        helper.start({QStringLiteral("/bin/true")}, {QStringLiteral("XDG_VTNR=2")});
        QTRY_COMPARE(started.count(), 1);
        helper.terminate();
        QTest::qWait(150);
        QCOMPARE(finished.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);

        qunsetenv(kFakeHelper);
    }

    void helperReportsAWrongPassword()
    {
        qputenv("CUTEFISH_DM_HELPER", QCoreApplication::applicationFilePath().toLocal8Bit());
        qputenv(kFakeHelper, "1");
        dm::HelperProcess helper;
        QSignalSpy failed(&helper, &dm::HelperProcess::failed);
        QSignalSpy finished(&helper, &dm::HelperProcess::finished);
        QSignalSpy questions(&helper, &dm::HelperProcess::question);

        helper.login(QStringLiteral("cutefish-dm"), QStringLiteral("alice"), true, 2);
        QTRY_COMPARE(questions.count(), 1);
        helper.answer(QStringLiteral("wrong"));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toBool());
        QVERIFY(!helper.isAuthenticated());

        qunsetenv(kFakeHelper);
    }
};

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsSet(kFakeHelper))
        return fakeHelper();

    QCoreApplication app(argc, argv);
    DisplayManagerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "dm_test.moc"
