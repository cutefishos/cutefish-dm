// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

// The daemon under test never touches a VT or logind: every VT is taken at
// once and every switch succeeds.

#include "daemon/terminal.h"

namespace dm {

bool setGraphicsMode(int)
{
    return true;
}

void restoreTextMode(int)
{
}

bool isControlled(int)
{
    return true;
}

int activeVt()
{
    return 0;
}

bool activateSession(const std::string &)
{
    return true;
}

} // namespace dm
