/**
 * @file    m8_signal_table.h
 * @brief   M8 信号滤波配置表（编译期只读）
 * @author  胡望伟
 * @date    2026-04-13
 *
 * @note    每行定义一路 DI 信号的滤波参数、极性、事件绑定和报警绑定。
 *          新增信号时优先扩展本表，避免将防抖与极性规则散落到业务代码中。
 */

#ifndef CONFIG_MACHINE_M8_SIGNAL_TABLE_H
#define CONFIG_MACHINE_M8_SIGNAL_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "common/event_types.h"
#include "common/sw_types.h"
#include "common/io_handle.h"
#include "adapters/hal/linux_hw/drv/drv_io.h"
#include "domain/model/alarm_code.h"

/* -------------------------------------------------------------------------
 * 信号标识
 * ------------------------------------------------------------------------- */
typedef enum
{
    M8_SIG_ESTOP = 0,         /* 急停 */
    M8_SIG_GANTRY_FWD_LIM,    /* 龙门前限位 */
    M8_SIG_GANTRY_REV_LIM,    /* 龙门后限位 */
    M8_SIG_LIFT_UP_LIM,       /* 顶刷升降上限位 */
    M8_SIG_LIFT_DOWN_LIM,     /* 顶刷升降下限位 */
    M8_SIG_MAX
} m8_signal_id_t;

/* -------------------------------------------------------------------------
 * 单路信号配置
 * ------------------------------------------------------------------------- */
typedef struct
{
    m8_signal_id_t sig_id;         /**< 信号标识 */
    io_di_t        io_id;          /**< DI 句柄 */
    bool           active_low;     /**< true=低电平有效（常闭接法） */
    uint8_t        trig_count;     /**< 触发确认次数 */
    uint8_t        release_count;  /**< 释放确认次数 */
    event_type_t   evt_active;     /**< 确认触发时发布的事件 */
    event_type_t   evt_inactive;   /**< 确认释放时发布的事件 */
    uint16_t       alarm_code;     /**< 关联报警码，0=不联动 */
} m8_signal_cfg_t;

/* -------------------------------------------------------------------------
 * 配置表
 *
 * 说明：
 *   1. 急停要求快速响应，因此触发 1 次确认、释放 3 次确认。
 *   2. 限位信号采用对称 3 次确认，抑制机械触点抖动。
 *   3. alarm_code 用于将滤波后的稳定状态同步给 alarm_core。
 * ------------------------------------------------------------------------- */
static const m8_signal_cfg_t m8_signal_table[] = {
/*  sig_id                  io_id                 active_low  trig  rel  evt_active              evt_inactive           alarm_code */
    { M8_SIG_ESTOP,          DI_ESTOP,             true,       1U,   3U,  EVT_HW_ESTOP_ON,        EVT_HW_ESTOP_OFF,      ALARM_CODE_ESTOP          },
    { M8_SIG_GANTRY_FWD_LIM, DI_GANTRY_FWD_LIMIT,  false,      3U,   3U,  EVT_HW_GANTRY_FWD_LIM, EVT_NONE,              0U                        },
    { M8_SIG_GANTRY_REV_LIM, DI_GANTRY_REV_LIMIT,  false,      3U,   3U,  EVT_HW_GANTRY_REV_LIM, EVT_NONE,              0U                        },
    { M8_SIG_LIFT_UP_LIM,    DI_LIFT_UP_LIMIT,     false,      3U,   3U,  EVT_HW_LIFT_UP_LIM,    EVT_NONE,              0U                        },
    { M8_SIG_LIFT_DOWN_LIM,  DI_LIFT_DOWN_LIMIT,   false,      3U,   3U,  EVT_HW_LIFT_DOWN_LIM,  EVT_NONE,              0U                        },
};

#define M8_SIGNAL_TABLE_SIZE  ((int)ARRAY_SIZE(m8_signal_table))

#endif /* CONFIG_MACHINE_M8_SIGNAL_TABLE_H */
