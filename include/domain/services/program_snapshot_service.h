#ifndef DOMAIN_SERVICES_PROGRAM_SNAPSHOT_SERVICE_H
#define DOMAIN_SERVICES_PROGRAM_SNAPSHOT_SERVICE_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

/**
 * @file program_snapshot_service.h
 * @brief 定义启动前程序快照校验服务。
 */
operation_result_t program_snapshot_service_capture(system_context_t *system_context, const char *program_id);

#endif
