#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSocketNotifier>

#include <csignal>
#include <sys/signalfd.h>
#include <unistd.h>

#include "config.h"
#include "daemon.h"

int main(int argc, char *argv[])
{
    // Through signalfd, so SIGTERM runs the daemon's teardown.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    const int signalFd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cutefish-dm"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption configOption(QStringLiteral("config"),
                                          QStringLiteral("Configuration file to read."),
                                          QStringLiteral("path"),
                                          QStringLiteral("/etc/cutefish-dm.conf"));
    parser.addOption(configOption);
    parser.process(app);

    if (geteuid() != 0) {
        qCritical("cutefish-dm has to run as root");
        return 1;
    }

    dm::Daemon daemon(dm::Config::load(parser.value(configOption)));

    if (signalFd >= 0) {
        auto *notifier = new QSocketNotifier(signalFd, QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [signalFd] {
            signalfd_siginfo info;
            while (read(signalFd, &info, sizeof(info)) == sizeof(info)) {
            }
            QCoreApplication::quit();
        });
    }

    if (!daemon.start())
        return 1;

    return app.exec();
}
