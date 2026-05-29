#ifndef PLATFORM_LINUX_CONTROLLER_SCHEDULER_LINUX_H
#define PLATFORM_LINUX_CONTROLLER_SCHEDULER_LINUX_H

#include <stdbool.h>
#include <stddef.h>

#include "application/coordinators/system_context.h"
#include "platform/controller_scheduler.h"

/**
 * @file controller_scheduler_linux.h
 * @brief 声明 Linux 平台调度器实现与测试注入口。
 */

/**
 * @brief Linux 调度器测试故障注入点。
 */
typedef enum
{
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_NONE = 0,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_TIMER_READ,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_WAKEUP_READ,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_WAKEUP_WRITE,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_COMMAND_READ,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_MAIN_LOOP_RUN
} controller_scheduler_linux_test_failpoint_t;

/**
 * @brief 在测试中注入一次周期触发。
 *
 * @param controller_scheduler 调度器对象。
 * @param expiration_count 周期到期次数。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t controller_scheduler_linux_test_inject_period(controller_scheduler_t *controller_scheduler,
                                                                 unsigned int expiration_count);

/**
 * @brief 在测试中注入一条命令行输入。
 *
 * @param controller_scheduler 调度器对象。
 * @param command_line 命令行文本。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 输出响应缓冲区大小。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t controller_scheduler_linux_test_inject_command(controller_scheduler_t *controller_scheduler,
                                                                  const char *command_line, char *response_line,
                                                                  size_t response_line_size);

/**
 * @brief 在测试中注入通知事件。
 *
 * @param controller_scheduler 调度器对象。
 * @param notification_count 通知次数。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t controller_scheduler_linux_test_inject_notification(controller_scheduler_t *controller_scheduler,
                                                                       unsigned int notification_count);

/**
 * @brief 在测试中注入退出事件。
 *
 * @param controller_scheduler 调度器对象。
 * @param immediate 是否立即退出。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t controller_scheduler_linux_test_inject_exit(controller_scheduler_t *controller_scheduler,
                                                               bool immediate);

/**
 * @brief 在测试中推进一次调度器分发步骤。
 *
 * @param controller_scheduler 调度器对象。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t controller_scheduler_linux_test_step(controller_scheduler_t *controller_scheduler);

/**
 * @brief 在测试中执行一次事件收集并分发。
 *
 * @param controller_scheduler 调度器对象。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t controller_scheduler_linux_test_poll_once(controller_scheduler_t *controller_scheduler);

/**
 * @brief 设置 Linux 调度器测试故障注入点。
 *
 * @param controller_scheduler 调度器对象。
 * @param controller_scheduler_linux_test_failpoint 目标故障点。
 * @param enabled 是否启用。
 */
void controller_scheduler_linux_test_set_failpoint(
    controller_scheduler_t *controller_scheduler,
    controller_scheduler_linux_test_failpoint_t controller_scheduler_linux_test_failpoint, bool enabled);

/**
 * @brief 为测试覆盖强制设置本轮周期耗时。
 *
 * @param controller_scheduler 调度器对象。
 * @param cycle_duration_ms 强制写入的周期耗时，单位毫秒。
 */
void controller_scheduler_linux_test_set_cycle_duration(controller_scheduler_t *controller_scheduler,
                                                        unsigned long cycle_duration_ms);

#endif
