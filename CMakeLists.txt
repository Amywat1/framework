cmake_minimum_required(VERSION 3.10.0)

# BUILD_SIM=ON 时构建 PC 模拟器目标（M8_sim），替换 bsp/driver 为 sim/ 桩
option(BUILD_SIM "Build PC wash-engine simulator (no hardware)" OFF)

project(M8 VERSION 0.1.0 LANGUAGES C CXX)

# -------------------------------------------------------
# 语言标准
# -------------------------------------------------------
set(CMAKE_C_STANDARD   99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 调试符号
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# -------------------------------------------------------
# Git 短哈希注入
# -------------------------------------------------------
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()
add_compile_definitions(SW_GIT_HASH="${GIT_HASH}")

# ==============================
# 嵌入式目标（非 sim 构建）
# ==============================
if(NOT BUILD_SIM)

include(FindPkgConfig)

pkg_check_modules(BLE_LIB REQUIRED ble)
if(NOT BLE_LIB_FOUND)
    message(FATAL_ERROR "未找到 BLE 库")
endif()

pkg_check_modules(WIFI_LIB REQUIRED wifi)
if(NOT WIFI_LIB_FOUND)
    message(FATAL_ERROR "未找到 WIFI 库")
endif()

# -------------------------------------------------------
# 全局编译选项（§5.10：零 warning）
# -------------------------------------------------------
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -Wno-unused-parameter   # snack SDK 回调参数常有未使用
)

# -------------------------------------------------------
# 源文件（GLOB_RECURSE + CONFIGURE_DEPENDS 保证增删文件后自动重配）
# -------------------------------------------------------
file(GLOB_RECURSE src CONFIGURE_DEPENDS
    app/*.c app/*.h app/*.cpp
    bsp/*.c bsp/*.h bsp/*.cpp
    common/*.c common/*.h
    component/*.c component/*.h
    driver/*.c driver/*.h
    service/*.c service/*.h
    protocol/*.c protocol/*.h
    config/*.h
)

# sim/ 桩文件不参与嵌入式构建
list(FILTER src EXCLUDE REGEX ".*/sim/.*")

add_executable(${PROJECT_NAME} ${src})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}                         # 项目根目录，使 app/ bsp/ common/ 等旧路径可见
    ${CMAKE_SOURCE_DIR}/src                     # 新架构根目录，使 domain/ ports/ adapters/ 等路径可见
    /usr/local/include/snack
    /usr/local/include/snack/canfestival
    ${BLE_LIB_INCLUDE_DIRS}
    ${WIFI_LIB_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    snack
    io-exp
    canfestival
    paho-mqtt3as
    canfestival_unix
    dl
    rt
    pthread
    ${BLE_LIB_LIBRARIES}
    ${WIFI_LIB_LIBRARIES}
)

# -------------------------------------------------------
# 固件/二进制归档（发布时使用）
# 命名规则：M8_<版本>_<Git哈希>
# -------------------------------------------------------
set(RELEASE_NAME "M8_v${PROJECT_VERSION}_${GIT_HASH}")
add_custom_target(release
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_BINARY_DIR}/${RELEASE_NAME}
    DEPENDS ${PROJECT_NAME}
    COMMENT "Package release: ${RELEASE_NAME}"
)

endif() # NOT BUILD_SIM

# ======================================================================
# PC 模拟器目标（BUILD_SIM=ON）
# 仅编译 wash_engine + component/ + sim/，无需任何嵌入式 SDK
# 用法：cmake -B build_sim -DBUILD_SIM=ON && cmake --build build_sim
# ======================================================================
if(BUILD_SIM)
    add_compile_definitions(BUILD_SIM=1)

    set(sim_sources
        app/wash_engine.c
        app/wash_config.c
        component/comp_brush.c
        component/comp_gantry.c
        component/comp_top_lift.c
        component/comp_water.c
        sim/sim_driver.c
        sim/sim_console.c
        sim/sim_main.c
    )

    add_executable(M8_sim ${sim_sources})

    target_include_directories(M8_sim PRIVATE
        ${CMAKE_SOURCE_DIR}/sim   # 优先：提供 io_exp/demo.h 等桩头文件
        ${CMAKE_SOURCE_DIR}       # 项目根，使 component/ app/ 等路径可见
    )

    target_link_libraries(M8_sim PRIVATE pthread)

    target_compile_options(M8_sim PRIVATE -Wall -Wextra -g)

    message(STATUS "BUILD_SIM enabled → target M8_sim")
endif()
