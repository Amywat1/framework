#ifndef PLATFORM_WORKER_THREAD_H
#define PLATFORM_WORKER_THREAD_H

#include <stdbool.h>

#include "shared/result_types.h"

/**
 * @file worker_thread.h
 * @brief 定义平台层通用后台工作线程接口。
 *
 * @details 提供两类句柄：
 *   - `worker_thread_t`：由创建方持有，用于启动、停止、等待、销毁线程。
 *   - `worker_run_ctx_t`：由线程入口函数持有，用于查询停止请求和等待通知。
 *
 * 两者相互独立，业务入口函数无需引用管理句柄，消除自指依赖。
 */

typedef struct try_lock_t try_lock_t;
typedef struct worker_run_ctx_t worker_run_ctx_t;
typedef struct worker_thread_t worker_thread_t;

/**
 * @brief 后台工作线程入口函数签名。
 *
 * @param run_ctx 运行控制句柄，可用于查询停止请求或等待通知。
 * @param context 调用方提供的业务上下文。
 */
typedef void (*worker_thread_entry_t)(worker_run_ctx_t *run_ctx, void *context);

/**
 * @brief 工作线程启动配置。
 */
typedef struct worker_thread_config_t
{
    worker_thread_entry_t entry;
    void *context;
} worker_thread_config_t;

/* — 非阻塞互斥锁，用于跨线程共享数据的保护 — */

/**
 * @brief 创建并初始化一个非阻塞互斥锁。
 * @return 成功返回锁实例指针；内存不足时返回 `0`。
 */
try_lock_t *try_lock_create(void);

/**
 * @brief 尝试获取锁，非阻塞。
 * @param lock 锁实例，不能为空。
 * @return 获取成功返回 `true`；锁已被持有或参数非法时返回 `false`。
 */
bool try_lock_acquire(try_lock_t *lock);

/**
 * @brief 阻塞等待获取锁，直到成功或超时。
 * @param lock 锁实例，不能为空。
 * @param timeout_ms 最大等待时长（毫秒）；传入 `0` 等同于非阻塞尝试。
 * @return 获取成功返回 `true`；超时或参数非法时返回 `false`。
 */
bool try_lock_acquire_timeout(try_lock_t *lock, unsigned long timeout_ms);

/**
 * @brief 释放锁。
 * @param lock 锁实例，不能为空。
 */
void try_lock_release(try_lock_t *lock);

/**
 * @brief 销毁锁并释放关联内存。
 * @param lock 锁实例；允许传入 `0`，此时为无操作。
 */
void try_lock_destroy(try_lock_t *lock);

/* — 运行控制 API，在入口函数内部使用 — */

/**
 * @brief 查询后台工作线程是否已收到停止请求。
 *
 * @param run_ctx 运行控制句柄；允许传入 `0`，此时返回 `true`。
 * @return 已收到停止请求时返回 `true`。
 */
bool worker_run_ctx_stop_requested(const worker_run_ctx_t *run_ctx);

/**
 * @brief 读取当前单调时钟毫秒值。
 *
 * @param run_ctx 运行控制句柄；允许传入 `0`，此时返回 `0`。
 * @return 当前单调时钟毫秒值；系统调用失败时返回 `0`。
 */
unsigned long worker_run_ctx_current_time_ms(const worker_run_ctx_t *run_ctx);

/**
 * @brief 等待一次线程通知或超时。
 *
 * @param run_ctx 运行控制句柄；允许传入 `0`，此时返回 `false`。
 * @param timeout_ms 最大等待时长（毫秒）；传入 `0` 表示仅探测是否已有通知。
 * @return 收到通知时返回 `true`；超时、句柄无效或已收到停止请求时返回 `false`。
 */
bool worker_run_ctx_wait(worker_run_ctx_t *run_ctx, unsigned long timeout_ms);

/* — 管理 API，由创建方使用 — */

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
 * @brief 请求后台工作线程尽快停止。
 *
 * @param worker_thread 工作线程句柄；允许传入 `0`，此时为无操作。
 * @return 句柄有效时返回 `operation_result_ok()`；底层广播失败时返回失败结果。
 */
operation_result_t worker_thread_request_stop(worker_thread_t *worker_thread);

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
 * @note 若线程仍在运行，会先请求停止并等待退出，再释放句柄。
 * @param worker_thread 工作线程句柄；允许传入 `0`，此时为无操作。
 */
void worker_thread_destroy(worker_thread_t *worker_thread);

#endif
