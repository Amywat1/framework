#include "application/use_cases/process_formal_command.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "application/coordinators/runtime_event_recorder.h"
#include "application/coordinators/runtime_result_projection.h"
#include "application/use_cases/query_wash_session_status.h"
#include "platform/linux/main_loop.h"
#include "shared/error_codes.h"
#include "src/application/coordinators/system_context_private.h"

/**
 * @brief 正式命令请求的解析结果
 * @note 用于内部处理；has_trigger 表示命令产生触发事件，requires_queue 表示需要入队
 */
typedef struct formal_command_request_t
{
    bool has_trigger;
    bool requires_queue;
    wash_trigger_event_t wash_trigger_event;
} formal_command_request_t;

/**
 * @brief 将错误码转换为字符串形式
 * @param error_code 错误码
 * @return 错误码对应的字符串；未知错误返回 "unknown_error"
 */
static const char *error_code_to_string(error_code_t error_code)
{
    switch (error_code)
    {
    case ERROR_CODE_OK:
        return "ok";
    case ERROR_CODE_INVALID_ARGUMENT:
        return "invalid_argument";
    case ERROR_CODE_INVALID_STATE:
        return "invalid_state";
    case ERROR_CODE_PRECHECK_FAILED:
        return "precheck_failed";
    case ERROR_CODE_VEHICLE_NOT_ALLOWED:
        return "vehicle_not_allowed";
    case ERROR_CODE_SAFETY_INTERLOCK_ACTIVE:
        return "safety_interlock_active";
    case ERROR_CODE_RESOURCE_UNAVAILABLE:
        return "resource_unavailable";
    case ERROR_CODE_TIMEOUT:
        return "timeout";
    case ERROR_CODE_PARSE_FAILED:
        return "parse_failed";
    case ERROR_CODE_IO_FAILED:
        return "io_failed";
    case ERROR_CODE_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown_error";
    }
}

/**
 * @brief 将会话状态转换为字符串形式
 * @param session_state 会话状态
 * @return 状态对应的字符串；未知状态返回 "none"
 */
static const char *session_state_to_string(session_state_t session_state)
{
    switch (session_state)
    {
    case SESSION_STATE_CREATED:
        return "created";
    case SESSION_STATE_RUNNING:
        return "running";
    case SESSION_STATE_COMPLETED:
        return "completed";
    case SESSION_STATE_ABORTED:
        return "aborted";
    default:
        return "none";
    }
}

/**
 * @brief 将工步执行状态转换为字符串形式
 * @param execution_state 执行状态
 * @return 状态对应的字符串；未知状态返回 "none"
 */
static const char *execution_state_to_string(execution_state_t execution_state)
{
    switch (execution_state)
    {
    case EXECUTION_STATE_RUNNING:
        return "running";
    case EXECUTION_STATE_COMPLETED:
        return "completed";
    case EXECUTION_STATE_ABORTED:
        return "aborted";
    default:
        return "none";
    }
}

static const char *device_state_to_string(device_state_t device_state)
{
    switch (device_state)
    {
    case DEVICE_STATE_INIT:
        return "init";
    case DEVICE_STATE_RECOVERING:
        return "recovering";
    case DEVICE_STATE_IDLE:
        return "idle";
    case DEVICE_STATE_RUNNING:
        return "running";
    case DEVICE_STATE_EXCEPTION:
        return "exception";
    case DEVICE_STATE_STOPPED:
    default:
        return "stopped";
    }
}

static const char *start_rejection_reason(device_state_t device_state)
{
    switch (device_state)
    {
    case DEVICE_STATE_INIT:
        return "device_state_init";
    case DEVICE_STATE_STOPPED:
        return "device_state_stopped";
    case DEVICE_STATE_RECOVERING:
        return "device_state_recovering";
    case DEVICE_STATE_RUNNING:
        return "device_state_running";
    case DEVICE_STATE_EXCEPTION:
        return "device_state_exception";
    case DEVICE_STATE_IDLE:
    default:
        return "device_not_idle";
    }
}

static const char *stop_rejection_reason(void) { return "stop_requires_running"; }

static const char *fault_clear_rejection_reason(void) { return "fault_clear_requires_exception"; }

static const char *lifecycle_state_to_string(segment_lifecycle_state_t lifecycle_state)
{
    switch (lifecycle_state)
    {
    case SEGMENT_LIFECYCLE_ENTERING:
        return "entering";
    case SEGMENT_LIFECYCLE_RUNNING:
        return "running";
    case SEGMENT_LIFECYCLE_EXITING:
        return "exiting";
    case SEGMENT_LIFECYCLE_COMPLETED:
        return "completed";
    case SEGMENT_LIFECYCLE_ABORTED:
        return "aborted";
    case SEGMENT_LIFECYCLE_PENDING:
    default:
        return "pending";
    }
}

