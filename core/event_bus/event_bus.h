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
#include <stddef.h>

/* -------------------------------------------------------------------------
 * 事件处理函数类型
 * ------------------------------------------------------------------------- */
typedef void (*event_handler_t)(const event_t *evt);

/** 批量订阅表项 */
typedef struct
{
    event_type_t    type;
    event_handler_t handler;
} event_subscription_t;

/* -------------------------------------------------------------------------
 * 不可恢复故障（fatal）
 * ------------------------------------------------------------------------- */

/** 故障原因枚举（可按需扩展）*/
typedef enum
{
    EVENT_BUS_FATAL_SEM_WAIT = 1,   /**< sem_wait 返回非 EINTR 错误（信号量损坏）*/
} event_bus_fatal_reason_t;

/**
 * @brief  不可恢复故障回调类型
 *
 * @par 回调约束（必须严格遵守）：
 *   - 禁止调用 event_publish() / event_subscribe()（event_bus 已不可用）
 *   - 禁止依赖任何需要 event_bus 的业务逻辑
 *   - 禁止长时间阻塞或等待其他线程
 *   - 应直接操作底层 HAL/driver 将输出置安全态（最佳努力）
 *   - 必须是幂等的
 *   - 必须以 abort() 或 _exit() 终止进程，不可仅 return
 *
 * @par 为何必须终止进程：
 *   dispatch 线程由 scheduler 以 pthread_detach 创建，无法被外部 join。
 *   若回调仅 return，dispatch 线程退出而进程继续存活，
 *   systemd 不会重启，系统进入"事件总线已死但进程仍在"的危险状态。
 *
 * @param reason    故障原因
 * @param sys_errno 触发时的 errno 值
 */
typedef void (*event_bus_fatal_cb_t)(event_bus_fatal_reason_t reason, int sys_errno);

/* -------------------------------------------------------------------------
 * 运行统计
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint32_t published_count;      /* 成功入队的事件数 */
    uint32_t dispatched_count;     /* 成功出队并进入分发的事件数 */
    uint32_t dropped_count;        /* 因队列满丢弃的事件数 */
    uint32_t subscribe_count;      /* 已登记的订阅槽数量 */
    uint32_t queue_depth;          /* 当前队列深度 */
    uint32_t queue_peak_depth;     /* 历史最大队列深度 */
    uint32_t sem_post_fail_count;  /* sem_post 失败次数 */
    uint32_t sem_wait_fail_count;  /* sem_wait 非 EINTR 失败次数 */
} event_bus_stats_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  注册不可恢复故障回调
 * @note   须在 event_bus_init() 之后、dispatch 线程启动之前调用。
 *         未注册时默认行为：仅记录日志后 dispatch 线程退出（进程不终止，不安全）。
 * @param  cb  回调函数；传 NULL 可清除注册
 */
void event_bus_set_fatal_cb(event_bus_fatal_cb_t cb);

/**
 * @brief  初始化事件总线（清空队列和订阅表）
 * @note   须在 time_util_init() 之后、dispatch 线程启动前调用。
 *         若需重置（如测试场景），必须先停止 dispatch 线程再调用，
 *         否则 sem 操作是 POSIX 未定义行为。
 * @retval SW_OK / SW_ERR_HW（sem_init 失败）
 */
sw_err_t event_bus_init(void);

/**
 * @brief  请求停止事件总线分发循环
 * @note   调用后 event_publish / event_subscribe 会返回 SW_ERR_NOT_INIT。
 *         dispatch 线程会先排空队列，再从 event_bus_dispatch_loop() 返回。
 *         - joinable 线程（如单元测试）：可在 shutdown 后 pthread_join 等待退出。
 *         - detached 线程（生产路径）：不可 join；总线不可恢复故障由 fatal
 *           回调终止进程，正常 shutdown 由调用方自行协调退出时序。
 * @retval SW_OK / SW_ERR_NOT_INIT / SW_ERR_HW
 */
sw_err_t event_bus_shutdown(void);

/**
 * @brief  发布事件
 * @param  type   事件类型（须通过 event_type_is_valid 校验）
 * @param  param  简单载荷（报警码、错误码等；无载荷传 0）
 * @retval SW_OK
 * @retval SW_ERR_NOT_INIT  未调用 event_bus_init
 * @retval SW_ERR_PARAM     type 非法
 * @retval SW_ERR_HW        sem_post 失败，事件已回滚
 * @retval SW_ERR_OVERFLOW  队列已满，事件被丢弃
 */
sw_err_t event_publish(event_type_t type, uint32_t param);

/**
 * @brief  订阅事件
 * @param  type     要订阅的事件类型
 * @param  handler  事件处理函数（在 dispatch 线程上下文中调用）
 * @retval SW_OK
 * @retval SW_ERR_NOT_INIT  未调用 event_bus_init 或已进入 shutdown
 * @retval SW_ERR_PARAM     参数非法
 * @retval SW_ERR_OVERFLOW  该事件的订阅槽已满（见 EVENT_BUS_MAX_SUBS_PER_EVT）
 */
sw_err_t event_subscribe(event_type_t type, event_handler_t handler);

/**
 * @brief  按表批量订阅（遇首个失败即返回）
 * @param  subs   订阅表（type + handler）
 * @param  count  表项数量
 * @retval SW_OK / SW_ERR_NOT_INIT / SW_ERR_PARAM / SW_ERR_OVERFLOW
 */
sw_err_t event_subscribe_table(const event_subscription_t *subs, size_t count);

/**
 * @brief  获取当前运行统计（线程安全，值拷贝）
 * @param  stats  输出统计结构体
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t event_bus_get_stats(event_bus_stats_t *stats);

/**
 * @brief  事件分发循环（阻塞，由 event_dispatch_thread 调用）
 * @note   正常运行时阻塞等待事件；收到 shutdown 请求后会排空队列并返回。
 *         此函数仍是 POSIX 线程取消点（sem_wait），也可通过 pthread_cancel 退出。
 */
void event_bus_dispatch_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_EVENT_BUS_H */
