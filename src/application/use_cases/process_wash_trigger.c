#include "application/use_cases/process_wash_trigger.h"

#include <string.h>

#include "application/coordinators/runtime_event_recorder.h"
#include "application/coordinators/runtime_result_projection.h"
#include "domain/model/runtime_state_text.h"
#include "domain/services/program_snapshot_service.h"
#include "domain/services/recovery_state_machine.h"
#include "domain/services/wait_timeout_service.h"
#include "domain/services/wash_execution_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "shared/error_codes.h"
#include "src/application/coordinators/device_runtime_private.h"
#include "src/application/use_cases/command_matrix_reason_codes.h"

/**
 * @brief 从 device_runtime 构建会话服务参数。
 * @return 会话服务参数。
 */
static wash_session_service_args_t build_session_service_args(void)
{
    wash_session_service_args_t wash_session_service_args;

    device_runtime_private_build_session_service_args(&wash_session_service_args);
    return wash_session_service_args;
}

/**
 * @brief 从 device_runtime 构建程序快照服务参数。
 * @return 程序快照服务参数。
 */
static program_snapshot_service_args_t build_program_snapshot_service_args(void)
{
    program_snapshot_service_args_t program_snapshot_service_args;

    device_runtime_private_build_program_snapshot_service_args(&program_snapshot_service_args);
    return program_snapshot_service_args;
}

/**
 * @brief 从 device_runtime 构建工步执行服务参数。
 * @return 工步执行服务参数。
 */
static wash_execution_service_args_t build_execution_service_args(void)
{
    wash_execution_service_args_t wash_execution_service_args;

    device_runtime_private_build_execution_service_args(&wash_execution_service_args);
    return wash_execution_service_args;
}

/**
 * @brief 将触发处理结果投影到运行时结果和转移记录。
 * @param wash_trigger_event 原始触发事件；为空时仅记录结果码。
 * @param previous_device_state 转移前设备状态。
 * @param current_device_state 转移后设备状态。
 * @param result_code 结果码。
 * @param reason_code 原因码。
 */
static void project_trigger_result(const wash_trigger_event_t *wash_trigger_event,
                                   device_state_t previous_device_state, device_state_t current_device_state,
                                   const char *result_code, const char *reason_code)
{
    runtime_result_projection_t runtime_result_projection;

    if (wash_trigger_event == 0)
    {
        runtime_event_recorder_set_latest_result(result_code, reason_code);
        return;
    }

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection, result_code, reason_code);
    runtime_result_projection_set_transition(
        &runtime_result_projection, TRANSITION_ENTITY_REQUEST,
        wash_trigger_event->trigger_id[0] != '\0' ? wash_trigger_event->trigger_id : "internal-trigger",
        wash_trigger_event->trigger_type, runtime_state_text_device_state(previous_device_state),
        runtime_state_text_device_state(current_device_state), result_code, reason_code);
    runtime_event_recorder_apply_projection(&runtime_result_projection);
}

/**
 * @brief 将执行中止结果映射为会话中止结果码。
 * @param wash_execution 当前执行快照；为空时按安全停止处理。
 * @return 与执行终态对应的会话结果码。
 */
static result_code_t map_execution_abort_result(const wash_execution_t *wash_execution)
{
    if (wash_execution == 0)
    {
        return RESULT_CODE_SAFE_STOP;
    }
    if (wash_execution->execution_result == EXECUTION_RESULT_EXIT_TIMEOUT ||
        wash_execution->execution_result == EXECUTION_RESULT_EXIT_FAILED)
    {
        return RESULT_CODE_EXIT_FAILED;
    }
    if (wash_execution->execution_result == EXECUTION_RESULT_SEGMENT_TIMEOUT)
    {
        return RESULT_CODE_SEGMENT_TIMEOUT;
    }
    if (wash_execution->execution_result == EXECUTION_RESULT_STOPPED)
    {
        return RESULT_CODE_MANUAL_ABORT;
    }
    return RESULT_CODE_SAFE_STOP;
}

/**
 * @brief 根据当前执行状态推进下一段或完成会话收尾。
 * @return 推进成功返回 `ok`，状态非法或下游失败时返回对应错误。
 */
