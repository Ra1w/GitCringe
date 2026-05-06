cmake_minimum_required(VERSION 3.24)
project(git_cringe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG master
)

FetchContent_MakeAvailable(SQLiteCpp)

add_executable(main src/main.cpp)

target_link_libraries(main PRIVATE SQLiteCpp)
target_include_directories(main PRIVATE include)

