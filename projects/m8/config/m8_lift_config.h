/**
 * @file    m8_lift_config.h
 * @brief   M8 机型顶刷升降机构配置常量。
 */
#ifndef MACHINES_M8_CONFIG_M8_LIFT_CONFIG_H
#define MACHINES_M8_CONFIG_M8_LIFT_CONFIG_H

/** tick 节拍（ms），须与电机 tick 线程节拍一致 */
#define CFG_LIFT_TICK_MS                20U
/** 看门狗超时（ms）：tick 停止后多久触发急停 */
#define CFG_LIFT_WATCHDOG_MS            200U
/** 方向切换最小冷却时间（ms），保护继电器触点 */
#define CFG_LIFT_COOLDOWN_MS            500U
/** 换向停止等待时间（ms） */
#define CFG_LIFT_REVERSAL_STOP_MS       500U
/** 加速时间（ms），继电器无需加减速，设为 0 */
#define CFG_LIFT_ACCEL_MS               0U
/** 减速时间（ms） */
#define CFG_LIFT_DECEL_MS               0U
/** 单次运动最大超时（ms），超时后触发故障 */
#define CFG_LIFT_DEFAULT_MAX_MOVE_MS    30000U

/** 挡位数量（继电器只有一速） */
#define CFG_LIFT_GEAR_COUNT             1
/**
 * 挡位 0 频率（厘赫）。
 * 继电器驱动不使用该值，填非零虚拟值以满足 MCC 参数非零约束。
 */
#define CFG_LIFT_GEAR_FREQ              5000

#endif /* MACHINES_M8_CONFIG_M8_LIFT_CONFIG_H */
