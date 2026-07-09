/**
 * @file    m8_alarm_table.h
 * @brief   M8 机型报警配置表（DI + 防抖 + 报警码 + 等级 + 描述）
 * @author  HUWANGWEI
 * @date    2026-06-28
 *
 * @note    新增或修改 M8 硬件 DI 报警，只改本文件的 M8_HW_ALARM_TABLE。
 *          一行 = 一个报警源，包含：DI 引脚、极性、独立防抖参数、
 *          报警码（由 ALM_C_* / 部件编号 / ALM_N_* 三段组成）、
 *          等级、清除方式、描述。
 *
 *          报警码命名规则（6 位十进制）：
 *            ALARM_CODE_MAKE( ALM_C_*, 部件编号, ALM_N_* )
 *            = 大类(1位) * 100000 + 编号(3位) * 100 + 故障性质(2位)
 *          大类与故障性质常量见 framework/domain/safety/model/alarm_code.h（ALM_C_* / ALM_N_*）。
 *          部件编号为本文件内按大类分组的 ALM_<CLASS>_* 宏。
 */

#ifndef CONFIG_MACHINE_M8_ALARM_TABLE_H
#define CONFIG_MACHINE_M8_ALARM_TABLE_H

#include "framework/domain/safety/model/alarm_code.h"
#include "projects/m8/config/m8_io_pins.h"

/* =========================================================================
 * M8 部件编号（大类内流水号，001-999）
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * 大类1 动力硬件（电机/泵/风机）
 * ------------------------------------------------------------------------- */
#define ALM_POWER_GANTRY_MOTOR      1U  /**< 龙门电机 */
#define ALM_POWER_TOP_BRUSH_ROT     2U  /**< 顶刷旋转电机 */
#define ALM_POWER_TOP_BRUSH_LIFT    3U  /**< 顶刷升降电机 */
#define ALM_POWER_SIDE_BRUSH        4U  /**< 侧刷旋转电机 */
#define ALM_POWER_FAN               5U  /**< 风机旋转电机 */

/* -------------------------------------------------------------------------
 * 大类2 感知硬件（传感器/光电/限位）
 * ------------------------------------------------------------------------- */
#define ALM_SENSE_GANTRY_ENC         1U  /**< 龙门行走码盘 */
#define ALM_SENSE_GANTRY_FWD_LIM     2U  /**< 龙门前限位 */
#define ALM_SENSE_GANTRY_REV_LIM     3U  /**< 龙门后限位 */
#define ALM_SENSE_TOP_BRUSH_UP_LIM   4U  /**< 顶刷上限位 */
#define ALM_SENSE_TOP_BRUSH_DN_LIM   5U  /**< 顶刷下限位 */
#define ALM_SENSE_TOP_BRUSH_POT      6U  /**< 顶刷推杆电位计 */
#define ALM_SENSE_TOP_BRUSH_BUMPER   7U  /**< 顶刷防撞 */
#define ALM_SENSE_TOP_BRUSH_SW       8U  /**< 顶刷切换开关 */
#define ALM_SENSE_TOP_BRUSH_OVL      9U  /**< 顶刷过载 */
#define ALM_SENSE_SIDE_BRUSH_SW      10U /**< 侧刷切换开关 */
#define ALM_SENSE_SIDE_BRUSH_OVL     11U /**< 侧刷过载 */
#define ALM_SENSE_HEIGHT_RADAR       12U /**< 高度测距雷达 */
#define ALM_SENSE_BUMPER_LEFT        13U /**< 左防撞胶条 */
#define ALM_SENSE_BUMPER_ROD_LEFT    14U /**< 左防撞杆 */
#define ALM_SENSE_BUMPER_RIGHT       15U /**< 右防撞胶条 */
#define ALM_SENSE_BUMPER_ROD_RIGHT   16U /**< 右防撞杆 */
#define ALM_SENSE_ESTOP              17U /**< 急停 */

/** @brief M8 急停报警码（供运行模式桥接识别） */
#define M8_ALARM_CODE_ESTOP \
    ALARM_CODE_MAKE(ALM_C_SENSE, ALM_SENSE_ESTOP, ALM_N_SAFETY)

#define ALM_SENSE_FRONT_WHEEL_OPT    18U /**< 前轮检测光电 */
#define ALM_SENSE_REAR_WHEEL_OPT     19U /**< 后轮锁紧光电 */
#define ALM_SENSE_REAR_LOCK_HOME     20U /**< 后轮锁紧机构原点 */
#define ALM_SENSE_FOAM_LEVEL         21U /**< 泡沫液位 */
#define ALM_SENSE_WAX_LEVEL          22U /**< 水蜡液位 */
#define ALM_SENSE_PUMP_OVL           23U /**< 水泵过载 */

/* -------------------------------------------------------------------------
 * 大类3 执行元件（气缸/阀/接触器）
 * ------------------------------------------------------------------------- */
#define ALM_ACT_TOP_BRUSH_CONTACTOR   1U  /**< 顶刷切换接触器 */
#define ALM_ACT_SIDE_BRUSH_CONTACTOR  2U  /**< 侧刷切换接触器 */
#define ALM_ACT_PUMP_CONTACTOR        3U  /**< 水泵接触器 */

