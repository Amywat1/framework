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
#include "adapters/hal/linux_hw/drv/drv_vfd.h"

/** @brief 注册 hal_vfd_linux 实现到 hal_vfd_port */
void hal_vfd_linux_register(void);

/**
 * @brief  初始化并绑定指定 id 的 VFD 实例
 * @param  id  hal_vfd_port 实例标识
 * @note   速度 IO 需单独调用 hal_vfd_linux_instance_config_speed_io 配置
 */
sw_err_t hal_vfd_linux_instance_init(hal_vfd_id_t  id,
                                     const char   *serial_port,
                                     int           baud,
                                     int           modbus_addr,
                                     io_do_t       pin_fwd,
                                     io_do_t       pin_rev,
                                     io_do_t       pin_rst);

/**
 * @brief  配置指定 id 实例的速度 IO 引脚与挡位映射
 * @note   须在 hal_vfd_linux_instance_init 之后、首次调用 hal_vfd_linux_instance_run 前完成
 */
sw_err_t hal_vfd_linux_instance_config_speed_io(hal_vfd_id_t  id,
                                                 io_do_t       pin_spd1,
                                                 io_do_t       pin_spd2,
                                                 const uint8_t spd_cfg[VFD_GEAR_MAX]);

/**
 * @brief  运行变频器至指定挡位（正=正转，负=反转，0=停止）
 * @note   方向切换时内部自动先停止并等待延迟；本接口不设置 Modbus 频率
 */
sw_err_t hal_vfd_linux_instance_run(hal_vfd_id_t id, drv_vfd_gear_t gear);

/**
 * @brief  通过 Modbus 设置变频器目标频率（与挡位控制独立）
 */
sw_err_t hal_vfd_linux_instance_set_freq(hal_vfd_id_t id, uint16_t freq_hz);

/** @brief  为已绑定实例注册 drv_vfd 事件回调 */
void hal_vfd_linux_instance_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code));

/**
 * @brief  设置指定实例的 monitor 读取项掩码（DRV_VFD_MON_* 标志位组合）
 * @note   DRV_VFD_MON_NONE 停止所有周期读取；DRV_VFD_MON_ALL 全部开启（默认）
 */
sw_err_t hal_vfd_linux_instance_set_monitor_mask(hal_vfd_id_t id, drv_vfd_monitor_mask_t mask);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_LINUX_HW_HAL_VFD_LINUX_H */
