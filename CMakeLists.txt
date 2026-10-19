cmake_minimum_required(VERSION 3.22)

project(cutefish-dm
    VERSION 1.0
    DESCRIPTION "Cutefish display manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
option(BUILD_TESTING "Build the test suite" OFF)
include(CTest)

find_package(CutefishToolkit REQUIRED)
find_library(PAM_LIBRARY pam)
if(NOT PAM_LIBRARY)
    message(FATAL_ERROR "cutefish-dm needs libpam")
endif()

set(DM_HELPER_PATH ${CMAKE_INSTALL_FULL_LIBEXECDIR}/cutefish-dm-helper)
set(DM_SESSION_WRAPPER ${CMAKE_INSTALL_FULL_LIBEXECDIR}/cutefish-dm-session)

add_library(cutefish-dm-common STATIC common/wire.cpp)
target_link_libraries(cutefish-dm-common PUBLIC Cutefish::ToolkitBase)

add_executable(cutefish-dm
    daemon/backgrounds.cpp
    daemon/config.cpp
    daemon/daemon.cpp
    daemon/greeterserver.cpp
    daemon/helperprocess.cpp
    daemon/main.cpp
    daemon/sessions.cpp
    daemon/state.cpp
    daemon/terminal.cpp
)
target_compile_definitions(cutefish-dm PRIVATE
    DM_HELPER_PATH="${DM_HELPER_PATH}"
    DM_SESSION_WRAPPER="${DM_SESSION_WRAPPER}"
)
target_link_libraries(cutefish-dm PRIVATE cutefish-dm-common)

# No event loop and no threads: PAM modules and the session's fork run here.
add_executable(cutefish-dm-helper helper/main.cpp)
target_link_libraries(cutefish-dm-helper PRIVATE cutefish-dm-common ${PAM_LIBRARY})

install(TARGETS cutefish-dm DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS cutefish-dm-helper DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(PROGRAMS data/cutefish-dm-session DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES data/cutefish-dm.service DESTINATION /usr/lib/systemd/system)
install(FILES data/cutefish-dm.pam DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d
        RENAME cutefish-dm)
install(FILES data/cutefish-dm-greeter.pam DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d
        RENAME cutefish-dm-greeter)
install(FILES data/org.cutefish.DisplayManager.conf
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/dbus-1/system.d)
foreach(unit pipewire.socket pipewire.service pipewire-pulse.socket pipewire-pulse.service
        wireplumber.service filter-chain.service)
    install(FILES data/no-greeter-audio.conf
            DESTINATION /usr/lib/systemd/user/${unit}.d
            RENAME cutefish-dm.conf)
endforeach()

cutefish_add_translations(cutefish-dm SOURCES daemon helper)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
