#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_PRIVATE_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_PRIVATE_H

#include <stddef.h>
#include <stdbool.h>
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
#include "domain/services/program_snapshot_service.h"
#include "domain/services/wash_execution_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "shared/timeouts.h"

/**
 * @file system_context_private.h
 * @brief 定义组合根的私有内部状态。
 */

operation_result_t system_context_private_require_active(const system_context_t system_context);
operation_result_t system_context_private_bind_scheduler(system_context_t system_context);
void system_context_private_unbind_scheduler(system_context_t system_context);
bool system_context_private_has_scheduler_binding(const system_context_t system_context);
device_state_t system_context_private_device_state(const system_context_t system_context);
void system_context_private_set_device_state(system_context_t system_context, device_state_t device_state);
const actuator_port_t *system_context_private_actuator_port(const system_context_t system_context);
const sensor_port_t *system_context_private_sensor_port(const system_context_t system_context);
bool system_context_private_global_fault_present(const system_context_t system_context);
const char *system_context_private_global_fault_reason(const system_context_t system_context);
void system_context_private_set_global_fault(system_context_t system_context, const char *fault_code, const char *fault_reason);
void system_context_private_clear_global_fault(system_context_t system_context);
const state_transition_record_t *system_context_private_last_transition_record(const system_context_t system_context);
state_transition_record_t *system_context_private_last_transition_record_mutable(system_context_t system_context);
void system_context_private_set_latest_result(system_context_t system_context,
    const char *result_code,
    const char *reason_code);
const event_logger_port_t *system_context_private_event_logger_port(const system_context_t system_context);
void system_context_private_build_session_service_args(system_context_t system_context,
    wash_session_service_args_t *wash_session_service_args);
void system_context_private_build_program_snapshot_service_args(system_context_t system_context,
    program_snapshot_service_args_t *program_snapshot_service_args);
void system_context_private_build_execution_service_args(system_context_t system_context,
    wash_execution_service_args_t *wash_execution_service_args);
const wash_session_t *system_context_private_wash_session(const system_context_t system_context);
wash_session_t *system_context_private_wash_session_mutable(system_context_t system_context);
const wash_execution_t *system_context_private_wash_execution(const system_context_t system_context);
wash_execution_t *system_context_private_wash_execution_mutable(system_context_t system_context);
const wait_condition_t *system_context_private_wait_condition(const system_context_t system_context);
wait_condition_t *system_context_private_wait_condition_mutable(system_context_t system_context);
const program_snapshot_t *system_context_private_program_snapshot(const system_context_t system_context);
void system_context_private_advance_time(system_context_t system_context, unsigned long elapsed_ms);
operation_result_t system_context_private_append_trigger(system_context_t system_context, const wash_trigger_event_t *wash_trigger_event);
const wash_trigger_event_t *system_context_private_pending_trigger_at(const system_context_t system_context, unsigned int index);
void system_context_private_remove_pending_trigger_at(system_context_t system_context, unsigned int remove_index);
bool system_context_private_debug_is_in_use(const system_context_t system_context);

#endif
