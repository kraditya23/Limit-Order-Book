cmake_minimum_required(VERSION 3.14)
project(Limit-Order-Book VERSION 1.0 LANGUAGES CXX)

# Build switches (we can change them -D<NAME>=ON/OFF when configuring)
option(LOB_BUILD_TESTS          "Build the Google Test suite"                       ON)
option(LOB_BUILD_INTERIM_BENCH  "Build the interim benchmark harness in bench/"     OFF)
option(LOB_SANITIZE             "Instrument with address sanitizer and UBSan"       OFF)
option(LOB_NATIVE               "Tune release builds for this machine's CPU"        ON)

# without this CMake asks GCC and Clang for -std=gnu++20, which allows GNU only extras.
# OFF gives strict standard C++
set(CMAKE_CXX_EXTENSIONS OFF) 

# settings shared by every target we own (not applicable to third party code)
add_library(lob_options INTERFACE)
target_compile_options(lob_options INTERFACE -Wall -Wextra -Wpedantic -Werror)

if (LOB_NATIVE)
    target_compile_options(lob_options INTERFACE $<$<CONFIG:Release>:-march=native>)
endif()

# AddressSanitizer(ASan) catches out of bounds reads and writes, use after free, and double free
# UndefinedBehaviorSanitizer(UBSan) adds runtime checks for undefined behavior: signed integer overflow,
# shifting by too many bits, misaligned pointers etc
if (LOB_SANITIZE)
    target_compile_options(lob_options INTERFACE
        -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(lob_options INTERFACE -fsanitize=address,undefined)
endif()

# the core engine library
add_library(lob_core STATIC src/book.cpp)
target_include_directories(lob_core PUBLIC include)
target_compile_features(lob_core PUBLIC cxx_std_20)
target_link_libraries(lob_core PRIVATE lob_options)

# the interim benchmark harness
if (LOB_BUILD_INTERIM_BENCH)
    add_executable(lob_interim_bench bench/lob_benchmark.cpp)
    target_link_libraries(lob_interim_bench PRIVATE lob_core lob_options)
endif()

if (LOB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()