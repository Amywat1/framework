#ifndef DOMAIN_SERVICES_PRECHECK_SERVICE_H
#define DOMAIN_SERVICES_PRECHECK_SERVICE_H

#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file precheck_service.h
 * @brief 定义启动前预检服务。
 */

/**
 * @brief 对当前资源与传感器快照执行启动前预检。
 *
 * @param sensor_port 传感器端口，不能为空，且必须实现 `read_snapshot` 回调。
 * @return 预检通过返回 `operation_result_ok()`；否则返回带明确错误码的失败结果。
 */
operation_result_t precheck_service_run(const sensor_port_t *sensor_port);

#endif
