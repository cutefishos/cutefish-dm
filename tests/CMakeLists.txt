find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

add_executable(dm-test
    dm_test.cpp
    ../daemon/config.cpp
    ../daemon/greeterserver.cpp
    ../daemon/helperprocess.cpp
    ../daemon/sessions.cpp
    ../daemon/state.cpp
)
target_compile_definitions(dm-test PRIVATE
    DM_HELPER_PATH="/nonexistent/cutefish-dm-helper"
    DM_SESSION_WRAPPER="/nonexistent/cutefish-dm-session"
)
target_include_directories(dm-test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(dm-test PRIVATE cutefish-dm-common Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME dm COMMAND dm-test)
