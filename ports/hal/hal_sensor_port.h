/**
 * @file    hal_sensor_port.h
 * @brief   传感器与状态查询 HAL 端口接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    包含限位开关、急停等接口；VFD 诊断见 hal_vfd_port。
 */

#ifndef PORTS_HAL_SENSOR_PORT_H
#define PORTS_HAL_SENSOR_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool (*gantry_at_fwd_limit)(void);
    bool (*gantry_at_rev_limit)(void);
    bool (*lift_at_top)(void);
    bool (*lift_at_bottom)(void);
    bool (*is_estop_active)(void);
    void (*poll_input_events)(void);
} hal_sensor_ops_t;

void                    hal_sensor_register(const hal_sensor_ops_t *ops);
const hal_sensor_ops_t *hal_sensor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_SENSOR_PORT_H */
