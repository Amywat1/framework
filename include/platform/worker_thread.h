#ifndef PLATFORM_WORKER_THREAD_H
#define PLATFORM_WORKER_THREAD_H

#include <stdbool.h>

#include "shared/result_types.h"

/**
 * @file worker_thread.h
 * @brief 定义平台层通用后台工作线程壳子接口。
 */

typedef struct worker_thread_t worker_thread_t;

/**
 * @brief 后台工作线程入口函数。
 *
 * @param worker_thread 工作线程句柄，可用于查询停止请求状态或等待通知。
 * @param context 调用方提供的业务上下文。
 */
typedef void (*worker_thread_entry_t)(worker_thread_t *worker_thread, void *context);

/**
 * @brief 工作线程启动配置。
 */
typedef struct worker_thread_config_t
{
    worker_thread_entry_t entry;
    void *context;
} worker_thread_config_t;

/**
 * @brief 启动一个后台工作线程。
 *
 * @param worker_thread 输出线程句柄，不能为空。
 * @param worker_thread_config 线程入口和上下文配置，不能为空。
 * @return 启动成功返回 `operation_result_ok()`；参数非法或线程创建失败返回失败结果。
 */
operation_result_t worker_thread_start(worker_thread_t **worker_thread,
                                       const worker_thread_config_t *worker_thread_config);

/**
 * @brief 向后台工作线程投递一次唤醒通知。
 *
 * @param worker_thread 工作线程句柄；允许传入 `0`，此时为无操作。
 * @return 句柄有效时返回 `operation_result_ok()`；底层通知失败时返回失败结果。
 */
operation_result_t worker_thread_notify(worker_thread_t *worker_thread);

/**
 * @brief 等待一次线程通知或超时。
 *
 * @param worker_thread 工作线程句柄；允许传入 `0`，此时返回 `false`。
 * @param timeout_ms 最大等待时长，单位毫秒；传入 `0` 表示仅探测是否已有通知。
 * @return 收到通知时返回 `true`；超时、句柄无效或已收到停止请求时返回 `false`。
 */
bool worker_thread_wait(worker_thread_t *worker_thread, unsigned long timeout_ms);

/**
 * @brief 请求后台工作线程尽快停止。
 *
 * @param worker_thread 工作线程句柄；允许传入 `0`，此时为无操作。
 * @return 句柄有效时返回 `operation_result_ok()`；句柄非法时返回失败结果。
 */
operation_result_t worker_thread_request_stop(worker_thread_t *worker_thread);

/**
 * @brief 查询后台工作线程是否已收到停止请求。
 *
 * @param worker_thread 工作线程句柄；允许传入 `0`。
 * @return 已收到停止请求时返回 `true`。
 */
bool worker_thread_stop_requested(const worker_thread_t *worker_thread);

/**
 * @brief 等待后台工作线程退出。
 *
 * @param worker_thread 工作线程句柄；允许传入 `0`，此时返回成功。
 * @return 等待成功返回 `operation_result_ok()`；线程等待失败返回失败结果。
 */
operation_result_t worker_thread_join(worker_thread_t *worker_thread);

/**
 * @brief 销毁后台工作线程句柄。
 *
 * @param worker_thread 工作线程句柄；允许传入 `0`，此时为无操作。
 * @note 若线程仍在运行，会先请求停止并等待退出，再释放句柄。
 */
void worker_thread_destroy(worker_thread_t *worker_thread);

#endif
