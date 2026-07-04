/**
 * @file    m8_alarm_sw_table.h
 * @brief   M8 机型调用点触发报警目录（按需通讯 + 流程/动作超时 + 软件逻辑）
 * @author  HUWANGWEI
 * @date    2026-06-28
 *
 * @note    本表收录所有**不经 DI 边沿检测、不经心跳 watchdog**的报警。
 *          这三类报警的共同特征是"触发点在代码调用处"：
 *
 *          1. 按需通讯失败：HAL adapter 操作函数发送命令超时/无响应时 trigger，
 *             下次操作成功时 clear（AUTO_STATIC 则自愈；LATCHED 须人工复位）。
 *
 *          2. 流程/动作超时：domain unit 状态机在动作超时时 trigger，
 *             动作成功完成或下次动作启动时 clear。
 *             trigger 点：gantry.c / top_brush.c 等 domain unit 操作内部。
 *
 *          3. 软件逻辑事件：业务层或流程引擎主动上报的记录性事件。
 *
 *          X-macro 列：class  index  nature  level  clear  desc
 *          （无 pin / 极性 / 防抖字段，仅提供 alarm_core 目录条目）
 */

#ifndef CONFIG_MACHINE_M8_SW_ALARM_TABLE_H
#define CONFIG_MACHINE_M8_SW_ALARM_TABLE_H

#include "framework/domain/safety/model/alarm_code.h"
#include "projects/m8/config/m8_alarm_table.h"

/* =========================================================================
 * M8 调用点触发报警总表
 * ========================================================================= */
#define M8_SW_ALARM_TABLE(X) \
    \
    /* ------------------------------------------------------------------ \
     * 按需通讯失败（HAL adapter 操作调用点 trigger/clear）               \
     * ------------------------------------------------------------------ */ \
    X(ALM_C_CTRL, ALM_CTRL_VOICE,  ALM_N_COMM_LOST, \
      ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, "语音模块通讯失败") \
    \
    /* ------------------------------------------------------------------ \
     * 流程类—龙门行走（domain/device/mechanism/gantry.c 超时时触发）          \
     * ------------------------------------------------------------------ */ \
    X(ALM_C_SENSE, ALM_SENSE_GANTRY_ENC,     ALM_N_SIG_ERR, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_AUTO_STATIC, "龙门码盘行走无脉冲") \
    X(ALM_C_SENSE, ALM_SENSE_GANTRY_FWD_LIM, ALM_N_TIMEOUT, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_LATCHED,     "龙门前限位动作超时") \
    X(ALM_C_SENSE, ALM_SENSE_GANTRY_REV_LIM, ALM_N_TIMEOUT, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_LATCHED,     "龙门后限位动作超时") \
    \
    /* ------------------------------------------------------------------ \
     * 流程类—顶刷升降（domain/device/mechanism/top_brush.c 超时时触发）       \
     * ------------------------------------------------------------------ */ \
    X(ALM_C_SENSE, ALM_SENSE_TOP_BRUSH_UP_LIM, ALM_N_TIMEOUT, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_LATCHED,     "顶刷上限位动作超时") \
    X(ALM_C_SENSE, ALM_SENSE_TOP_BRUSH_DN_LIM, ALM_N_TIMEOUT, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_LATCHED,     "顶刷下限位动作超时") \
    X(ALM_C_SENSE, ALM_SENSE_TOP_BRUSH_POT,    ALM_N_SIG_ERR, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_AUTO_STATIC, "顶刷推杆电位计读数异常") \
    X(ALM_C_SENSE, ALM_SENSE_TOP_BRUSH_SW,     ALM_N_SIG_ERR, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_AUTO_STATIC, "顶刷切换开关状态不一致") \
    \
    /* ------------------------------------------------------------------ \
     * 流程类—侧刷                                                         \
     * ------------------------------------------------------------------ */ \
    X(ALM_C_SENSE, ALM_SENSE_SIDE_BRUSH_SW,    ALM_N_SIG_ERR, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_AUTO_STATIC, "侧刷切换开关状态不一致") \
    \
    /* ------------------------------------------------------------------ \
     * 流程类—高度与车辆检测（触发点待流程引擎实现后接入）                 \
     * ------------------------------------------------------------------ */ \
    X(ALM_C_SENSE, ALM_SENSE_HEIGHT_RADAR,     ALM_N_SIG_ERR, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_AUTO_STATIC, "高度测距雷达信号异常") \
    X(ALM_C_SENSE, ALM_SENSE_FRONT_WHEEL_OPT,  ALM_N_SIG_ERR, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_AUTO_STATIC, "前轮检测光电状态异常") \
    X(ALM_C_SENSE, ALM_SENSE_REAR_WHEEL_OPT,   ALM_N_SIG_ERR, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_AUTO_STATIC, "后轮锁紧光电状态异常") \
    X(ALM_C_SENSE, ALM_SENSE_REAR_LOCK_HOME,   ALM_N_TIMEOUT, \
      ALARM_LEVEL_MAJOR, ALARM_CLEAR_LATCHED,     "后轮锁紧机构原点动作超时") \
    \
    /* ------------------------------------------------------------------ \
     * 软件逻辑事件（记录性，不影响安全态）                                \
     * ------------------------------------------------------------------ */ \
    X(ALM_C_SW, ALM_SW_USER_STOP, ALM_N_OTHER, \
      ALARM_LEVEL_MINOR, ALARM_CLEAR_AUTO_STATIC, "用户主动停止")

#endif /* CONFIG_MACHINE_M8_SW_ALARM_TABLE_H */
