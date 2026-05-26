#ifndef PLATFORM_LINUX_SCHEDULER_LINUX_H
#define PLATFORM_LINUX_SCHEDULER_LINUX_H

#include <stdbool.h>
#include <stddef.h>

#include "platform/scheduler.h"

/**
 * @file scheduler_linux.h
 * @brief 声明 Linux 平台调度器实现与测试注入口。
 */

/**
 * @brief Linux 调度器测试故障注入点。
 */
typedef enum
{
    SCHEDULER_LINUX_TEST_FAIL_NONE = 0,
    SCHEDULER_LINUX_TEST_FAIL_TIMER_READ,
    SCHEDULER_LINUX_TEST_FAIL_WAKEUP_READ,
    SCHEDULER_LINUX_TEST_FAIL_WAKEUP_WRITE,
    SCHEDULER_LINUX_TEST_FAIL_COMMAND_READ,
    SCHEDULER_LINUX_TEST_FAIL_CONTROL_TICK_RUN
} scheduler_linux_test_failpoint_t;

/**
 * @brief 在测试中注入一次周期触发。
 *
 * @param scheduler 调度器对象。
 * @param expiration_count 周期到期次数。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_linux_test_inject_period(scheduler_t *scheduler,
                                                                 unsigned int expiration_count);

/**
 * @brief 在测试中注入一条命令行输入。
 *
 * @param scheduler 调度器对象。
 * @param command_line 命令行文本。
 * @param response_line 输出响应缓冲区。
 * @param response_line_size 输出响应缓冲区大小。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_linux_test_inject_command(scheduler_t *scheduler,
                                                                  const char *command_line, char *response_line,
                                                                  size_t response_line_size);

/**
 * @brief 在测试中注入通知事件。
 *
 * @param scheduler 调度器对象。
 * @param notification_count 通知次数。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_linux_test_inject_notification(scheduler_t *scheduler,
                                                                       unsigned int notification_count);

/**
 * @brief 在测试中注入退出事件。
 *
 * @param scheduler 调度器对象。
 * @param immediate 是否立即退出。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_linux_test_inject_exit(scheduler_t *scheduler,
                                                               bool immediate);

/**
 * @brief 在测试中推进一次调度器分发步骤。
 *
 * @param scheduler 调度器对象。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_linux_test_step(scheduler_t *scheduler);

/**
 * @brief 在测试中执行一次事件收集并分发。
 *
 * @param scheduler 调度器对象。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_linux_test_poll_once(scheduler_t *scheduler);

/**
 * @brief 设置 Linux 调度器测试故障注入点。
 *
 * @param scheduler 调度器对象。
 * @param scheduler_linux_test_failpoint 目标故障点。
 * @param enabled 是否启用。
 */
void scheduler_linux_test_set_failpoint(
    scheduler_t *scheduler,
    scheduler_linux_test_failpoint_t scheduler_linux_test_failpoint, bool enabled);

/**
 * @brief 为测试覆盖强制设置本轮周期耗时。
 *
 * @param scheduler 调度器对象。
 * @param cycle_duration_ms 强制写入的周期耗时，单位毫秒。
 */
void scheduler_linux_test_set_cycle_duration(scheduler_t *scheduler,
                                                        unsigned long cycle_duration_ms);

#endif
