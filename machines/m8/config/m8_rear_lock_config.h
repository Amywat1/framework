/**
 * @file    m8_rear_lock_config.h
 * @brief   M8 机型后轮锁止机构配置常量。
 */
#ifndef MACHINES_M8_CONFIG_M8_REAR_LOCK_CONFIG_H
#define MACHINES_M8_CONFIG_M8_REAR_LOCK_CONFIG_H

/** tick 节拍（ms） */
#define CFG_REAR_LOCK_TICK_MS                20U
/** 看门狗超时（ms） */
#define CFG_REAR_LOCK_WATCHDOG_MS            200U
/** 方向切换最小冷却时间（ms），保护继电器触点 */
#define CFG_REAR_LOCK_COOLDOWN_MS            300U
/** 换向停止等待时间（ms） */
#define CFG_REAR_LOCK_REVERSAL_STOP_MS       300U
/** 加速时间（ms） */
#define CFG_REAR_LOCK_ACCEL_MS               0U
/** 减速时间（ms） */
#define CFG_REAR_LOCK_DECEL_MS               0U
/** 单次运动最大超时（ms） */
#define CFG_REAR_LOCK_DEFAULT_MAX_MOVE_MS    10000U

/** 挡位数量（继电器只有一速） */
#define CFG_REAR_LOCK_GEAR_COUNT             1
/** 挡位 0 频率（厘赫，虚拟非零值） */
#define CFG_REAR_LOCK_GEAR_FREQ              5000

#endif /* MACHINES_M8_CONFIG_M8_REAR_LOCK_CONFIG_H */
