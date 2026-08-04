set(WDF_ROOT ${CMAKE_SOURCE_DIR})

add_compile_definitions(
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/params.json"
    DEPLOY_STORE_JSON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/device.json"
)

file(GLOB _demo_app CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c)
set(_demo_wiring
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/wiring_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/project_hooks_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/demo_machine_ops.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wiring/demo_alarm_catalog.c
)

# demo 只补齐框架分层目标未涵盖的部分：
#   - 安全端口仿真实现与急停仿真状态（属于 sim 装配选择）
#
# recovery / cutout / abort_home 改用 wdf_application 里的真实实现，
# 不再链接 tests/stubs/wash_orchestrator_stub.c：三者只依赖
# common/ports/runtime，demo 已具备，跑真实实现才能验证这段链路。
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
# wdf_observation_bridge：bootstrap 引用观测桥接入口，任何走 bootstrap 的可执行
# 文件都要提供该符号。Demo 未初始化观测，桥接会在启动时自动跳过。
target_link_libraries(wdf_smoke PRIVATE
    wdf_application
    wdf_observation_bridge
    wdf_services
    wdf_storage_json
    wdf_hal_sim
    pthread
)
