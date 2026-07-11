/**
 * @file    m8_mechanism_fault.c
 * @brief   M8 机构 PROCESS 故障 → alarm_binding_port 映射
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "projects/m8/bindings/m8_mechanism_fault.h"
#include "framework/ports/inbound/safety/alarm_binding_port.h"
#include "projects/m8/config/m8_alarm_table.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include <stddef.h>

void m8_gantry_on_process_fault(bool is_fwd, hal_motor_fault_code_t fault)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    uint32_t                   code;

    if ((ops == NULL) || (ops->trigger == NULL))
    {
        return;
    }

    if ((fault == HAL_MOTOR_FAULT_ENCODER_SIGNAL) || (fault == HAL_MOTOR_FAULT_WATCHDOG))
    {
        code = M8_GANTRY_ALM_ENC_ERR;
    }
    else if (is_fwd)
    {
        code = M8_GANTRY_ALM_FWD_TMO;
    }
    else
    {
        code = M8_GANTRY_ALM_REV_TMO;
    }

    (void)ops->trigger(code);
}

void m8_top_brush_lift_on_process_fault(bool is_up, hal_motor_fault_code_t fault)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    uint32_t                   code;

    (void)fault;

    if ((ops == NULL) || (ops->trigger == NULL))
    {
        return;
    }

    if (is_up)
    {
        code = M8_TOP_BRUSH_LIFT_ALM_UP_TMO;
    }
    else
    {
        code = M8_TOP_BRUSH_LIFT_ALM_DN_TMO;
    }

    (void)ops->trigger(code);
}

void m8_rear_lock_on_process_fault(bool is_lock, hal_motor_fault_code_t fault)
{
    (void)is_lock;
    (void)fault;
    /* 后轮锁 PROCESS 码待 guard 模块扩展后接入 */
}
