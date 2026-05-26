#ifndef SRC_STARTUP_APP_BOOTSTRAP_PRIVATE_H
#define SRC_STARTUP_APP_BOOTSTRAP_PRIVATE_H

#include "application/coordinators/device_runtime.h"
#include "platform/scheduler.h"
#include "startup/app_bootstrap.h"

/**
 * @brief 读取应用实例当前绑定的 device_runtime 句柄。
 *
 * @param app                 应用句柄，不能为空。
 * @param out_device_runtime  输出组合根句柄，不能为空。
 * @return 读取成功返回 `operation_result_ok()`，否则返回显式失败结果。
 */
operation_result_t app_private_read_device_runtime(const app_t *app,
                                                   device_runtime_t *out_device_runtime);

/**
 * @brief 读取应用实例当前绑定的调度器。
 *
 * @param app  应用句柄，不能为空。
 * @return 调度器指针；未创建或句柄非法时返回 `0`。
 */
scheduler_t *app_private_scheduler(const app_t *app);

#endif
