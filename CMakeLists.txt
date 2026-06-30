cmake_minimum_required(VERSION 3.10.0)

# BUILD_SIM=ON 时构建 PC 模拟器目标（M8_sim），使用仿真实现与兼容桩
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
# 源码按模块组织在仓库根目录
# -------------------------------------------------------
file(GLOB_RECURSE src CONFIGURE_DEPENDS
    app_main.cpp
    platform/*.c platform/*.h
    common/*.c common/*.h
    infrastructure/*.c infrastructure/*.h
    domain/*.c domain/*.h
    application/*.c application/*.h
    ports/*.c ports/*.h
    adapters/*.cpp
    adapters/*.c adapters/*.h
    config/*.c config/*.h
    machines/*.cpp
    machines/*.c machines/*.h
    tests/*.c tests/*.h
)
# 内置 cJSON（替代 snack SDK 路径，真机/sim 统一使用同一份源码）
list(APPEND src ${CMAKE_SOURCE_DIR}/third_party/cJSON/cJSON.c)

# sim_hw 适配器不参与嵌入式构建
list(FILTER src EXCLUDE REGEX ".*/sim_hw/.*")
# 仿真信号注入仅用于 BUILD_SIM
list(FILTER src EXCLUDE REGEX ".*/m8_signal_sim\\.c$")
# sim wiring only participates in BUILD_SIM
list(FILTER src EXCLUDE REGEX ".*/wiring_sim\.c$")
# 场景测试文件不参与产品构建
list(FILTER src EXCLUDE REGEX ".*/tests/.*")
# snack_wrapper.cpp 符号由 snack SDK 库提供，不重复编译（新路径 adapters/sdk/snack/）
list(FILTER src EXCLUDE REGEX ".*/snack_wrapper\.cpp$")

add_executable(${PROJECT_NAME} ${src})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}                         # 项目根目录：common/ driver/ domain/ ports/ adapters/ 等均在此
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
# adapters/hal/sim_hw/ 提供 HAL 仿真实现，不依赖嵌入式 SDK
# 用法：cmake -B build_sim -DBUILD_SIM=ON && cmake --build build_sim
#
# 注：仿真源文件齐全（sim_hw/ + bootstrap + wiring_sim 等），
#     若 GLOB 返回空列表则跳过 M8_sim target 创建。
# ======================================================================
if(BUILD_SIM)
    add_compile_definitions(BUILD_SIM=1)

    # -----------------------------------------------------------------------
    # sim 构建源文件：明确列出，避免误拉入真机专属文件
    # -----------------------------------------------------------------------
    file(GLOB_RECURSE sim_sources CONFIGURE_DEPENDS
        # 领域核心（纯逻辑，无硬件依赖）
        domain/*.c
        application/*.c
        infrastructure/services/*.c

        # 事件总线 + 调度器
        infrastructure/event_bus/*.c
        infrastructure/scheduler/*.c

        # 仿真构建使用 wiring_sim.c 完成依赖接线
        infrastructure/bootstrap/bootstrap.c
        platform/main/wiring_sim.c

        # 仿真 HAL 实现（sim_hw：io/vfd/motor 专属；generic：sensor/do_group 共用）
        ports/*.c
        adapters/hal/sim_hw/*.c
        adapters/hal/generic/hal_sensor.c
        adapters/hal/generic/hal_do_group.c

        # 存储适配器（param + deploy，路径可能与真机不同但接口相同）
        adapters/storage/json/*.c

        # UI 适配器（mqtt_command_parser + command_bridge，无 snack 依赖）
        adapters/ui/mqtt_cmd/*.c
        adapters/ui/command_bridge.c

        # M8 机型配置表
        machines/m8/config/m8_signal_table.c

        # M8 机型适配
        machines/m8/adapters/alarm/m8_alarm_init.c
        machines/m8/adapters/alarm/m8_alarm_adapt.c
        machines/m8/adapters/alarm/m8_comm_watchdog.c
        machines/m8/adapters/setup/m8_sensor.c
        machines/m8/adapters/setup/m8_motor_setup.c
        machines/m8/adapters/setup/m8_water_setup.c
        machines/m8/adapters/engine/engine_io_m8.c
        machines/m8/adapters/m8_signal_sim.c

        # 仿真主入口 + 交互控制台
        platform/sim/*.c

        # 公共层
        common/*.c
    )
    # 内置 cJSON（替代系统 libcjson-dev）
    list(APPEND sim_sources ${CMAKE_SOURCE_DIR}/third_party/cJSON/cJSON.c)

    # 排除仿真不需要或有真机依赖的文件
    list(FILTER sim_sources EXCLUDE REGEX ".*/linux_hw/.*")
    list(FILTER sim_sources EXCLUDE REGEX ".*/aliyun/.*")
    list(FILTER sim_sources EXCLUDE REGEX "cli_adapter\\.cpp")
    list(FILTER sim_sources EXCLUDE REGEX "m8_boot_profile\\.c")


    if(sim_sources)
        add_executable(M8_sim ${sim_sources})

        target_include_directories(M8_sim PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/platform/sim
        )

        target_link_libraries(M8_sim PRIVATE pthread)

        target_compile_options(M8_sim PRIVATE -Wall -Wextra -Werror -g)
        target_compile_definitions(M8_sim PRIVATE
            WASH_PROGRAM_NORMAL_PATH="${CMAKE_SOURCE_DIR}/doc/m8_normal_wash_program.json"
            WASH_PROGRAM_QUICK_PATH="${CMAKE_SOURCE_DIR}/doc/m8_normal_wash_program.json")

        message(STATUS "BUILD_SIM enabled 鈫?target M8_sim")
    else()
        message(STATUS "BUILD_SIM enabled 鈫?no sources found, M8_sim skipped")
    endif()

    # ======================================================================
    # 娴嬭瘯鐩爣锛坲nit + scenario锛?    # 涓?M8_sim 鍏辩敤鐩稿悓鐨?include 璺緞鍜岀紪璇戦€夐」锛?    # 涓嶄緷璧栫湡鏈?SDK锛坰nack / io_exp / modbus 绛夛級
    # 杩愯锛歝make --build build_sim -j4 && ctest --test-dir build_sim -V
    # ======================================================================
    enable_testing()

    # -----------------------------------------------------------------------
    # 鍏叡 include 璺緞锛堟墍鏈夋祴璇曞叡鐢級
    # -----------------------------------------------------------------------
    set(test_includes
        ${CMAKE_SOURCE_DIR}
    )

    # -----------------------------------------------------------------------
    # 场景测试的基础源文件集：
    #   sim_sources 去掉仿真主入口（platform/sim/），由各测试自带 main()
    # -----------------------------------------------------------------------
    set(scenario_base ${sim_sources})
    list(FILTER scenario_base EXCLUDE REGEX "platform/sim/sim_main\\.c$")
    list(FILTER scenario_base EXCLUDE REGEX "platform/sim/sim_console\\.c$")
    list(FILTER scenario_base EXCLUDE REGEX "tests/")

    # -----------------------------------------------------------------------
    # 辅助宏：添加单元测试
    #   add_unit_test(<name> <test_src> [extra_src ...])
    # -----------------------------------------------------------------------
    macro(add_unit_test name test_src)
        add_executable(${name}
            ${test_src}
            ${ARGN}
            third_party/unity/unity.c
            infrastructure/event_bus/event_bus.c
            ports/port_registry.c
            common/time_util.c
            platform/sim/snack_stub.c
        )
        target_include_directories(${name} PRIVATE
            ${test_includes}
            ${CMAKE_SOURCE_DIR}/third_party/unity
        )
        target_link_libraries(${name} PRIVATE pthread)
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -g)
        add_test(NAME ${name} COMMAND ${name})
    endmacro()

    # -----------------------------------------------------------------------
    # 单元测试（每个测试只链接自身逻辑所需的最小源集）
    # -----------------------------------------------------------------------

    # test_event_bus：只测 event_bus 本身
    add_unit_test(test_event_bus
        tests/unit/test_event_bus.c
    )

    # test_step_engine：已移除（测试的 wash_exec_step/wash_exec_clear_abort
    # 在迁移到 domain/engine 后已从 wash_orchestrator 删除）
    # TODO: 补写 test_engine_orchestrator 覆盖新的 engine 驱动洗车路径

    # test_dev_ctx：只测 dev_ctx 快照与并发写
    add_unit_test(test_dev_ctx
        tests/unit/test_dev_ctx.c
        infrastructure/services/dev_ctx/dev_ctx.c
    )

    # -----------------------------------------------------------------------
    # 方案 JSON 自动再生：YAML 更新时自动重新生成 JSON
    #   - 有 python3+pyyaml：YAML 修改后 cmake --build 即触发再生
    #   - 无 python3：使用已提交的 JSON，跳过自动生成（固件构建/CI 不需要 python）
    #   - 手动强制重生：cmake --build build_sim --target gen_engine_config
    # -----------------------------------------------------------------------
    set(ENGINE_YAML "${CMAKE_SOURCE_DIR}/doc/m8_normal_wash_program.yaml")
    set(ENGINE_JSON "${CMAKE_SOURCE_DIR}/doc/m8_normal_wash_program.json")

    find_program(PYTHON3_EXECUTABLE NAMES python3 python)
    if(PYTHON3_EXECUTABLE)
        add_custom_command(
            OUTPUT  ${ENGINE_JSON}
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/yaml2json.py
                    ${ENGINE_YAML} ${ENGINE_JSON}
            DEPENDS ${ENGINE_YAML} ${CMAKE_SOURCE_DIR}/tools/yaml2json.py
            COMMENT "Regenerating engine config JSON from YAML"
            VERBATIM)
        # 手动触发目标（也接受 OUTPUT 依赖，不重复转换）
        add_custom_target(gen_engine_config DEPENDS ${ENGINE_JSON})
    endif()

    # -----------------------------------------------------------------------
    # 通用控制引擎单元测试（domain/engine + JSON 加载器经 cJSON）
    # -----------------------------------------------------------------------
    add_unit_test(test_engine_expr
        tests/unit/test_engine_expr.c
        domain/engine/engine_expr.c
    )
    target_link_libraries(test_engine_expr PRIVATE m)
    target_compile_definitions(test_engine_expr PRIVATE UNITY_INCLUDE_DOUBLE)

    add_unit_test(test_engine_config
        tests/unit/test_engine_config.c
        adapters/storage/json/engine_program_json.c
        ports/storage/engine_program_loader_port.c
        domain/engine/engine_model.c
        domain/engine/engine_expr.c
        third_party/cJSON/cJSON.c
    )
    target_link_libraries(test_engine_config PRIVATE m)
    target_compile_definitions(test_engine_config PRIVATE
        M8_CONFIG_PATH="${CMAKE_SOURCE_DIR}/doc/m8_normal_wash_program.json"
        UNITY_INCLUDE_DOUBLE)
    if(PYTHON3_EXECUTABLE)
        add_dependencies(test_engine_config gen_engine_config)
    endif()

    add_unit_test(test_engine_runtime
        tests/unit/test_engine_runtime.c
        domain/engine/engine.c
        domain/engine/engine_io.c
        adapters/storage/json/engine_program_json.c
        ports/storage/engine_program_loader_port.c
        domain/engine/engine_model.c
        domain/engine/engine_expr.c
        adapters/hal/sim_hw/engine_io_sim.c
        third_party/cJSON/cJSON.c
    )
    target_link_libraries(test_engine_runtime PRIVATE m)

    # -----------------------------------------------------------------------
    # 引擎端到端场景（仅链接引擎 + JSON 加载器 + sim IO 后端 + 设备模型）
    # -----------------------------------------------------------------------
    add_executable(scenario_engine_normal_wash
        tests/scenario/scenario_engine_normal_wash.c
        tests/support/m8_device_model.c
        domain/engine/engine.c
        domain/engine/engine_io.c
        adapters/storage/json/engine_program_json.c
        ports/storage/engine_program_loader_port.c
        domain/engine/engine_model.c
        domain/engine/engine_expr.c
        adapters/hal/sim_hw/engine_io_sim.c
        third_party/cJSON/cJSON.c
        third_party/unity/unity.c
    )
    target_include_directories(scenario_engine_normal_wash PRIVATE
        ${test_includes}
        ${CMAKE_SOURCE_DIR}/third_party/unity
    )
    target_link_libraries(scenario_engine_normal_wash PRIVATE pthread m)
    target_compile_options(scenario_engine_normal_wash PRIVATE -Wall -Wextra -Werror -g)
    target_compile_definitions(scenario_engine_normal_wash PRIVATE
        M8_CONFIG_PATH="${CMAKE_SOURCE_DIR}/doc/m8_normal_wash_program.json")
    add_test(NAME scenario_engine_normal_wash COMMAND scenario_engine_normal_wash)
    if(PYTHON3_EXECUTABLE)
        add_dependencies(scenario_engine_normal_wash gen_engine_config)
    endif()

    # -----------------------------------------------------------------------
    # 场景集成测试（链接完整 sim 栈，每个测试自带 main()）
    # -----------------------------------------------------------------------
    foreach(scenario
            scenario_standard_wash)

        add_executable(${scenario}
            tests/scenario/${scenario}.c
            ${scenario_base}
        )
        target_include_directories(${scenario} PRIVATE ${test_includes})
        target_link_libraries(${scenario} PRIVATE pthread)
        target_compile_options(${scenario} PRIVATE -Wall -Wextra -Werror -g)
        target_compile_definitions(${scenario} PRIVATE
            WASH_PROGRAM_NORMAL_PATH="${CMAKE_SOURCE_DIR}/doc/m8_normal_wash_program.json"
            WASH_PROGRAM_QUICK_PATH="${CMAKE_SOURCE_DIR}/doc/m8_normal_wash_program.json")
        add_test(NAME ${scenario} COMMAND ${scenario})

    endforeach()

    # -----------------------------------------------------------------------
    # 驱动层单元测试（使用 Modbus / io_exp 桩替代真实硬件库）
    #
    # 关键设计：
    #   - tests/stubs/ 目录下的假头文件（modbus/modbus.h 等）通过优先 include
    #     路径覆盖真实库头，使驱动源码不改动即可链接桩实现
    #   - 测试不调用 drv_io_start，io_exp 桩函数只用于满足链接器，不会被执行
    #   - drv_vfd 的 monitor worker 线程会启动，但测试中立即设置 MON_NONE 防止
    #     后台 Modbus 调用干扰桩计数
    # -----------------------------------------------------------------------
    set(drv_stub_includes
        ${CMAKE_SOURCE_DIR}/tests/stubs   # 假 modbus/io_exp 头文件（优先于系统路径）
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party/unity
    )

    set(drv_common_src
        common/time_util.c
        platform/sim/snack_stub.c
        third_party/unity/unity.c
    )

    set(modbus_stub_src
        tests/support/modbus_stub.c
    )

    # test_drv_vfd：变频器驱动单元测试
    add_executable(test_drv_vfd
        tests/unit/test_drv_vfd.c
        adapters/hal/linux_hw/drv/drv_vfd.c
        ${modbus_stub_src}
        ${drv_common_src}
    )
    target_include_directories(test_drv_vfd PRIVATE ${drv_stub_includes})
    target_link_libraries(test_drv_vfd PRIVATE pthread)
    target_compile_options(test_drv_vfd PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME test_drv_vfd COMMAND test_drv_vfd)

    # test_drv_voice：语音模块驱动单元测试
    add_executable(test_drv_voice
        tests/unit/test_drv_voice.c
        adapters/hal/linux_hw/drv/drv_voice.c
        ${modbus_stub_src}
        ${drv_common_src}
    )
    target_include_directories(test_drv_voice PRIVATE ${drv_stub_includes})
    target_link_libraries(test_drv_voice PRIVATE pthread)
    target_compile_options(test_drv_voice PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME test_drv_voice COMMAND test_drv_voice)

    # test_drv_io：CAN IO 子板驱动单元测试
    add_executable(test_drv_io
        tests/unit/test_drv_io.c
        adapters/hal/linux_hw/drv/drv_io.c
        tests/support/io_exp_stub.c
        ${drv_common_src}
    )
    target_include_directories(test_drv_io PRIVATE ${drv_stub_includes})
    target_link_libraries(test_drv_io PRIVATE pthread)
    target_compile_options(test_drv_io PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME test_drv_io COMMAND test_drv_io)

    message(STATUS "Tests registered: 8 unit + 2 scenario (run with: ctest --test-dir build_sim -V)")

endif()