/* -------------------------------------------------------------------------
 * 大类4 控制硬件（控制器/电源/子板/变频器/驱动器）
 * ------------------------------------------------------------------------- */
#define ALM_CTRL_GANTRY_VFD    1U  /**< 龙门行走变频器 */
#define ALM_CTRL_BRUSH_VFD     2U  /**< 毛刷旋转变频器 */
#define ALM_CTRL_FAN_VFD       3U  /**< 风机旋转变频器 */
#define ALM_CTRL_IO_BOARD      4U  /**< 子板 */
#define ALM_CTRL_VOICE         5U  /**< 语音模块 */

/* -------------------------------------------------------------------------
 * 大类9 逻辑/软件
 * ------------------------------------------------------------------------- */
#define ALM_SW_USER_STOP       1U  /**< 用户 app 操作停止 */

/* =========================================================================
 * M8 硬件 DI 报警总表（新增/修改报警只改此处）
 *
 * 列：pin           active_low  trig_max  rel_max
 *     class         index                 nature
 *     level                 clear                    desc
 * ========================================================================= */
#define M8_HW_ALARM_TABLE(X) \
    /* --- 安全触发（trig=1 立即响应，rel=3 缓释，AUTO_STATIC）--- */ \
    X(M8_IO_DI_ESTOP,               true,  1U, 3U, \
      ALM_C_SENSE, ALM_SENSE_ESTOP,            ALM_N_SAFETY,   \
      ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "急停按钮触发") \
    X(M8_IO_DI_TOP_BRUSH_COLLISION, true,  1U, 3U, \
      ALM_C_SENSE, ALM_SENSE_TOP_BRUSH_BUMPER, ALM_N_SAFETY,   \
      ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "顶刷防撞触发") \
    X(M8_IO_DI_BUMPER_LEFT,         true,  1U, 3U, \
      ALM_C_SENSE, ALM_SENSE_BUMPER_LEFT,      ALM_N_SAFETY,   \
      ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "左防撞胶条触发") \
    X(M8_IO_DI_BUMPER_ROD_LEFT,     true,  1U, 3U, \
      ALM_C_SENSE, ALM_SENSE_BUMPER_ROD_LEFT,  ALM_N_SAFETY,   \
      ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "左防撞杆触发") \
    X(M8_IO_DI_BUMPER_RIGHT,        true,  1U, 3U, \
      ALM_C_SENSE, ALM_SENSE_BUMPER_RIGHT,     ALM_N_SAFETY,   \
      ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "右防撞胶条触发") \
    X(M8_IO_DI_BUMPER_ROD_RIGHT,    true,  1U, 3U, \
      ALM_C_SENSE, ALM_SENSE_BUMPER_ROD_RIGHT, ALM_N_SAFETY,   \
      ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "右防撞杆触发") \
    /* --- 过载（trig=3 防抖，LATCHED 须人工复位）--- */ \
    X(M8_IO_DI_TOP_BRUSH_OVERLOAD,  false, 3U, 3U, \
      ALM_C_SENSE, ALM_SENSE_TOP_BRUSH_OVL,    ALM_N_OVERLOAD, \
      ALARM_LEVEL_MAJOR,    ALARM_CLEAR_LATCHED,     "顶刷电机过载") \
    X(M8_IO_DI_SIDE_BRUSH_OVERLOAD, false, 3U, 3U, \
      ALM_C_SENSE, ALM_SENSE_SIDE_BRUSH_OVL,   ALM_N_OVERLOAD, \
      ALARM_LEVEL_MAJOR,    ALARM_CLEAR_LATCHED,     "侧刷电机过载") \
    X(M8_IO_DI_WATER_PUMP_OVERLOAD, false, 3U, 3U, \
      ALM_C_SENSE, ALM_SENSE_PUMP_OVL,         ALM_N_OVERLOAD, \
      ALARM_LEVEL_MAJOR,    ALARM_CLEAR_LATCHED,     "水泵过载") \
    /* --- 变频器/控制硬件报警 DI 反馈（trig=3，LATCHED）--- */ \
    X(M8_IO_DI_GANTRY_ALARM,        false, 3U, 3U, \
      ALM_C_CTRL,  ALM_CTRL_GANTRY_VFD,       ALM_N_HW_FAULT, \
      ALARM_LEVEL_CRITICAL, ALARM_CLEAR_LATCHED,     "龙门变频器报警反馈") \
    X(M8_IO_DI_BRUSH_ALARM,         false, 3U, 3U, \
      ALM_C_CTRL,  ALM_CTRL_BRUSH_VFD,        ALM_N_HW_FAULT, \
      ALARM_LEVEL_MAJOR,    ALARM_CLEAR_LATCHED,     "侧刷变频器报警反馈") \
    X(M8_IO_DI_FAN_ALARM,           false, 3U, 3U, \
      ALM_C_CTRL,  ALM_CTRL_FAN_VFD,          ALM_N_HW_FAULT, \
      ALARM_LEVEL_MINOR,    ALARM_CLEAR_AUTO_STATIC, "风机报警反馈")

#endif /* CONFIG_MACHINE_M8_ALARM_TABLE_H */
