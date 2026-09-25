#include "terminal.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>

#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace dm {

namespace {

int openVt(int vt)
{
    return ::open(QStringLiteral("/dev/tty%1").arg(vt).toLocal8Bit().constData(),
                  O_RDWR | O_NOCTTY | O_CLOEXEC);
}

} // namespace

bool setGraphicsMode(int vt)
{
    const int fd = openVt(vt);
    if (fd < 0)
        return false;
    const bool set = ioctl(fd, KDSETMODE, KD_GRAPHICS) == 0;
    ::close(fd);
    return set;
}

void restoreTextMode(int vt)
{
    const int fd = openVt(vt);
    if (fd < 0)
        return;
    vt_mode mode {};
    int kdMode = KD_TEXT;
    if (ioctl(fd, VT_GETMODE, &mode) == 0 && mode.mode == VT_AUTO
        && ioctl(fd, KDGETMODE, &kdMode) == 0 && kdMode == KD_GRAPHICS)
        ioctl(fd, KDSETMODE, KD_TEXT);
    ::close(fd);
}

bool isControlled(int vt)
{
    const int fd = openVt(vt);
    if (fd < 0)
        return false;
    vt_mode mode {};
    const bool controlled = ioctl(fd, VT_GETMODE, &mode) == 0 && mode.mode == VT_PROCESS;
    ::close(fd);
    return controlled;
}

int activeVt()
{
    QFile file(QStringLiteral("/sys/class/tty/tty0/active"));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return QString::fromLatin1(file.readAll()).trimmed().mid(3).toInt();
}

bool activateSession(const QString& sessionId)
{
    QDBusInterface manager(QStringLiteral("org.freedesktop.login1"),
                           QStringLiteral("/org/freedesktop/login1"),
                           QStringLiteral("org.freedesktop.login1.Manager"),
                           QDBusConnection::systemBus());
    const QDBusReply<void> reply = manager.call(QStringLiteral("ActivateSession"), sessionId);
    if (!reply.isValid())
        qWarning() << "Cannot activate session" << sessionId << reply.error().message();
    return reply.isValid();
}

} // namespace dm
