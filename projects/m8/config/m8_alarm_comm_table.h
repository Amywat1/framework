/**
 * @file    m8_alarm_comm_table.h
 * @brief   M8 机型周期通讯设备心跳监控表
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef CONFIG_MACHINE_M8_COMM_WATCHDOG_TABLE_H
#define CONFIG_MACHINE_M8_COMM_WATCHDOG_TABLE_H

#include "framework/domain/safety/model/alarm_types.h"
#include "projects/m8/config/m8_alarm_table.h"

/**
 * @brief  周期通讯设备心跳监控总表
 *
 * 列：dev_id, timeout_ms, class, index, nature, level, response,
 *     clear, immediate_cutout, desc
 */
#define M8_COMM_WATCHDOG_TABLE(X) \
    X(COMM_DEV_GANTRY_VFD, 500U, ALM_C_CTRL, ALM_CTRL_GANTRY_VFD, ALM_N_COMM_LOST, \
      ALARM_LEVEL_CRITICAL, RESP_STOP_IMMEDIATELY, ALARM_CLEAR_MANUAL_RESET, true, \
      "龙门变频器通讯丢失") \
    X(COMM_DEV_BRUSH_VFD,  500U, ALM_C_CTRL, ALM_CTRL_BRUSH_VFD,  ALM_N_COMM_LOST, \
      ALARM_LEVEL_MAJOR, RESP_COMPLETE_THEN_ASSESS, ALARM_CLEAR_MANUAL_RESET, false, \
      "毛刷变频器通讯丢失") \
    X(COMM_DEV_FAN_VFD,    500U, ALM_C_CTRL, ALM_CTRL_FAN_VFD,    ALM_N_COMM_LOST, \
      ALARM_LEVEL_MINOR, RESP_LOG_ONLY, ALARM_CLEAR_MANUAL_RESET, false, \
      "风机变频器通讯丢失") \
    X(COMM_DEV_IO_BOARD,   300U, ALM_C_CTRL, ALM_CTRL_IO_BOARD,   ALM_N_COMM_LOST, \
      ALARM_LEVEL_CRITICAL, RESP_STOP_IMMEDIATELY, ALARM_CLEAR_MANUAL_RESET, true, \
      "IO子板通讯丢失")

#endif /* CONFIG_MACHINE_M8_COMM_WATCHDOG_TABLE_H */
