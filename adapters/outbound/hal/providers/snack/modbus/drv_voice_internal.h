/**
 * @file drv_voice_internal.h
 * @brief Snack 语音 provider 私有布局。
 */
#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_INTERNAL_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_INTERNAL_H

#include "adapters/outbound/hal/providers/snack/modbus/drv_modbus_link_internal.h"
#include "adapters/outbound/hal/providers/snack/modbus/drv_voice.h"

#include <pthread.h>
#include <stdbool.h>

struct drv_voice {
    drv_modbus_link_t link;
    bool              comm_ok;
    uint16_t          notify_fail_count;
    pthread_mutex_t   notify_mutex;
    void (*event_cb)(int event_code);
};

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_INTERNAL_H */
