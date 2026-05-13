cmake_minimum_required(VERSION 3.24)
project(gitcringe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG 3.3.3
)

FetchContent_MakeAvailable(SQLiteCpp)

add_executable(main src/main.cpp 
                    src/repo.cpp 
                    src/cmd_add.cpp
                    src/cmd_commit.cpp
                    src/cmd_help.cpp
                    src/cmd_init.cpp
                    src/cmd_merge.cpp
                    src/cmd_diff.cpp
                    src/cmd_status.cpp
                    src/cmd_exec.cpp
                    src/cmd_log.cpp
                    src/cmd_switch.cpp
                    src/cmd_branch.cpp
                    src/cmd_squash.cpp
                    src/cmd_show.cpp)

target_link_libraries(main PRIVATE SQLiteCpp)
target_include_directories(main PRIVATE include)


set_target_properties(main PROPERTIES OUTPUT_NAME "gitcringe")

install(TARGETS main DESTINATION bin)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_CONTACT "Artyom Chugunov <ArtyomCh2007@yandex.ru>, Mihail Afanasiev <michele.afanasiev@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER ${CPACK_PACKAGE_CONTACT})

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A simple local version control system. Like Git, but cringe")

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)
