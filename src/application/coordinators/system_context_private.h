#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_PRIVATE_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_PRIVATE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "application/coordinators/system_context.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/state_transition_record.h"
#include "domain/model/vehicle_type.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_session.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/event_logger_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/timeouts.h"

/**
 * @file system_context_private.h
 * @brief 定义组合根的私有内部状态。
 */

#define SYSTEM_CONTEXT_POOL_CAPACITY 8u
#define SYSTEM_CONTEXT_HANDLE_GENERATION_WINDOW 65536u
#define SYSTEM_CONTEXT_POOL_INVALID_INDEX ((unsigned int)(~0u))

typedef struct system_context_runtime_t {
    wash_program_t wash_program;
    vehicle_type_t vehicle_type;
    wash_session_t wash_session;
    wash_execution_t wash_execution;
    wait_condition_t wait_condition;
    program_snapshot_t program_snapshot;
    runtime_snapshot_t runtime_snapshot;
    state_transition_record_t last_transition_record;
    wash_trigger_event_t pending_triggers[MAX_PENDING_TRIGGER_COUNT];
    unsigned int pending_trigger_count;
    unsigned long current_time_ms;
    unsigned long next_session_sequence;
    unsigned long next_execution_sequence;
    unsigned long next_wait_condition_sequence;
    bool global_fault_present;
    char global_fault_code[64];
    char global_fault_reason[128];
    char last_result_code[32];
    char last_reason_code[64];
    bool scheduler_bound;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    event_logger_port_t event_logger_port;
    program_repository_port_t program_repository_port;
} system_context_runtime_t;

struct system_context_handle {
    uintptr_t opaque_handle_token;
};

typedef struct system_context_pool_slot_t {
    bool in_use;
    unsigned int next_free_index;
    unsigned int generation;
    system_context_runtime_t runtime;
} system_context_pool_slot_t;

typedef struct system_context_pool_state_t {
    bool initialized;
    unsigned int free_head_index;
    unsigned int free_count;
    system_context_pool_slot_t slots[SYSTEM_CONTEXT_POOL_CAPACITY];
} system_context_pool_state_t;

operation_result_t system_context_private_require_active(const system_context_t system_context);
operation_result_t system_context_private_bind_scheduler(system_context_t system_context);
void system_context_private_unbind_scheduler(system_context_t system_context);
bool system_context_private_has_scheduler_binding(const system_context_t system_context);
unsigned int system_context_private_slot_index(const system_context_t system_context);
system_context_runtime_t *system_context_private_runtime_mutable(system_context_t system_context);
const system_context_runtime_t *system_context_private_runtime_const(const system_context_t system_context);
#define system_context_private_runtime(system_context) \
    _Generic((system_context), \
        const system_context_t: system_context_private_runtime_const, \
        system_context_t: system_context_private_runtime_mutable, \
        default: system_context_private_runtime_mutable)(system_context)
unsigned int system_context_private_debug_free_count(void);
unsigned int system_context_private_debug_capacity(void);
bool system_context_private_debug_is_in_use(const system_context_t system_context);

#endif
