// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "backgrounds.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <optional>
#include <string_view>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/file.h"
#include "base/settings.h"

using namespace cutefish;

namespace dm {

static constexpr std::string_view kSettingsSuffix = ".conf";
static constexpr std::string_view kPictureSuffix = ".picture";

static constexpr std::array<std::string_view, 14> kLanguageKeys = {
    "LANG",       "LANGUAGE",   "LC_CTYPE",    "LC_NUMERIC", "LC_TIME",
    "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES", "LC_PAPER",   "LC_NAME",
    "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION",
};

static bool isLanguageKey(std::string_view name)
{
    return name == "LC_ALL"
            || std::find(kLanguageKeys.begin(), kLanguageKeys.end(), name) != kLanguageKeys.end();
}

static bool isLanguageValue(std::string_view value)
{
    if (value.size() > 256)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 0x20 && c != 0x7f; });
}

static bool userExists(const std::string &user)
{
    return getpwnam(user.c_str()) != nullptr;
}

static bool isColor(const std::string &color)
{
    if (color.size() != 4 && color.size() != 7 && color.size() != 9)
        return false;

    if (color.front() != '#')
        return false;

    return color.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
}

// The whole file from its start, or nothing for anything but a regular file
// within the limit. The descriptor is the caller's, so its offset is left alone.
static std::optional<std::string> readPicture(int fd)
{
    struct stat info = {};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > Backgrounds::kMaximumPictureSize)
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

    return bytes.empty() ? std::nullopt : std::optional<std::string>(std::move(bytes));
}

Backgrounds::Backgrounds(std::string directory)
    : m_directory(std::move(directory))
{
}

std::string Backgrounds::settingsPath(const std::string &user) const
{
    return m_directory + '/' + user + std::string(kSettingsSuffix);
}

std::string Backgrounds::picturePath(const std::string &user) const
{
    return m_directory + '/' + user + std::string(kPictureSuffix);
}

void Backgrounds::removeStale()
{
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(m_directory, error)) {
        const std::string name = entry.path().filename().string();
        std::string user;
        if (name.ends_with(kSettingsSuffix))
            user = name.substr(0, name.size() - kSettingsSuffix.size());
        else if (name.ends_with(kPictureSuffix))
            user = name.substr(0, name.size() - kPictureSuffix.size());

        if (user.empty() || !userExists(user))
            std::filesystem::remove(entry.path(), error);
    }
}

void Backgrounds::setPicture(const std::string &user, base::dbus::UnixFd file, std::function<void(bool)> done)
{
    const uint64_t request = ++m_lastRequest;
    m_latest[user] = request;

    const std::string pending = picturePath(user) + '.' + std::to_string(request);
    m_copies[request] = base::ThreadPool::global().run(
            [file, pending] {
                const std::optional<std::string> bytes = readPicture(file.fd());
                return bytes && base::file::writeAtomically(pending, *bytes);
            },
            [this, user, request, pending, done = std::move(done)](bool copied) {
                m_copies.erase(request);
                if (!copied) {
                    unlink(pending.c_str());
                    done(false);
                    return;
                }

                // A later request took over; it has its own answer.
                if (m_latest[user] != request) {
                    unlink(pending.c_str());
                    done(true);
                    return;
                }

                const std::string picture = picturePath(user);
                if (rename(pending.c_str(), picture.c_str()) != 0) {
                    unlink(pending.c_str());
                    done(false);
                    return;
                }

                base::Settings settings(settingsPath(user));
                settings.setValue("Appearance/BackgroundType", 0);
                settings.setValue("Appearance/Wallpaper", picture);
                done(settings.sync());
            });
}

bool Backgrounds::setColor(const std::string &user, const std::string &color)
{
    if (!isColor(color))
        return false;

    m_latest[user] = ++m_lastRequest;

    base::Settings settings(settingsPath(user));
    settings.setValue("Appearance/BackgroundType", 1);
    settings.setValue("Appearance/BackgroundColor", color);
    return settings.sync();
}

bool Backgrounds::setLanguage(const std::string &user, const std::map<std::string, std::string> &environment)
{
    for (const auto &[name, value] : environment) {
        if (!isLanguageKey(name) || !isLanguageValue(value))
            return false;
    }

    base::Settings settings(settingsPath(user));
    for (const std::string_view key : kLanguageKeys) {
        const std::string path = "Language/" + std::string(key);
        const auto found = environment.find(std::string(key));
        if (found != environment.end() && !found->second.empty())
            settings.setValue(path, found->second);
        else
            settings.remove(path);
    }
    return settings.sync();
}

} // namespace dm
