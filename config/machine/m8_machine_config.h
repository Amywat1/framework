/**
 * @file    m8_machine_config.h
 * @brief   M8 机型硬件参数（CAN 总线、Modbus、步进电机）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    本文件只含与硬件物理连接相关的常量（总线名、波特率、地址、
 *          串口设备名等），功能开关见 m8_features.h。
 */

#ifndef CONFIG_MACHINE_M8_MACHINE_CONFIG_H
#define CONFIG_MACHINE_M8_MACHINE_CONFIG_H

/* -------------------------------------------------------------------------
 * 刷子 VFD Modbus RTU 参数
 * ------------------------------------------------------------------------- */
#define CFG_VFD_BRUSH_MODBUS_ADDR   1           /* Modbus 从机地址 */
#define CFG_VFD_BRUSH_SERIAL_PORT   "/dev/ttyS1"
#define CFG_VFD_BRUSH_BAUD          9600

/* -------------------------------------------------------------------------
 * 龙门 VFD Modbus RTU 参数
 * 与刷子 VFD 共享同一串口，通过不同从机地址区分
 * ------------------------------------------------------------------------- */
#define CFG_VFD_GANTRY_MODBUS_ADDR  2
#define CFG_VFD_GANTRY_SERIAL_PORT  "/dev/ttyS1"
#define CFG_VFD_GANTRY_BAUD         9600

/* -------------------------------------------------------------------------
 * 步进电机脉冲参数
 * ------------------------------------------------------------------------- */
#define CFG_STEPPER_PULSE_US        100U    /* 单脉冲宽度（µs），需按驱动器配置调整 */
#define CFG_STEPPER_PULSE_BATCH     50U     /* 每批脉冲数（批后检查限位）*/
#define CFG_LIFT_UP_MAX_PULSES      5000U   /* 上升最大脉冲数（限位兜底）*/
#define CFG_LIFT_DOWN_DEF_PULSES    500U    /* 下降默认脉冲数（pulses=0 时使用）*/

/* -------------------------------------------------------------------------
 * 接触器切换参数
 * ------------------------------------------------------------------------- */
#define CFG_BRUSH_CONTACTOR_WAIT_MS 200U    /* VFD 停止后等待接触器吸合（ms）*/

/* -------------------------------------------------------------------------
 * 水系统时序参数
 * ------------------------------------------------------------------------- */
#define CFG_WATER_VALVE_OPEN_DELAY_MS    200U   /* 开阀后等待阀体到位再开泵（ms）*/
#define CFG_WATER_PUMP_STOP_DELAY_MS     300U   /* 关泵后等待管路泄压再关阀（ms）*/
#define CFG_WATER_PUMP_DRY_RUN_TIMEOUT_S 10U    /* 泵空转保护超时（秒）*/

/* -------------------------------------------------------------------------
 * 入口指示灯参数
 * ------------------------------------------------------------------------- */
#define CFG_ENTRY_LIGHT_BLINK_HALF_MS    200U   /* 闪烁半周期（亮/灭各持续时长，ms）*/

#endif /* CONFIG_MACHINE_M8_MACHINE_CONFIG_H */
