#ifndef APPLICATION_COORDINATORS_DEVICE_RUNTIME_H
#define APPLICATION_COORDINATORS_DEVICE_RUNTIME_H

#include <stdbool.h>

#include "application/coordinators/scheduler_runtime_port.h"
#include "domain/model/domain_enums.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/event_logger_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file device_runtime.h
 * @brief 声明主控运行期组合根接口。
 */

/**
 * @brief 主控运行期组合根的不透明正式句柄。
 *
 * @note 这是对内部静态单实例运行态的正式引用，外部无法看到其内部表示。
 * @note 外部只允许通过 `device_runtime_acquire()` 获取合法句柄，不得伪造、解引用或依赖其底层布局。
 */
struct device_runtime_handle;
typedef struct device_runtime_handle *device_runtime_t;

/**
 * @brief 获取正式主控单实例运行时句柄。
 *
 * @param device_runtime 输出正式运行时句柄，不能为空。
 * @return 获取成功返回 `operation_result_ok()`；实例已占用或参数非法时返回失败结果。
 */
operation_result_t device_runtime_acquire(device_runtime_t *device_runtime);

/**
 * @brief 释放正式主控运行时句柄并归还内部单实例占用。
 *
 * @param device_runtime 正式主控运行时句柄。
 * @return 释放成功返回 `operation_result_ok()`；句柄非法或已释放时返回失败结果。
 */
operation_result_t device_runtime_release(device_runtime_t device_runtime);

/**
 * @brief 重置主控运行态，但保留已装配的外部端口和当前占用关系。
 *
 * @param device_runtime 正式主控运行时句柄。
 * @return 重置成功返回 `operation_result_ok()`；句柄非法或已释放时返回失败结果。
 */
operation_result_t device_runtime_reset(device_runtime_t device_runtime);

/**
 * @brief 将传感器端口装配到主控上下文。
 *
 * @param device_runtime 主控上下文，不能为空。
 * @param sensor_port 传感器端口；传入 `0` 时清空该端口。
 */
void device_runtime_set_sensor_port(device_runtime_t device_runtime, const sensor_port_t *sensor_port);

/**
 * @brief 将执行机构端口装配到主控上下文。
 *
 * @param device_runtime 主控上下文，不能为空。
 * @param actuator_port 执行机构端口；传入 `0` 时清空该端口。
 */
void device_runtime_set_actuator_port(device_runtime_t device_runtime, const actuator_port_t *actuator_port);

/**
 * @brief 将事件日志端口装配到主控上下文。
 *
 * @param device_runtime 主控上下文，不能为空。
 * @param event_logger_port 日志端口；传入 `0` 时清空该端口。
 */
void device_runtime_set_event_logger_port(device_runtime_t device_runtime,
                                          const event_logger_port_t *event_logger_port);

/**
 * @brief 将程序仓储端口装配到主控上下文。
 *
 * @param device_runtime 主控上下文，不能为空。
 * @param program_repository_port 程序仓储端口；传入 `0` 时清空该端口。
 */
void device_runtime_set_program_repository_port(device_runtime_t device_runtime,
                                                const program_repository_port_t *program_repository_port);

/**
 * @brief 读取当前已装配的程序仓储端口。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 程序仓储端口只读指针；无上下文时返回 `0`。
 */
const program_repository_port_t *device_runtime_program_repository_port(const device_runtime_t device_runtime);

/**
 * @brief 读取主控当前时间。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 当前时间；`device_runtime` 为空时返回 `0`。
 */
unsigned long device_runtime_current_time_ms(const device_runtime_t device_runtime);

/**
 * @brief 读取待处理触发数量。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 当前待处理触发数量；`device_runtime` 为空时返回 `0`。
 */
unsigned int device_runtime_pending_trigger_count(const device_runtime_t device_runtime);

/**
 * @brief 统计指定触发 ID 当前仍在队列中的数量。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @param trigger_id 目标触发 ID；允许为 `0`。
 * @return 匹配数量。
 */
unsigned int device_runtime_count_pending_triggers_by_id(const device_runtime_t device_runtime, const char *trigger_id);

/**
 * @brief 统计指定触发类型当前仍在队列中的数量。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @param trigger_type 目标触发类型。
 * @return 匹配数量。
 */
unsigned int device_runtime_count_pending_triggers_by_type(const device_runtime_t device_runtime,
                                                           trigger_type_t trigger_type);

/**
 * @brief 判断主控当前是否仍有待处理运行工作。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 存在待处理触发或已到期超时时返回 `true`。
 */
bool device_runtime_has_pending_work(const device_runtime_t device_runtime);

/**
 * @brief 判断当前是否存在已激活的等待条件。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 等待条件激活时返回 `true`。
 */
bool device_runtime_wait_condition_active(const device_runtime_t device_runtime);

