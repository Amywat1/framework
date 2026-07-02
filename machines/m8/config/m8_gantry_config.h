/**
 * @file    m8_gantry_config.h
 * @brief   M8 机型龙门行走机构配置参数。
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#ifndef MACHINES_M8_CONFIG_M8_GANTRY_CONFIG_H
#define MACHINES_M8_CONFIG_M8_GANTRY_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 速度挡位（MCC gear_freq，单位：厘赫 = 0.01Hz）
 * 与 GANTRY_HIGH_SPEED DO 联动：
 *   低速挡 → HIGH_SPEED DO 断开（VFD 使用内部预设低速）
 *   高速挡 → HIGH_SPEED DO 吸合（VFD 使用内部预设高速）
 * ------------------------------------------------------------------------- */
#define CFG_GANTRY_GEAR_FREQ_LOW    1500   /**< 15 Hz，低速爬行（回原点/精定位） */
#define CFG_GANTRY_GEAR_FREQ_HIGH   4000   /**< 40 Hz，高速行走（正常洗车行程） */
#define CFG_GANTRY_GEAR_COUNT       2

/** 高速 DO 切换阈值（Hz）：换算后频率 >= 此值时拉高 GANTRY_HIGH_SPEED DO */
#define CFG_GANTRY_HIGH_SPEED_THRESHOLD_HZ  25U

/* -------------------------------------------------------------------------
 * MCC 执行器时序参数
 * ------------------------------------------------------------------------- */
#define CFG_GANTRY_TICK_MS              20      /**< tick 节拍（ms） */
#define CFG_GANTRY_WATCHDOG_MS          200     /**< tick 缺拍阈值（ms） */
#define CFG_GANTRY_COOLDOWN_MS          0       /**< 停机冷却期（ms，龙门无需冷却） */
#define CFG_GANTRY_ACCEL_MS             1000    /**< 加速时间（ms） */
#define CFG_GANTRY_DECEL_MS             1000    /**< 减速时间（ms） */
#define CFG_GANTRY_REVERSAL_STOP_MS     500     /**< 换向等待时间（ms） */
#define CFG_GANTRY_DEFAULT_MAX_MOVE_MS  120000  /**< 单次运动超时兜底（ms，2 分钟） */

/* -------------------------------------------------------------------------
 * 编码器参数
 * ------------------------------------------------------------------------- */
/** 连续多拍无脉冲变化时告警（0=不检测）；20ms×10=200ms 无脉冲视为卡滞 */
#define CFG_GANTRY_ENC_STALL_TICKS  10

/** 单拍最大跳变脉冲数（0=不检测）；超出视为信号干扰 */
#define CFG_GANTRY_ENC_JUMP_MAX     500

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_CONFIG_M8_GANTRY_CONFIG_H */
