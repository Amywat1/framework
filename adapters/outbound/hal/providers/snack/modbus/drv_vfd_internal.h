/**
 * @file drv_vfd_internal.h
 * @brief Snack VFD provider 私有布局。
 */
#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VFD_INTERNAL_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VFD_INTERNAL_H

#include "adapters/outbound/hal/providers/snack/modbus/drv_modbus_link_internal.h"
#include "adapters/outbound/hal/providers/snack/modbus/drv_vfd.h"

#include <pthread.h>

struct drv_vfd {
    drv_modbus_link_t               link;
    io_do_t                         pin_fwd;
    io_do_t                         pin_rev;
    io_do_t                         pin_rst;
    io_do_t                         pin_spd1;
    io_do_t                         pin_spd2;
    uint8_t                         spd_cfg[VFD_GEAR_MAX];
    uint8_t                         gear_count;
    hal_vfd_gear_t                  gear;
    hal_vfd_state_t                 state;
    const drv_vfd_modbus_profile_t *profile;
    drv_vfd_do_set_fn               do_set;
    pthread_mutex_t                 io_mutex;
};

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VFD_INTERNAL_H */
