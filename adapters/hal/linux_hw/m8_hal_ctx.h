/**
 * @file    m8_hal_ctx.h
 * @brief   M8 linux_hw 适配器内部共享状态（不对外暴露）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    仅供 adapters/hal/linux_hw/ 内部各 .c 文件互相引用。
 *          禁止 domain/、application/、ports/ 包含此文件。
 */

#ifndef ADAPTERS_HAL_LINUX_HW_M8_HAL_CTX_H
#define ADAPTERS_HAL_LINUX_HW_M8_HAL_CTX_H

#include "adapters/hal/linux_hw/drv/drv_vfd.h"
#include "common/sw_error.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 硬件初始化
 * 在 bootstrap 阶段调用，完成 VFD 和步进电机的初始化。
 * ------------------------------------------------------------------------- */
sw_err_t m8_linux_hw_init(void);

/* -------------------------------------------------------------------------
 * VFD 实例访问（由 hal_motion_linux.c 持有，其他适配器通过此接口访问）
 * ------------------------------------------------------------------------- */
drv_vfd_t *m8_ctx_vfd_brush(void);
drv_vfd_t *m8_ctx_vfd_gantry(void);

#endif /* ADAPTERS_HAL_LINUX_HW_M8_HAL_CTX_H */
