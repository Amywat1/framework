/**
 * @file    hal_do_group_linux.h
 * @brief   Linux 真机 DO 组×槽位 HAL（仅供 machine 层 bind）
 * @author  胡望伟
 * @date    2026-06-07
 */

#ifndef ADAPTERS_HAL_LINUX_HW_HAL_DO_GROUP_LINUX_H
#define ADAPTERS_HAL_LINUX_HW_HAL_DO_GROUP_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ports/hal/hal_do_group_port.h"
#include "common/io_handle.h"
#include "common/sw_error.h"

/** @brief  注册 hal_do_group_linux 实现到 hal_do_group_port */
void hal_do_group_linux_register(void);

/**
 * @brief  绑定组内槽位到 DO 引脚
 * @param  group  DO 组编号
 * @param  slot   组内槽位编号
 * @param  pin    数字输出句柄；IO_HANDLE_NULL 表示未安装
 */
sw_err_t hal_do_group_linux_bind(hal_do_group_t group,
                                 hal_do_slot_t  slot,
                                 io_do_t        pin);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_LINUX_HW_HAL_DO_GROUP_LINUX_H */
