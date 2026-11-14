// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "vtguard.h"

#include <chrono>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "base/event_loop.h"
#include "terminal.h"

using namespace cutefish;

namespace dm {

int returnFromEmptyTerminal(int active, int greeterVt, int sessionVt, bool greeterUp, bool sessionUp)
{
    if (active <= 0 || greeterVt == sessionVt)
        return 0;
    if (active == greeterVt && !greeterUp && sessionUp)
        return sessionVt;
    if (active == sessionVt && !sessionUp && greeterUp)
        return greeterVt;
    return 0;
}

ActiveVtWatch::ActiveVtWatch(std::function<void(int vt)> changed)
    : m_changed(std::move(changed))
    , m_last(activeVt())
{
    base::EventLoop *loop = base::EventLoop::current();
    if (!loop)
        return;
    m_fd = ::open("/sys/class/tty/tty0/active", O_RDONLY | O_CLOEXEC);
    if (m_fd >= 0) {
        // A sysfs attribute reports a change as priority data; it is read
        // again from its start to wait for the next one.
        char buffer[16];
        (void)::pread(m_fd, buffer, sizeof(buffer), 0);
        m_watch = loop->watch(m_fd, EPOLLPRI | EPOLLERR, [this](uint32_t) {
            char drain[16];
            (void)::pread(m_fd, drain, sizeof(drain), 0);
            check();
        });
    }
    if (!m_watch)
        m_timer = loop->startTimer(std::chrono::milliseconds(500), true, [this] { check(); });
}

ActiveVtWatch::~ActiveVtWatch()
{
    if (base::EventLoop *loop = base::EventLoop::current()) {
        if (m_watch)
            loop->unwatch(m_watch);
        if (m_timer)
            loop->stopTimer(m_timer);
    }
    if (m_fd >= 0)
        ::close(m_fd);
}

void ActiveVtWatch::check()
{
    const int vt = activeVt();
    if (vt <= 0 || vt == m_last)
        return;
    m_last = vt;
    m_changed(vt);
}

} // namespace dm