static operation_result_t advance_or_finish_session(void)
{
    const program_snapshot_t *program_snapshot;
    const wash_execution_t *wash_execution;
    const wash_session_t *wash_session;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    wash_execution = device_runtime_private_wash_execution();
    wash_session = device_runtime_private_wash_session();
    program_snapshot = device_runtime_private_program_snapshot();
    if (wash_execution == 0 || wash_session == 0 || program_snapshot == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    if (wash_execution->execution_state == EXECUTION_STATE_ABORTED && wash_session_is_running(wash_session))
    {
        wash_session_service_args = build_session_service_args();
        result = wash_session_state_machine_abort(
            &wash_session_service_args, map_execution_abort_result(wash_execution),
            wash_execution->reason_code[0] != '\0' ? wash_execution->reason_code : wash_session->abort_reason,
            &wash_session_transition_fact);
        if (result.ok)
        {
            device_runtime_private_apply_device_runtime_result(
                wash_execution->execution_result == EXECUTION_RESULT_STOPPED ? DEVICE_STATE_STOPPED
                                                                             : DEVICE_STATE_EXCEPTION,
                "aborted", wash_session_transition_fact.reason_code);
        }
        return result;
    }

    if (wash_execution->execution_state != EXECUTION_STATE_COMPLETED ||
        wash_execution->lifecycle_state != SEGMENT_LIFECYCLE_COMPLETED)
    {
        return operation_result_ok();
    }

    if (wash_execution->segment_index + 1 < program_snapshot->frozen_program.segment_count)
    {
        wash_execution_service_args = build_execution_service_args();
        result = wash_execution_service_begin_next_segment(&wash_execution_service_args, &wash_execution_fact);
        if (result.ok)
        {
            runtime_event_recorder_set_latest_result(wash_execution_fact.result_code, wash_execution_fact.reason_code);
        }
        return result;
    }

    wash_session_service_args = build_session_service_args();
    result = wash_session_state_machine_complete(&wash_session_service_args, RESULT_CODE_SUCCESS,
                                                 &wash_session_transition_fact);
    if (result.ok)
    {
        device_runtime_private_apply_device_runtime_result(DEVICE_STATE_IDLE, "completed",
                                                           "program_finished");
    }
    return result;
}

/**
 * @brief 处理 homing 触发并驱动恢复状态机。
 * @return 处理成功返回 `ok`，状态不满足或恢复失败时返回对应错误。
 */
static operation_result_t handle_homing(void)
{
    const wash_session_t *wash_session;
    const actuator_port_t *actuator_port;
    const sensor_port_t *sensor_port;
    const char *failure_reason_code;
    wash_trigger_event_t wash_trigger_event;
    device_state_t device_state;
    operation_result_t result;

    wash_session = device_runtime_private_wash_session();
    actuator_port = device_runtime_private_actuator_port();
    sensor_port = device_runtime_private_sensor_port();
    if (wash_session == 0 || actuator_port == 0 || sensor_port == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    device_state = device_runtime_private_device_state();
    if (wash_session_is_running(wash_session))
    {
        wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_HOMING, 0, "homing", "homing-command",
                                device_runtime_current_time_ms());
        project_trigger_result(&wash_trigger_event, device_state, device_state, "rejected",
                               command_matrix_running_session_exists_reason());
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (device_state != DEVICE_STATE_STOPPED)
    {
        wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_HOMING, 0, "homing", "homing-command",
                                device_runtime_current_time_ms());
        project_trigger_result(&wash_trigger_event, device_state, device_state, "rejected",
                               command_matrix_homing_requires_stopped_reason());
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_HOMING, 0, "homing", "homing-command",
                            device_runtime_current_time_ms());
    device_runtime_private_clear_global_fault();
    device_runtime_private_apply_device_runtime_result(DEVICE_STATE_RECOVERING, 0, 0);
    failure_reason_code = 0;
    result =
        recovery_state_machine_execute(actuator_port, sensor_port, RECOVERY_MODE_HOME_ROOF_BRUSH, &failure_reason_code);
    if (!result.ok || device_runtime_private_global_fault_present())
    {
        device_runtime_private_apply_device_runtime_result(DEVICE_STATE_EXCEPTION, 0, 0);
        project_trigger_result(&wash_trigger_event, DEVICE_STATE_STOPPED, DEVICE_STATE_EXCEPTION,
                               "error",
                               failure_reason_code != 0 ? failure_reason_code
                                                        : (device_runtime_private_global_fault_present()
                                                               ? "recovering_alarm_triggered"
                                                               : "homing_failed"));
        return !result.ok ? result : operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    device_runtime_private_apply_device_runtime_result(DEVICE_STATE_IDLE, 0, 0);
    project_trigger_result(&wash_trigger_event, DEVICE_STATE_STOPPED, DEVICE_STATE_IDLE, "accepted",
                           "homing_completed");
    return operation_result_ok();
}

/**
 * @brief 处理 start 触发并启动新的洗车会话。
 * @param wash_trigger_event 当前触发事件。
 * @return 处理成功返回 `ok`，前置条件不满足或启动失败时返回对应错误。
 */
static operation_result_t handle_start(const wash_trigger_event_t *wash_trigger_event)
{
    device_state_t device_state;
    operation_result_t result;
    const char *reason_code;
    program_snapshot_service_args_t program_snapshot_service_args;
    wash_execution_t *wash_execution;
    wash_session_t *wash_session;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    device_state = device_runtime_private_device_state();
    if (device_state != DEVICE_STATE_IDLE)
    {
        project_trigger_result(wash_trigger_event, device_state, device_state, "rejected",
                               command_matrix_start_rejection_reason(device_state));
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (device_runtime_private_global_fault_present())
    {
        project_trigger_result(wash_trigger_event, device_state, device_state, "rejected",
                               "global_fault_active");
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_session = device_runtime_private_wash_session_mutable();
    wash_execution = device_runtime_private_wash_execution_mutable();
    if (wash_session == 0 || wash_execution == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (wash_session_is_running(wash_session))
    {
        project_trigger_result(wash_trigger_event, device_state, device_state, "rejected",
                               command_matrix_running_session_exists_reason());
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    program_snapshot_service_args = build_program_snapshot_service_args();
    result = program_snapshot_service_capture(&program_snapshot_service_args, wash_trigger_event->program_id);
    if (!result.ok)
    {
        reason_code = result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE ? "program_unavailable" : "program_invalid";
        project_trigger_result(wash_trigger_event, device_state, device_state, "rejected", reason_code);
        return result;
    }

    wash_session_service_args = build_session_service_args();
    result = wash_session_state_machine_start(&wash_session_service_args, wash_trigger_event->program_id,
                                              &wash_session_transition_fact);
    if (!result.ok)
    {
        return result;
    }

    result = device_runtime_private_apply_start_accepted(wash_session, wash_execution);
    if (result.ok)
    {
        project_trigger_result(wash_trigger_event, device_state, DEVICE_STATE_RUNNING, "accepted",
                               wash_session_transition_fact.reason_code);
    }
    return result;
}

/**
 * @brief 处理 stop 触发并中止当前运行会话。
 * @param wash_trigger_event 当前触发事件。
 * @return 处理成功返回 `ok`，状态不满足或中止失败时返回对应错误。
 */
static operation_result_t handle_stop(const wash_trigger_event_t *wash_trigger_event)
{
    device_state_t device_state;
    const wash_session_t *wash_session;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    wash_session = device_runtime_private_wash_session();
    if (wash_session == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    device_state = device_runtime_private_device_state();
    if (device_state != DEVICE_STATE_RUNNING || !wash_session_is_running(wash_session))
    {
        project_trigger_result(wash_trigger_event, device_state, device_state, "rejected",
                               command_matrix_stop_rejection_reason());
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_execution_service_args = build_execution_service_args();
    result = wash_execution_service_handle_stop(&wash_execution_service_args, wash_trigger_event->signal_code,
                                                &wash_execution_fact);
    if (!result.ok)
    {
        return result;
    }

    wash_session_service_args = build_session_service_args();
    result = wash_session_state_machine_abort(&wash_session_service_args, RESULT_CODE_MANUAL_ABORT,
                                              wash_trigger_event->signal_code, &wash_session_transition_fact);
    if (result.ok)
    {
        device_runtime_private_apply_device_runtime_result(DEVICE_STATE_STOPPED, "aborted",
                                                           wash_trigger_event->signal_code);
        project_trigger_result(wash_trigger_event, DEVICE_STATE_RUNNING, DEVICE_STATE_STOPPED,
                               "aborted", wash_trigger_event->signal_code);
    }
    return result;
}

/**
 * @brief 处理 fault 触发，包括故障记录与 clear 分支。
 * @param wash_trigger_event 当前触发事件。
 * @return 处理成功返回 `ok`，状态不满足或下游处理失败时返回对应错误。
 */
static operation_result_t handle_fault(const wash_trigger_event_t *wash_trigger_event)
{
    bool clear_requested;
    device_state_t device_state;
    const wash_session_t *wash_session;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    clear_requested = strcmp(wash_trigger_event->signal_code, "clear") == 0;
    device_state = device_runtime_private_device_state();
    wash_session = device_runtime_private_wash_session();
    if (wash_session == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (clear_requested)
    {
        if (device_state != DEVICE_STATE_EXCEPTION)
        {
            project_trigger_result(wash_trigger_event, device_state, device_state, "rejected",
                                   command_matrix_fault_clear_rejection_reason());
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }
        device_runtime_private_clear_global_fault();
        device_runtime_private_apply_device_runtime_result(DEVICE_STATE_STOPPED, 0, 0);
        project_trigger_result(wash_trigger_event, DEVICE_STATE_EXCEPTION, DEVICE_STATE_STOPPED,
                               "accepted", "global_fault_cleared");
        return operation_result_ok();
    }
    if (!wash_session_is_running(wash_session))
    {
        device_runtime_private_set_global_fault(wash_trigger_event->signal_code,
                                                wash_trigger_event->correlation_key);
        device_runtime_private_apply_device_runtime_result(DEVICE_STATE_EXCEPTION, 0, 0);
        project_trigger_result(wash_trigger_event, device_state, DEVICE_STATE_EXCEPTION, "accepted",
                               "global_fault_recorded");
        return operation_result_ok();
    }
    device_runtime_private_set_global_fault(wash_trigger_event->signal_code,
                                            wash_trigger_event->correlation_key);

    wash_execution_service_args = build_execution_service_args();
    result = wash_execution_service_handle_fault(&wash_execution_service_args, wash_trigger_event->signal_code,
                                                 &wash_execution_fact);
    if (!result.ok)
    {
        return result;
    }

    wash_session_service_args = build_session_service_args();
    result = wash_session_state_machine_abort(&wash_session_service_args, RESULT_CODE_SAFE_STOP,
                                              wash_trigger_event->signal_code, &wash_session_transition_fact);
    if (result.ok)
    {
        device_runtime_private_apply_device_runtime_result(DEVICE_STATE_EXCEPTION, "aborted",
                                                           wash_trigger_event->signal_code);
    }
    return result;
}

/**
 * @brief 处理 timeout 触发并推进执行超时收尾。
 * @return 处理成功返回 `ok`，超时事实或执行处理失败时返回对应错误。
 */
static operation_result_t handle_timeout(void)
{
    operation_result_t result;
    wait_condition_t *wait_condition;
    wait_timeout_fact_t wait_timeout_fact;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;

    wait_condition = device_runtime_private_wait_condition_mutable();
    if (wait_condition == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    result = wait_timeout_service_handle_timeout(wait_condition, device_runtime_current_time_ms(),
                                                 &wait_timeout_fact);
    if (!result.ok)
    {
        return result;
    }

    wash_execution_service_args = build_execution_service_args();
    result = wash_execution_service_handle_timeout(&wash_execution_service_args, &wash_execution_fact);
    if (result.ok)
    {
        runtime_event_recorder_set_latest_result(wash_execution_fact.result_code, wash_execution_fact.reason_code);
        return advance_or_finish_session();
    }
    return result;
}

operation_result_t process_wash_trigger_execute(const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    result = device_runtime_require_active();
    if (!result.ok)
    {
        return result;
    }
    if (wash_trigger_event == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    switch (wash_trigger_event->trigger_type)
    {
    case TRIGGER_TYPE_START:
        return handle_start(wash_trigger_event);
    case TRIGGER_TYPE_HOMING:
        return handle_homing();
    case TRIGGER_TYPE_STOP:
        return handle_stop(wash_trigger_event);
    case TRIGGER_TYPE_FAULT:
        return handle_fault(wash_trigger_event);
    case TRIGGER_TYPE_TIMEOUT:
        return handle_timeout();
    default:
        return operation_result_ok();
    }
}

/**
 * @brief 推进一次运行中会话的执行 tick，并按需驱动会话收尾。
 * @return 推进成功返回 `ok`，状态非法或下游推进失败时返回对应错误。
 */
operation_result_t process_wash_runtime_tick(void)
{
    const wash_session_t *wash_session;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;

    result = device_runtime_require_active();
    if (!result.ok)
    {
        return result;
    }

    wash_session = device_runtime_private_wash_session();
    if (wash_session == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (!wash_session_is_running(wash_session))
    {
        return operation_result_ok();
    }

    wash_execution_service_args = build_execution_service_args();
    result = wash_execution_service_tick(&wash_execution_service_args, &wash_execution_fact);
    if (!result.ok)
    {
        return result;
    }
    if (wash_execution_fact.changed)
    {
        runtime_event_recorder_set_latest_result(wash_execution_fact.result_code, wash_execution_fact.reason_code);
    }
    return advance_or_finish_session();
}
