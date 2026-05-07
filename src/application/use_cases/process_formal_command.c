#include "application/use_cases/process_formal_command.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "application/coordinators/runtime_event_recorder.h"
#include "application/coordinators/runtime_result_projection.h"
#include "application/use_cases/query_wash_session_status.h"
#include "platform/linux/main_loop.h"
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

static void apply_request_projection(system_context_t *system_context,
    trigger_type_t trigger_type,
    const char *entity_id,
    const char *previous_state,
    const char *current_state,
    const char *latest_result_code,
    const char *latest_reason_code,
    const char *transition_result_code,
    const char *transition_reason_code,
    runtime_event_log_kind_t runtime_event_log_kind)
{
    runtime_result_projection_t runtime_result_projection;

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection, latest_result_code, latest_reason_code);
    runtime_result_projection_set_transition(&runtime_result_projection,
        TRANSITION_ENTITY_REQUEST,
        entity_id,
        trigger_type,
        previous_state,
        current_state,
        transition_result_code,
        transition_reason_code,
        runtime_event_log_kind);
    runtime_event_recorder_apply_projection(system_context, &runtime_result_projection);
}

static void remember_protocol_error(system_context_t *system_context, const char *reason_code)
{
    apply_request_projection(system_context,
        TRIGGER_TYPE_REJECT,
        "formal-command",
        "received",
        "error",
        "error",
        reason_code,
        "error",
        reason_code,
        RUNTIME_EVENT_LOG_REJECTION);
}

static void remember_command_rejection(system_context_t *system_context,
    trigger_type_t trigger_type,
    const char *reason_code)
{
    apply_request_projection(system_context,
        trigger_type,
        system_context->wash_session.session_id[0] != '\0' ? system_context->wash_session.session_id : "stdin",
        "received",
        "rejected",
        "rejected",
        reason_code,
        "rejected",
        reason_code,
        RUNTIME_EVENT_LOG_REJECTION);
}

static void remember_submit_rejection(system_context_t *system_context,
    trigger_type_t trigger_type,
    const char *reason_code)
{
    apply_request_projection(system_context,
        trigger_type,
        "pending-trigger-queue",
        "prepared",
        "rejected",
        "rejected",
        reason_code,
        "rejected",
        reason_code,
        RUNTIME_EVENT_LOG_REJECTION);
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
    wash_trigger_event->source[sizeof(wash_trigger_event->source) - 1] = '\0';
}

