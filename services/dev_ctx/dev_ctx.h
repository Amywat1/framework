/**
 * @file    dev_ctx.h
 * @brief   设备状态快照接口（组合只读视图）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef SERVICES_DEV_CTX_DEV_CTX_H
#define SERVICES_DEV_CTX_DEV_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/safety/model/alarm_types.h"
#include "domain/wash/model/wash_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    operational_mode_t operational_mode;
    bool               service_enabled;
    bool               estop_active;
    wash_mode_t        wash_mode;
    bool               cloud_connected;
    safety_posture_t   safety_posture;
    bool               blocking_active;
    uint32_t           top_alarm_code;
    unsigned           active_alarm_count;
    alarm_instance_t   active_list[ALARM_ACTIVE_MAX];
} device_context_t;

void               dev_ctx_init(void);
device_context_t   dev_ctx_snapshot(void);
operational_mode_t dev_ctx_get_operational_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_DEV_CTX_DEV_CTX_H */
