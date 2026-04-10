/**
 * @file    machine_config.h
 * @brief   M8 龙门洗车机机型功能开关宏
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef MACHINE_CONFIG_H
#define MACHINE_CONFIG_H

/* -------------------------------------------------------------------------
 * IO 板 CAN 总线参数
 * ------------------------------------------------------------------------- */
#define CFG_IO_CAN_BUS              "can0"
#define CFG_IO_CAN_BAUD             1000000
#define CFG_IO_SELF_NODE            0x10
#define CFG_IO_BOARD_COUNT          1       /* M8 共 1 块 IO 子板 */

/* IO 板轮询与在线检测时序 */
#define CFG_IO_UPDATE_FREQ_MS       30      /* 输入/输出缓冲刷新周期（ms）*/
#define CFG_IO_CHECK_OFFLINE_MS     300     /* 全部在线时的在线检测间隔（ms）*/
#define CFG_IO_CHECK_ONLINE_MS      2000    /* 有板掉线时的重连检测间隔（ms）*/
#define CFG_IO_OFFLINE_CNT          3       /* 连续 N 次无响应后判定掉线 */

/* -------------------------------------------------------------------------
 * 刷子硬件安装标志
 * 未安装时相关报警自动降级为 NOTICE，不停机
 * ------------------------------------------------------------------------- */
#define CFG_BRUSH_TOP_INSTALLED         1   /* 顶刷：已安装 */
#define CFG_BRUSH_SIDE_INSTALLED        1   /* 侧刷（左右同控）：已安装 */
#define CFG_TOP_LIFT_INSTALLED          1   /* 顶刷升降：已安装 */

/* -------------------------------------------------------------------------
 * 水路功能开关
 * ------------------------------------------------------------------------- */
#define CFG_WATER_PUMP_INSTALLED        1
#define CFG_WATER_CURTAIN_INSTALLED     1   /* 清水水帘 */
#define CFG_WATER_FOAM_INSTALLED        1   /* 泡沫+预洗液 */
#define CFG_WATER_BRUSH_INSTALLED       1   /* 侧刷冲水 */
#define CFG_WATER_HIGHPRES_INSTALLED    1   /* 高压冲洗 */

/* -------------------------------------------------------------------------
 * 入口设备
 * ------------------------------------------------------------------------- */
#define CFG_ENTRY_LIGHT_INSTALLED       1   /* 入口指示灯 */
#define CFG_ENTRY_ROD_INSTALLED         1   /* 电动推杆（入口挡杆）*/

/* -------------------------------------------------------------------------
 * 云端通信
 * ------------------------------------------------------------------------- */
#define CFG_CLOUD_ALIYUN                1   /* 使用阿里云物联网平台 */
/* #define CFG_CLOUD_AWS                1 */

/* -------------------------------------------------------------------------
 * 刷子 VFD Modbus RTU 参数
 * ------------------------------------------------------------------------- */
#define CFG_VFD_BRUSH_MODBUS_ADDR   1       /* 从机地址 */
#define CFG_VFD_BRUSH_SERIAL_PORT   "/dev/ttyS1"
#define CFG_VFD_BRUSH_BAUD          9600

/* -------------------------------------------------------------------------
 * 龙门 VFD Modbus RTU 参数
 * ------------------------------------------------------------------------- */
#define CFG_VFD_GANTRY_MODBUS_ADDR  2
#define CFG_VFD_GANTRY_SERIAL_PORT  "/dev/ttyS1"    /* 与刷子 VFD 同串口，不同地址 */
#define CFG_VFD_GANTRY_BAUD         9600

/* -------------------------------------------------------------------------
 * 步进电机脉冲参数
 * ------------------------------------------------------------------------- */
#define CFG_STEPPER_PULSE_US        100U    /* 单脉冲宽度（µs），需按驱动器配置调整 */

#endif /* MACHINE_CONFIG_H */
