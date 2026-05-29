#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/event_logger_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file system_context.h
 * @brief 声明主控运行期组合根接口。
 */

/**
 * @brief 主控运行期组合根的不透明正式句柄。
 *
 * @note 这是对内部静态单实例运行态的正式引用，外部无法看到其内部表示。
 * @note 外部只允许通过 `system_context_acquire()` 获取合法句柄，不得伪造、解引用或依赖其底层布局。
 */
struct system_context_handle;
typedef struct system_context_handle *system_context_t;

/**
 * @brief 获取正式主控单实例运行时句柄。
 *
 * @param system_context 输出正式运行时句柄，不能为空。
 * @return 获取成功返回 `operation_result_ok()`；实例已占用或参数非法时返回失败结果。
 */
operation_result_t system_context_acquire(system_context_t *system_context);

/**
 * @brief 释放正式主控运行时句柄并归还内部单实例占用。
 *
 * @param system_context 正式主控运行时句柄。
 * @return 释放成功返回 `operation_result_ok()`；句柄非法或已释放时返回失败结果。
 */
operation_result_t system_context_release(system_context_t system_context);

/**
 * @brief 重置主控运行态，但保留已装配的外部端口和当前占用关系。
 *
 * @param system_context 正式主控运行时句柄。
 * @return 重置成功返回 `operation_result_ok()`；句柄非法或已释放时返回失败结果。
 */
operation_result_t system_context_reset(system_context_t system_context);

/**
 * @brief 将传感器端口装配到主控上下文。
 *
 * @param system_context 主控上下文，不能为空。
 * @param sensor_port 传感器端口；传入 `0` 时清空该端口。
 */
void system_context_set_sensor_port(system_context_t system_context, const sensor_port_t *sensor_port);

/**
 * @brief 将执行机构端口装配到主控上下文。
 *
 * @param system_context 主控上下文，不能为空。
 * @param actuator_port 执行机构端口；传入 `0` 时清空该端口。
 */
void system_context_set_actuator_port(system_context_t system_context, const actuator_port_t *actuator_port);

/**
 * @brief 将事件日志端口装配到主控上下文。
 *
 * @param system_context 主控上下文，不能为空。
 * @param event_logger_port 日志端口；传入 `0` 时清空该端口。
 */
void system_context_set_event_logger_port(system_context_t system_context,
                                          const event_logger_port_t *event_logger_port);

/**
 * @brief 将程序仓储端口装配到主控上下文。
 *
 * @param system_context 主控上下文，不能为空。
 * @param program_repository_port 程序仓储端口；传入 `0` 时清空该端口。
 */
void system_context_set_program_repository_port(system_context_t system_context,
                                                const program_repository_port_t *program_repository_port);

/**
 * @brief 读取当前已装配的程序仓储端口。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 程序仓储端口只读指针；无上下文时返回 `0`。
 */
const program_repository_port_t *system_context_program_repository_port(const system_context_t system_context);

/**
 * @brief 读取主控当前时间。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 当前时间；`system_context` 为空时返回 `0`。
 */
unsigned long system_context_current_time_ms(const system_context_t system_context);

/**
 * @brief 读取待处理触发数量。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 当前待处理触发数量；`system_context` 为空时返回 `0`。
 */
unsigned int system_context_pending_trigger_count(const system_context_t system_context);

/**
 * @brief 统计指定触发 ID 当前仍在队列中的数量。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @param trigger_id 目标触发 ID；允许为 `0`。
 * @return 匹配数量。
 */
unsigned int system_context_count_pending_triggers_by_id(const system_context_t system_context, const char *trigger_id);

/**
 * @brief 统计指定触发类型当前仍在队列中的数量。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @param trigger_type 目标触发类型。
 * @return 匹配数量。
 */
unsigned int system_context_count_pending_triggers_by_type(const system_context_t system_context,
                                                           trigger_type_t trigger_type);

/**
 * @brief 判断主控当前是否仍有待处理运行工作。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 存在待处理触发或已到期超时时返回 `true`。
 */
bool system_context_has_pending_runtime_work(const system_context_t system_context);

/**
 * @brief 判断当前是否存在已激活的等待条件。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 等待条件激活时返回 `true`。
 */
