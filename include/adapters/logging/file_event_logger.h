#ifndef ADAPTERS_LOGGING_FILE_EVENT_LOGGER_H
#define ADAPTERS_LOGGING_FILE_EVENT_LOGGER_H

#include "application/coordinators/device_runtime.h"
#include "shared/result_types.h"

/**
 * @file file_event_logger.h
 * @brief 定义文件日志适配器初始化入口。
 */

/**
 * @brief 将主控事件日志端口绑定到文件输出实现。
 *
 * @param device_runtime 主控共享上下文，不能为空。
 * @param log_path 日志文件路径，不能为空。
 */
operation_result_t file_event_logger_init(device_runtime_t device_runtime, const char *log_path);

#endif
