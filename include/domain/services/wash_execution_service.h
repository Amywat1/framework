#ifndef DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H
#define DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H

#include <stdbool.h>

#include "domain/model/program_snapshot.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_session.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file wash_execution_service.h
 * @brief 定义工步段执行服务。
 */
/**
 * @brief 描述一次工步执行推进产生的事实。
 */
typedef struct wash_execution_fact_t
{
    /**< 本次推进是否产生状态变化。 */
    bool changed;
    /**< 当前执行实例 ID。 */
    char execution_id[32];
    /**< 推进前的生命周期状态。 */
    char previous_state[16];
    /**< 推进后的生命周期状态。 */
    char current_state[16];
    /**< 本次推进对应的结果码。 */
    char result_code[32];
    /**< 本次推进对应的原因码。 */
    char reason_code[64];
    /**< 当前关联的工步段 ID。 */
    char segment_id[32];
} wash_execution_fact_t;

/**
 * @brief 描述工步执行服务所需的最小依赖切片。
 */
typedef struct wash_execution_service_args_t
{
    /**< 当前执行对象。 */
    wash_execution_t *wash_execution;
    /**< 当前会话对象。 */
    wash_session_t *wash_session;
    /**< 当前等待条件对象。 */
    wait_condition_t *wait_condition;
    /**< 已冻结的程序快照。 */
    const program_snapshot_t *program_snapshot;
    /**< 执行机构端口。 */
    actuator_port_t *actuator_port;
    /**< 传感器端口。 */
    sensor_port_t *sensor_port;
    /**< 执行序列号分配器。 */
    unsigned long *next_execution_sequence;
    /**< 等待条件序列号分配器。 */
    unsigned long *next_wait_condition_sequence;
    /**< 当前运行时间。 */
    unsigned long current_time_ms;
} wash_execution_service_args_t;

/**
 * @brief 工步执行服务的最小依赖切片。
 *
 * @note 本切片只包含执行服务完成职责所需的对象、端口和序列号。
 */
/**
 * @brief 装载并启动下一段工步执行。
 * @param wash_execution_service_args 执行服务参数。
 * @param wash_execution_fact 输出执行事实。
 * @return 成功返回 `operation_result_ok()`，参数非法或状态不满足时返回失败结果。
 */
operation_result_t wash_execution_service_begin_next_segment(wash_execution_service_args_t *wash_execution_service_args,
                                                             wash_execution_fact_t *wash_execution_fact);
/**
 * @brief 推进当前工步段执行一次 tick。
 * @param wash_execution_service_args 执行服务参数。
 * @param wash_execution_fact 输出执行事实。
 * @return 成功返回 `operation_result_ok()`，执行失败时返回对应错误。
 */
operation_result_t wash_execution_service_advance_segment(wash_execution_service_args_t *wash_execution_service_args,
                                               wash_execution_fact_t *wash_execution_fact);
/**
 * @brief 处理一次人工 stop 请求。
 * @param wash_execution_service_args 执行服务参数。
 * @param reason_code 停止原因码。
 * @param wash_execution_fact 输出执行事实。
 * @return 成功返回 `operation_result_ok()`，处理失败时返回对应错误。
 */
operation_result_t wash_execution_service_handle_stop(wash_execution_service_args_t *wash_execution_service_args,
                                                      const char *reason_code,
                                                      wash_execution_fact_t *wash_execution_fact);
/**
 * @brief 处理一次 fault 中止请求。
 * @param wash_execution_service_args 执行服务参数。
 * @param reason_code 故障原因码。
 * @param wash_execution_fact 输出执行事实。
 * @return 成功返回 `operation_result_ok()`，处理失败时返回对应错误。
 */
operation_result_t wash_execution_service_handle_fault(wash_execution_service_args_t *wash_execution_service_args,
                                                       const char *reason_code,
                                                       wash_execution_fact_t *wash_execution_fact);
/**
 * @brief 处理一次等待超时事件。
 * @param wash_execution_service_args 执行服务参数。
 * @param wash_execution_fact 输出执行事实。
 * @return 成功返回 `operation_result_ok()`，处理失败时返回对应错误。
 */
operation_result_t wash_execution_service_handle_timeout(wash_execution_service_args_t *wash_execution_service_args,
                                                         wash_execution_fact_t *wash_execution_fact);

#endif
