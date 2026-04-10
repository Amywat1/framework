/**
 * @file    event_bus.h
 * @brief   统一事件总线接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    设计原则：
 *          - 发布方只传 type + param，timestamp 由总线入队时自动填充
 *          - handler 收到只读 event_t *，包含完整三字段
 *          - event_bus 不创建线程，dispatch_loop 由 core/scheduler 的
 *            event_dispatch_thread 调用
 *          - 零动态内存，队列大小编译期固定（thread_config.h）
 */

#ifndef CORE_EVENT_BUS_H
#define CORE_EVENT_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/event_types.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 事件处理函数类型
 * ------------------------------------------------------------------------- */
typedef void (*event_handler_t)(const event_t *evt);

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化事件总线（清空队列和订阅表）
 * @note   须在 time_util_init() 之后、dispatch 线程启动前调用。
 *         若需重置（如测试场景），必须先停止 dispatch 线程再调用，
 *         否则 sem 操作是 POSIX 未定义行为。
 * @retval SW_OK / SW_ERR_HW（sem_init 失败）
 */
sw_err_t event_bus_init(void);

/**
 * @brief  发布事件
 * @param  type   事件类型（不可为 EVT_NONE 或 >= EVT_MAX）
 * @param  param  简单载荷（报警码、错误码等；无载荷传 0）
 * @retval SW_OK
 * @retval SW_ERR_NOT_INIT  未调用 event_bus_init
 * @retval SW_ERR_PARAM     type 非法
 * @retval SW_ERR_OVERFLOW  队列已满，事件被丢弃
 */
sw_err_t event_publish(event_type_t type, uint32_t param);

/**
 * @brief  订阅事件
 * @param  type     要订阅的事件类型
 * @param  handler  事件处理函数（在 dispatch 线程上下文中调用）
 * @retval SW_OK
 * @retval SW_ERR_PARAM     参数非法
 * @retval SW_ERR_OVERFLOW  该事件的订阅槽已满（见 EVENT_BUS_MAX_SUBS_PER_EVT）
 */
sw_err_t event_subscribe(event_type_t type, event_handler_t handler);

/**
 * @brief  事件分发循环（阻塞，由 event_dispatch_thread 调用，永不返回）
 * @note   此函数是 POSIX 线程取消点（sem_wait），可通过 pthread_cancel 退出
 */
void event_bus_dispatch_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_EVENT_BUS_H */
