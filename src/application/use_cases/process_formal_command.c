#include "application/use_cases/process_formal_command.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "application/coordinators/runtime_event_recorder.h"
#include "application/coordinators/runtime_result_projection.h"
#include "application/coordinators/control_tick.h"
#include "application/use_cases/query_wash_session_status.h"
#include "domain/model/runtime_state_text.h"
#include "shared/error_codes.h"
#include "src/application/coordinators/device_runtime_private.h"
#include "src/application/use_cases/command_matrix_reason_codes.h"

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
 * @brief 去除字符串末尾的空白字符。
 * @param text 待处理字符串。
 */
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

/**
 * @brief 跳过字符串前导空白。
 * @param text 原始字符串。
 * @return 首个非空白字符位置。
 */
static char *skip_leading_whitespace(char *text)
{
    while (text != 0 && *text != '\0' && isspace((unsigned char)*text))
    {
        text += 1;
    }
    return text;
}

/**
 * @brief 从当前游标读取一个空白分隔 token。
 * @param cursor 命令解析游标。
 * @return 读取到的 token；没有则返回 0。
 */
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

/**
 * @brief 读取游标剩余内容并裁掉首尾多余空白。
 * @param cursor 命令解析游标。
 * @return 剩余文本；没有则返回 0。
 */
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

bool process_formal_command_result_is_accepted(const char *result_code)
{
    if (result_code == 0)
    {
        return true;
    }
    return strcmp(result_code, "ignored") != 0 && strcmp(result_code, "rejected") != 0 &&
           strcmp(result_code, "error") != 0;
}

void process_formal_command_format_response(char *response_line, size_t response_line_size, const char *result_code,
                                            bool accepted, const char *detail)
{
    if (response_line == 0 || response_line_size == 0)
    {
        return;
    }

    snprintf(response_line, response_line_size, "result=%s accepted=%s detail=%s",
             result_code != 0 ? result_code : "unknown", accepted ? "true" : "false", detail != 0 ? detail : "none");
}

/**
 * @brief 将命令处理结果投影到运行时结果记录。
 * @param trigger_type 触发类型。
 * @param entity_id 转移实体标识。
 * @param previous_state 前一状态字符串。
 * @param current_state 当前状态字符串。
 * @param latest_result_code 最新结果码。
 * @param latest_reason_code 最新原因码。
 * @param transition_result_code 转移结果码。
 * @param transition_reason_code 转移原因码。
 */
static void apply_request_projection(trigger_type_t trigger_type,
                                     const char *entity_id, const char *previous_state, const char *current_state,
                                     const char *latest_result_code, const char *latest_reason_code,
                                     const char *transition_result_code, const char *transition_reason_code)
{
    runtime_result_projection_t runtime_result_projection;

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection, latest_result_code, latest_reason_code);
    runtime_result_projection_set_transition(&runtime_result_projection, TRANSITION_ENTITY_REQUEST, entity_id,
                                             trigger_type, previous_state, current_state, transition_result_code,
                                             transition_reason_code);
    runtime_event_recorder_apply_projection(&runtime_result_projection);
}

/**
 * @brief 记录协议层解析错误。
 * @param reason_code 错误原因码。
 */
static void remember_protocol_error(const char *reason_code)
{
    apply_request_projection(TRIGGER_TYPE_REJECT, "formal-command", "received", "error", "error",
                             reason_code, "error", reason_code);
}

/**
 * @brief 记录命令在预检查阶段被拒绝。
 * @param trigger_type 触发类型。
 * @param reason_code 拒绝原因码。
 */
static void remember_command_rejection(trigger_type_t trigger_type, const char *reason_code)
{
    const wash_session_t *wash_session;

    wash_session = device_runtime_private_wash_session();
    apply_request_projection(
        trigger_type,
        (wash_session != 0 && wash_session->session_id[0] != '\0') ? wash_session->session_id : "stdin", "received",
        "rejected", "rejected", reason_code, "rejected", reason_code);
}

/**
 * @brief 记录触发事件提交队列失败。
 * @param trigger_type 触发类型。
 * @param reason_code 拒绝原因码。
 */
static void remember_submit_rejection(trigger_type_t trigger_type, const char *reason_code)
{
    apply_request_projection(trigger_type, "pending-trigger-queue", "prepared", "rejected", "rejected",
                             reason_code, "rejected", reason_code);
}

/**
 * @brief 为 stdin 触发生成内部 trigger_id 和来源。
 * @param wash_trigger_event 待写入的触发事件。
 */
static void assign_stdin_trigger_id(wash_trigger_event_t *wash_trigger_event)
{
    if (wash_trigger_event == 0)
    {
        return;
    }

    snprintf(wash_trigger_event->trigger_id, sizeof(wash_trigger_event->trigger_id), "stdin-%lu-%u",
             device_runtime_current_time_ms(), device_runtime_pending_trigger_count());
    strncpy(wash_trigger_event->source, "stdin", sizeof(wash_trigger_event->source) - 1);
    wash_trigger_event->source[sizeof(wash_trigger_event->source) - 1] = '\0';
}

