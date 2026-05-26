#ifndef APPLICATION_COORDINATORS_DEVICE_RUNTIME_LAYOUT_H
#define APPLICATION_COORDINATORS_DEVICE_RUNTIME_LAYOUT_H

#include <stdatomic.h>

#include "src/application/coordinators/device_runtime_private.h"

#define MAX_EXTERNAL_TRIGGER_QUEUE_COUNT 8u

/**
 * @file device_runtime_layout.h
 * @brief 暴露组合根运行时布局的白盒内部头，仅供单实例组合根实现与测试观察使用。
 * @note 本文件使用 C11 _Generic 泛型选择宏，要求 GCC 4.9+ 或等效 C11 兼容工具链。
 */
typedef struct device_runtime_state_t
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
    program_repository_port_t program_repository_port;
} device_runtime_state_t;

typedef struct device_runtime_instance_t
{
    bool in_use;
    device_runtime_state_t runtime;
} device_runtime_instance_t;

device_runtime_state_t *device_runtime_private_runtime_mutable(device_runtime_t device_runtime);
const device_runtime_state_t *device_runtime_private_runtime_const(const device_runtime_t device_runtime);

#define device_runtime_private_runtime(device_runtime)                                                                 \
    _Generic((device_runtime),                                                                                         \
        const device_runtime_t: device_runtime_private_runtime_const,                                                  \
        device_runtime_t: device_runtime_private_runtime_mutable,                                                      \
        default: device_runtime_private_runtime_mutable)(device_runtime)

#endif
