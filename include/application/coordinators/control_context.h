#ifndef APPLICATION_COORDINATORS_CONTROL_CONTEXT_H
#define APPLICATION_COORDINATORS_CONTROL_CONTEXT_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file control_context.h
 * @brief 声明主控运行期组合根接口。
 *
 * @note control_context 是进程内唯一的单实例模块，所有接口直接操作内部静态实例，
 *       无需外部传入句柄。生命周期由引导层 `control_context_init` / `control_context_deinit` 管理。
 * @note 未激活时各接口可能返回失败、空指针或默认态；调用方须以返回值与判空为准，不得当作有效业务态。
 */

/**
 * @brief 初始化主控单实例运行时。
 *
 * @return 初始化成功返回 `operation_result_ok()`；实例已初始化时返回失败结果。
 */
operation_result_t control_context_init(void);

/**
 * @brief 反初始化主控运行时，释放内部资源并重置状态。
 *
 * @note 未初始化时调用按幂等安全返回 `operation_result_ok()`。
 * @note 调度器仍绑定时返回失败结果。
 * @return 成功返回 `operation_result_ok()`；调度器未解绑时返回失败结果。
 */
operation_result_t control_context_deinit(void);

/**
 * @brief 重置主控运行态，但保留已装配的外部端口和当前占用关系。
 *
 * @return 重置成功返回 `operation_result_ok()`；实例未激活时返回失败结果。
 */
operation_result_t control_context_reset_runtime_keep_bindings(void);

/**
 * @brief 将传感器端口装配到主控上下文。
 *
 * @param sensor_port 传感器端口；传入 `0` 时清空该端口。
 */
void control_context_set_sensor_port(const sensor_port_t *sensor_port);

/**
 * @brief 将执行机构端口装配到主控上下文。
 *
 * @param actuator_port 执行机构端口；传入 `0` 时清空该端口。
 */
void control_context_set_actuator_port(const actuator_port_t *actuator_port);

/**
 * @brief 将程序仓储端口装配到主控上下文。
 *
 * @param program_repository_port 程序仓储端口；传入 `0` 时清空该端口。
 */
void control_context_set_program_repository_port(const program_repository_port_t *program_repository_port);

/**
 * @brief 读取当前已装配的程序仓储端口。
 *
 * @return 程序仓储端口只读指针；实例未激活时返回 `0`。
 */
const program_repository_port_t *control_context_program_repository_port(void);

/**
 * @brief 读取主控当前时间。
 *
 * @return 当前时间；实例未激活时返回 `0`。
 */
unsigned long control_context_current_time_ms(void);

/**
 * @brief 读取待处理触发数量。
 *
 * @return 当前待处理触发数量；实例未激活时返回 `0`。
 */
unsigned int control_context_pending_trigger_count(void);

/**
 * @brief 统计指定触发 ID 当前仍在队列中的数量。
 *
 * @param trigger_id 目标触发 ID；允许为 `0`。
 * @return 匹配数量。
 */
unsigned int control_context_count_pending_triggers_by_id(const char *trigger_id);

/**
 * @brief 统计指定触发类型当前仍在队列中的数量。
 *
 * @param trigger_type 目标触发类型。
 * @return 匹配数量。
 */
unsigned int control_context_count_pending_triggers_by_type(trigger_type_t trigger_type);

/**
 * @brief 判断主控当前是否仍有待处理运行工作。
 *
 * @return 存在待处理触发或已到期超时时返回 `true`。
 */
bool control_context_has_pending_work(void);

/**
 * @brief 判断当前是否存在已激活的等待条件。
 *
 * @return 等待条件激活时返回 `true`。
 */
bool control_context_wait_condition_active(void);

/**
 * @brief 计算当前等待条件距离超时还剩余多少毫秒。
 *
 * @return 剩余毫秒数；无有效等待或已到期时返回 `0`。
 */
unsigned long control_context_wait_timeout_ms(void);

/**
 * @brief 读取最近一次对外结果码。
 *
 * @return 最近结果码；无值时返回空字符串。
 */
const char *control_context_last_result_code(void);

/**
 * @brief 读取最近一次对外原因码。
 *
 * @return 最近原因码；无值时返回空字符串。
 */
const char *control_context_last_reason_code(void);

/**
 * @brief 绑定调度器到系统上下文，防止重复创建或提前释放。
 *
 * @note 由平台层调度器在 create 内部调用；已绑定时返回失败，可用作防重复保护。
 * @param scheduler_binding 调度器绑定对象，不能为空，由调用方按自身层级解释。
 * @return 绑定成功返回 `operation_result_ok()`；已绑定或实例未激活时返回失败。
 */
operation_result_t control_context_bind_scheduler(void *scheduler_binding);

/**
 * @brief 解除调度器与系统上下文的绑定关系。
 *
 * @note 由平台层调度器在 destroy 内部调用。
 */
void control_context_unbind_scheduler(void);

/**
 * @brief 读取当前绑定到系统上下文的调度器。
 *
 * @return 已绑定调度器对象；未绑定或上下文未激活时返回 `0`。
 */
void *control_context_bound_scheduler(void);

/**
 * @brief 读取指定索引处的待处理触发事件（只读）。
 *
 * @param index 目标索引，必须小于 `control_context_pending_trigger_count()`。
 * @return 指向触发事件的只读指针；索引越界或实例未激活时返回 `0`。
 */
const wash_trigger_event_t *control_context_pending_trigger_at(unsigned int index);

/**
 * @brief 向待处理队列追加一个触发事件。
 *
 * @param wash_trigger_event 待追加的触发事件，不能为空。
 * @return 追加成功返回 `operation_result_ok()`；队列已满或参数非法时返回失败。
 */
operation_result_t control_context_append_trigger(const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 从待处理队列中移除指定索引处的触发事件。
 *
 * @param index 目标索引，必须小于 `control_context_pending_trigger_count()`。
 */
void control_context_remove_pending_trigger_at(unsigned int index);

/**
 * @brief 从外部触发收件箱中尝试取出一个触发事件。
 *
 * @param wash_trigger_event 输出触发事件，不能为空。
 * @return 成功取到事件返回 `true`；当前收件箱为空或实例未激活时返回 `false`。
 */
bool control_context_try_pop_external_trigger(wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 读取当前等待条件（只读）。
 *
 * @return 指向等待条件的只读指针；无有效等待条件时返回 `0`。
 */
const wait_condition_t *control_context_wait_condition(void);

/**
 * @brief 推进主控内部时钟。
 *
 * @param elapsed_ms 本次推进的毫秒数。
 */
void control_context_advance_time(unsigned long elapsed_ms);

#endif
