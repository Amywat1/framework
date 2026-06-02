/**
 * @file    alarm_core.h
 * @brief   报警核心引擎接口（防抖 / 等级 / 恢复策略，数据与引擎分离）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    配置表内嵌于 alarm_core.c，机型特定触发逻辑通过回调注入。
 *          引擎通过 event_bus 发布 EVT_ALARM_TRIGGERED / EVT_ALARM_CLEARED。
 *          alarm_core_tick_ms() 须由报警轮询任务每 ALARM_POLL_PERIOD_MS 调用一次。
 */

#ifndef DOMAIN_SAFETY_ALARM_CORE_H
#define DOMAIN_SAFETY_ALARM_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/safety_types.h"
#include "domain/model/alarm_code.h"
#include "common/sw_error.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 轮询周期（ms），由调用方保证
 * ------------------------------------------------------------------------- */
#define ALARM_POLL_PERIOD_MS    10

/* -------------------------------------------------------------------------
 * 报警配置表条目（引擎内部用，不对外暴露）
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint16_t         code;
    int              trigger_ms;    /* 触发防抖延时（0=立即）*/
    alarm_level_t    level;
    int              recover_ms;    /* 恢复防抖延时（ms）*/
    alarm_recover_t  recover;       /* 恢复方式位掩码 */
    const char      *desc;
} alarm_entry_t;

/* -------------------------------------------------------------------------
 * 适配器注入（机型 HAL 在启动时注册回调，轮询中推送原始触发状态）
 * ------------------------------------------------------------------------- */
typedef void (*alarm_poll_fn_t)(void);
typedef void (*alarm_emc_reset_fn_t)(void);

void alarm_core_register_poll_fn(alarm_poll_fn_t fn);
void alarm_core_register_emc_reset_fn(alarm_emc_reset_fn_t fn);
void alarm_core_set_raw_trigger(uint16_t code, bool triggered, bool just_notice);
void alarm_core_set_state(uint16_t code, bool active, bool just_notice);

/* -------------------------------------------------------------------------
 * 引擎接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化报警引擎（清零所有运行状态）
 */
sw_err_t alarm_core_init(void);

/**
 * @brief  引擎时间片（每 ALARM_POLL_PERIOD_MS 调用一次）
 *         内部执行：① 调用 poll_fn ② 防抖计时 ③ AUTO 恢复计时
 * @param  elapsed_ms  自上次调用以来经过的时间（通常等于 ALARM_POLL_PERIOD_MS）
 */
void alarm_core_tick_ms(int elapsed_ms);

/**
 * @brief  查询指定报警是否激活
 */
bool alarm_core_is_active(uint16_t code);

/**
 * @brief  查询是否有 ERROR 级别报警激活（需立即停机）
 */
bool alarm_core_has_error(void);

/**
 * @brief  查询是否有 WARNING 级别报警激活（当前动作可继续，完成后停机）
 */
bool alarm_core_has_warning(void);

/**
 * @brief  手动复位所有 ALARM_RECOVER_MANUAL 类报警（按下复位按钮后调用）
 * @note   同时执行 emc_reset_fn（如已注册）
 */
void alarm_core_manual_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_ALARM_CORE_H */
