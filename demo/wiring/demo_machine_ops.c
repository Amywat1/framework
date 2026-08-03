/**
 * @file    demo_machine_ops.c
 * @brief   Demo 仿真 machine_ops 实现
 */

#include "common/event_types.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"

static void demo_deferred_stop_all(void)
{
}

static void demo_abort_home(void)
{
    /* Demo 无真实机构：立即回报完成，避免卡在 ABORT_HOMING */
    (void)event_publish(EVT_ABORT_HOME_DONE, (uint32_t)SW_OK);
}

static sw_err_t demo_start_wash(wash_mode_t mode)
{
    (void)mode;
    (void)event_publish(EVT_WASH_SESSION_STARTED, wash_session_started_evt_param(mode));
    (void)event_publish(EVT_WASH_DONE, 0U);
    return SW_OK;
}

static void demo_abort_wash(wash_abort_cause_t cause)
{
    (void)event_publish(EVT_WASH_ABORTED, wash_abort_evt_param(cause));
}

static sw_err_t demo_home_device(void)
{
    /* Demo 无真实机构：立即回报归位成功，满足 recovery 异步等待 */
    (void)event_publish(EVT_OP_MODE_HOME_COMPLETED, 1U);
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
    .abort_home              = demo_abort_home,
    .start_wash              = demo_start_wash,
    .abort_wash              = demo_abort_wash,
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
