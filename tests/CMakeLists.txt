include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)

set(BUILD_GMOCK   OFF CACHE BOOL "" FORCE)  # because only gtest is needed
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)  # never install with this project
FetchContent_MakeAvailable(googletest)

add_executable(lob_tests
    test_standalone.cpp
    standalone_avl.cpp
)

target_link_libraries(lob_tests PRIVATE lob_core lob_options GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(lob_tests)