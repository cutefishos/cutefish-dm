// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "displays.h"

#include <cerrno>
#include <filesystem>
#include <optional>
#include <string_view>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/file.h"
#include "base/json.h"

using namespace cutefish;

namespace dm {

static constexpr std::string_view kSuffix = ".json";

// The whole file from its start; the descriptor is the caller's, so its
// offset is left alone.
static std::optional<std::string> readAll(int fd)
{
    struct stat info = {};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0
        || info.st_size > Displays::kMaximumSize)
        return std::nullopt;

    std::string bytes;
    bytes.resize(size_t(info.st_size));
    size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t count = pread(fd, bytes.data() + done, bytes.size() - done, off_t(done));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return std::nullopt;
        done += size_t(count);
    }
    return bytes;
}

Displays::Displays(std::string directory)
    : m_directory(std::move(directory))
{
}

std::string Displays::path(const std::string &user) const
{
    return m_directory + '/' + user + std::string(kSuffix);
}

void Displays::removeStale()
{
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(m_directory, error)) {
        const std::string name = entry.path().filename().string();
        const std::string user =
                name.ends_with(kSuffix) ? name.substr(0, name.size() - kSuffix.size()) : std::string();
        if (user.empty() || getpwnam(user.c_str()) == nullptr)
            std::filesystem::remove(entry.path(), error);
    }
}

bool Displays::set(const std::string &user, int fd)
{
    const std::optional<std::string> bytes = readAll(fd);
    if (!bytes)
        return false;
    // What the login screen reads must at least be the shape it reads.
    const std::optional<base::json::Value> root = base::json::Value::parse(*bytes);
    if (!root || !root->isObject())
        return false;
    return base::file::writeAtomically(path(user), *bytes);
}

} // namespace dm
