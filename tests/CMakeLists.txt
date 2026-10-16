find_package(GTest REQUIRED)

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
target_link_libraries(dm-test PRIVATE cutefish-dm-common GTest::gtest)
add_test(NAME dm COMMAND dm-test)
