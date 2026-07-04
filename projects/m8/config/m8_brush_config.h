/**
 * @file    m8_brush_config.h
 * @brief   M8 机型刷子控制参数配置。
 *
 * 包含接触器时序、变频器挡位频率、MCC 电机运行参数。
 * 所有数值均为硬件测试后的调试值，修改须经充分测试。
 */
#ifndef MACHINES_M8_CONFIG_M8_BRUSH_CONFIG_H
#define MACHINES_M8_CONFIG_M8_BRUSH_CONFIG_H

/* -------------------------------------------------------------------------
 * 接触器时序参数
 * ------------------------------------------------------------------------- */

/** 断电后等待接触器弹簧回位完成（ms） */
#define CFG_BRUSH_CONTACTOR_RELEASE_MS   200U

/** 通电后等待接触器触点吸合并稳定（ms） */
#define CFG_BRUSH_CONTACTOR_CLOSE_MS     300U

/* -------------------------------------------------------------------------
 * 变频器挡位频率（单位：厘赫，即 0.01 Hz）
 *
 * 刷子 VFD 只需正转，挡位从低速到高速排列。
 * 挡位索引对应 brush_start() 的 speed_gear 参数。
 * ------------------------------------------------------------------------- */

/** 挡位 0：低速（用于点动或边缘过渡）—— 20 Hz */
#define CFG_BRUSH_GEAR_FREQ_LOW          2000

/** 挡位 1：正常洗车速度 —— 35 Hz */
#define CFG_BRUSH_GEAR_FREQ_NORMAL       3500

/** 挡位 2：高速（强力洗涤）—— 45 Hz */
#define CFG_BRUSH_GEAR_FREQ_HIGH         4500

/** 有效挡位数 */
#define CFG_BRUSH_GEAR_COUNT             3

/* -------------------------------------------------------------------------
 * MCC 电机运行参数
 * ------------------------------------------------------------------------- */

/** 停机冷却期：变频器停止后至接触器允许切换的最短等待时间（ms）
 *  须大于 CFG_BRUSH_CONTACTOR_RELEASE_MS，由 MCC 的 cooldown 机制保证 */
#define CFG_BRUSH_COOLDOWN_MS            500

/** 加速时间（ms），与变频器加速时间参数对应 */
#define CFG_BRUSH_ACCEL_MS               1500

/** 减速时间（ms），与变频器减速时间参数对应 */
#define CFG_BRUSH_DECEL_MS               2000

/** 电机超时兜底（ms）：brush 仅使用 run_continuous，此值仅用于满足 MCC 配置约束 */
#define CFG_BRUSH_DEFAULT_MAX_MOVE_MS    300000

/** MCC tick 周期（ms） */
#define CFG_BRUSH_TICK_MS                20

/** MCC 看门狗缺拍阈值（ms）：超过此值触发安全切断 */
#define CFG_BRUSH_WATCHDOG_MS            200

/* -------------------------------------------------------------------------
 * 电流监测参数（由 VFD Modbus 读取，单位：0.01 A）
 * ------------------------------------------------------------------------- */

/** 匀速段电流上限：超过则判过载（0.01 A，对应实际电流值需现场标定） */
#define CFG_BRUSH_CUR_MAX_STEADY         1500   /* 15.00 A */

/** 匀速段电流下限：低于则判断带/空转 */
#define CFG_BRUSH_CUR_MIN_STEADY         20     /* 0.20 A */

/** 电流越限持续确认门限（ms） */
#define CFG_BRUSH_CUR_CONFIRM_MS         2000

/** 启动后延迟多长时间才启用电流监测（等待启动电流回落，ms） */
#define CFG_BRUSH_CUR_STARTUP_DELAY_MS   3000

#endif /* MACHINES_M8_CONFIG_M8_BRUSH_CONFIG_H */
