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

#include "driver/drv_vfd.h"
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

/* -------------------------------------------------------------------------
 * 龙门运动方向（motion 更新，sensor encoder callback 读取）
 * ------------------------------------------------------------------------- */
void m8_ctx_set_gantry_fwd(bool is_fwd);
bool m8_ctx_gantry_is_fwd(void);

/* -------------------------------------------------------------------------
 * 龙门位置计数器（encoder callback 更新，sensor port 读取）
 * ------------------------------------------------------------------------- */
int32_t m8_ctx_get_gantry_pos(void);
void    m8_ctx_reset_gantry_pos(void);
void    m8_ctx_encoder_tick(void);   /* 每次码盘上升沿调用一次 */

#endif /* ADAPTERS_HAL_LINUX_HW_M8_HAL_CTX_H */