/**
 * @brief 执行 status 命令并写入响应。
 * @param response_line 响应缓冲区。
 * @param response_line_size 缓冲区大小。
 * @return 执行结果。
 */
static operation_result_t execute_status(char *response_line, size_t response_line_size)
{
    char detail[384];
    const char *summary_reason;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    result = query_wash_session_status_execute(&wash_session_status_view);
    if (!result.ok)
    {
        process_formal_command_format_response(response_line, response_line_size, error_code_to_string(result.error_code), false,
                          "status_query_failed");
        return result;
    }

    summary_reason = wash_session_status_view.reason_code[0] != '\0' ? wash_session_status_view.reason_code : "none";
    snprintf(detail, sizeof(detail),
             "device=%s session=%s state=%s execution=%s lifecycle=%s stage=%s wait=%s global_fault=%s reason=%s",
             runtime_state_text_device_state(wash_session_status_view.device_state),
             wash_session_status_view.session_id[0] != '\0' ? wash_session_status_view.session_id : "none",
             runtime_state_text_session_state(wash_session_status_view.session_state),
             runtime_state_text_execution_state(wash_session_status_view.execution_state),
             runtime_state_text_segment_lifecycle_state(wash_session_status_view.lifecycle_state),
             wash_session_status_view.stage_id[0] != '\0' ? wash_session_status_view.stage_id : "none",
             wash_session_status_view.wait_reason[0] != '\0' ? wash_session_status_view.wait_reason : "none",
             wash_session_status_view.global_fault_present ? "true" : "false", summary_reason);
    process_formal_command_format_response(response_line, response_line_size, "status", true, detail);
    return operation_result_ok();
}

/**
 * @brief 解析正式命令并生成待执行请求。
 * @param command_line 原始命令行。
 * @param formal_command_request 输出的命令请求。
 * @param response_line 响应缓冲区。
 * @param response_line_size 缓冲区大小。
 * @return 解析或预检查结果。
 */
