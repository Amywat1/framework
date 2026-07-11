set(WDF_ROOT ${CMAKE_SOURCE_DIR})

# 辅助宏：添加单元测试（每个测试只链接自身逻辑所需的最小源集）
# 用法：add_unit_test(<name> <test_src> [extra_src ...])
macro(add_unit_test name test_src)
    add_executable(${name}
        ${test_src}
        ${ARGN}
        ${WDF_ROOT}/third_party/unity/unity.c
    )
    target_include_directories(${name} PRIVATE
        ${WDF_ROOT}
        ${WDF_ROOT}/third_party/unity
    )
    target_link_libraries(${name} PRIVATE pthread)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# ---------------------------------------------------------------------------
# 单元测试
# 运行：cmake --build build -j4 && ctest --test-dir build -V
# ---------------------------------------------------------------------------

add_unit_test(test_event_bus
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_event_bus.c
    ${WDF_ROOT}/runtime/event_bus/event_bus.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_hal_io_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_hal_io_sim.c
    ${WDF_ROOT}/adapters/outbound/hal/sim/hal_io_sim.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_pulse_out
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_pulse_out.c
    ${WDF_ROOT}/common/pulse_out.c
    ${WDF_ROOT}/common/time_util.c
)

add_unit_test(test_io_handle
    ${CMAKE_CURRENT_SOURCE_DIR}/common/test_io_handle.c
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

add_unit_test(test_json_param_store
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/test_json_param_store.c
    ${WDF_ROOT}/adapters/outbound/storage/json/json_param_store.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_json_param_store PRIVATE
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_BINARY_DIR}/tests/param_store_test.json"
)

add_unit_test(test_scheduler
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_scheduler.c
    ${WDF_ROOT}/runtime/scheduler/thread_registry.c
    ${WDF_ROOT}/runtime/scheduler/scheduler.c
    ${WDF_ROOT}/runtime/scheduler/periodic_task.c
    ${WDF_ROOT}/common/log.c
)

add_unit_test(test_svc_param
    ${CMAKE_CURRENT_SOURCE_DIR}/services/test_svc_param.c
    ${WDF_ROOT}/services/param/svc_param.c
    ${WDF_ROOT}/adapters/outbound/storage/json/json_param_store.c
    ${WDF_ROOT}/ports/port_registry.c
    ${WDF_ROOT}/common/log.c
    ${WDF_ROOT}/third_party/cJSON/cJSON.c
)
target_compile_definitions(test_svc_param PRIVATE
    PARAM_STORE_JSON_FILE_PATH="${CMAKE_BINARY_DIR}/tests/svc_param_test.json"
)
