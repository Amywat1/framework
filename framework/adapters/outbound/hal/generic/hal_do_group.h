/**
 * @file    hal_do_group.h
 * @brief   DO 组×槽位 HAL 内部接口（仅供机型适配层绑定槽位）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仅供 projects/<project>/wiring/ 在 bootstrap 阶段调用；
 *          业务层仍通过 hal_do_group_port 访问。
 *          本文件不含任何平台专属 SDK 依赖，可用于任何已注册
 *          hal_io_port 的目标平台。
 */

#ifndef ADAPTERS_HAL_GENERIC_HAL_DO_GROUP_H
#define ADAPTERS_HAL_GENERIC_HAL_DO_GROUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/ports/outbound/hal/hal_do_group_port.h"
#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"

/** @brief  注册通用 DO 组 HAL 实现到 hal_do_group_port */
void hal_do_group_generic_register(void);

/**
 * @brief  绑定组内槽位到 DO 引脚
 * @param  group  DO 组编号
 * @param  slot   组内槽位编号
 * @param  pin    数字输出句柄；IO_HANDLE_NULL 表示未安装
 */
sw_err_t hal_do_group_bind(hal_do_group_t group,
                           hal_do_slot_t  slot,
                           io_do_t        pin);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_GENERIC_HAL_DO_GROUP_H */
