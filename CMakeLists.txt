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
# 过渡期：旧目录（app/ bsp/ component/ service/）与新目录（src/）共存
# 收尾阶段 C-2 删除旧目录后，可删除下方"旧架构"区块
# -------------------------------------------------------
file(GLOB_RECURSE src CONFIGURE_DEPENDS
    # 旧架构（过渡期保留，阶段收尾 C-2 删除）
    app/*.c app/*.h app/*.cpp
    bsp/*.c bsp/*.h bsp/*.cpp
    component/*.c component/*.h
    service/*.c service/*.h
    protocol/*.c protocol/*.h
    # 公共层（始终保留）
    common/*.c common/*.h
    driver/*.c driver/*.h
    config/*.h
    # 新架构 src/（各阶段逐步填充）
    src/*.c src/*.h src/*.cpp
)

# 旧 sim/ 桩和新 sim_hw 适配器不参与嵌入式构建
list(FILTER src EXCLUDE REGEX ".*/sim/.*")
list(FILTER src EXCLUDE REGEX ".*/sim_hw/.*")

# 已被新架构替换的旧文件——排除避免符号重复
# 过渡期逐步增加；收尾阶段 C-2 直接删除旧目录后可移除此段
list(FILTER src EXCLUDE REGEX ".*/service/svc_param\\.c$")
list(FILTER src EXCLUDE REGEX ".*/service/svc_param\\.h$")

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
# 新架构：src/adapters/hal/sim_hw/ 提供 HAL 仿真实现，无需嵌入式 SDK
# 用法：cmake -B build_sim -DBUILD_SIM=ON && cmake --build build_sim
#
# 注：仿真源文件在阶段六（sim_hw/ 实现）完成后才会有内容，
#     当前阶段 GLOB 返回空列表，跳过 M8_sim target 创建。
# ======================================================================
if(BUILD_SIM)
    add_compile_definitions(BUILD_SIM=1)

    # -----------------------------------------------------------------------
    # cJSON：sim 构建使用系统 libcjson（嵌入式目标由 snack SDK 提供）
    # 安装方式：sudo apt install libcjson-dev
    # -----------------------------------------------------------------------
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CJSON QUIET libcjson)
    endif()
    if(NOT CJSON_FOUND)
        find_path(CJSON_INCLUDE_DIR NAMES cJSON.h PATH_SUFFIXES cjson)
        find_library(CJSON_LIBRARY  NAMES cjson)
        if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
            set(CJSON_FOUND TRUE)
            set(CJSON_INCLUDE_DIRS  ${CJSON_INCLUDE_DIR})
            set(CJSON_LIBRARIES     ${CJSON_LIBRARY})
        endif()
    endif()
    if(CJSON_FOUND)
        message(STATUS "cJSON found → ${CJSON_LIBRARIES}")
    else()
        message(FATAL_ERROR
            "cJSON not found — M8_sim 构建需要 libcjson。\n"
            "  Ubuntu/Debian: sudo apt install libcjson-dev\n"
            "  或指定路径:    cmake -DCJSON_DIR=<path> ...")
    endif()

    # -----------------------------------------------------------------------
    # sim 构建源文件：明确列出，避免误拉入真机专属文件
    # -----------------------------------------------------------------------
    file(GLOB_RECURSE sim_sources CONFIGURE_DEPENDS
        # 领域核心（纯逻辑，无硬件依赖）
        src/domain/*.c
        src/application/*.c
        src/service/*.c

        # 事件总线 + 调度器
        src/core/event_bus/*.c
        src/core/scheduler/*.c

        # bootstrap：bootstrap.c 含 BUILD_SIM 隔离，wiring_sim.c 替代 wiring.c
        src/core/bootstrap/bootstrap.c
        src/core/bootstrap/wiring_sim.c

        # 仿真 HAL 实现
        src/adapters/hal/sim_hw/*.c

        # 存储适配器（param + deploy，路径可能与真机不同但接口相同）
        src/adapters/storage/json/*.c

        # UI 适配器（mqtt_command_parser + command_bridge，无 snack 依赖）
        src/adapters/ui/mqtt_cmd/*.c
        src/adapters/ui/command_bridge.c

        # M8 报警适配（通过 ports/ 访问 HAL，与 sim HAL 兼容）
        src/adapters/machine/m8/m8_alarm_adapt.c

        # 仿真主入口 + 交互控制台
        tools/simulator/*.c

        # 公共层
        common/*.c
    )

    # 排除仿真不需要或有真机依赖的文件
    list(FILTER sim_sources EXCLUDE REGEX ".*/linux_hw/.*")
    list(FILTER sim_sources EXCLUDE REGEX ".*/aliyun/.*")
    list(FILTER sim_sources EXCLUDE REGEX "cli_adapter\\.cpp")
    list(FILTER sim_sources EXCLUDE REGEX "m8_boot_profile\\.c")

    if(sim_sources)
        add_executable(M8_sim ${sim_sources})

        target_include_directories(M8_sim PRIVATE
            ${CMAKE_SOURCE_DIR}                  # common/ tools/ 等
            ${CMAKE_SOURCE_DIR}/src              # 新架构 domain/ ports/ 等
            ${CMAKE_SOURCE_DIR}/tools/simulator  # sim_console.h 等
        )

        if(CJSON_FOUND)
            target_include_directories(M8_sim PRIVATE ${CJSON_INCLUDE_DIRS})
            target_link_libraries(M8_sim PRIVATE pthread ${CJSON_LIBRARIES})
        else()
            target_link_libraries(M8_sim PRIVATE pthread)
        endif()

        target_compile_options(M8_sim PRIVATE -Wall -Wextra -Werror -g)

        message(STATUS "BUILD_SIM enabled → target M8_sim")
    else()
        message(STATUS "BUILD_SIM enabled → no sources found, M8_sim skipped")
    endif()

endif()
