#ifndef APPLICATION_COORDINATORS_DEVICE_RUNTIME_PRIVATE_H
#define APPLICATION_COORDINATORS_DEVICE_RUNTIME_PRIVATE_H

#include "application/coordinators/device_runtime.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/state_transition_record.h"
#include "domain/model/vehicle_type.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_session.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"
#include "domain/services/program_snapshot_service.h"
#include "domain/services/wash_execution_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "shared/timeouts.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @file device_runtime_private.h
 * @brief 定义系统上下文协调层的私有内部接口。
 */

/**
 * @brief 校验系统上下文句柄当前是否处于激活状态。
 * @param device_runtime 系统上下文句柄，允许为 `0`（此时直接返回失败）。
 * @return 句柄合法且实例激活时返回成功；否则返回 ERROR_CODE_INVALID_STATE。
 */
operation_result_t device_runtime_private_require_active(const device_runtime_t device_runtime);

/**
 * @brief 将设备状态从 INIT 推进至 STOPPED，标志端口装配完成、设备就绪可接受命令。
 * @param device_runtime 主控运行时句柄，调用前必须已通过 `device_runtime_acquire` 获取。
 * @return 成功返回 `operation_result_ok()`；设备状态非 INIT 或句柄非法时返回失败。
 */
operation_result_t device_runtime_private_enter_stopped(device_runtime_t device_runtime);

/** @name 调度器绑定 */
/** @{ */

/**
 * @brief 绑定调度器到系统上下文，标记调度器已建立。
 * @note 由 Application 层（app_bootstrap.c）在调度器创建成功后调用；
 *       每个上下文实例仅允许绑定一次，重复绑定返回失败。
 * @param device_runtime 系统上下文，必须处于激活状态。
 * @return 绑定成功返回 `operation_result_ok()`；已绑定或句柄非法时返回失败。
 */
operation_result_t device_runtime_private_bind_scheduler(device_runtime_t device_runtime,
                                                         void *scheduler_binding);

/**
 * @brief 解除调度器与系统上下文的绑定关系。
 * @note 由 Application 层（app_bootstrap.c）在调度器销毁后调用。
 * @param device_runtime 系统上下文；允许为 `0`（此时为空操作）。
 */
void device_runtime_private_unbind_scheduler(device_runtime_t device_runtime);

/** @brief 读取已绑定调度器；未绑定或句柄非法时返回 `0`。 */
void *device_runtime_private_bound_scheduler(const device_runtime_t device_runtime);

/** @} */

/** @name 设备状态与端口 */
/** @{ */

/** @brief 读取当前设备状态；句柄非法时返回 `DEVICE_STATE_INIT`。 */
device_state_t device_runtime_private_device_state(const device_runtime_t device_runtime);

/** @brief 设置当前设备状态；句柄非法时为空操作。 */
void device_runtime_private_set_device_state(device_runtime_t device_runtime, device_state_t device_state);

/** @brief 读取执行机构端口（只读）；句柄非法时返回 `0`。 */
const actuator_port_t *device_runtime_private_actuator_port(const device_runtime_t device_runtime);

/** @brief 读取传感器端口（只读）；句柄非法时返回 `0`。 */
const sensor_port_t *device_runtime_private_sensor_port(const device_runtime_t device_runtime);

/** @} */

/** @name 全局故障 */
/** @{ */

/** @brief 判断是否存在未清除的全局故障。 */
bool device_runtime_private_global_fault_present(const device_runtime_t device_runtime);

/** @brief 读取全局故障原因描述；句柄非法时返回空字符串。 */
const char *device_runtime_private_global_fault_reason(const device_runtime_t device_runtime);

/**
 * @brief 设置全局故障信息。
 * @param fault_code   故障码（机器可读短标识），不能为空。
 * @param fault_reason 故障原因描述（人类可读），允许为空（不更新现有原因）。
 */
void device_runtime_private_set_global_fault(device_runtime_t device_runtime, const char *fault_code,
                                             const char *fault_reason);

/** @brief 清除全局故障标志及相关信息。 */
void device_runtime_private_clear_global_fault(device_runtime_t device_runtime);

/** @} */

/** @name 运行时结果 */
/** @{ */

/** @brief 读取最近一次状态迁移记录（只读）；句柄非法时返回 `0`。 */
const state_transition_record_t *device_runtime_private_last_transition_record(const device_runtime_t device_runtime);

/** @brief 读取最近一次状态迁移记录（可写）；句柄非法时返回 `0`。 */
state_transition_record_t *device_runtime_private_last_transition_record_mutable(device_runtime_t device_runtime);

/** @brief 写入最近对外结果码与原因码；任一参数为 `0` 时对应字段保持不变。 */
void device_runtime_private_set_latest_result(device_runtime_t device_runtime, const char *result_code,
                                              const char *reason_code);

/**
 * @brief 统一写入设备运行时落点。
 * @param device_runtime 系统上下文；必须为当前激活实例。
 * @param device_state 需要落到的设备状态。
 * @param result_code 最新结果码；传入 `0` 时保持现有结果码不变。
 * @param reason_code 最新原因码；传入 `0` 时保持现有原因码不变。
 */
void device_runtime_private_apply_device_runtime_result(device_runtime_t device_runtime, device_state_t device_state,
                                                        const char *result_code, const char *reason_code);
/**
 * @brief 应用 start accepted 路径的会话启动落点。
 * @param device_runtime 系统上下文；调用前要求 session start 已成功。
 * @param wash_session 可变洗车会话；调用后会清空上次关联键。
 * @param wash_execution 可变执行快照；调用后会重置执行状态并启动首段。
 * @return 首段启动结果；成功时会落到 running，并写入 accepted/session_started。
 */
