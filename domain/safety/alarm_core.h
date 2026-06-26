/**
 * @file    alarm_core.h
 * @brief   报警核心引擎：配置表 + 活跃集 + 激活/清除 + 等级聚合
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    alarm_core 是 domain/safety 的报警权威事实源：
 *            - 持有报警目录（报警码 → 等级/清除方式/描述）；目录是数据，
 *              由外部经 alarm_core_load() 注入（来源为 JSON 配置）
 *            - alarm_core_init() 先装载「内置兜底目录」（安全必需项），保证
 *              即使配置加载失败也不会丢失安全报警；随后 load 成功则整表替换
 *            - 维护活跃报警集（同一报警码同一时刻至多一个活跃实例）
 *            - 激活/清除时发布 EVT_ALARM_TRIGGERED / EVT_ALARM_CLEARED
 *          安全态（OK/WARNING/LOCKOUT）由本模块按活跃集最高等级派生，
 *          但 EVT_SAFETY_* 的发布交由 domain/safety/safety_fsm 统一负责。
 *          trigger/clear 可能在 io_poll 线程调用、查询在 dispatch 线程调用，
 *          内部以互斥锁保护活跃集。
 */

#ifndef DOMAIN_SAFETY_ALARM_CORE_H
#define DOMAIN_SAFETY_ALARM_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/model/alarm_code.h"
#include "domain/model/safety_types.h"
#include <stdint.h>

/**
 * @brief  初始化报警核心：装载内置兜底目录、清空活跃集、注册 alarm_binding_port
 * @retval SW_OK
 * @note   兜底目录仅含安全必需项（至少 CRITICAL 类）；正常应在 init 后再用
 *         alarm_core_load() 以完整 JSON 目录整表替换。
 */
sw_err_t alarm_core_init(void);

/**
 * @brief  加载报警目录（整表替换；通常由 bootstrap 用 JSON 解析结果调用）
 * @param  defs   报警定义数组
 * @param  count  条目数
 * @retval SW_OK / SW_ERR_PARAM（defs 为空）/ SW_ERR_OVERFLOW（count 超过 ALARM_CATALOG_MAX）
 * @note   替换目录的同时清空活跃集；应在报警触发开始前（启动阶段）调用。
 */
sw_err_t alarm_core_load(const alarm_def_t *defs, unsigned count);

/**
 * @brief  置位报警（alarm_binding_port.trigger 实现；线程安全）
 * @param  alarm_code  报警码
 * @retval SW_OK / SW_ERR_PARAM（未知报警码）
 * @note   状态由「未激活」翻转为「激活」时发布 EVT_ALARM_TRIGGERED；重复触发幂等
 */
sw_err_t alarm_core_trigger(uint32_t alarm_code);

/**
 * @brief  清除报警（alarm_binding_port.clear 实现；线程安全）
 * @param  alarm_code  报警码
 * @retval SW_OK / SW_ERR_PARAM（未知报警码）
 * @note   状态由「激活」翻转为「未激活」时发布 EVT_ALARM_CLEARED；重复清除幂等
 */
sw_err_t alarm_core_clear(uint32_t alarm_code);

/**
 * @brief  按当前活跃集最高等级派生的安全态
 * @retval SAFETY_STATE_OK / WARNING / LOCKOUT
 */
safety_state_t alarm_core_safety_state(void);

/**
 * @brief  当前活跃集中等级最高的报警码（用于上报/显示）
 * @retval 活跃报警码；无活跃报警时返回 ALARM_CODE_NONE
 */
uint32_t alarm_core_top_code(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_ALARM_CORE_H */
