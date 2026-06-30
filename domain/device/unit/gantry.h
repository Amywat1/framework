/**
 * @file    gantry.h
 * @brief   龙门行走设备接口（机构级，不依赖具体执行机构类型）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    龙门模块只管理行走逻辑与归位状态；硬件驱动细节通过
 *          gantry_actuator_ops_t 注入，由 machine 适配层实现。
 *          支持频率和挡位两种速度控制模式。
 */

#ifndef DOMAIN_DEVICE_GANTRY_H
#define DOMAIN_DEVICE_GANTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 执行器回调
 * ------------------------------------------------------------------------- */

/** 运动完成回调（归位完成、超时、故障）*/
typedef void (*gantry_done_fn)(sw_err_t result);

/**
 * @brief  执行器操作表
 * @note   move_freq / stop / set_done_cb / get_pos / clear_pos /
 *         at_fwd_limit / at_rev_limit 为必填；其余可为 NULL。
 *         move_gear 为 NULL 时 gantry_fwd_gear / gantry_rev_gear 返回 SW_ERR_NOT_SUPPORT。
 *         is_running / is_fault / get_current 为 NULL 时分别返回 false / false / 0。
 */
typedef struct
{
    /** 频率模式运动（speed_ref：正=前进，负=后退，0 停止） */
    sw_err_t (*move_freq)(int speed_ref);
    /** 挡位模式运动（gear_dir：正=前进，负=后退，abs=挡位号 1=最低档）；NULL=不支持 */
    sw_err_t (*move_gear)(int8_t gear_dir);
    /** 停止 */
    sw_err_t (*stop)(void);
    /** 注册运动完成回调（归位完成由此通知） */
    sw_err_t (*set_done_cb)(gantry_done_fn cb);
    /** 读取位置（码盘脉冲数） */
    int32_t  (*get_pos)(void);
    /** 清零位置计数 */
    sw_err_t (*clear_pos)(void);
    /** 查询前限位 */
    bool     (*at_fwd_limit)(void);
    /** 查询后限位 */
    bool     (*at_rev_limit)(void);
    /** 查询是否正在运动；NULL 时返回 false */
    bool     (*is_running)(void);
    /** 查询执行机构是否故障；NULL 时返回 false */
    bool     (*is_fault)(void);
    /** 读取负载电流（0.1A 单位）；NULL 时返回 0 */
    uint16_t (*get_current)(void);
} gantry_actuator_ops_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化龙门组件并注入执行器
 * @param  ops  执行器操作表，必填项不可为 NULL（见结构体注释）
 */
sw_err_t gantry_init(const gantry_actuator_ops_t *ops);

/**
 * @brief  频率模式前进
 * @param  freq_hz  VFD 频率（0.01Hz）；传 0 等同于 gantry_stop()
 */
sw_err_t gantry_fwd(uint16_t freq_hz);

/**
 * @brief  频率模式后退
 * @param  freq_hz  VFD 频率（0.01Hz）；传 0 等同于 gantry_stop()
 */
sw_err_t gantry_rev(uint16_t freq_hz);

/**
 * @brief  挡位模式前进；ops.move_gear 为 NULL 时返回 SW_ERR_NOT_SUPPORT
 * @param  gear  挡位号（1=最低档）；传 0 等同于 gantry_stop()
 */
sw_err_t gantry_fwd_gear(uint8_t gear);

/**
 * @brief  挡位模式后退；ops.move_gear 为 NULL 时返回 SW_ERR_NOT_SUPPORT
 * @param  gear  挡位号（1=最低档）；传 0 等同于 gantry_stop()
 */
sw_err_t gantry_rev_gear(uint8_t gear);

/**
 * @brief  停止龙门
 */
sw_err_t gantry_stop(void);

/**
 * @brief  开始归位（非阻塞）
 *         以频率模式后退，到后限位后自动停止、清零位置并通知 EVT_COMP_HOME_DONE。
 * @param  freq_hz  归位速度（建议慢速）
 * @retval SW_OK       归位启动成功（或已在后限位，立即发出完成通知）
 * @retval SW_ERR_BUSY 正在归位中
 */
sw_err_t gantry_home_start(uint16_t freq_hz);

/**
 * @brief  查询龙门是否在前限位
 */
bool gantry_at_fwd_limit(void);

/**
 * @brief  查询龙门是否在后限位
 */
bool gantry_at_rev_limit(void);

/**
 * @brief  获取当前位置（码盘脉冲数，归位后为 0）
 */
int32_t gantry_get_pos(void);

/**
 * @brief  重置位置计数（归位完成后由 home 逻辑调用）
 */
void gantry_reset_pos(void);

/**
 * @brief  查询龙门是否正在运动
 */
bool gantry_is_running(void);

/**
 * @brief  查询龙门执行机构是否故障
 */
bool gantry_is_fault(void);

/**
 * @brief  获取龙门负载电流（0.1A 单位；不支持时返回 0）
 */
uint16_t gantry_get_current(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_GANTRY_H */
