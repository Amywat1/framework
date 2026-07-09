/**
 * @file    dev_ctx.h
 * @brief   设备状态快照接口（统一只读视图）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef SERVICE_DEV_CTX_H
#define SERVICE_DEV_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/device_control/model/device_state.h"
#include "framework/domain/wash/model/wash_types.h"
#include "framework/domain/safety/model/safety_types.h"
#include "framework/domain/safety/model/alarm_types.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    operational_mode_t operational_mode;
    bool               service_enabled;
    bool               estop_active;
    wash_mode_t        wash_mode;
    int32_t            gantry_pos;
    bool               cloud_connected;
    safety_posture_t   safety_posture;
    bool               blocking_active;
    uint32_t           top_alarm_code;
    unsigned           active_alarm_count;
    alarm_instance_t   active_list[ALARM_ACTIVE_MAX];
} device_context_t;

sw_err_t dev_ctx_init(void);
device_context_t dev_ctx_snapshot(void);
operational_mode_t dev_ctx_get_operational_mode(void);

void dev_ctx_set_operational_mode(operational_mode_t mode);
void dev_ctx_set_service_enabled(bool enabled);
void dev_ctx_set_estop_active(bool active);
void dev_ctx_set_wash_mode(wash_mode_t mode);
void dev_ctx_set_gantry_pos(int32_t pos);
void dev_ctx_set_safety_posture(safety_posture_t posture);
void dev_ctx_set_alarm_projection(bool blocking_active,
                                  uint32_t top_code,
                                  const alarm_instance_t *list,
                                  unsigned count);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_DEV_CTX_H */
