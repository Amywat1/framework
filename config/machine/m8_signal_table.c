/**
 * @file    m8_signal_table.c
 * @brief   M8 DI 信号滤波配置表定义
 * @author  HUWANGWEI
 * @date    2026-06-07
 *
 * @note    行下标与 m8_signal_id_t 枚举值严格对应；新增信号时须同步扩充此表。
 *          使用指定初始化器：缺失枚举值的行将零初始化，bind_cfg_valid 会在启动时
 *          报错拦截，不会静默通过。
 */

#include "config/machine/m8_signal_table.h"
#include "common/sw_types.h"

const m8_signal_cfg_t m8_signal_table[M8_SIG_MAX] = {
/*                           io_id                       active_low  trig  rel */
    [M8_SIG_ESTOP]              = { M8_IO_DI_ESTOP,              true,  1U, 3U },
    [M8_SIG_GANTRY_FWD_LIM]     = { M8_IO_DI_GANTRY_FWD_LIMIT,   false, 3U, 3U },
    [M8_SIG_GANTRY_REV_LIM]     = { M8_IO_DI_GANTRY_REV_LIMIT,   false, 3U, 3U },
    [M8_SIG_LIFT_UP_LIM]        = { M8_IO_DI_LIFT_UP_LIMIT,      false, 3U, 3U },
    [M8_SIG_LIFT_DOWN_LIM]      = { M8_IO_DI_LIFT_DOWN_LIMIT,    false, 3U, 3U },
    /* 报警源 DI：极性按现场实际接线确认，此处暂用高电平有效 */
    [M8_SIG_SIDE_BRUSH_OVERLOAD] = { M8_IO_DI_SIDE_BRUSH_OVERLOAD, false, 3U, 3U },
    [M8_SIG_FAN_ALARM]          = { M8_IO_DI_FAN_ALARM,         false, 3U, 3U },
};

/* 编译期确认表未在中途截断（行数应恰好等于枚举上限） */
_Static_assert(ARRAY_SIZE(m8_signal_table) == (unsigned)M8_SIG_MAX,
               "m8_signal_table 行数与 M8_SIG_MAX 不一致");