static void trim_trailing_whitespace(char *text)
{
    size_t length;

    if (text == 0)
    {
        return;
    }

    length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
    {
        text[length - 1] = '\0';
        length -= 1;
    }
}

static char *skip_leading_whitespace(char *text)
{
    while (text != 0 && *text != '\0' && isspace((unsigned char)*text))
    {
        text += 1;
    }
    return text;
}

static char *take_token(char **cursor)
{
    char *start;
    char *end;

    if (cursor == 0 || *cursor == 0)
    {
        return 0;
    }

    start = skip_leading_whitespace(*cursor);
    if (*start == '\0')
    {
        *cursor = start;
        return 0;
    }

    end = start;
    while (*end != '\0' && !isspace((unsigned char)*end))
    {
        end += 1;
    }
    if (*end != '\0')
    {
        *end = '\0';
        end += 1;
    }

    *cursor = end;
    return start;
}

static char *take_remainder(char **cursor)
{
    char *start;

    if (cursor == 0 || *cursor == 0)
    {
        return 0;
    }

    start = skip_leading_whitespace(*cursor);
    if (*start == '\0')
    {
        *cursor = start;
        return 0;
    }

    trim_trailing_whitespace(start);
    *cursor = start + strlen(start);
    return start;
}

static void write_result_line(char *response_line, size_t response_line_size, const char *result_code, bool accepted,
                              const char *detail)
{
    if (response_line == 0 || response_line_size == 0)
    {
        return;
    }

    snprintf(response_line, response_line_size, "result=%s accepted=%s detail=%s",
             result_code != 0 ? result_code : "unknown", accepted ? "true" : "false", detail != 0 ? detail : "none");
}

static void apply_request_projection(system_context_t system_context, trigger_type_t trigger_type,
                                     const char *entity_id, const char *previous_state, const char *current_state,
                                     const char *latest_result_code, const char *latest_reason_code,
                                     const char *transition_result_code, const char *transition_reason_code,
                                     runtime_event_log_kind_t runtime_event_log_kind)
{
    runtime_result_projection_t runtime_result_projection;

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection, latest_result_code, latest_reason_code);
    runtime_result_projection_set_transition(&runtime_result_projection, TRANSITION_ENTITY_REQUEST, entity_id,
                                             trigger_type, previous_state, current_state, transition_result_code,
                                             transition_reason_code, runtime_event_log_kind);
    runtime_event_recorder_apply_projection(system_context, &runtime_result_projection);
}

static void remember_protocol_error(system_context_t system_context, const char *reason_code)
{
    apply_request_projection(system_context, TRIGGER_TYPE_REJECT, "formal-command", "received", "error", "error",
                             reason_code, "error", reason_code, RUNTIME_EVENT_LOG_REJECTION);
}

static void remember_command_rejection(system_context_t system_context, trigger_type_t trigger_type,
                                       const char *reason_code)
{
    const wash_session_t *wash_session;

    wash_session = system_context_private_wash_session(system_context);
    apply_request_projection(
        system_context, trigger_type,
        (wash_session != 0 && wash_session->session_id[0] != '\0') ? wash_session->session_id : "stdin", "received",
        "rejected", "rejected", reason_code, "rejected", reason_code, RUNTIME_EVENT_LOG_REJECTION);
}

static void remember_submit_rejection(system_context_t system_context, trigger_type_t trigger_type,
                                      const char *reason_code)
{
    apply_request_projection(system_context, trigger_type, "pending-trigger-queue", "prepared", "rejected", "rejected",
                             reason_code, "rejected", reason_code, RUNTIME_EVENT_LOG_REJECTION);
}

static void assign_stdin_trigger_id(system_context_t system_context, wash_trigger_event_t *wash_trigger_event)
{
    if (wash_trigger_event == 0)
    {
        return;
    }

    snprintf(wash_trigger_event->trigger_id, sizeof(wash_trigger_event->trigger_id), "stdin-%lu-%u",
             system_context_current_time_ms(system_context), system_context_pending_trigger_count(system_context));
    strncpy(wash_trigger_event->source, "stdin", sizeof(wash_trigger_event->source) - 1);
    wash_trigger_event->source[sizeof(wash_trigger_event->source) - 1] = '\0';
}