operation_result_t device_runtime_private_apply_start_accepted(device_runtime_t device_runtime,
                                                               wash_session_t *wash_session,
                                                               wash_execution_t *wash_execution);

/** @} */

/** @name 服务参数构建 */
/** @{ */

/** @brief 将当前运行时状态填充到会话服务参数结构体中。 */
void device_runtime_private_build_session_service_args(device_runtime_t device_runtime,
                                                       wash_session_service_args_t *wash_session_service_args);

/** @brief 将当前运行时状态填充到程序快照服务参数结构体中。 */
void device_runtime_private_build_program_snapshot_service_args(
    device_runtime_t device_runtime, program_snapshot_service_args_t *program_snapshot_service_args);

/** @brief 将当前运行时状态填充到工步执行服务参数结构体中。 */
void device_runtime_private_build_execution_service_args(device_runtime_t device_runtime,
                                                         wash_execution_service_args_t *wash_execution_service_args);

/** @} */

/** @name 聚合根快照 */
/** @{ */

/** @brief 读取当前洗车会话（只读）；句柄非法时返回 `0`。 */
const wash_session_t *device_runtime_private_wash_session(const device_runtime_t device_runtime);

/** @brief 读取当前洗车会话（可写）；句柄非法时返回 `0`。 */
wash_session_t *device_runtime_private_wash_session_mutable(device_runtime_t device_runtime);

/** @brief 读取当前工步执行态（只读）；句柄非法时返回 `0`。 */
const wash_execution_t *device_runtime_private_wash_execution(const device_runtime_t device_runtime);

/** @brief 读取当前工步执行态（可写）；句柄非法时返回 `0`。 */
wash_execution_t *device_runtime_private_wash_execution_mutable(device_runtime_t device_runtime);

/** @brief 读取当前等待条件（只读）；句柄非法时返回 `0`。 */
const wait_condition_t *device_runtime_private_wait_condition(const device_runtime_t device_runtime);

/** @brief 读取当前等待条件（可写）；句柄非法时返回 `0`。 */
wait_condition_t *device_runtime_private_wait_condition_mutable(device_runtime_t device_runtime);

/** @brief 读取当前程序快照（只读）；句柄非法时返回 `0`。 */
const program_snapshot_t *device_runtime_private_program_snapshot(const device_runtime_t device_runtime);

/** @} */

/** @name 时间与触发队列 */
/** @{ */

/**
 * @brief 推进系统上下文内部时钟。
 * @param device_runtime 系统上下文，不能为空。
 * @param elapsed_ms 本次推进的毫秒数。
 */
void device_runtime_private_advance_time(device_runtime_t device_runtime, unsigned long elapsed_ms);

/**
 * @brief 向待处理触发队列追加一个事件。
 * @note 队列已满（达到 MAX_PENDING_TRIGGER_COUNT）时返回失败，触发被丢弃。
 * @param device_runtime 系统上下文，不能为空。
 * @param wash_trigger_event 待追加的触发事件，不能为空。
 * @return 追加成功返回 `operation_result_ok()`；队列已满时返回 ERROR_CODE_RESOURCE_UNAVAILABLE。
 */
operation_result_t device_runtime_private_append_trigger(device_runtime_t device_runtime,
                                                         const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 向后台线程专用的外部触发收件箱追加一个触发事件。
 * @note 当前实现面向单生产者（后台报警线程）+ 单消费者（控制线程）场景。
 * @param device_runtime 系统上下文，不能为空。
 * @param wash_trigger_event 待投递的触发事件，不能为空。
 * @return 入队成功返回 `operation_result_ok()`；收件箱已满或参数非法返回失败结果。
 */
operation_result_t device_runtime_private_enqueue_external_trigger(device_runtime_t device_runtime,
                                                                  const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 从外部触发收件箱中尝试取出一个触发事件。
 * @param device_runtime 系统上下文，不能为空。
 * @param wash_trigger_event 输出触发事件，不能为空。
 * @return 成功取到事件返回 `true`；当前收件箱为空时返回 `false`。
 */
bool device_runtime_private_try_pop_external_trigger(device_runtime_t device_runtime,
                                                     wash_trigger_event_t *wash_trigger_event);

/**
 * @brief 读取外部触发收件箱当前积压数量。
 * @param device_runtime 系统上下文；允许传入 `0`。
 * @return 当前积压数量。
 */
unsigned int device_runtime_private_external_trigger_count(const device_runtime_t device_runtime);

/**
 * @brief 读取指定索引处的待处理触发事件（只读）。
 * @param device_runtime 系统上下文；允许为 `0`。
 * @param index 目标索引，必须小于 `device_runtime_pending_trigger_count()`。
 * @return 指向触发事件的只读指针；索引越界或上下文为空时返回 `0`。
 */
const wash_trigger_event_t *device_runtime_private_pending_trigger_at(const device_runtime_t device_runtime,
                                                                      unsigned int index);

/**
 * @brief 从待处理队列中移除指定索引处的触发事件，后续元素前移。
 * @param device_runtime 系统上下文，不能为空。
 * @param remove_index 目标索引，必须小于 `device_runtime_pending_trigger_count()`。
 */
void device_runtime_private_remove_pending_trigger_at(device_runtime_t device_runtime, unsigned int remove_index);

/** @} */

/** @name 测试辅助 */
/** @{ */

/**
 * @brief 调试用：判断系统上下文实例当前是否处于占用状态。
 * @note 仅用于测试断言，不应在生产路径中使用。
 * @return 实例正被占用时返回 `true`。
 */
bool device_runtime_private_debug_is_in_use(const device_runtime_t device_runtime);

/** @} */

#endif
