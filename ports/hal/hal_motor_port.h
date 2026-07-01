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
    sw_err_t (*set_output)(int id, int speed_ref); /**< 频率模式：speed_ref >0 正转，<0 反转，0 停止 */
    sw_err_t (*set_gear)(int id, int8_t gear);     /**< 挡位模式：正=正转，负=反转，abs=挡位号（1=最低档）；NULL=不支持 */
    bool     (*at_fwd_limit)(int id);
    bool     (*at_rev_limit)(int id);
    sw_err_t (*read_hw_pulse)(int id, uint32_t *p_value);  /* 读取硬件脉冲计数器；SW_ERR_COMM 表示链路离线 */
    sw_err_t (*clear_hw_pulse)(int id);                    /* 清零硬件脉冲计数器 */
    sw_err_t (*read_current)(int id, uint16_t *p_current); /* 读取电机负载电流，单位 0.01A */
    sw_err_t (*read_running)(int id, bool *p_is_running);  /* 查询驱动器是否确认在运行；SW_OK+true=运行，SW_OK+false=停止，其它=通信失败 */
    sw_err_t (*fault_reset)(int id);                       /* 驱动层故障复位（如 VFD RST 脉冲） */
} hal_motor_ops_t;

void                  hal_motor_register(const hal_motor_ops_t *ops);
const hal_motor_ops_t *hal_motor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_HAL_MOTOR_PORT_H */
