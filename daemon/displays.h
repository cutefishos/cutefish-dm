// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

namespace dm {

// Each user's display configuration, the compositor's display.json, kept
// where the login screen can read it: the user's home may be closed to it
// or not mounted yet. The login screen sets its outputs up as the last user
// had them, the refresh rate included, instead of at the monitors' preferred
// modes.
class Displays
{
public:
    // The largest configuration kept, in bytes.
    static constexpr long kMaximumSize = 1024 * 1024;

    explicit Displays(std::string directory);

    // Removes what belongs to no existing user.
    void removeStale();
    // Keeps what `fd` reads as `user`'s: false for anything but a regular
    // file within kMaximumSize holding a JSON object.
    bool set(const std::string &user, int fd);

    std::string path(const std::string &user) const;

private:
    std::string m_directory;
};

} // namespace dm
