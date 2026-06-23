cmake_minimum_required(VERSION 3.14)
project(Limit-Order-Book VERSION 1.0 LANGUAGES CXX)

# enforcing modern cpp (C++ 20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# aggressive warninggs ON
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wpedantic")

# optimising for speed(O3) and targetting the native architecture
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

# telling the compiler where to find the headers
include_directories(include)

# creating static library out of source files 
# this tells cmake how to build the core engine
add_library(lob_core STATIC src/book.cpp)