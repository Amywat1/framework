#ifndef APPLICATION_COORDINATORS_CONTROL_CONTEXT_PRIVATE_H
#define APPLICATION_COORDINATORS_CONTROL_CONTEXT_PRIVATE_H

#include "application/coordinators/control_context.h"
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
 * @file control_context_private.h
 * @brief 定义系统上下文协调层的私有内部接口。
 */

/**
 * @brief 将设备状态从 INIT 推进至 STOPPED，标志端口装配完成、设备就绪可接受命令。
 * @return 成功返回 `operation_result_ok()`；设备状态非 INIT 或实例未激活时返回失败。
 */
operation_result_t control_context_private_enter_stopped(void);

/** @name 设备状态与端口 */
/** @{ */

/** @brief 读取当前设备状态；实例未激活时返回 `DEVICE_STATE_INIT`。 */
device_state_t control_context_private_device_state(void);

/** @brief 设置当前设备状态；实例未激活时为空操作。 */
void control_context_private_set_device_state(device_state_t device_state);

/** @brief 读取执行机构端口（只读）；实例未激活时返回 `0`。 */
const actuator_port_t *control_context_private_actuator_port(void);

/** @brief 读取传感器端口（只读）；实例未激活时返回 `0`。 */
const sensor_port_t *control_context_private_sensor_port(void);

/** @} */

/** @name 全局故障 */
/** @{ */

/** @brief 判断是否存在未清除的全局故障。 */
bool control_context_private_global_fault_present(void);

/** @brief 读取全局故障原因描述；实例未激活时返回空字符串。 */
const char *control_context_private_global_fault_reason(void);

/**
 * @brief 设置全局故障信息。
 * @param fault_code   故障码（机器可读短标识），不能为空。
 * @param fault_reason 故障原因描述（人类可读），允许为空（不更新现有原因）。
 */
void control_context_private_set_global_fault(const char *fault_code, const char *fault_reason);

/** @brief 清除全局故障标志及相关信息。 */
void control_context_private_clear_global_fault(void);

/** @} */

/** @name 运行时结果 */
/** @{ */

/** @brief 读取最近一次状态迁移记录（只读）；实例未激活时返回 `0`。 */
const state_transition_record_t *control_context_private_last_transition_record(void);

/** @brief 读取最近一次状态迁移记录（可写）；实例未激活时返回 `0`。 */
state_transition_record_t *control_context_private_last_transition_record_mutable(void);

/** @brief 写入最近对外结果码与原因码；任一参数为 `0` 时对应字段保持不变。 */
void control_context_private_set_latest_result(const char *result_code, const char *reason_code);

/**
 * @brief 统一写入设备运行时落点。
 * @param device_state 需要落到的设备状态。
 * @param result_code 最新结果码；传入 `0` 时保持现有结果码不变。
 * @param reason_code 最新原因码；传入 `0` 时保持现有原因码不变。
 */
void control_context_private_apply_control_context_result(device_state_t device_state,
                                                        const char *result_code, const char *reason_code);
/**
 * @brief 应用 start accepted 路径的会话启动落点。
 * @param wash_session 可变洗车会话；调用后会清空上次关联键。
 * @param wash_execution 可变执行快照；调用后会重置执行状态并启动首段。
 * @return 首段启动结果；成功时会落到 running，并写入 accepted/session_started。
 */
operation_result_t control_context_private_apply_start_accepted(wash_session_t *wash_session,
                                                               wash_execution_t *wash_execution);

/** @} */

/** @name 服务参数构建 */
/** @{ */

/** @brief 将当前运行时状态填充到会话服务参数结构体中。 */
void control_context_private_build_session_service_args(wash_session_service_args_t *wash_session_service_args);

/** @brief 将当前运行时状态填充到程序快照服务参数结构体中。 */
void control_context_private_build_program_snapshot_service_args(
    program_snapshot_service_args_t *program_snapshot_service_args);

/** @brief 将当前运行时状态填充到工步执行服务参数结构体中。 */
void control_context_private_build_execution_service_args(wash_execution_service_args_t *wash_execution_service_args);

/** @} */

/** @name 聚合根快照 */
/** @{ */

/** @brief 读取当前洗车会话（只读）；实例未激活时返回 `0`。 */
const wash_session_t *control_context_private_wash_session(void);

/** @brief 读取当前洗车会话（可写）；实例未激活时返回 `0`。 */
wash_session_t *control_context_private_wash_session_mutable(void);

/** @brief 读取当前工步执行态（只读）；实例未激活时返回 `0`。 */
const wash_execution_t *control_context_private_wash_execution(void);

/** @brief 读取当前工步执行态（可写）；实例未激活时返回 `0`。 */
wash_execution_t *control_context_private_wash_execution_mutable(void);

/** @brief 读取当前等待条件（可写）；实例未激活时返回 `0`。 */
wait_condition_t *control_context_private_wait_condition_mutable(void);

/** @brief 读取当前程序快照（只读）；实例未激活时返回 `0`。 */
const program_snapshot_t *control_context_private_program_snapshot(void);

/** @} */

/** @name 时间与触发队列 */
/** @{ */

/**
 * @brief 向后台线程专用的外部触发收件箱追加一个触发事件。
 * @note 当前实现面向单生产者（后台报警线程）+ 单消费者（控制线程）场景。
 * @param wash_trigger_event 待投递的触发事件，不能为空。
 * @return 入队成功返回 `operation_result_ok()`；收件箱已满或参数非法返回失败结果。
 */
operation_result_t control_context_private_enqueue_external_trigger(const wash_trigger_event_t *wash_trigger_event);

/** @} */

#endif
