/**
 * @file    hal_do_group_sim.h
 * @brief   仿真 DO 组×槽位 HAL（仅供 machine 层 bind）
 * @author  胡望伟
 * @date    2026-06-07
 */

#ifndef ADAPTERS_HAL_SIM_HW_HAL_DO_GROUP_SIM_H
#define ADAPTERS_HAL_SIM_HW_HAL_DO_GROUP_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ports/hal/hal_do_group_port.h"
#include "common/io_handle.h"
#include "common/sw_error.h"

void hal_do_group_sim_register(void);

/** @brief  标记 group×slot 已启用（仿真忽略 pin） */
sw_err_t hal_do_group_sim_bind(hal_do_group_t group,
                               hal_do_slot_t  slot,
                               io_do_t        pin);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_SIM_HW_HAL_DO_GROUP_SIM_H */
