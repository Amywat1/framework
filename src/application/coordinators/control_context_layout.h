#ifndef APPLICATION_COORDINATORS_CONTROL_CONTEXT_LAYOUT_H
#define APPLICATION_COORDINATORS_CONTROL_CONTEXT_LAYOUT_H

#include <stdatomic.h>

#include "src/application/coordinators/control_context_private.h"

#define MAX_EXTERNAL_TRIGGER_QUEUE_COUNT 8u

/**
 * @file control_context_layout.h
 * @brief 暴露组合根运行时布局的白盒内部头，仅供单实例组合根实现与测试观察使用。
 */
typedef struct control_context_state_t
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
} control_context_state_t;

typedef struct control_context_instance_t
{
    bool initialized;
    control_context_state_t runtime;
} control_context_instance_t;

/** @brief 读取可变运行时状态；实例未激活时返回 `0`。 */
control_context_state_t *control_context_private_runtime_mutable(void);

/** @brief 读取只读运行时状态；实例未激活时返回 `0`。 */
const control_context_state_t *control_context_private_runtime_const(void);

#endif
