#ifndef SRC_APPLICATION_JOBS_ALARM_DETECT_JOB_H
#define SRC_APPLICATION_JOBS_ALARM_DETECT_JOB_H

#include "application/coordinators/system_context.h"
#include "application/services/alarm_evaluator.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file alarm_detect_job.h
 * @brief 定义后台报警检测作业接口。
 */

/**
 * @brief 基于快照执行一次后台报警检测，并在需要时投递 fault 外部触发。
 *
 * @param system_context 系统上下文，不能为空。
 * @param alarm_evaluator 报警判定器，不能为空。
 * @param sensor_snapshot 本次采样快照，不能为空。
 * @param occurred_at_ms 本次检测时间戳。
 * @return 执行成功返回 `operation_result_ok()`；外部触发入队失败返回失败结果。
 */
operation_result_t alarm_detect_job_process_snapshot(system_context_t system_context,
                                                     alarm_evaluator_t *alarm_evaluator,
                                                     const sensor_snapshot_t *sensor_snapshot,
                                                     unsigned long occurred_at_ms);

#endif
