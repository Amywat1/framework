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

# -------------------------------------------------------
# 项目部署/参数存储路径注入（framework/adapters/outbound/storage/json 通用
# 适配器本身不知道具体项目路径，由项目在此处通过编译宏提供；
# 默认值取自 projects/m8/config/m8_deploy_defaults.h 的 M8 部署路径）
# -------------------------------------------------------
add_compile_definitions(
    PARAM_STORE_JSON_FILE_PATH="/home/neardi/m8/params.json"
    DEPLOY_STORE_JSON_FILE_PATH="/home/neardi/m8/device.json"
)

set(MCC_INCLUDE ${CMAKE_SOURCE_DIR}/third_party/mcc/motor_control_core-1.0.0-aarch64/include)

# =========================================================================
# 源文件分组
#
# 目录结构对应 doc/软件设计框架说明.md：
#   framework/   跨项目复用能力（common/runtime/services/domain/application/ports/adapters）
#   projects/m8/ M8 项目落地资产（config/bindings/adapters/app/wiring/programs/tests）
#
# 每组使用 GLOB_RECURSE + CONFIGURE_DEPENDS：目录内增删文件后自动重新 configure，
# 不需要手工维护文件清单；目标之间按模块粒度组合，而不是对整仓库整体 glob 后再
# 事后排除，避免迁移或新增模块时静默漏收/误收文件。
# =========================================================================

