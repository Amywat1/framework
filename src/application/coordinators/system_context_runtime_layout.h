#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_RUNTIME_LAYOUT_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_RUNTIME_LAYOUT_H

#include <stdatomic.h>

#include "src/application/coordinators/system_context_private.h"

#define MAX_EXTERNAL_TRIGGER_QUEUE_COUNT 8u

/**
 * @file system_context_runtime_layout.h
 * @brief 暴露组合根运行时布局的白盒内部头，仅供单实例组合根实现与测试观察使用。
 * @note 本文件使用 C11 _Generic 泛型选择宏，要求 GCC 4.9+ 或等效 C11 兼容工具链。
 */
typedef struct system_context_runtime_t
{
    wash_program_t wash_program;
    vehicle_type_t vehicle_type;
    wash_session_t wash_session;
    wash_execution_t wash_execution;
    wait_condition_t wait_condition;
    program_snapshot_t program_snapshot;
    runtime_snapshot_t runtime_snapshot;
    state_transition_record_t last_transition_record;
    wash_trigger_event_t pending_triggers[MAX_PENDING_TRIGGER_COUNT];
    wash_trigger_event_t external_triggers[MAX_EXTERNAL_TRIGGER_QUEUE_COUNT];
    unsigned int pending_trigger_count;
    unsigned long current_time_ms;
    unsigned long next_session_sequence;
    unsigned long next_execution_sequence;
    unsigned long next_wait_condition_sequence;
    atomic_uint external_trigger_read_index;
    atomic_uint external_trigger_write_index;
    atomic_uint external_trigger_count;
    device_state_t device_state;
    bool global_fault_present;
    char global_fault_code[64];
    char global_fault_reason[128];
    char last_result_code[32];
    char last_reason_code[64];
    void *scheduler_binding;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    event_logger_port_t event_logger_port;
    program_repository_port_t program_repository_port;
} system_context_runtime_t;

typedef struct system_context_instance_state_t
{
    bool in_use;
    system_context_runtime_t runtime;
} system_context_instance_state_t;

system_context_runtime_t *system_context_private_runtime_mutable(system_context_t system_context);
const system_context_runtime_t *system_context_private_runtime_const(const system_context_t system_context);

#define system_context_private_runtime(system_context)                                                                 \
    _Generic((system_context),                                                                                         \
        const system_context_t: system_context_private_runtime_const,                                                  \
        system_context_t: system_context_private_runtime_mutable,                                                      \
        default: system_context_private_runtime_mutable)(system_context)

#endif
