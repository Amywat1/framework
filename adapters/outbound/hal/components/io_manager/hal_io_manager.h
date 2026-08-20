/**
 * @file    hal_io_manager.h
 * @brief   通用 HAL I/O 单所有者事务管理器。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_COMPONENTS_IO_MANAGER_HAL_IO_MANAGER_H
#define ADAPTERS_OUTBOUND_HAL_COMPONENTS_IO_MANAGER_HAL_IO_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 管理器固定请求载荷上限，避免运行期动态内存。 */
#define HAL_IO_MANAGER_PAYLOAD_MAX 64U

/** 管理器固定请求槽位数量。 */
#define HAL_IO_MANAGER_QUEUE_CAPACITY 64U

/** 事务优先级。数值越大越优先。 */
typedef enum {
    HAL_IO_MANAGER_PRIORITY_LOW = 0,
    HAL_IO_MANAGER_PRIORITY_NORMAL,
    HAL_IO_MANAGER_PRIORITY_HIGH,
    HAL_IO_MANAGER_PRIORITY_CRITICAL
} hal_io_manager_priority_t;

/**
 * @brief  后端同步事务执行函数。
 * @param  ctx             后端上下文。
 * @param  request         管理器复制的请求载荷。
 * @param  request_size    请求载荷长度。
 * @param  response        管理器拥有的响应载荷。
 * @param  response_size   响应载荷容量。
 * @retval SW_OK           事务成功。
 * @retval 其他            后端明确错误。
 */
typedef sw_err_t (*hal_io_manager_execute_fn)(void       *ctx,
                                              const void *request,
                                              size_t      request_size,
                                              void       *response,
                                              size_t      response_size);

/**
 * @brief  周期轮询函数。
 * @param  ctx 后端上下文。
 * @note   只在管理器 worker 中调用，函数内允许访问同步后端 SDK。
 */
typedef void (*hal_io_manager_tick_fn)(void *ctx);

/** 管理器配置。配置在 init 后只读。 */
typedef struct {
    const char               *name;
    uint32_t                  tick_period_ms;
    hal_io_manager_tick_fn    tick;
    void                     *tick_ctx;
    hal_io_manager_execute_fn execute;
    void                     *execute_ctx;
} hal_io_manager_cfg_t;

/**
 * @brief  初始化管理器状态，不创建 worker。
 * @param  cfg 配置，不可为 NULL。
 * @retval SW_OK / SW_ERR_PARAM / SW_ERR_STATE。
 */
sw_err_t hal_io_manager_init(const hal_io_manager_cfg_t *cfg);

/**
 * @brief  创建唯一 I/O worker。
 * @retval SW_OK / SW_ERR_NOT_INIT / SW_ERR_STATE / SW_ERR_HW。
 */
sw_err_t hal_io_manager_start(void);

/**
 * @brief  提交一个有界同步事务。
 * @param  priority       事务优先级。
 * @param  timeout_ms     从入队开始的最长等待时间。
 * @param  request        请求载荷，可为空但长度必须为 0。
 * @param  request_size   请求载荷长度，不得超过 HAL_IO_MANAGER_PAYLOAD_MAX。
 * @param  response       成功返回时接收响应，可为空但长度必须为 0。
 * @param  response_size  响应载荷容量，不得超过 HAL_IO_MANAGER_PAYLOAD_MAX。
 * @retval SW_OK           后端执行成功。
 * @retval SW_ERR_TIMEOUT  等待超时；事务可能仍在 worker 中执行。
 * @retval SW_ERR_BUSY     请求槽位已满。
 * @retval SW_ERR_STATE    worker 尚未启动。
 * @retval 其他            参数、生命周期或后端错误。
 * @note   超时后管理器仍会安全回收内部请求槽，不会访问调用方的响应缓冲。
 *         worker 内嵌套调用会直接执行后端，避免安全回调自锁。
 */
sw_err_t hal_io_manager_call(hal_io_manager_priority_t priority,
                             uint32_t                  timeout_ms,
                             const void               *request,
                             size_t                    request_size,
                             void                     *response,
                             size_t                    response_size);

/**
 * @brief  判断当前线程是否为 I/O worker。
 * @return true 当前线程为 worker，否则为 false。
 */
bool hal_io_manager_is_worker_thread(void);

#ifdef HAL_IO_MANAGER_UNIT_TEST
/**
 * @brief  单元测试停止并清空 worker；生产代码不得调用。
 * @retval SW_OK          worker 已停止且状态已清空。
 * @retval SW_ERR_TIMEOUT worker 未在测试停止时限内退出。
 */
sw_err_t hal_io_manager_reset_for_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_COMPONENTS_IO_MANAGER_HAL_IO_MANAGER_H */
