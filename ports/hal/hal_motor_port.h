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
    bool     (*encoder_counter_online)(int id);           /* 码盘计数链路当前是否在线 */
    sw_err_t (*read_hw_pulse)(int id, uint32_t *p_value);  /* 读取硬件脉冲计数器原始值 */
    sw_err_t (*clear_hw_pulse)(int id);                    /* 清零硬件脉冲计数器 */
    sw_err_t (*read_current)(int id, uint16_t *p_current); /* 读取电机负载电流，单位 0.01A */
    sw_err_t (*read_status)(int id, uint16_t *p_status);   /* 读取电机实际运行状态字 */
} hal_motor_ops_t;

void                  hal_motor_register(const hal_motor_ops_t *ops);
const hal_motor_ops_t *hal_motor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_HAL_MOTOR_PORT_H */
