/**
 * @file    m8_fan_config.h
 * @brief   M8 机型风机 VFD 电机运动参数配置。
 *
 * @note    风机改由共享电机执行器管理后，本文件里的冷却/加减速/运行频率
 *          均为占位值，尚无现场实测记录（不同于 m8_brush_config.h 已标注
 *          "硬件测试后的调试值"）。上真机前须现场标定后再确认这些数值。
 */
#ifndef MACHINES_M8_CONFIG_M8_FAN_CONFIG_H
#define MACHINES_M8_CONFIG_M8_FAN_CONFIG_H

/** MCC tick 周期（ms），须与共享执行器 tick 一致 */
#define CFG_FAN_TICK_MS                 20U
/** MCC 看门狗缺拍阈值（ms） */
#define CFG_FAN_WATCHDOG_MS              200U
/** 停机冷却期（ms）：占位值，待现场标定 */
#define CFG_FAN_COOLDOWN_MS              500U
/** 风机仅正转，无换向场景 */
#define CFG_FAN_REVERSAL_STOP_MS         0U
/** 加速时间（ms）：占位值，待现场标定 */
#define CFG_FAN_ACCEL_MS                 1000U
/** 减速时间（ms）：占位值，待现场标定 */
#define CFG_FAN_DECEL_MS                 1000U
/** 电机超时兜底（ms）：风机仅使用 run_continuous，此值仅用于满足 MCC 配置约束 */
#define CFG_FAN_DEFAULT_MAX_MOVE_MS      300000U

/** 有效挡位数：风机单速运行 */
#define CFG_FAN_GEAR_COUNT               1
/** 挡位 0 运行频率（厘赫，即 0.01 Hz）：占位值，待现场标定 */
#define CFG_FAN_GEAR_FREQ                5000

#endif /* MACHINES_M8_CONFIG_M8_FAN_CONFIG_H */
