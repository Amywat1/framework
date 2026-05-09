#ifndef DOMAIN_SERVICES_PROGRAM_SNAPSHOT_SERVICE_H
#define DOMAIN_SERVICES_PROGRAM_SNAPSHOT_SERVICE_H

#include "domain/model/program_snapshot.h"
#include "domain/model/wash_program.h"
#include "domain/ports/program_repository_port.h"
#include "shared/result_types.h"

/**
 * @file program_snapshot_service.h
 * @brief 定义启动前程序快照校验服务。
 */

/**
 * @brief 描述程序快照服务所需的最小输入集合。
 */
typedef struct program_snapshot_service_args_t {
    program_snapshot_t *program_snapshot;
    wash_program_t *wash_program;
    program_repository_port_t *program_repository_port;
    unsigned long current_time_ms;
} program_snapshot_service_args_t;

/**
 * @brief 抓取并校验启动所需的程序快照。
 *
 * @param program_snapshot_service_args 程序快照服务输入，不能为空。
 * @param program_id 目标程序标识，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法、程序不可用或快照不合法时返回失败结果。
 *
 * @note 本切片不得扩展为依赖整个 `system_context_t`。
 */
operation_result_t program_snapshot_service_capture(program_snapshot_service_args_t *program_snapshot_service_args,
    const char *program_id);

#endif
