/**
 * @file    hal_motor_port.h
 * @brief   通用电机 HAL 端口接口
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#ifndef PORTS_HAL_HAL_MOTOR_PORT_H
#define PORTS_HAL_HAL_MOTOR_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    sw_err_t (*set_output)(int id, int speed_ref);
    bool     (*at_fwd_limit)(int id);
    bool     (*at_rev_limit)(int id);
    int32_t  (*get_pos)(int id);
    sw_err_t (*clear_pos)(int id);
} hal_motor_ops_t;

void                  hal_motor_register(const hal_motor_ops_t *ops);
const hal_motor_ops_t *hal_motor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_HAL_MOTOR_PORT_H */