file(GLOB_RECURSE fw_common_src        CONFIGURE_DEPENDS framework/common/*.c framework/common/*.h)
file(GLOB_RECURSE fw_runtime_src       CONFIGURE_DEPENDS framework/runtime/*.c framework/runtime/*.h)
file(GLOB_RECURSE fw_services_src      CONFIGURE_DEPENDS framework/services/*.c framework/services/*.h)
file(GLOB_RECURSE fw_domain_src        CONFIGURE_DEPENDS framework/domain/*.c framework/domain/*.h)
file(GLOB_RECURSE fw_application_src   CONFIGURE_DEPENDS framework/application/*.c framework/application/*.h)
file(GLOB_RECURSE fw_ports_src         CONFIGURE_DEPENDS framework/ports/*.c framework/ports/*.h)
file(GLOB_RECURSE fw_adapters_cloud_src   CONFIGURE_DEPENDS framework/adapters/outbound/cloud/*.c framework/adapters/outbound/cloud/*.h)
file(GLOB_RECURSE fw_adapters_hal_components_src CONFIGURE_DEPENDS framework/adapters/outbound/hal/components/*.c framework/adapters/outbound/hal/components/*.h)
file(GLOB_RECURSE fw_adapters_hal_snack_src  CONFIGURE_DEPENDS framework/adapters/outbound/hal/providers/snack/*.c framework/adapters/outbound/hal/providers/snack/*.h)
file(GLOB_RECURSE fw_adapters_hal_mcc_src    CONFIGURE_DEPENDS framework/adapters/outbound/hal/providers/mcc/*.c framework/adapters/outbound/hal/providers/mcc/*.h)
file(GLOB_RECURSE fw_adapters_hal_sim_src     CONFIGURE_DEPENDS framework/adapters/outbound/hal/sim/*.c framework/adapters/outbound/hal/sim/*.h)
file(GLOB_RECURSE fw_adapters_storage_src CONFIGURE_DEPENDS framework/adapters/outbound/storage/*.c framework/adapters/outbound/storage/*.h)
file(GLOB_RECURSE fw_adapters_runtime_snack_src CONFIGURE_DEPENDS framework/adapters/runtime/snack/*.cpp framework/adapters/runtime/snack/*.h)

# 仿真构建只需要 components/ 下机型无关的组合层（sensor/do_group/vfd）以及
# electric-motor 端口的通用适配器（motor exec 端口 -> MCC，MCC 本身经
# tests/stubs/mcc/motor_control_core_sim.c 提供仿真实现）；pulse_out 已迁移到
# framework/common/，由 fw_common_src 统一收录。
set(fw_adapters_hal_components_sim_src
    framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.c
    framework/adapters/outbound/hal/components/do_group_mapper/hal_do_group_mapper.c
    framework/adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.c
    framework/adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c
)

file(GLOB_RECURSE m8_config_src   CONFIGURE_DEPENDS projects/m8/config/*.c projects/m8/config/*.h)
file(GLOB_RECURSE m8_bindings_src CONFIGURE_DEPENDS projects/m8/bindings/*.c projects/m8/bindings/*.h)
file(GLOB_RECURSE m8_adapters_alarm_src CONFIGURE_DEPENDS projects/m8/adapters/alarm/*.c projects/m8/adapters/alarm/*.h)
file(GLOB_RECURSE m8_adapters_cli_src   CONFIGURE_DEPENDS projects/m8/adapters/cli/*.c projects/m8/adapters/cli/*.h)
file(GLOB_RECURSE m8_adapters_cloud_src CONFIGURE_DEPENDS projects/m8/adapters/cloud/*.c projects/m8/adapters/cloud/*.h)

# projects/m8/adapters/hal/ 内混装真机与仿真各一个机型专属实现，按目标分别选用
set(m8_adapters_hal_io_src     projects/m8/adapters/hal/m8_io_adapter.c)
set(m8_adapters_hal_vfdsim_src projects/m8/adapters/hal/hal_vfd_sim_m8.c)

set(m8_adapters_runtime_src
    projects/m8/adapters/m8_runtime_adapter.cpp
    projects/m8/adapters/m8_runtime_adapter.h
)

set(m8_app_src     projects/m8/app/app_main.cpp)
file(GLOB m8_app_sim_src CONFIGURE_DEPENDS projects/m8/app/sim/*.c)

set(m8_wiring_src     projects/m8/wiring/wiring.c projects/m8/wiring/project_hooks.c)
set(m8_wiring_sim_src projects/m8/wiring/wiring_sim.c projects/m8/wiring/project_hooks_sim.c)

# 仿真构建不需要真机专属绑定：上电安全初始化（m8_boot_profile）与 VFD/语音
# 参数下发（m8_vfd_setup/m8_voice_setup）只在真机路径由 project_hooks.c 调用；
# 仿真侧由 project_hooks_sim.c 提供对应钩子的空实现/仿真实现（见 framework/
# runtime/bootstrap/project_hooks.h）
set(m8_bindings_sim_src ${m8_bindings_src})
list(FILTER m8_bindings_sim_src EXCLUDE REGEX "m8_boot_profile\\.(c|h)$")
list(FILTER m8_bindings_sim_src EXCLUDE REGEX "m8_vfd_setup\\.(c|h)$")
list(FILTER m8_bindings_sim_src EXCLUDE REGEX "m8_voice_setup\\.(c|h)$")

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

set(m8_bindings_real_src ${m8_bindings_src})
list(FILTER m8_bindings_real_src EXCLUDE REGEX "m8_signal_sim\\.(c|h)$")

set(src
    ${m8_app_src}
    ${fw_common_src}
    ${fw_runtime_src}
    ${fw_services_src}
    ${fw_domain_src}
    ${fw_application_src}
    ${fw_ports_src}
    ${fw_adapters_cloud_src}
    ${fw_adapters_hal_components_src}
    ${fw_adapters_hal_snack_src}
    ${fw_adapters_hal_mcc_src}
    ${fw_adapters_runtime_snack_src}
    ${fw_adapters_storage_src}
    ${m8_config_src}
    ${m8_bindings_real_src}
    ${m8_adapters_alarm_src}
    ${m8_adapters_cli_src}
    ${m8_adapters_cloud_src}
    ${m8_adapters_runtime_src}
    ${m8_adapters_hal_io_src}
    ${m8_wiring_src}
    ${CMAKE_SOURCE_DIR}/third_party/cJSON/cJSON.c
)

add_executable(${PROJECT_NAME} ${src})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}                         # 项目根目录：framework/ 与 projects/ 均以此为 include 根
    ${MCC_INCLUDE}
    /usr/local/include/snack
    /usr/local/include/snack/canfestival
    ${BLE_LIB_INCLUDE_DIRS}
    ${WIFI_LIB_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/mcc/motor_control_core-1.0.0-aarch64/lib/libmotor_control_core.a
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
# framework/adapters/outbound/hal/sim/ 提供 HAL 仿真实现，不依赖嵌入式 SDK
# 用法：cmake -B build_sim -DBUILD_SIM=ON && cmake --build build_sim
#
# 注：仿真源文件齐全（sim/ + bootstrap + wiring_sim 等），
#     若 GLOB 返回空列表则跳过 M8_sim target 创建。
# ======================================================================
if(BUILD_SIM)
    add_compile_definitions(BUILD_SIM=1)

    # -----------------------------------------------------------------------
    # sim 构建源文件：按模块变量组合，避免误拉入真机专属文件
    # -----------------------------------------------------------------------
    set(sim_sources
        # 领域核心（纯逻辑，无硬件依赖）
        ${fw_domain_src}
        ${fw_application_src}
        ${fw_services_src}

        # 事件总线 + 调度器 + 启动编排
        ${fw_runtime_src}

        # 仿真构建使用 wiring_sim.c 完成依赖接线
        ${m8_wiring_sim_src}

        # 端口层
        ${fw_ports_src}

        # 仿真 HAL 实现（sim：io/vfd/motor 专属；generic：sensor/do_group 共用）
        ${fw_adapters_hal_sim_src}
        ${fw_adapters_hal_components_sim_src}

        # 存储适配器（param + deploy，路径可能与真机不同但接口相同）
        ${fw_adapters_storage_src}

        # M8 机型配置表
        ${m8_config_src}

        # M8 机型适配（不含真机专属的 boot_profile/vfd_setup/voice_setup）
        ${m8_bindings_sim_src}
        ${m8_adapters_alarm_src}

        # 物模型点表通用引擎（机型无关，纯逻辑，不依赖云 SDK）+ M8 物模型点表
        ${fw_adapters_cloud_src}
        ${m8_adapters_cloud_src}

        # M8 VFD 仿真绑定（机型专属：仅龙门支持反转等规则）
        ${m8_adapters_hal_vfdsim_src}

        # 仿真主入口 + 交互控制台
        ${m8_app_sim_src}

        # 公共层
        ${fw_common_src}
    )
    # Provider 适配器依赖真机 SDK，仿真不拉入 snack/io_exp provider
    list(FILTER sim_sources EXCLUDE REGEX "/cloud/providers/snack/")
    list(FILTER sim_sources EXCLUDE REGEX "/hal/providers/snack/")

    # 内置 cJSON（替代系统 libcjson-dev）
    list(APPEND sim_sources ${CMAKE_SOURCE_DIR}/third_party/cJSON/cJSON.c)

    # MCC 仿真桩（替代 aarch64 预编译库，供 x86_64 sim 构建使用）
    list(APPEND sim_sources ${CMAKE_SOURCE_DIR}/tests/stubs/mcc/motor_control_core_sim.c)

    if(sim_sources)
        add_executable(M8_sim ${sim_sources})

        target_include_directories(M8_sim PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/projects/m8/app/sim
            ${MCC_INCLUDE}
        )

        target_link_libraries(M8_sim PRIVATE pthread)

        target_compile_options(M8_sim PRIVATE -Wall -Wextra -Werror -g)
        target_compile_definitions(M8_sim PRIVATE
            WASH_PROGRAM_NORMAL_PATH="${CMAKE_SOURCE_DIR}/projects/m8/programs/m8_normal_wash_program.json"
            WASH_PROGRAM_QUICK_PATH="${CMAKE_SOURCE_DIR}/projects/m8/programs/m8_normal_wash_program.json")

        message(STATUS "BUILD_SIM enabled -> target M8_sim")
    else()
        message(STATUS "BUILD_SIM enabled -> no sources found, M8_sim skipped")
    endif()

    # ======================================================================
    # 测试目标（unit + scenario）
    # 与 M8_sim 共用相同的 include 路径和编译选项，不依赖真机 SDK
    # （snack / io_exp / modbus 等）
    # 运行：cmake --build build_sim -j4 && ctest --test-dir build_sim -V
    # ======================================================================
    enable_testing()

    # -----------------------------------------------------------------------
    # 公共 include 路径（所有测试共用）
    # -----------------------------------------------------------------------
    set(test_includes
        ${CMAKE_SOURCE_DIR}
        ${MCC_INCLUDE}
    )

    # -----------------------------------------------------------------------
    # 场景测试的基础源文件集：
    #   sim_sources 去掉仿真主入口（projects/m8/app/sim/sim_main.c、sim_console.c），
    #   由各测试自带 main()
    # -----------------------------------------------------------------------
    set(scenario_base ${sim_sources})
    list(FILTER scenario_base EXCLUDE REGEX "app/sim/sim_main\\.c$")
    list(FILTER scenario_base EXCLUDE REGEX "app/sim/sim_console\\.c$")

    # -----------------------------------------------------------------------
    # 辅助宏：添加单元测试
    #   add_unit_test(<name> <test_src> [extra_src ...])
    # -----------------------------------------------------------------------
    macro(add_unit_test name test_src)
        add_executable(${name}
            ${test_src}
            ${ARGN}
            third_party/unity/unity.c
            framework/runtime/event_bus/event_bus.c
            framework/ports/port_registry.c
            framework/common/time_util.c
            framework/common/log.c
            projects/m8/app/sim/snack_stub.c
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
        tests/framework/runtime/test_event_bus.c
    )

    # test_dev_ctx：只测 dev_ctx 快照与并发写
    add_unit_test(test_dev_ctx
        tests/framework/application/test_dev_ctx.c
        framework/services/dev_ctx/dev_ctx.c
    )

    # test_m8_tsl_table：M8 物模型点位表上行序列化 + 下行分发（项目级测试）
    add_unit_test(test_m8_tsl_table
        projects/m8/tests/test_m8_tsl_table.c
        framework/adapters/outbound/cloud/tsl/tsl_report.c
        framework/adapters/outbound/cloud/tsl/tsl_command.c
        projects/m8/adapters/cloud/m8_tsl_table.c
        projects/m8/bindings/m8_sensor.c
        projects/m8/config/m8_signal_table.c
        framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.c
        framework/services/dev_ctx/dev_ctx.c
        framework/application/orchestrators/report_aggregator.c
        framework/runtime/scheduler/thread_registry.c
        framework/runtime/scheduler/periodic_task.c
        third_party/cJSON/cJSON.c
    )

    # -----------------------------------------------------------------------
    # 方案 JSON 自动再生：YAML 更新时自动重新生成 JSON
    #   - 有 python3+pyyaml：YAML 修改后 cmake --build 即触发再生
    #   - 无 python3：使用已提交的 JSON，跳过自动生成（固件构建/CI 不需要 python）
    #   - 手动强制重生：cmake --build build_sim --target gen_engine_config
    # -----------------------------------------------------------------------
    set(ENGINE_YAML "${CMAKE_SOURCE_DIR}/projects/m8/programs/m8_normal_wash_program.yaml")
    set(ENGINE_JSON "${CMAKE_SOURCE_DIR}/projects/m8/programs/m8_normal_wash_program.json")

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
    # 通用控制引擎单元测试（framework/domain/wash/engine + JSON 加载器经 cJSON）
    # -----------------------------------------------------------------------
    add_unit_test(test_engine_expr
        tests/framework/domain/test_engine_expr.c
        framework/domain/wash/engine/engine_expr.c
    )
    target_link_libraries(test_engine_expr PRIVATE m)
    target_compile_definitions(test_engine_expr PRIVATE UNITY_INCLUDE_DOUBLE)

    add_unit_test(test_engine_config
        tests/framework/domain/test_engine_config.c
        framework/adapters/outbound/storage/json/engine_program_json.c
        framework/ports/outbound/storage/engine_program_loader_port.c
        framework/domain/wash/model/engine_model.c
        framework/domain/wash/engine/engine_expr.c
        third_party/cJSON/cJSON.c
    )
    target_link_libraries(test_engine_config PRIVATE m)
    target_compile_definitions(test_engine_config PRIVATE
        M8_CONFIG_PATH="${CMAKE_SOURCE_DIR}/projects/m8/programs/m8_normal_wash_program.json"
        UNITY_INCLUDE_DOUBLE)
    if(PYTHON3_EXECUTABLE)
        add_dependencies(test_engine_config gen_engine_config)
    endif()

    add_unit_test(test_engine_runtime
        tests/framework/domain/test_engine_runtime.c
        framework/domain/wash/engine/engine.c
        framework/domain/wash/engine/engine_io.c
        framework/adapters/outbound/storage/json/engine_program_json.c
        framework/ports/outbound/storage/engine_program_loader_port.c
        framework/domain/wash/model/engine_model.c
        framework/domain/wash/engine/engine_expr.c
        framework/adapters/outbound/hal/sim/engine_io_sim.c
        third_party/cJSON/cJSON.c
    )
    target_link_libraries(test_engine_runtime PRIVATE m)

    # -----------------------------------------------------------------------
    # 引擎端到端场景（仅链接引擎 + JSON 加载器 + sim IO 后端 + 设备模型，项目级）
    # -----------------------------------------------------------------------
    add_executable(scenario_engine_normal_wash
        projects/m8/tests/scenario/scenario_engine_normal_wash.c
        projects/m8/tests/scenario/m8_device_model.c
        framework/domain/wash/engine/engine.c
        framework/domain/wash/engine/engine_io.c
        framework/adapters/outbound/storage/json/engine_program_json.c
        framework/ports/outbound/storage/engine_program_loader_port.c
        framework/domain/wash/model/engine_model.c
        framework/domain/wash/engine/engine_expr.c
        framework/adapters/outbound/hal/sim/engine_io_sim.c
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
        M8_CONFIG_PATH="${CMAKE_SOURCE_DIR}/projects/m8/programs/m8_normal_wash_program.json")
    add_test(NAME scenario_engine_normal_wash COMMAND scenario_engine_normal_wash)
    if(PYTHON3_EXECUTABLE)
        add_dependencies(scenario_engine_normal_wash gen_engine_config)
    endif()

    # -----------------------------------------------------------------------
    # 场景集成测试（链接完整 sim 栈，每个测试自带 main()，项目级）
    # -----------------------------------------------------------------------
    foreach(scenario
            scenario_standard_wash)

        add_executable(${scenario}
            projects/m8/tests/scenario/${scenario}.c
            ${scenario_base}
        )
        target_include_directories(${scenario} PRIVATE ${test_includes})
        target_link_libraries(${scenario} PRIVATE pthread)
        target_compile_options(${scenario} PRIVATE -Wall -Wextra -Werror -g)
        target_compile_definitions(${scenario} PRIVATE
            WASH_PROGRAM_NORMAL_PATH="${CMAKE_SOURCE_DIR}/projects/m8/programs/m8_normal_wash_program.json"
            WASH_PROGRAM_QUICK_PATH="${CMAKE_SOURCE_DIR}/projects/m8/programs/m8_normal_wash_program.json")
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
        framework/common/time_util.c
        framework/common/log.c
        projects/m8/app/sim/snack_stub.c
        third_party/unity/unity.c
    )

    set(modbus_stub_src
        tests/support/modbus_stub.c
    )

    # test_drv_vfd：变频器驱动单元测试
    add_executable(test_drv_vfd
        tests/framework/adapters/test_drv_vfd.c
        framework/adapters/outbound/hal/providers/snack/modbus/drv_vfd.c
        framework/adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.c
        ${modbus_stub_src}
        ${drv_common_src}
    )
    target_include_directories(test_drv_vfd PRIVATE ${drv_stub_includes})
    target_link_libraries(test_drv_vfd PRIVATE pthread)
    target_compile_options(test_drv_vfd PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME test_drv_vfd COMMAND test_drv_vfd)

    # test_drv_voice：语音模块驱动单元测试
    add_executable(test_drv_voice
        tests/framework/adapters/test_drv_voice.c
        framework/adapters/outbound/hal/providers/snack/modbus/drv_voice.c
        framework/adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.c
        ${modbus_stub_src}
        ${drv_common_src}
    )
    target_include_directories(test_drv_voice PRIVATE ${drv_stub_includes})
    target_link_libraries(test_drv_voice PRIVATE pthread)
    target_compile_options(test_drv_voice PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME test_drv_voice COMMAND test_drv_voice)

    # test_drv_io：io_exp CAN IO 子板 provider 单元测试
    add_executable(test_drv_io
        tests/framework/adapters/test_drv_io.c
        framework/adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.c
        tests/support/io_exp_stub.c
        ${drv_common_src}
    )
    target_include_directories(test_drv_io PRIVATE ${drv_stub_includes})
    target_link_libraries(test_drv_io PRIVATE pthread)
    target_compile_options(test_drv_io PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME test_drv_io COMMAND test_drv_io)

    # -----------------------------------------------------------------------
    # HAL 通用适配层单元测试（mock hal_io_ops_t，无硬件依赖）
    # -----------------------------------------------------------------------

    # test_hal_sensor：DI 通道滤波防抖逻辑
    add_unit_test(test_hal_sensor
        tests/framework/adapters/test_hal_sensor.c
        framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.c
        framework/runtime/scheduler/periodic_task.c
        framework/runtime/scheduler/thread_registry.c
    )

    # test_hal_do_group：DO 组×槽位绑定与输出
    add_unit_test(test_hal_do_group
        tests/framework/adapters/test_hal_do_group.c
        framework/adapters/outbound/hal/components/do_group_mapper/hal_do_group_mapper.c
    )

    # -----------------------------------------------------------------------
    # HAL 仿真层单元测试（纯内存实现，无硬件依赖）
    # -----------------------------------------------------------------------

    # test_hal_io_sim：IO 仿真后端（DI/DO/脉冲计数器）
    add_unit_test(test_hal_io_sim
        tests/framework/adapters/test_hal_io_sim.c
        framework/adapters/outbound/hal/sim/hal_io_sim.c
    )

    # test_hal_vfd_sim：变频器仿真 HAL（状态机 + 参数校验，M8 机型专属绑定，项目级）
    add_unit_test(test_hal_vfd_sim
        projects/m8/tests/test_hal_vfd_sim.c
        projects/m8/adapters/hal/hal_vfd_sim_m8.c
        framework/adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.c
        framework/common/pulse_out.c
        framework/runtime/scheduler/periodic_task.c
        framework/runtime/scheduler/thread_registry.c
    )

    # -----------------------------------------------------------------------
    # 机构领域层单元测试（使用 MCC 仿真桩替代 aarch64 预编译库；
    # 机构模块经 hal_motor_exec_adapter 转发到 MCC，因此需一并链接该适配器）
    # -----------------------------------------------------------------------

    # test_gantry：龙门行走机构领域层（初始化 / 运动命令 / 状态映射 / 故障恢复）
    add_unit_test(test_gantry
        tests/framework/domain/test_gantry.c
        framework/domain/device_control/mechanism/gantry.c
        framework/adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c
        tests/stubs/mcc/motor_control_core_sim.c
    )

    # test_brush：刷子机构领域层（薄封装 / 启停 / 调速 / 状态映射 / 故障恢复）
    add_unit_test(test_brush
        tests/framework/domain/test_brush.c
        framework/domain/device_control/mechanism/brush.c
        framework/adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c
        tests/stubs/mcc/motor_control_core_sim.c
    )

    # test_fan：风机机构领域层（初始化 / 启停 / 状态映射 / 故障恢复）
    add_unit_test(test_fan
        tests/framework/domain/test_fan.c
        framework/domain/device_control/mechanism/fan.c
        framework/adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c
        tests/stubs/mcc/motor_control_core_sim.c
    )

    # test_lift：顶刷升降机构领域层（FORWARD=上升 / 状态映射 / 故障恢复）
    add_unit_test(test_lift
        tests/framework/domain/test_lift.c
        framework/domain/device_control/mechanism/lift.c
        framework/adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c
        tests/stubs/mcc/motor_control_core_sim.c
    )

    # test_rear_lock：后轮锁止机构领域层（FORWARD=锁止 / 状态映射 / 故障恢复）
    add_unit_test(test_rear_lock
        tests/framework/domain/test_rear_lock.c
        framework/domain/device_control/mechanism/rear_lock.c
        framework/adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c
        tests/stubs/mcc/motor_control_core_sim.c
    )

    message(STATUS "Tests registered: 20 unit + 2 scenario (run with: ctest --test-dir build_sim -V)")

endif()
