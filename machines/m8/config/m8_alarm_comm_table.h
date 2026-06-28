/**
 * @file    m8_alarm_comm_table.h
 * @brief   M8 机型周期通讯设备心跳监控表
 * @author  HUWANGWEI
 * @date    2026-06-28
 *
 * @note    本表仅包含**周期通讯型**设备——控制器与其持续交换数据，静默即异常。
 *          非周期（按需）通讯设备的报警见 m8_sw_alarm_table.h，
 *          由各自 HAL adapter 在操作失败时在调用点直接 trigger/clear。
 *
 *          X-macro 列说明：
 *            dev_id     — 设备枚举标识（comm_dev_id_t 成员名）
 *            timeout_ms — 超过此时长未收到心跳则判定通讯丢失
 *            class      — 报警码大类（ALM_C_*）
 *            index      — 大类内部件编号（ALM_CTRL_*）
 *            nature     — 故障性质（ALM_N_*）
 *            level      — 报警等级
 *            clear      — 清除方式（通讯丢失通常为 LATCHED）
 *            desc       — 中文描述
 */

#ifndef CONFIG_MACHINE_M8_COMM_WATCHDOG_TABLE_H
#define CONFIG_MACHINE_M8_COMM_WATCHDOG_TABLE_H

#include "domain/model/alarm_code.h"
#include "machines/m8/config/m8_alarm_table.h"

/* =========================================================================
 * M8 周期通讯设备心跳监控总表
 *   新增周期通讯设备：在此加一行，其余无需改动
 * ========================================================================= */
/*            dev_id              timeout_ms  class        index                 nature           level                 clear                  desc                  */
#define M8_COMM_WATCHDOG_TABLE(X) \
    X(COMM_DEV_GANTRY_VFD, 500U, ALM_C_CTRL, ALM_CTRL_GANTRY_VFD, ALM_N_COMM_LOST, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_LATCHED, "龙门变频器通讯丢失") \
    X(COMM_DEV_BRUSH_VFD,  500U, ALM_C_CTRL, ALM_CTRL_BRUSH_VFD,  ALM_N_COMM_LOST, ALARM_LEVEL_MAJOR,    ALARM_CLEAR_LATCHED, "毛刷变频器通讯丢失") \
    X(COMM_DEV_FAN_VFD,    500U, ALM_C_CTRL, ALM_CTRL_FAN_VFD,    ALM_N_COMM_LOST, ALARM_LEVEL_MINOR,    ALARM_CLEAR_LATCHED, "风机变频器通讯丢失") \
    X(COMM_DEV_IO_BOARD,   300U, ALM_C_CTRL, ALM_CTRL_IO_BOARD,   ALM_N_COMM_LOST, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_LATCHED, "IO子板通讯丢失")

#endif /* CONFIG_MACHINE_M8_COMM_WATCHDOG_TABLE_H */
