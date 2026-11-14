// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <functional>

namespace dm {

// Where to send the seat when it lands on `active`, or 0 to leave it there.
// The display manager's own terminals have nothing to show without what it
// runs on them: the greeter's is blank once the greeter has gone (getty is
// kept off it), and a console signed in on the sessions' one keeps the next
// graphical login from starting there. With both up, as while switching
// users, both are where they should be.
int returnFromEmptyTerminal(int active, int greeterVt, int sessionVt, bool greeterUp, bool sessionUp);

// Calls `changed` with the new active virtual terminal whenever it changes,
// however it was switched: by a compositor, logind, or the kernel itself
// from a text console. The kernel notifies on tty0's "active" attribute;
// where that cannot be watched, it is read twice a second.
class ActiveVtWatch
{
public:
    explicit ActiveVtWatch(std::function<void(int vt)> changed);
    ~ActiveVtWatch();

    ActiveVtWatch(const ActiveVtWatch &) = delete;
    ActiveVtWatch &operator=(const ActiveVtWatch &) = delete;

private:
    void check();

    std::function<void(int)> m_changed;
    int m_fd = -1;
    int m_watch = 0;
    int m_timer = 0;
    int m_last = 0;
};

} // namespace dm
