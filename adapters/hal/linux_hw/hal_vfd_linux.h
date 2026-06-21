/**
 * @file    hal_vfd_linux.h
 * @brief   Linux 真机 VFD HAL 内部接口（仅供机型适配层绑定实例）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    仅供 adapters/machine/ 在 bootstrap 阶段调用；
 *          业务层仍通过 hal_vfd_port 访问。
 */

#ifndef ADAPTERS_HAL_LINUX_HW_HAL_VFD_LINUX_H
#define ADAPTERS_HAL_LINUX_HW_HAL_VFD_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ports/hal/hal_vfd_port.h"
#include "common/io_handle.h"
#include "common/sw_error.h"

/** @brief 注册 hal_vfd_linux 实现到 hal_vfd_port */
void hal_vfd_linux_register(void);

/**
 * @brief  初始化并绑定指定 id 的 VFD 实例
 * @param  id  hal_vfd_port 实例标识
 */
sw_err_t hal_vfd_linux_instance_init(hal_vfd_id_t  id,
                                     const char   *serial_port,
                                     int           baud,
                                     int           modbus_addr,
                                     io_do_t       pin_fwd,
                                     bool          has_rev,
                                     io_do_t       pin_rev,
                                     io_do_t       pin_rst);

/** @brief  为已绑定实例注册 drv_vfd 事件回调 */
void hal_vfd_linux_instance_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code));

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_LINUX_HW_HAL_VFD_LINUX_H */
