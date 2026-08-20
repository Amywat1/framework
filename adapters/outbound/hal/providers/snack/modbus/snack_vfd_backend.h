/**
 * @file    snack_vfd_backend.h
 * @brief   Linux 真机 VFD HAL 内部接口（仅供机型适配层绑定实例）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    仅供 projects/<project>/wiring/ 在 bootstrap 阶段调用；
 *          业务层仍通过 hal_vfd_port 访问。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_SNACK_VFD_BACKEND_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_SNACK_VFD_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "adapters/outbound/hal/components/vfd_manager/hal_vfd_manager_bind.h"
#include "adapters/outbound/hal/providers/snack/modbus/drv_vfd.h"
#include "common/io_handle.h"
#include "common/sw_error.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"

#include <stdbool.h>
#include <stdint.h>

#define SNACK_VFD_BACKEND_SPEED_GEAR_COUNT 3U
/** @brief speed_io 数组元素编码；0 表示该挡位的速度 IO 全低。 */
#define SNACK_VFD_BACKEND_SPEED_IO(s1, s2) ((uint8_t)(((s2) ? 0x02U : 0U) | ((s1) ? 0x01U : 0U)))

typedef struct {
    const char *serial_port;
    int         baud;
    int         modbus_addr;
    /** @brief 实例使用的厂商 Modbus profile，生命周期须覆盖实例。 */
    const drv_vfd_modbus_profile_t *profile;

    io_do_t pin_fwd;
    io_do_t pin_rev;
    io_do_t pin_rst;

    io_do_t pin_spd1;
    io_do_t pin_spd2;
    uint8_t gear_count;
    uint8_t speed_io[SNACK_VFD_BACKEND_SPEED_GEAR_COUNT];

    hal_vfd_monitor_mask_t monitor_mask;

    /** @brief 电流采样周期（ms），0 表示使用默认值 HAL_VFD_DEFAULT_MONITOR_PERIOD_MS */
    uint32_t current_period_ms;
} snack_vfd_backend_instance_cfg_t;

/** @brief  注册 snack_vfd_backend（内部调用 hal_vfd_manager_register），不绑定实例、不初始化硬件。 */
void snack_vfd_backend_register(void);

/**
 * @brief  配置并绑定指定 VFD 实例到 components/vfd_manager。
 * @param  id   VFD 实例编号。
 * @param  cfg  实例硬件参数，内部按值保存供 hal_vfd.init() 使用。
 * @retval SW_OK        成功。
 * @retval SW_ERR_PARAM 参数非法。
 * @retval SW_ERR_BUSY  该实例已绑定，禁止重复调用。
 * @note   本接口不调用 drv_vfd_init()；真实硬件初始化只在 hal_vfd.ops.init() 路径执行。
 */
sw_err_t snack_vfd_backend_instance_setup(hal_vfd_id_t id, const snack_vfd_backend_instance_cfg_t *cfg);

/**
 * @brief  更新指定实例的通信监测掩码（须在 instance_bind 之后调用）
 */
sw_err_t snack_vfd_backend_instance_set_monitor_mask(hal_vfd_id_t id, hal_vfd_monitor_mask_t mask);

#ifdef SNACK_VFD_BACKEND_UNIT_TEST
void snack_vfd_backend_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_SNACK_VFD_BACKEND_H */
