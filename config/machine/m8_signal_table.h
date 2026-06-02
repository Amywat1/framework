/**
 * @file    m8_signal_table.h
 * @brief   M8 信号滤波配置表（编译期只读）
 * @author  胡望伟
 * @date    2026-04-13
 *
 * @note    每行定义一路 DI 信号的滤波参数、极性及报警绑定。
 *          io_id 使用 IO_HANDLE_MAKE 常量编码，避免依赖 drv_io.h 复合字面量。
 */

#ifndef CONFIG_MACHINE_M8_SIGNAL_TABLE_H
#define CONFIG_MACHINE_M8_SIGNAL_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "common/sw_types.h"
#include "common/io_handle.h"
#include "domain/model/alarm_code.h"

/* 与 config/machine/m8_io_table.h 中 DI 点位一致（子板 1）*/
#define M8_SIG_DI_ESTOP_RAW           IO_HANDLE_MAKE(IO_KIND_DI, 1U, 21U)
#define M8_SIG_DI_GANTRY_FWD_RAW      IO_HANDLE_MAKE(IO_KIND_DI, 1U, 13U)
#define M8_SIG_DI_GANTRY_REV_RAW      IO_HANDLE_MAKE(IO_KIND_DI, 1U, 14U)
#define M8_SIG_DI_LIFT_UP_RAW         IO_HANDLE_MAKE(IO_KIND_DI, 1U, 15U)
#define M8_SIG_DI_LIFT_DOWN_RAW       IO_HANDLE_MAKE(IO_KIND_DI, 1U, 16U)

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
    uint16_t       alarm_code;     /**< 关联报警码，0=不联动 */
} m8_signal_cfg_t;

/* -------------------------------------------------------------------------
 * 配置表
 * ------------------------------------------------------------------------- */
static const m8_signal_cfg_t m8_signal_table[] = {
/*  sig_id                  io_id                       active_low  trig  rel  alarm_code */
    { M8_SIG_ESTOP,          { M8_SIG_DI_ESTOP_RAW },      true,       1U,   3U,  ALARM_CODE_ESTOP          },
    { M8_SIG_GANTRY_FWD_LIM, { M8_SIG_DI_GANTRY_FWD_RAW }, false,      3U,   3U,  0U                        },
    { M8_SIG_GANTRY_REV_LIM, { M8_SIG_DI_GANTRY_REV_RAW }, false,      3U,   3U,  0U                        },
    { M8_SIG_LIFT_UP_LIM,    { M8_SIG_DI_LIFT_UP_RAW },    false,      3U,   3U,  0U                        },
    { M8_SIG_LIFT_DOWN_LIM,  { M8_SIG_DI_LIFT_DOWN_RAW },  false,      3U,   3U,  0U                        },
};

#define M8_SIGNAL_TABLE_SIZE  ((int)ARRAY_SIZE(m8_signal_table))

#endif /* CONFIG_MACHINE_M8_SIGNAL_TABLE_H */
