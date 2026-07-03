/**
 * @file    m8_vfd_table.h
 * @brief   M8 机型 VFD 实例配置表
 * @author  HUWANGWEI
 * @date    2026-04-14
 *
 * @note    本文件描述 M8 机型上各个 VFD 实例的硬件接线与通信参数。
 *          `driver/drv_vfd.h` 仍只保留通用驱动接口；
 *          具体到“刷子/龙门”的串口、地址、引脚归属统一收口到这里。
 */

#ifndef CONFIG_MACHINE_M8_VFD_TABLE_H
#define CONFIG_MACHINE_M8_VFD_TABLE_H

#include "machines/m8/config/m8_machine_config.h"
#include "machines/m8/config/m8_io_pins.h"

/* -------------------------------------------------------------------------
 * VFD 实例 id（与 hal_vfd_port.h 的 hal_vfd_id_t 对应）
 * 此处为 M8 机型拓扑，不得放入通用 port 层
 * ------------------------------------------------------------------------- */
#define HAL_VFD_ID_NONE    (-1)
#define HAL_VFD_GANTRY      0
#define HAL_VFD_BRUSH       1
#define HAL_VFD_FAN         2
#define HAL_VFD_ID_MAX      3  /* M8 VFD 实例总数 */

/* -------------------------------------------------------------------------
 * 刷子 VFD（仅正转）
 * ------------------------------------------------------------------------- */
#define M8_VFD_BRUSH_SERIAL_PORT   CFG_VFD_BRUSH_SERIAL_PORT
#define M8_VFD_BRUSH_BAUD          CFG_VFD_BRUSH_BAUD
#define M8_VFD_BRUSH_ADDR          CFG_VFD_BRUSH_MODBUS_ADDR
#define M8_VFD_BRUSH_PIN_FWD       M8_IO_DO_SIDE_BRUSH_FWD
#define M8_VFD_BRUSH_PIN_REV       ((io_do_t){IO_HANDLE_NULL})   /* 刷子仅正转，无反转引脚 */
#define M8_VFD_BRUSH_PIN_RST       M8_IO_DO_SIDE_BRUSH_RST

/* -------------------------------------------------------------------------
 * 龙门 VFD（支持正反转）
 * ------------------------------------------------------------------------- */
#define M8_VFD_GANTRY_SERIAL_PORT  CFG_VFD_GANTRY_SERIAL_PORT
#define M8_VFD_GANTRY_BAUD         CFG_VFD_GANTRY_BAUD
#define M8_VFD_GANTRY_ADDR         CFG_VFD_GANTRY_MODBUS_ADDR
#define M8_VFD_GANTRY_PIN_FWD      M8_IO_DO_GANTRY_FWD
#define M8_VFD_GANTRY_PIN_REV      M8_IO_DO_GANTRY_REV
#define M8_VFD_GANTRY_PIN_RST      M8_IO_DO_GANTRY_RST

/* -------------------------------------------------------------------------
 * 风机 VFD（仅正转）
 * ------------------------------------------------------------------------- */
#define M8_VFD_FAN_SERIAL_PORT     CFG_VFD_FAN_SERIAL_PORT
#define M8_VFD_FAN_BAUD            CFG_VFD_FAN_BAUD
#define M8_VFD_FAN_ADDR            CFG_VFD_FAN_MODBUS_ADDR
#define M8_VFD_FAN_PIN_FWD         M8_IO_DO_FAN_START
#define M8_VFD_FAN_PIN_REV         ((io_do_t){IO_HANDLE_NULL})   /* 风机仅正转，无反转引脚 */
#define M8_VFD_FAN_PIN_RST         M8_IO_DO_FAN_RESET

#endif /* CONFIG_MACHINE_M8_VFD_TABLE_H */
