#ifndef APPLICATION_COORDINATORS_SCHEDULER_RUNTIME_PORT_H
#define APPLICATION_COORDINATORS_SCHEDULER_RUNTIME_PORT_H

#include <stdbool.h>

#include "domain/ports/event_logger_port.h"
#include "shared/result_types.h"

/**
 * @file scheduler_runtime_port.h
 * @brief 声明调度器运行时端口接口。
 *
 * @details 由应用层定义，规定基础设施层调度器所需的运行时能力集合。
 *          基础设施层调度器通过此接口与应用层运行时交互，
 *          不直接依赖 `device_runtime_t` 具体类型。
 */

/**
 * @brief 调度器所需运行时能力的端口接口。
 *
 * @note 所有函数指针必须非空；`context` 由实现方（应用层适配器）填入，
 *       调度器透传此指针，不关心其内部表示。
 */
typedef struct scheduler_runtime_port_t
{
    void *context; /**< 实现方私有上下文，由初始化函数写入，调度器透传。 */

    /**
     * @brief 读取当前运行时逻辑时间（毫秒）。
     * @param context 实现方私有上下文。
     * @return 当前逻辑时间（毫秒）。
     */
    unsigned long (*current_time_ms)(void *context);

    /**
     * @brief 读取待处理触发事件数量。
     * @param context 实现方私有上下文。
     * @return 当前待处理触发数量。
     */
    unsigned int (*pending_trigger_count)(void *context);

    /**
     * @brief 判断运行时当前是否存在待处理工作。
     * @param context 实现方私有上下文。
     * @return 存在待处理触发或已到期超时时返回 `true`。
     */
    bool (*has_pending_work)(void *context);

    /**
     * @brief 读取当前已装配的事件日志端口。
     * @param context 实现方私有上下文。
     * @return 事件日志端口只读指针；未装配时返回 `0`。
     */
    const event_logger_port_t *(*event_logger_port)(void *context);

    /**
     * @brief 按给定时长推进运行时逻辑时钟。
     * @param context 实现方私有上下文。
     * @param elapsed_ms 本次推进的毫秒数。
     */
    void (*advance_time)(void *context, unsigned long elapsed_ms);

    /**
     * @brief 执行一次有界主控单拍推进。
     * @param context 实现方私有上下文。
     * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
     */
    operation_result_t (*run_control_tick)(void *context);
} scheduler_runtime_port_t;

#endif
