#include "adapters/inbound/cli_command_adapter.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "application/use_cases/acknowledge_fault.h"
#include "application/use_cases/process_wash_trigger.h"
#include "application/use_cases/query_wash_session_status.h"
#include "application/use_cases/start_wash_cycle.h"
#include "application/use_cases/stop_wash_cycle.h"
#include "domain/model/state_transition_record.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/error_codes.h"

static const char *error_code_to_string(error_code_t error_code)
{
    switch (error_code) {
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

static const char *session_state_to_string(session_state_t session_state)
{
    switch (session_state) {
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

static const char *execution_state_to_string(execution_state_t execution_state)
{
    switch (execution_state) {
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

static void trim_trailing_whitespace(char *text)
{
    size_t length;

    if (text == 0) {
        return;
    }
    length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        text[length - 1] = '\0';
        length -= 1;
    }
}

static char *skip_leading_whitespace(char *text)
{
    while (text != 0 && *text != '\0' && isspace((unsigned char)*text)) {
        text += 1;
    }
    return text;
}

static char *take_token(char **cursor)
{
    char *start;
    char *end;

    if (cursor == 0 || *cursor == 0) {
        return 0;
    }
    start = skip_leading_whitespace(*cursor);
    if (*start == '\0') {
        *cursor = start;
        return 0;
    }
    end = start;
    while (*end != '\0' && !isspace((unsigned char)*end)) {
        end += 1;
    }
    if (*end != '\0') {
        *end = '\0';
        end += 1;
    }
    *cursor = end;
    return start;
}

static char *take_remainder(char **cursor)
{
    char *start;

    if (cursor == 0 || *cursor == 0) {
        return 0;
    }
    start = skip_leading_whitespace(*cursor);
    if (*start == '\0') {
        *cursor = start;
        return 0;
    }
    trim_trailing_whitespace(start);
    *cursor = start + strlen(start);
    return start;
}

static void remember_protocol_error(system_context_t *system_context, const char *reason_code)
{
    if (system_context == 0) {
        return;
    }

    strncpy(system_context->last_result_code, "error", sizeof(system_context->last_result_code) - 1);
    strncpy(system_context->last_reason_code, reason_code, sizeof(system_context->last_reason_code) - 1);
    state_transition_record_init(&system_context->last_transition_record,
        TRANSITION_ENTITY_REQUEST,
        "stdin",
        TRIGGER_TYPE_REJECT,
        "received",
        "error",
        "error",
        reason_code,
        system_context->current_time_ms);
    if (system_context->event_logger_port.log_rejection != 0) {
        system_context->event_logger_port.log_rejection(system_context->event_logger_port.context, &system_context->last_transition_record);
    }
}

static void remember_command_rejection(system_context_t *system_context,
    trigger_type_t trigger_type,
    const char *reason_code)
{
    if (system_context == 0 || reason_code == 0) {
        return;
    }

    strncpy(system_context->last_result_code, "rejected", sizeof(system_context->last_result_code) - 1);
    strncpy(system_context->last_reason_code, reason_code, sizeof(system_context->last_reason_code) - 1);
    state_transition_record_init(&system_context->last_transition_record,
        TRANSITION_ENTITY_REQUEST,
        system_context->wash_session.session_id[0] != '\0' ? system_context->wash_session.session_id : "stdin",
        trigger_type,
        "received",
        "rejected",
        "rejected",
        reason_code,
        system_context->current_time_ms);
    if (system_context->event_logger_port.log_rejection != 0) {
        system_context->event_logger_port.log_rejection(system_context->event_logger_port.context, &system_context->last_transition_record);
    }
}

static void write_result_line(char *response_line,
    size_t response_line_size,
    const char *result_code,
    bool accepted,
    const char *detail)
{
    if (response_line == 0 || response_line_size == 0) {
        return;
    }

    snprintf(response_line,
        response_line_size,
        "result=%s accepted=%s detail=%s",
        result_code != 0 ? result_code : "unknown",
        accepted ? "true" : "false",
        detail != 0 ? detail : "none");
}

static operation_result_t execute_feedback(system_context_t *system_context, const char *signal_code)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        0,
        signal_code,
        signal_code,
        system_context->current_time_ms);
    return process_wash_trigger_execute(system_context, &wash_trigger_event);
}

static void assign_stdin_trigger_id(system_context_t *system_context, wash_trigger_event_t *wash_trigger_event)
{
    if (system_context == 0 || wash_trigger_event == 0) {
        return;
    }

    snprintf(wash_trigger_event->trigger_id,
        sizeof(wash_trigger_event->trigger_id),
        "stdin-%lu-%u",
        system_context->current_time_ms,
        system_context->pending_trigger_count);
    strncpy(wash_trigger_event->source, "stdin", sizeof(wash_trigger_event->source) - 1);
}

static operation_result_t execute_status(system_context_t *system_context, char *response_line, size_t response_line_size)
{
    char detail[384];
    const char *summary_reason;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    if (!result.ok) {
        write_result_line(response_line,
            response_line_size,
            error_code_to_string(result.error_code),
            false,
            "status_query_failed");
        return result;
    }

    summary_reason = wash_session_status_view.reason_code[0] != '\0'
        ? wash_session_status_view.reason_code
        : "none";
    if (!wash_session_status_view.has_active_session
        && wash_session_status_view.global_fault_present
        && wash_session_status_view.global_fault_reason[0] != '\0') {
        summary_reason = wash_session_status_view.global_fault_reason;
    }
    snprintf(detail,
        sizeof(detail),
        "session=%s state=%s execution=%s stage=%s wait=%s global_fault=%s reason=%s",
        wash_session_status_view.session_id[0] != '\0' ? wash_session_status_view.session_id : "none",
        session_state_to_string(wash_session_status_view.session_state),
        execution_state_to_string(wash_session_status_view.execution_state),
        wash_session_status_view.stage_id[0] != '\0' ? wash_session_status_view.stage_id : "none",
        wash_session_status_view.wait_reason[0] != '\0' ? wash_session_status_view.wait_reason : "none",
        wash_session_status_view.global_fault_present ? "true" : "false",
        summary_reason);
    write_result_line(response_line, response_line_size, "status", true, detail);
    return operation_result_ok();
}

operation_result_t cli_command_adapter_finalize_trigger_response(system_context_t *system_context,
    operation_result_t result,
    char *response_line,
    size_t response_line_size)
{
    bool accepted;
    const char *result_code;
    const char *detail;

    if (!result.ok) {
        result_code = error_code_to_string(result.error_code);
        detail = system_context->last_reason_code[0] != '\0'
            ? system_context->last_reason_code
            : result_code;
        write_result_line(response_line, response_line_size, result_code, false, detail);
        return result;
    }

    accepted = strcmp(system_context->last_result_code, "ignored") != 0
        && strcmp(system_context->last_result_code, "rejected") != 0
        && strcmp(system_context->last_result_code, "error") != 0;
    result_code = system_context->last_result_code[0] != '\0'
        ? system_context->last_result_code
        : "accepted";
    detail = system_context->last_reason_code[0] != '\0'
        ? system_context->last_reason_code
        : "none";
    write_result_line(response_line, response_line_size, result_code, accepted, detail);
    return result;
}

operation_result_t cli_command_adapter_prepare_line(system_context_t *system_context,
    const char *command_line,
    wash_trigger_event_t *wash_trigger_event,
    bool *has_trigger,
    char *response_line,
    size_t response_line_size)
{
    char command_buffer[256];
    char *cursor;
    char *command_name;
    char *argument_1;
    char *argument_2;

    if (system_context == 0 || command_line == 0 || response_line == 0 || response_line_size == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (wash_trigger_event == 0 || has_trigger == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    memset(response_line, 0, response_line_size);
    memset(wash_trigger_event, 0, sizeof(*wash_trigger_event));
    *has_trigger = false;
    strncpy(command_buffer, command_line, sizeof(command_buffer) - 1);
    command_buffer[sizeof(command_buffer) - 1] = '\0';
    trim_trailing_whitespace(command_buffer);
    cursor = skip_leading_whitespace(command_buffer);
    if (*cursor == '\0') {
        remember_protocol_error(system_context, "empty_command");
        write_result_line(response_line, response_line_size, "parse_failed", false, "empty_command");
        return operation_result_fail(ERROR_CODE_PARSE_FAILED);
    }

    command_name = take_token(&cursor);
    argument_1 = take_token(&cursor);
    argument_2 = take_remainder(&cursor);

    if (strcmp(command_name, "start") == 0) {
        if (argument_1 == 0 || argument_1[0] == '\0' || argument_2 != 0) {
            remember_protocol_error(system_context, "start_requires_program_id");
            write_result_line(response_line, response_line_size, "parse_failed", false, "start_requires_program_id");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        if (wash_session_is_running(&system_context->wash_session)) {
            remember_command_rejection(system_context, TRIGGER_TYPE_START, "running_session_exists");
            write_result_line(response_line, response_line_size, "invalid_state", false, "running_session_exists");
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }
        wash_trigger_event_init(wash_trigger_event,
            TRIGGER_TYPE_START,
            argument_1,
            0,
            "start-command",
            system_context->current_time_ms);
        assign_stdin_trigger_id(system_context, wash_trigger_event);
        *has_trigger = true;
        return operation_result_ok();
    }

    if (strcmp(command_name, "stop") == 0) {
        if (argument_1 != 0) {
            remember_protocol_error(system_context, "stop_takes_no_argument");
            write_result_line(response_line, response_line_size, "parse_failed", false, "stop_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        wash_trigger_event_init(wash_trigger_event,
            TRIGGER_TYPE_STOP,
            0,
            "manual-stop",
            "stop-command",
            system_context->current_time_ms);
        assign_stdin_trigger_id(system_context, wash_trigger_event);
        *has_trigger = true;
        return operation_result_ok();
    }

    if (strcmp(command_name, "feedback") == 0) {
        if (argument_1 == 0 || argument_1[0] == '\0' || argument_2 != 0) {
            remember_protocol_error(system_context, "feedback_requires_signal_code");
            write_result_line(response_line, response_line_size, "parse_failed", false, "feedback_requires_signal_code");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        wash_trigger_event_init(wash_trigger_event,
            TRIGGER_TYPE_DEVICE_FEEDBACK,
            0,
            argument_1,
            argument_1,
            system_context->current_time_ms);
        assign_stdin_trigger_id(system_context, wash_trigger_event);
        *has_trigger = true;
        return operation_result_ok();
    }

    if (strcmp(command_name, "fault") == 0) {
        if (argument_1 == 0 || argument_1[0] == '\0') {
            remember_protocol_error(system_context, "fault_requires_code");
            write_result_line(response_line, response_line_size, "parse_failed", false, "fault_requires_code");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        if (strcmp(argument_1, "clear") == 0) {
            if (argument_2 != 0 && argument_2[0] != '\0') {
                remember_protocol_error(system_context, "fault_clear_takes_no_argument");
                write_result_line(response_line, response_line_size, "parse_failed", false, "fault_clear_takes_no_argument");
                return operation_result_fail(ERROR_CODE_PARSE_FAILED);
            }
            wash_trigger_event_init(wash_trigger_event,
                TRIGGER_TYPE_FAULT,
                0,
                "clear",
                0,
                system_context->current_time_ms);
            assign_stdin_trigger_id(system_context, wash_trigger_event);
            *has_trigger = true;
            return operation_result_ok();
        }
        if (argument_2 == 0 || argument_2[0] == '\0') {
            remember_protocol_error(system_context, "fault_requires_reason");
            write_result_line(response_line, response_line_size, "parse_failed", false, "fault_requires_reason");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        wash_trigger_event_init(wash_trigger_event,
            TRIGGER_TYPE_FAULT,
            0,
            argument_1,
            argument_2,
            system_context->current_time_ms);
        assign_stdin_trigger_id(system_context, wash_trigger_event);
        *has_trigger = true;
        return operation_result_ok();
    }

    if (strcmp(command_name, "status") == 0) {
        if (argument_1 != 0) {
            remember_protocol_error(system_context, "status_takes_no_argument");
            write_result_line(response_line, response_line_size, "parse_failed", false, "status_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        return execute_status(system_context, response_line, response_line_size);
    }

    remember_protocol_error(system_context, "unsupported_command");
    write_result_line(response_line, response_line_size, "unsupported", false, "unsupported_command");
    return operation_result_fail(ERROR_CODE_UNSUPPORTED);
}

operation_result_t cli_command_adapter_process_line(system_context_t *system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size)
{
    bool has_trigger;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    result = cli_command_adapter_prepare_line(system_context,
        command_line,
        &wash_trigger_event,
        &has_trigger,
        response_line,
        response_line_size);
    if (!result.ok || !has_trigger) {
        return result;
    }

    switch (wash_trigger_event.trigger_type) {
        case TRIGGER_TYPE_START:
            result = start_wash_cycle_execute(system_context, wash_trigger_event.program_id);
            break;
        case TRIGGER_TYPE_STOP:
            result = stop_wash_cycle_with_reason_execute(system_context, wash_trigger_event.signal_code);
            break;
        case TRIGGER_TYPE_DEVICE_FEEDBACK:
            result = execute_feedback(system_context, wash_trigger_event.signal_code);
            break;
        case TRIGGER_TYPE_FAULT:
            result = acknowledge_fault_execute(system_context,
                wash_trigger_event.signal_code,
                wash_trigger_event.correlation_key[0] != '\0' ? wash_trigger_event.correlation_key : 0);
            break;
        default:
            result = operation_result_fail(ERROR_CODE_UNSUPPORTED);
            break;
    }
    return cli_command_adapter_finalize_trigger_response(system_context, result, response_line, response_line_size);
}
