#ifndef APPLICATION_PORTS_OUTBOUND_SCHEDULER_SYNC_PORT_H
#define APPLICATION_PORTS_OUTBOUND_SCHEDULER_SYNC_PORT_H

#include <stdbool.h>

#include "shared/result_types.h"

/**
 * @file scheduler_sync_port.h
 * @brief 声明调度器同步执行出站端口接口。
 *
 * @details 应用层通过此端口在命令处理中同步推进控制循环 tick，以获取即时执行结果。
 *          依赖方向：应用层（调用方）→ 此端口接口 ← 平台层（实现方）。
 */

/**
 * @brief 调度器同步执行出站端口。
 */
typedef struct scheduler_sync_port_t
{
    /** @brief 读取当前待处理触发事件数。 */
    unsigned int (*pending_trigger_count)(void *context);
    /** @brief 判断调度器当前是否处于运行态（`RUNNING`）。 */
    bool (*is_running)(void *context);
    /** @brief 同步执行一轮有界 tick；失败时返回对应错误。 */
    operation_result_t (*execute_bounded_ticks)(void *context);
    void *context; /**< 实现侧上下文指针。 */
} scheduler_sync_port_t;

#endif
