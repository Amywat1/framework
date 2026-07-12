/**
 * @file    demo_machine_ops.c
 * @brief   Demo 仿真 machine_ops 实现
 */

#include "common/sw_error.h"
#include "ports/outbound/machine/machine_ops_port.h"

static void demo_deferred_stop_all(void)
{
}

static void demo_safety_home(void)
{
}

static sw_err_t demo_home_device(void)
{
    return SW_OK;
}

static sw_err_t demo_manual_actuator(uint32_t act_id, int32_t param)
{
    (void)act_id;
    (void)param;
    return SW_OK;
}

static sw_err_t demo_stop_all_outputs(void)
{
    return SW_OK;
}

static const machine_ops_t s_demo_machine_ops = {
    .deferred_stop_all       = demo_deferred_stop_all,
    .safety_home             = demo_safety_home,
    .home_device             = demo_home_device,
    .execute_manual_actuator = demo_manual_actuator,
    .stop_all_outputs        = demo_stop_all_outputs,
};

/**
 * @brief  注册 Demo machine_ops
 */
sw_err_t demo_machine_ops_register(void)
{
    machine_ops_register(&s_demo_machine_ops);
    return SW_OK;
}
