#pragma once

#include <string>
#include <vector>

namespace dm {

struct Config {
    // Separate VTs keep a session's end off the screen; VT1 has the boot splash.
    int greeterVt = 1;
    int sessionVt = 2;
    std::string greeterUser = "cutefish-greeter";
    std::vector<std::string> greeterCommand = {"/usr/bin/cutefish-compositor", "--login"};
    std::string greeterService = "cutefish-dm-greeter";
    std::string sessionService = "cutefish-dm";
    std::vector<std::string> sessionDirectories = {"/usr/local/share/wayland-sessions", "/usr/share/wayland-sessions"};
    std::string defaultSession = "cutefish-wayland";
    std::string statePath = "/var/lib/cutefish-dm/state.conf";
    std::string runtimeDirectory = "/run/cutefish-dm";

    // Defaults, overridden by whatever `path` sets.
    static Config load(const std::string &path = "/etc/cutefish-dm.conf");
};

// Splits a command line at white space; double quotes group, and three in a
// row stand for a quote character.
std::vector<std::string> splitCommand(const std::string &command);

} // namespace dm
