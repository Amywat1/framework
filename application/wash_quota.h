/**
 * @file    wash_quota.h
 * @brief   离线开洗次数授权：可监控则放行，离线只靠平台下发的剩余次数
 *
 * @note    不用时间宽限。云连接或项目把「可监控」置位时不增加次数。
 *          只有 wash_quota_apply_grant() 且序号严格递增才能改写剩余次数。
 *          remaining / grant_serial 经参数存储落盘，上电由 init 恢复；可监控不落盘。
 */

#ifndef APPLICATION_WASH_QUOTA_H
#define APPLICATION_WASH_QUOTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief 单次授权可下发的剩余次数上限 */
#define WASH_QUOTA_REMAINING_MAX (255U)

/**
 * @brief 次数授权快照（诊断与遥测）
 */
typedef struct {
    bool     monitorable; /**< 平台当前能监控订单时为 true */
    uint8_t  remaining;   /**< 离线剩余次数 */
    uint32_t grant_serial; /**< 最近一次已接受的授权序号 */
} wash_quota_info_t;

/**
 * @brief  初始化次数授权
 *
 * 从参数存储恢复 remaining / grant_serial，不是每次上电清零；无落盘记录时均为 0。
 * 可监控默认为 false，由云连接边沿或项目调用 set_monitorable 更新。
 *
 * @retval SW_OK 已就绪（存储未注册时仍用内存态，不失败）
 */
sw_err_t wash_quota_init(void);

/**
 * @brief  设置平台当前是否能监控订单
 * @param  monitorable  true 表示可监控，开洗不扣次数
 *
 * @note   只改监控标志，不增加 remaining。
 */
void wash_quota_set_monitorable(bool monitorable);

/**
 * @brief  当前是否视为可监控
 */
bool wash_quota_is_monitorable(void);

/**
 * @brief  平台下发离线剩余次数
 * @param  remaining  新的剩余次数，0..WASH_QUOTA_REMAINING_MAX
 * @param  serial     授权序号，必须大于当前 grant_serial
 * @retval SW_OK          已写入；已注册存储时已落盘
 * @retval SW_ERR_STATE   序号不大于已接受序号（拒绝重放或过期授权）
 * @retval SW_ERR_STORAGE 已注册存储但落盘失败（内存回退，重启仍见旧值）
 */
sw_err_t wash_quota_apply_grant(uint8_t remaining, uint32_t serial);

/**
 * @brief  判断当前能否开洗
 * @param[out] need_consume  非空时：离线放行则写 true，可监控放行则写 false
 * @retval SW_OK        允许开洗
 * @retval SW_ERR_PARAM need_consume 为空
 * @retval SW_ERR_STATE 不可监控且剩余为 0
 */
sw_err_t wash_quota_check_start(bool *need_consume);

/**
 * @brief  离线开洗成功后扣减一次并落盘
 * @retval SW_OK          已减一；已注册存储时已落盘
 * @retval SW_ERR_STATE   剩余已为 0（未改写）
 * @retval SW_ERR_STORAGE 已注册存储但落盘失败（次数回退）
 *
 * @note   仅在 start_job 成功之后调用。可监控路径不得调用。
 */
sw_err_t wash_quota_consume_one(void);

/**
 * @brief  读取当前授权快照
 * @param[out] info  输出；为空则忽略
 */
void wash_quota_get_info(wash_quota_info_t *info);

/**
 * @brief  测试复位：剩余 0、序号 0、可监控为 true
 *
 * @note   仅单测使用，使既有放行用例不因默认离线拒绝而失败。
 */
void wash_quota_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_WASH_QUOTA_H */