static bool has_pending_trigger_id(const system_context_t *system_context, const char *trigger_id)
{
    unsigned int index;

    if (system_context == 0 || trigger_id == 0 || trigger_id[0] == '\0') {
        return false;
    }

    for (index = 0; index < system_context->pending_trigger_count; ++index) {
        if (strcmp(system_context->pending_triggers[index].trigger_id, trigger_id) == 0) {
            return true;
        }
    }

    return false;
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

operation_result_t process_formal_command_prepare_line(system_context_t *system_context,
    const char *command_line,
    formal_command_request_t *formal_command_request,
    char *response_line,
    size_t response_line_size)
{
    char command_buffer[256];
    char *cursor;
    char *command_name;
    char *argument_1;
    char *argument_2;

    if (system_context == 0 || command_line == 0 || formal_command_request == 0
        || response_line == 0 || response_line_size == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    memset(formal_command_request, 0, sizeof(*formal_command_request));
    memset(response_line, 0, response_line_size);

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

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event,
            TRIGGER_TYPE_START,
            argument_1,
            0,
            "start-command",
            system_context->current_time_ms);
        assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "stop") == 0) {
        if (argument_1 != 0) {
            remember_protocol_error(system_context, "stop_takes_no_argument");
            write_result_line(response_line, response_line_size, "parse_failed", false, "stop_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event,
            TRIGGER_TYPE_STOP,
            0,
            "manual-stop",
            "stop-command",
            system_context->current_time_ms);
        assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "feedback") == 0) {
        if (argument_1 == 0 || argument_1[0] == '\0' || argument_2 != 0) {
            remember_protocol_error(system_context, "feedback_requires_signal_code");
            write_result_line(response_line, response_line_size, "parse_failed", false, "feedback_requires_signal_code");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event,
            TRIGGER_TYPE_DEVICE_FEEDBACK,
            0,
            argument_1,
            argument_1,
            system_context->current_time_ms);
        assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "fault") == 0) {
        if (argument_1 == 0 || argument_1[0] == '\0') {
            remember_protocol_error(system_context, "fault_requires_code");
            write_result_line(response_line, response_line_size, "parse_failed", false, "fault_requires_code");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        if (strcmp(argument_1, "clear") == 0) {
            if (argument_2 != 0 && argument_2[0] != '\0') {
                remember_protocol_error(system_context, "fault_clear_takes_no_argument");
                write_result_line(response_line, response_line_size, "parse_failed", false, "fault_clear_takes_no_argument");
                return operation_result_fail(ERROR_CODE_PARSE_FAILED);
            }

            wash_trigger_event_init(&formal_command_request->wash_trigger_event,
                TRIGGER_TYPE_FAULT,
                0,
                "clear",
                0,
                system_context->current_time_ms);
            assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
            return operation_result_ok();
        }

        if (argument_2 == 0 || argument_2[0] == '\0') {
            remember_protocol_error(system_context, "fault_requires_reason");
            write_result_line(response_line, response_line_size, "parse_failed", false, "fault_requires_reason");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        wash_trigger_event_init(&formal_command_request->wash_trigger_event,
            TRIGGER_TYPE_FAULT,
            0,
            argument_1,
            argument_2,
            system_context->current_time_ms);
        assign_stdin_trigger_id(system_context, &formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "status") == 0) {
        if (argument_1 != 0) {
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

operation_result_t process_formal_command_finalize_response(system_context_t *system_context,
    operation_result_t result,
    char *response_line,
    size_t response_line_size)
{
    bool accepted;
    const char *detail;
    const char *result_code;

    if (system_context == 0 || response_line == 0 || response_line_size == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (!result.ok) {
        result_code = error_code_to_string(result.error_code);
        detail = system_context->last_reason_code[0] != '\0'
            ? system_context->last_reason_code
            : result_code;
        write_result_line(response_line, response_line_size, result_code, false, detail);
        return result;
    }

    result_code = system_context->last_result_code[0] != '\0'
        ? system_context->last_result_code
        : "accepted";
    detail = system_context->last_reason_code[0] != '\0'
        ? system_context->last_reason_code
        : "none";
    accepted = strcmp(result_code, "ignored") != 0
        && strcmp(result_code, "rejected") != 0
        && strcmp(result_code, "error") != 0;
    write_result_line(response_line, response_line_size, result_code, accepted, detail);
    return result;
}

operation_result_t process_formal_command_execute(system_context_t *system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size)
{
    formal_command_request_t formal_command_request;
    operation_result_t result;

    result = process_formal_command_prepare_line(system_context,
        command_line,
        &formal_command_request,
        response_line,
        response_line_size);
    if (!result.ok || !formal_command_request.has_trigger || !formal_command_request.requires_queue) {
        return result;
    }

    result = main_loop_submit_trigger(system_context, &formal_command_request.wash_trigger_event);
    if (!result.ok) {
        remember_submit_rejection(system_context,
            formal_command_request.wash_trigger_event.trigger_type,
            "trigger_queue_full");
        return process_formal_command_finalize_response(system_context, result, response_line, response_line_size);
    }

    while (has_pending_trigger_id(system_context, formal_command_request.wash_trigger_event.trigger_id)) {
        result = main_loop_run(system_context);
        if (!result.ok && has_pending_trigger_id(system_context, formal_command_request.wash_trigger_event.trigger_id)) {
            return result;
        }
    }

    return process_formal_command_finalize_response(system_context, result, response_line, response_line_size);
}
