/**
 * @file drv_modbus_link_internal.h
 * @brief Snack Modbus 链路 provider 私有布局。
 */
#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_MODBUS_LINK_INTERNAL_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_MODBUS_LINK_INTERNAL_H

#include "adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct _modbus modbus_t;

struct drv_modbus_link {
    modbus_t   *mb;
    const char *serial_port;
    int         baud;
    int         modbus_addr;
    void       *bus_lock;
    bool        mb_connected;
    uint16_t    comm_fail_count;
    uint16_t    reconnect_threshold;
    uint32_t    timeout_us;
};

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_MODBUS_LINK_INTERNAL_H */
