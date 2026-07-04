/**
 * @file    m8_machine_config.h
 * @brief   M8 机型硬件参数（CAN 总线、Modbus、时序）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    本文件只含与硬件物理连接相关的常量（总线名、波特率、地址、
 *          串口设备名等），功能开关见 m8_features.h。
 */

#ifndef CONFIG_MACHINE_M8_MACHINE_CONFIG_H
#define CONFIG_MACHINE_M8_MACHINE_CONFIG_H

/* -------------------------------------------------------------------------
 * IO 子板 CAN 总线参数
 * ------------------------------------------------------------------------- */
#define CFG_IO_CAN_BUS              "can0"
#define CFG_IO_CAN_BAUD             1000000
#define CFG_IO_SELF_NODE            0x10
#define CFG_IO_BOARD_COUNT          1       /* M8 共 1 块 IO 子板 */
#define CFG_IO_PIN_COUNT            32      /* 每块 IO 子板的 IO 点数 */

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
 * 风机 VFD Modbus RTU 参数
 * 与刷子/龙门 VFD 共享同一条 485 总线，通过不同从机地址区分
 * ------------------------------------------------------------------------- */
#define CFG_VFD_FAN_MODBUS_ADDR     3
#define CFG_VFD_FAN_SERIAL_PORT     "/dev/ttyS1"
#define CFG_VFD_FAN_BAUD            9600

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

/* -------------------------------------------------------------------------
 * 语音模块 Modbus RTU 参数
 * ------------------------------------------------------------------------- */
#define CFG_VOICE_SERIAL_PORT   "/dev/ttyS2"
#define CFG_VOICE_BAUD          9600
#define CFG_VOICE_MODBUS_ADDR   1

#endif /* CONFIG_MACHINE_M8_MACHINE_CONFIG_H */