static operation_result_t prepare_formal_command_request(const char *command_line,
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
        remember_protocol_error("empty_command");
        process_formal_command_format_response(response_line, response_line_size, "parse_failed", false, "empty_command");
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
            remember_protocol_error("start_requires_program_id");
            process_formal_command_format_response(response_line, response_line_size, "parse_failed", false, "start_requires_program_id");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }
        device_state = device_runtime_private_device_state();
        if (device_state != DEVICE_STATE_IDLE)
        {
            const char *reason_code;

            reason_code = command_matrix_start_rejection_reason(device_state);
            remember_command_rejection(TRIGGER_TYPE_START, reason_code);
            process_formal_command_format_response(response_line, response_line_size, "invalid_state", false, reason_code);
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }
        if (wash_session_is_running(device_runtime_private_wash_session()))
        {
            remember_command_rejection(TRIGGER_TYPE_START,
                                       command_matrix_running_session_exists_reason());
            process_formal_command_format_response(response_line, response_line_size, "invalid_state", false,
                              command_matrix_running_session_exists_reason());
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_START, argument_1, 0,
                                "start-command", device_runtime_current_time_ms());
        assign_stdin_trigger_id(&formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "homing") == 0)
    {
        device_state_t device_state;

        if (argument_1 != 0)
        {
            remember_protocol_error("homing_takes_no_argument");
            process_formal_command_format_response(response_line, response_line_size, "parse_failed", false, "homing_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        device_state = device_runtime_private_device_state();
        if (device_state != DEVICE_STATE_STOPPED)
        {
            remember_command_rejection(TRIGGER_TYPE_HOMING,
                                       command_matrix_homing_requires_stopped_reason());
            process_formal_command_format_response(response_line, response_line_size, "invalid_state", false,
                              command_matrix_homing_requires_stopped_reason());
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_HOMING, 0, "homing",
                                "homing-command", device_runtime_current_time_ms());
        assign_stdin_trigger_id(&formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "stop") == 0)
    {
        device_state_t device_state;

        if (argument_1 != 0)
        {
            remember_protocol_error("stop_takes_no_argument");
            process_formal_command_format_response(response_line, response_line_size, "parse_failed", false, "stop_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        device_state = device_runtime_private_device_state();
        if (device_state != DEVICE_STATE_RUNNING ||
            !wash_session_is_running(device_runtime_private_wash_session()))
        {
            remember_command_rejection(TRIGGER_TYPE_STOP, command_matrix_stop_rejection_reason());
            process_formal_command_format_response(response_line, response_line_size, "invalid_state", false,
                              command_matrix_stop_rejection_reason());
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_STOP, 0, "manual-stop",
                                "stop-command", device_runtime_current_time_ms());
        assign_stdin_trigger_id(&formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "fault") == 0)
    {
        if (argument_1 == 0 || argument_1[0] == '\0')
        {
            remember_protocol_error("fault_requires_code");
            process_formal_command_format_response(response_line, response_line_size, "parse_failed", false, "fault_requires_code");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        formal_command_request->requires_queue = true;
        formal_command_request->has_trigger = true;
        if (strcmp(argument_1, "clear") == 0)
        {
            device_state_t device_state;

            if (argument_2 != 0 && argument_2[0] != '\0')
            {
                remember_protocol_error("fault_clear_takes_no_argument");
                process_formal_command_format_response(response_line, response_line_size, "parse_failed", false,
                                  "fault_clear_takes_no_argument");
                return operation_result_fail(ERROR_CODE_PARSE_FAILED);
            }

            device_state = device_runtime_private_device_state();
            if (device_state != DEVICE_STATE_EXCEPTION)
            {
                remember_command_rejection(TRIGGER_TYPE_FAULT,
                                           command_matrix_fault_clear_rejection_reason());
                process_formal_command_format_response(response_line, response_line_size, "invalid_state", false,
                                  command_matrix_fault_clear_rejection_reason());
                return operation_result_fail(ERROR_CODE_INVALID_STATE);
            }

            wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_FAULT, 0, "clear", 0,
                                    device_runtime_current_time_ms());
            assign_stdin_trigger_id(&formal_command_request->wash_trigger_event);
            return operation_result_ok();
        }

        if (argument_2 == 0 || argument_2[0] == '\0')
        {
            remember_protocol_error("fault_requires_reason");
            process_formal_command_format_response(response_line, response_line_size, "parse_failed", false, "fault_requires_reason");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        wash_trigger_event_init(&formal_command_request->wash_trigger_event, TRIGGER_TYPE_FAULT, 0, argument_1,
                                argument_2, device_runtime_current_time_ms());
        assign_stdin_trigger_id(&formal_command_request->wash_trigger_event);
        return operation_result_ok();
    }

    if (strcmp(command_name, "status") == 0)
    {
        if (argument_1 != 0)
        {
            remember_protocol_error("status_takes_no_argument");
            process_formal_command_format_response(response_line, response_line_size, "parse_failed", false, "status_takes_no_argument");
            return operation_result_fail(ERROR_CODE_PARSE_FAILED);
        }

        formal_command_request->requires_queue = false;
        formal_command_request->has_trigger = false;
        return execute_status(response_line, response_line_size);
    }

    remember_protocol_error("unsupported_command");
    process_formal_command_format_response(response_line, response_line_size, "unsupported", false, "unsupported_command");
    return operation_result_fail(ERROR_CODE_UNSUPPORTED);
}

/**
 * @brief 根据最终执行结果补全响应文本。
 * @param result 当前执行结果。
 * @param response_line 响应缓冲区。
 * @param response_line_size 缓冲区大小。
 * @return 原始执行结果。
 */
static operation_result_t finalize_formal_command_response(operation_result_t result,
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
        detail = device_runtime_last_reason_code()[0] != '\0'
                     ? device_runtime_last_reason_code()
                     : result_code;
        process_formal_command_format_response(response_line, response_line_size, result_code, false, detail);
        return result;
    }

    result_code = device_runtime_last_result_code()[0] != '\0'
                      ? device_runtime_last_result_code()
                      : "accepted";
    detail = device_runtime_last_reason_code()[0] != '\0'
                 ? device_runtime_last_reason_code()
                 : "none";
    accepted = process_formal_command_result_is_accepted(result_code);
    process_formal_command_format_response(response_line, response_line_size, result_code, accepted, detail);
    return result;
}

operation_result_t process_formal_command_execute(const char *command_line,
                                                  char *response_line, size_t response_line_size)
{
    formal_command_request_t formal_command_request;
    operation_result_t result;

    result = device_runtime_require_active();
    if (!result.ok)
    {
        process_formal_command_format_response(response_line, response_line_size, error_code_to_string(result.error_code), false,
                          "invalid_device_runtime");
        return result;
    }

    result = prepare_formal_command_request(command_line, &formal_command_request, response_line,
                                            response_line_size);
    if (!result.ok || !formal_command_request.has_trigger || !formal_command_request.requires_queue)
    {
        return result;
    }

    result = control_tick_submit_trigger(&formal_command_request.wash_trigger_event);
    if (!result.ok)
    {
        remember_submit_rejection(formal_command_request.wash_trigger_event.trigger_type,
                                  "trigger_queue_full");
        return finalize_formal_command_response(result, response_line, response_line_size);
    }
    process_formal_command_format_response(response_line, response_line_size, "accepted", true, "queued");
    return result;
}
