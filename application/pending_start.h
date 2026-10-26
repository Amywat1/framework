/**
 * @file    pending_start.h
 * @brief   未准入先点启动的门锁：就位后再按站点延迟放行
 *
 * @note    可选。未登记周期任务则只记住、不会补放行。
 *          延迟读 PARAM_KEY_PENDING_START_DELAY_MS：0 或不存在表示不记住。
 *          放行看 device_ops.is_admit_ready，忘掉看入场占用。
 *          未注册占用口不因空场忘掉。未注册 is_admit_ready 视为已准入。
 */

#ifndef APPLICATION_PENDING_START_H
#define APPLICATION_PENDING_START_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief 就位后再等的上限（毫秒） */
#define PENDING_START_DELAY_MAX_MS 30000U
/** @brief 周期推进间隔（毫秒） */
#define PENDING_START_TICK_MS 100U

/**
 * @brief  读取并夹紧站点延迟
 * @return 0 表示不记住启动；否则为 1..PENDING_START_DELAY_MAX_MS
 */
uint32_t pending_start_delay_ms(void);

/**
 * @brief  复位门锁（上电或测试）
 */
void pending_start_init(void);

/**
 * @brief  消化一次启动命令结果
 * @param  start_ret  当次 job_admit / 网关返回值
 * @note   仅当失败且当时待机、队中有单、未准入、延迟非 0 时记住。
 *         已准入仍失败（占用满、阻断等）不记住。成功则清掉。
 */
void pending_start_on_start_result(sw_err_t start_ret);

/**
 * @brief  周期推进：准入满延迟后补放行；空队、离场或离开待机则忘掉
 */
void pending_start_tick(void);

/**
 * @brief  登记周期任务（须在 scheduler_start_all 前）
 * @retval SW_OK 已登记
 * @retval 其它  周期任务表满或参数非法
 */
sw_err_t pending_start_register_task(void);

/**
 * @brief  当前是否记住了一次未准入启动
 */
bool pending_start_is_armed(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_PENDING_START_H */
