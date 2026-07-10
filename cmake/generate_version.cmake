# 从 project(VERSION) 生成 include/sw_version.h，并注入 Git 短哈希

find_package(Git QUIET)

set(WDF_GIT_HASH "unknown")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE WDF_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(WDF_GIT_HASH STREQUAL "")
        set(WDF_GIT_HASH "unknown")
    endif()
endif()

set(WDF_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${WDF_GENERATED_INCLUDE_DIR}")

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/templates/sw_version.h.in"
    "${WDF_GENERATED_INCLUDE_DIR}/sw_version.h"
    @ONLY
)

message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION} (${WDF_GIT_HASH})")
