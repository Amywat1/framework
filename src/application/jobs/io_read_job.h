#ifndef SRC_APPLICATION_JOBS_IO_READ_JOB_H
#define SRC_APPLICATION_JOBS_IO_READ_JOB_H

#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file io_read_job.h
 * @brief 定义后台 IO 采样作业接口。
 */

/**
 * @brief 读取一次后台报警采样快照。
 *
 * @param sensor_port 传感器端口，不能为空，且必须提供 `read_snapshot`。
 * @param sensor_snapshot 输出采样快照，不能为空。
 * @return 采样成功返回 `operation_result_ok()`；读取失败返回失败结果。
 */
operation_result_t io_read_job_read_snapshot(const sensor_port_t *sensor_port, sensor_snapshot_t *sensor_snapshot);

#endif