bool system_context_wait_condition_active(const system_context_t system_context);

/**
 * @brief 计算当前等待条件距离超时还剩余多少毫秒。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 剩余毫秒数；无有效等待或已到期时返回 `0`。
 */
unsigned long system_context_wait_timeout_ms(const system_context_t system_context);

/**
 * @brief 读取最近一次对外结果码。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 最近结果码；无值时返回空字符串。
 */
const char *system_context_last_result_code(const system_context_t system_context);

/**
 * @brief 读取最近一次对外原因码。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 最近原因码；无值时返回空字符串。
 */
const char *system_context_last_reason_code(const system_context_t system_context);

/**
 * @brief 绑定调度器到系统上下文，防止重复创建或提前释放。
 *
 * @note 由平台层调度器在 create 内部调用；已绑定时返回失败，可用作防重复保护。
 * @param system_context 主控上下文，必须处于激活状态。
 * @param scheduler_binding 调度器绑定对象，不能为空，由调用方按自身层级解释。
 * @return 绑定成功返回 `operation_result_ok()`；已绑定或句柄非法时返回失败。
 */
operation_result_t system_context_bind_scheduler(system_context_t system_context,
                                                 void *scheduler_binding);

/**
 * @brief 解除调度器与系统上下文的绑定关系。
 *
 * @note 由平台层调度器在 destroy 内部调用。
 * @param system_context 主控上下文；允许为 `0`（此时为空操作）。
 */
void system_context_unbind_scheduler(system_context_t system_context);

/**
 * @brief 读取当前绑定到系统上下文的调度器。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 已绑定调度器对象；未绑定或上下文无效时返回 `0`。
 */
void *system_context_bound_scheduler(const system_context_t system_context);

/**
 * @brief 校验主控句柄当前是否处于激活状态。
 *
 * @param system_context 主控上下文句柄。
 * @return 句柄合法且实例激活时返回 `operation_result_ok()`；否则返回失败结果。
 */
operation_result_t system_context_require_active(system_context_t system_context);

/**
 * @brief 读取指定索引处的待处理触发事件（只读）。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @param index 目标索引，必须小于 `system_context_pending_trigger_count()`。
 * @return 指向触发事件的只读指针；索引越界或上下文为空时返回 `0`。
 */
const wash_trigger_event_t *system_context_pending_trigger_at(const system_context_t system_context,
                                                              unsigned int index);

/**
 * @brief 向待处理队列追加一个触发事件。
 *
 * @param system_context 主控上下文，不能为空。
 * @param wash_trigger_event 待追加的触发事件，不能为空。
 * @return 追加成功返回 `operation_result_ok()`；队列已满或参数非法时返回失败。
 */
operation_result_t system_context_append_trigger(system_context_t system_context,
                                                 const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 从待处理队列中移除指定索引处的触发事件。
 *
 * @param system_context 主控上下文，不能为空。
 * @param index 目标索引，必须小于 `system_context_pending_trigger_count()`。
 */
void system_context_remove_pending_trigger_at(system_context_t system_context, unsigned int index);

/**
 * @brief 从外部触发收件箱中尝试取出一个触发事件。
 *
 * @param system_context 主控上下文，不能为空。
 * @param wash_trigger_event 输出触发事件，不能为空。
 * @return 成功取到事件返回 `true`；当前收件箱为空或上下文无效时返回 `false`。
 */
bool system_context_try_pop_external_trigger(system_context_t system_context, wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 读取当前等待条件（只读）。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 指向等待条件的只读指针；无有效等待条件时返回 `0`。
 */
const wait_condition_t *system_context_wait_condition(const system_context_t system_context);

/**
 * @brief 推进主控内部时钟。
 *
 * @param system_context 主控上下文，不能为空。
 * @param elapsed_ms 本次推进的毫秒数。
 */
void system_context_advance_time(system_context_t system_context, unsigned long elapsed_ms);

/**
 * @brief 读取当前已装配的事件日志端口。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 事件日志端口只读指针；无上下文时返回 `0`。
 */
const event_logger_port_t *system_context_event_logger_port(const system_context_t system_context);

#endif