static operation_result_t execute_status(system_context_t system_context, char *response_line,
                                         size_t response_line_size)
{
    char detail[384];
    const char *summary_reason;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    if (!result.ok)
    {
        write_result_line(response_line, response_line_size, error_code_to_string(result.error_code), false,
                          "status_query_failed");
        return result;
    }

    summary_reason = wash_session_status_view.reason_code[0] != '\0' ? wash_session_status_view.reason_code : "none";
    snprintf(detail, sizeof(detail),
             "device=%s session=%s state=%s execution=%s lifecycle=%s stage=%s wait=%s global_fault=%s reason=%s",
             device_state_to_string(wash_session_status_view.device_state),
             wash_session_status_view.session_id[0] != '\0' ? wash_session_status_view.session_id : "none",
             session_state_to_string(wash_session_status_view.session_state),
             execution_state_to_string(wash_session_status_view.execution_state),
             lifecycle_state_to_string(wash_session_status_view.lifecycle_state),
             wash_session_status_view.stage_id[0] != '\0' ? wash_session_status_view.stage_id : "none",
             wash_session_status_view.wait_reason[0] != '\0' ? wash_session_status_view.wait_reason : "none",
             wash_session_status_view.global_fault_present ? "true" : "false", summary_reason);
    write_result_line(response_line, response_line_size, "status", true, detail);
    return operation_result_ok();
}

