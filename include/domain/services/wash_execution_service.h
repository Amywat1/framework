#ifndef DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H
#define DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H

#include <stdbool.h>

#include "domain/model/program_snapshot.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_session.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/ports/actuator_port.h"
#include "shared/result_types.h"

/**
 * @file wash_execution_service.h
 * @brief 定义单次执行推进与等待装配规则。
 */

/**
 * @brief 描述执行服务一次推进后返回给应用层的事实。
 */
typedef struct wash_execution_fact_t {
    bool changed;
    char execution_id[32];
    char previous_state[16];
    char current_state[16];
    char result_code[32];
    char reason_code[64];
    char stage_id[32];
} wash_execution_fact_t;

/**
 * @brief 描述执行服务完成职责所需的最小输入集合。
 */
typedef struct wash_execution_service_args_t {
    wash_execution_t *wash_execution;
    wash_session_t *wash_session;
    wait_condition_t *wait_condition;
    const program_snapshot_t *program_snapshot;
    actuator_port_t *actuator_port;
    unsigned long *next_execution_sequence;
    unsigned long *next_wait_condition_sequence;
    unsigned long current_time_ms;
} wash_execution_service_args_t;

/**
 * @brief 启动下一阶段执行并装配新的等待条件。
 *
 * @param wash_execution_service_args 执行服务输入，不能为空。
 * @param wash_execution_fact 输出执行事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或下发失败时返回失败结果。
 */
operation_result_t wash_execution_service_begin_next_stage(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact);

/**
 * @brief 处理一次匹配当前等待条件的设备反馈。
 *
 * @param wash_execution_service_args 执行服务输入，不能为空。
 * @param wash_trigger_event 当前反馈触发，不能为空。
 * @param wash_execution_fact 输出执行事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或反馈不匹配时返回失败结果。
 */
operation_result_t wash_execution_service_handle_feedback(wash_execution_service_args_t *wash_execution_service_args,
    const wash_trigger_event_t *wash_trigger_event,
    wash_execution_fact_t *wash_execution_fact);

/**
 * @brief 处理中止当前执行的停止请求。
 *
 * @param wash_execution_service_args 执行服务输入，不能为空。
 * @param reason_code 停止原因码。
 * @param wash_execution_fact 输出执行事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_stop(wash_execution_service_args_t *wash_execution_service_args,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact);

/**
 * @brief 处理导致当前执行终止的故障事件。
 *
 * @param wash_execution_service_args 执行服务输入，不能为空。
 * @param reason_code 故障原因码。
 * @param wash_execution_fact 输出执行事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_fault(wash_execution_service_args_t *wash_execution_service_args,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact);

/**
 * @brief 按 timeout-continue 决议结束当前执行并准备切换下一阶段。
 *
 * @param wash_execution_service_args 执行服务输入，不能为空。
 * @param wash_execution_fact 输出执行事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_timeout_continue(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact);

/**
 * @brief 按 timeout-finish 决议结束当前执行。
 *
 * @param wash_execution_service_args 执行服务输入，不能为空。
 * @param wash_execution_fact 输出执行事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_timeout_finish(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact);

/**
 * @brief 按 timeout-abort 决议中止当前执行。
 *
 * @param wash_execution_service_args 执行服务输入，不能为空。
 * @param wash_execution_fact 输出执行事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_execution_service_handle_timeout_abort(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact);

#endif
