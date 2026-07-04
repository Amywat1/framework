/**
 * @file    hal_vfd_linux.h
 * @brief   Linux 真机 VFD HAL 内部接口（仅供机型适配层绑定实例）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    仅供 projects/<project>/wiring/ 在 bootstrap 阶段调用；
 *          业务层仍通过 hal_vfd_port 访问。
 */

#ifndef ADAPTERS_HAL_LINUX_HW_HAL_VFD_LINUX_H
#define ADAPTERS_HAL_LINUX_HW_HAL_VFD_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/ports/outbound/hal/hal_vfd_port.h"
#include "framework/ports/outbound/hal/hal_vfd_bind.h"
#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

#define HAL_VFD_LINUX_SPEED_GEAR_COUNT 3U
/** @brief speed_io 数组元素编码；0 保留为停止态，不可作为有效挡位。 */
#define HAL_VFD_LINUX_SPEED_IO(s1, s2) \
    ((uint8_t)(((s2) ? 0x02U : 0U) | ((s1) ? 0x01U : 0U)))

typedef struct
{
    const char *serial_port;
    int         baud;
    int         modbus_addr;

    io_do_t     pin_fwd;
    io_do_t     pin_rev;
    io_do_t     pin_rst;

    bool        speed_io_enabled;
    io_do_t     pin_spd1;
    io_do_t     pin_spd2;
    uint8_t     speed_io[HAL_VFD_LINUX_SPEED_GEAR_COUNT];

    hal_vfd_monitor_mask_t monitor_mask;
} hal_vfd_linux_instance_cfg_t;

/** @brief  注册 hal_vfd_linux（内部调用 hal_vfd_generic_register） */
void hal_vfd_linux_register(void);

/** @brief  初始化 drv 实例并绑定 generic/hal_vfd backend */
sw_err_t hal_vfd_linux_instance_init(hal_vfd_id_t id,
                                     const hal_vfd_linux_instance_cfg_t *cfg);

/**
 * @brief  更新指定实例的通信监测掩码（须在 instance_init 之后调用）
 */
sw_err_t hal_vfd_linux_instance_set_monitor_mask(hal_vfd_id_t id,
                                                hal_vfd_monitor_mask_t mask);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_LINUX_HW_HAL_VFD_LINUX_H */
