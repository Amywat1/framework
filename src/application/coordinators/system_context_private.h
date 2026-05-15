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
 * @brief 定义系统上下文协调层的私有内部接口。
 */

operation_result_t system_context_private_require_active(const system_context_t system_context);
operation_result_t system_context_private_complete_initialization(system_context_t system_context);
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
/**
 * @brief 统一写入设备运行时落点。
 * @param system_context 系统上下文；必须为当前激活实例。
 * @param device_state 需要落到的设备状态。
 * @param result_code 最新结果码；传入 `0` 时保持现有结果码不变。
 * @param reason_code 最新原因码；传入 `0` 时保持现有原因码不变。
 */
void system_context_private_apply_device_runtime_result(system_context_t system_context,
    device_state_t device_state,
    const char *result_code,
    const char *reason_code);
/**
 * @brief 应用 start accepted 路径的会话启动落点。
 * @param system_context 系统上下文；调用前要求 session start 已成功。
 * @param wash_session 可变洗车会话；调用后会清空上次关联键。
 * @param wash_execution 可变执行快照；调用后会重置执行状态并启动首段。
 * @return 首段启动结果；成功时会落到 running，并写入 accepted/session_started。
 */
operation_result_t system_context_private_apply_start_accepted(system_context_t system_context,
    wash_session_t *wash_session,
    wash_execution_t *wash_execution);
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
