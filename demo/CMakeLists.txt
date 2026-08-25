set(WDF_ROOT ${CMAKE_SOURCE_DIR})

add_compile_definitions(
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/params.json"
    DEPLOY_STORE_JSON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/device.json"
)

file(GLOB _demo_app CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c)
set(_demo_wiring
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/wiring_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/project_hooks_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/demo_device_ops.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/demo_alarm_catalog.c
)

# demo 只补齐框架分层目标未涵盖的部分：
#   - 安全端口仿真实现与急停仿真状态（属于 sim 装配选择）
#
# recovery / cutout / abort_home 改用 wdf_application 里的真实实现，
# 不再链接 tests/stubs/wash_orchestrator_stub.c：三者只依赖
# common/ports/runtime，demo 已具备，跑真实实现才能验证这段链路。
#
# 这两个仿真文件刻意不导出为公共 CMake 目标，按路径直接装配，理由见
# scripts/check_arch_boundary.sh 的 UNEXPORTED_SOURCES。
set(_fw_demo_extra_src
    ${WDF_ROOT}/adapters/outbound/safety/sim/hw_estop_sim.c
    ${WDF_ROOT}/adapters/outbound/safety/sim/safety_sim.c
)

add_executable(wdf_smoke
    ${_demo_app}
    ${_demo_wiring}
    ${_fw_demo_extra_src}
)

target_include_directories(wdf_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(wdf_smoke PRIVATE -Wall -Wextra -Werror -g)

# 分层目标按依赖传递：wdf_application 会带入 domain / runtime / ports / common。
# 不链接 wdf_observation_bridge：桥接改为项目在 init_adapters 中显式启用后，
# bootstrap 不再引用该符号，未启用观测的 demo 也就不必提供它。
target_link_libraries(wdf_smoke PRIVATE
    wdf_application
    wdf_estop_poll
    wdf_storage_json
    wdf_hal_sim
    pthread
)

# 纳入 ctest：wdf_smoke 是全仓唯一的跨层链路存在性证明（行为契约里唯一一条 L4）。
# 此前它不在 ctest 内，于是长期以退出码 1 的状态腐化而无人察觉——而"demo 用例与
# 框架语义脱节"和"某次改动真的打断了急停链路"这两种失败表现完全一样。不进门禁，
# 这一级验证等于不存在。
#
# 超时给 60s 与单元测试一致：正常运行约 0.3s，余量留给非特权环境下急停采集线程
# 被降级为 SCHED_OTHER 时的调度延迟。
if(WDF_BUILD_TESTS)
    add_test(NAME wdf_smoke COMMAND wdf_smoke)
    set_tests_properties(wdf_smoke PROPERTIES
        LABELS "framework;smoke"
        TIMEOUT 60
    )
endif()
