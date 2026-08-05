/**
 * @file    observation_event_bridge.h
 * @brief   框架事件到观测记录的桥接
 * @author  HUWANGWEI
 * @date    2026-08-04
 *
 * @note    解决的问题：`observability/` 此前在框架生产路径零接入——`runtime/`、
 *          `application/`、`domain/`、`adapters/` 无任何引用，只有自身与单测在用。
 *          它的正确性因此只由单测保证，框架自身链路从未验证过它。
 *
 * @note    为何用桥接而不在各层直接调 `observation_publish()`：
 *          `event_bus` 按边界规则 R4 必须对业务层无知，`domain` 也不应依赖旁路
 *          设施。散布调用会让核心基础设施反向依赖 observability，且每加一处
 *          记录点都要改一个与观测无关的模块。桥接把"哪些事件值得记录"这个
 *          决策集中在一处，各层只管发事件。
 *
 * @note    覆盖范围：只桥接已有 event_bus 事件。事件覆盖不到的路径（bootstrap
 *          阶段失败、切断失败、事件总线丢弃）不在此处——它们发生时 event_bus
 *          可能尚未就绪或正是故障源，需各自就地记录。
 *
 * @note    可选设施：本桥接是"项目没有自己的事件投影"时的通用默认实现，由项目
 *          显式启用，不由 bootstrap 代为挂接。它记录的 `event_code` 是框架
 *          `event_type_t` 原值、载荷是 4 字节 `evt->param`；项目若需要自有事件
 *          编码、更完整的载荷或事件触发的附带动作（如黑匣子冻结），应实现自己的
 *          投影并跳过本桥接，而不是两者并存。
 */

#ifndef APPLICATION_BRIDGES_OBSERVATION_EVENT_BRIDGE_H
#define APPLICATION_BRIDGES_OBSERVATION_EVENT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  订阅值得留痕的框架事件并转为观测记录
 *
 * @retval SW_OK           全部订阅成功
 * @retval SW_ERR_NOT_INIT `event_bus` 或 `observation` 尚未初始化
 * @retval 其他            订阅槽不足，返回 `event_subscribe` 的错误码
 *
 * @note   调用方与时机：由项目在 `project_hooks.init_adapters()` 中显式调用，
 *         须在 `observation_init()` 与 `event_bus_init()` 之后、
 *         `scheduler_start_all()` 之前。`bootstrap` 不自动挂接——项目若自带
 *         事件投影（把事件转成带项目语义编码的记录），重复挂接会让同一事件
 *         产生两条记录，且两条的 `event_code` 与载荷格式互不相同。
 * @note   记录失败（队列满）不影响业务：`observation_publish` 非阻塞，
 *         忙则丢弃并计入 `dropped_busy_count`，本桥接不重试也不阻塞分发线程。
 */
sw_err_t observation_event_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_BRIDGES_OBSERVATION_EVENT_BRIDGE_H */