/**
 * @brief 计算当前等待条件距离超时还剩余多少毫秒。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 剩余毫秒数；无有效等待或已到期时返回 `0`。
 */
unsigned long device_runtime_wait_timeout_ms(const device_runtime_t device_runtime);

/**
 * @brief 读取最近一次对外结果码。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 最近结果码；无值时返回空字符串。
 */
const char *device_runtime_last_result_code(const device_runtime_t device_runtime);

/**
 * @brief 读取最近一次对外原因码。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 最近原因码；无值时返回空字符串。
 */
const char *device_runtime_last_reason_code(const device_runtime_t device_runtime);

/**
 * @brief 绑定调度器到系统上下文，防止重复创建或提前释放。
 *
 * @note 由平台层调度器在 create 内部调用；已绑定时返回失败，可用作防重复保护。
 * @param device_runtime 主控上下文，必须处于激活状态。
 * @param scheduler_binding 调度器绑定对象，不能为空，由调用方按自身层级解释。
 * @return 绑定成功返回 `operation_result_ok()`；已绑定或句柄非法时返回失败。
 */
operation_result_t device_runtime_bind_scheduler(device_runtime_t device_runtime,
                                                 void *scheduler_binding);

/**
 * @brief 解除调度器与系统上下文的绑定关系。
 *
 * @note 由平台层调度器在 destroy 内部调用。
 * @param device_runtime 主控上下文；允许为 `0`（此时为空操作）。
 */
void device_runtime_unbind_scheduler(device_runtime_t device_runtime);

/**
 * @brief 读取当前绑定到系统上下文的调度器。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 已绑定调度器对象；未绑定或上下文无效时返回 `0`。
 */
void *device_runtime_bound_scheduler(const device_runtime_t device_runtime);

/**
 * @brief 校验主控句柄当前是否处于激活状态。
 *
 * @param device_runtime 主控上下文句柄。
 * @return 句柄合法且实例激活时返回 `operation_result_ok()`；否则返回失败结果。
 */
operation_result_t device_runtime_require_active(device_runtime_t device_runtime);

/**
 * @brief 读取指定索引处的待处理触发事件（只读）。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @param index 目标索引，必须小于 `device_runtime_pending_trigger_count()`。
 * @return 指向触发事件的只读指针；索引越界或上下文为空时返回 `0`。
 */
const wash_trigger_event_t *device_runtime_pending_trigger_at(const device_runtime_t device_runtime,
                                                              unsigned int index);

/**
 * @brief 向待处理队列追加一个触发事件。
 *
 * @param device_runtime 主控上下文，不能为空。
 * @param wash_trigger_event 待追加的触发事件，不能为空。
 * @return 追加成功返回 `operation_result_ok()`；队列已满或参数非法时返回失败。
 */
operation_result_t device_runtime_append_trigger(device_runtime_t device_runtime,
                                                 const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 从待处理队列中移除指定索引处的触发事件。
 *
 * @param device_runtime 主控上下文，不能为空。
 * @param index 目标索引，必须小于 `device_runtime_pending_trigger_count()`。
 */
void device_runtime_remove_pending_trigger_at(device_runtime_t device_runtime, unsigned int index);

/**
 * @brief 从外部触发收件箱中尝试取出一个触发事件。
 *
 * @param device_runtime 主控上下文，不能为空。
 * @param wash_trigger_event 输出触发事件，不能为空。
 * @return 成功取到事件返回 `true`；当前收件箱为空或上下文无效时返回 `false`。
 */
bool device_runtime_try_pop_external_trigger(device_runtime_t device_runtime, wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 读取当前等待条件（只读）。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 指向等待条件的只读指针；无有效等待条件时返回 `0`。
 */
const wait_condition_t *device_runtime_wait_condition(const device_runtime_t device_runtime);

/**
 * @brief 推进主控内部时钟。
 *
 * @param device_runtime 主控上下文，不能为空。
 * @param elapsed_ms 本次推进的毫秒数。
 */
void device_runtime_advance_time(device_runtime_t device_runtime, unsigned long elapsed_ms);

/**
 * @brief 读取当前已装配的事件日志端口。
 *
 * @param device_runtime 主控上下文；允许为 `0`。
 * @return 事件日志端口只读指针；无上下文时返回 `0`。
 */
const event_logger_port_t *device_runtime_event_logger_port(const device_runtime_t device_runtime);

/**
 * @brief 从 device_runtime 实例填充调度器运行时端口。
 *
 * @details 由应用层提供的适配器，将 device_runtime 各项能力映射到
 *          `scheduler_runtime_port_t` 函数指针接口，供基础设施层调度器使用。
 * @param port 待写入端口，不能为空。
 * @param device_runtime 主控运行时句柄，不能为空。
 */
void scheduler_runtime_port_init_from_device_runtime(scheduler_runtime_port_t *port,
                                                     device_runtime_t device_runtime);

#endif