static operation_result_t prepare_formal_command_request(system_context_t system_context, const char *command_line,
                                                         formal_command_request_t *formal_command_request,
                                                         char *response_line, size_t response_line_size)
{
    char command_buffer[256];
    char *cursor;
    char *command_name;
    char *argument_1;
    char *argument_2;

    if (command_line == 0 || formal_command_request == 0 || response_line == 0 || response_line_size == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    memset(formal_command_request, 0, sizeof(*formal_command_request));
    memset(response_line, 0, response_line_size);

    strncpy(command_buffer, command_line, sizeof(command_buffer) - 1);
    command_buffer[sizeof(command_buffer) - 1] = '\0';
    trim_trailing_whitespace(command_buffer);
    cursor = skip_leading_whitespace(command_buffer);
    if (*cursor == '\0')
    {
        remember_protocol_error(system_context, "empty_command");
        write_result_line(response_line, response_line_size, "parse_failed", false, "empty_command");
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    command_name = take_token(&cursor);
    argument_1 = take_token(&cursor);
    argument_2 = take_remainder(&cursor);

    if (strcmp(command_name, "start") == 0)
    {
        device_state_t device_state;

        if (argument_1 == 0 || argument_1[0] == '\0' || argument_2 != 0)
        {
            remember_protocol_error(system_context, "start_requires_program_id");
            write_result_line(response_line, response_line_size, "parse_failed", false, "start_requires_program_id");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        device_state = system_context_private_device_state(system_context);
        if (device_state != DEVICE_STATE_IDLE)
        {
            const char *reason_code;

            reason_code = start_rejection_reason(device_state);
            remember_command_rejection(system_context, TRIGGER_TYPE_START, reason_code);
            write_result_line(response_line, response_line_size, "invalid_state", false, reason_code);
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }
        if (wash_session_is_running(system_context_private_wash_session(system_context)))
        {
            remember_command_rejection(system_context, TRIGGER_TYPE_START, "running_session_exists");
            write_result_line(response_line, response_line_size, "invalid_state", false, "running_session_exists");
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_START, argument_1, 0,
                                "start-command", system_context_current_time_ms(system_context));
        assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "homing") == 0)
    {
        device_state_t device_state;

        if (argument_1 != 0)
        {
            remember_protocol_error(system_context, "homing_takes_no_argument");
            write_result_line(response_line, response_line_size, "parse_failed", false, "homing_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        device_state = system_context_private_device_state(system_context);
        if (device_state != DEVICE_STATE_STOPPED)
        {
            remember_command_rejection(system_context, TRIGGER_TYPE_HOMING, "homing_requires_stopped");
            write_result_line(response_line, response_line_size, "invalid_state", false, "homing_requires_stopped");
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_HOMING, 0, "homing",
                                "homing-command", system_context_current_time_ms(system_context));
        assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "stop") == 0)
    {
        device_state_t device_state;

        if (argument_1 != 0)
        {
            remember_protocol_error(system_context, "stop_takes_no_argument");
            write_result_line(response_line, response_line_size, "parse_failed", false, "stop_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        device_state = system_context_private_device_state(system_context);
        if (device_state != DEVICE_STATE_RUNNING ||
            !wash_session_is_running(system_context_private_wash_session(system_context)))
        {
            remember_command_rejection(system_context, TRIGGER_TYPE_STOP, stop_rejection_reason());
            write_result_line(response_line, response_line_size, "invalid_state", false, stop_rejection_reason());
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_STOP, 0, "manual-stop",
                                "stop-command", system_context_current_time_ms(system_context));
        assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "fault") == 0)
    {
        if (argument_1 == 0 || argument_1[0] == '\0')
        {
            remember_protocol_error(system_context, "fault_requires_code");
            write_result_line(response_line, response_line_size, "parse_failed", false, "fault_requires_code");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        if (strcmp(argument_1, "clear") == 0)
        {
            device_state_t device_state;

            if (argument_2 != 0 && argument_2[0] != '\0')
            {
                remember_protocol_error(system_context, "fault_clear_takes_no_argument");
                write_result_line(response_line, response_line_size, "parse_failed", false,
                                  "fault_clear_takes_no_argument");
                return operation_result_fail(ERROR_CODE_PARSE_FAILED);
            }

            device_state = system_context_private_device_state(system_context);
            if (device_state != DEVICE_STATE_EXCEPTION)
            {
                remember_command_rejection(system_context, TRIGGER_TYPE_FAULT, fault_clear_rejection_reason());
                write_result_line(response_line, response_line_size, "invalid_state", false,
                                  fault_clear_rejection_reason());
                return operation_result_fail(ERROR_CODE_INVALID_STATE);
            }

            wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_FAULT, 0, "clear", 0,
                                    system_context_current_time_ms(system_context));
            assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
            return operation_result_ok();
        }

        if (argument_2 == 0 || argument_2[0] == '\0')
        {
            remember_protocol_error(system_context, "fault_requires_reason");
            write_result_line(response_line, response_line_size, "parse_failed", false, "fault_requires_reason");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_FAULT, 0, argument_1,
                                argument_2, system_context_current_time_ms(system_context));
        assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "status") == 0)
    {
        if (argument_1 != 0)
        {
            remember_protocol_error(system_context, "status_takes_no_argument");
            write_result_line(response_line, response_line_size, "parse_failed", false, "status_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        formal_command_request->requires_queue = false;
        formal_command_request->has_trigger = false;
        return execute_status(system_context, response_line, response_line_size);
    }

    remember_protocol_error(system_context, "unsupported_command");
    write_result_line(response_line, response_line_size, "unsupported", false, "unsupported_command");
    return operation_result_fail(ERROR_CODE_UNSUPPORTED);
}

static operation_result_t finalize_formal_command_response(system_context_t system_context, operation_result_t result,
                                                           char *response_line, size_t response_line_size)
{
    bool accepted;
    const char *detail;
    const char *result_code;

    if (response_line == 0 || response_line_size == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (!result.ok)
    {
        result_code = error_code_to_string(result.error_code);
        detail = system_context_last_reason_code(system_context)[0] != '\0'
                     ? system_context_last_reason_code(system_context)
                     : result_code;
        write_result_line(response_line, response_line_size, result_code, false, detail);
        return result;
    }

    result_code = system_context_last_result_code(system_context)[0] != '\0'
                      ? system_context_last_result_code(system_context)
                      : "accepted";
    detail = system_context_last_reason_code(system_context)[0] != '\0'
                 ? system_context_last_reason_code(system_context)
                 : "none";
    accepted = strcmp(result_code, "ignored") != 0 && strcmp(result_code, "rejected") != 0 &&
               strcmp(result_code, "error") != 0;
    write_result_line(response_line, response_line_size, result_code, accepted, detail);
    return result;
}

operation_result_t process_formal_command_execute(system_context_t system_context, const char *command_line,
                                                  char *response_line, size_t response_line_size)
{
    formal_command_request_t formal_command_request;
    operation_result_t result;

    result = system_context_private_require_active(system_context);
    if (!result.ok)
    {
        write_result_line(response_line, response_line_size, error_code_to_string(result.error_code), false,
                          "invalid_system_context");
        return result;
    }

    result = prepare_formal_command_request(system_context, command_line, &formal_command_request, response_line,
                                            response_line_size);
    if (!result.ok || !formal_command_request.has_trigger || !formal_command_request.requires_queue)
    {
        return result;
    }

    result = main_loop_submit_trigger(system_context, &formal_command_request.wash_trigger_event);
    if (!result.ok)
    {
        remember_submit_rejection(system_context, formal_command_request.wash_trigger_event.trigger_type,
                                  "trigger_queue_full");
        return finalize_formal_command_response(system_context, result, response_line, response_line_size);
    }
    write_result_line(response_line, response_line_size, "accepted", true, "queued");
    return result;
}
