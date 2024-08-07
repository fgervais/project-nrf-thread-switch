# list(APPEND OVERLAY_CONFIG "debug.conf")
list(APPEND OVERLAY_CONFIG "dns.conf")
list(APPEND OVERLAY_CONFIG "secret.conf")
list(APPEND OVERLAY_CONFIG "thread-sed.conf")

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

execute_process(COMMAND git describe
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_DESCRIBE
                ERROR_QUIET)
string(STRIP "${GIT_DESCRIBE}" GIT_DESCRIBE)
STRING(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
       VERSION "${GIT_DESCRIBE}")

project(app LANGUAGES C VERSION ${VERSION})

configure_file(app_version.h.in ${CMAKE_BINARY_DIR}/app/include/app_version.h)
zephyr_include_directories(
    ${CMAKE_BINARY_DIR}/app/include
)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
        src/main.c)

zephyr_include_directories(
    configuration/${BOARD}
)
