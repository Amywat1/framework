/**
 * @file    scenario_standard_wash.c
 * @brief   场景测试：标准洗车流程
 */

#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/runtime/scheduler/scheduler.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/water.h"
#include "framework/domain/device_control/model/device_state.h"
#include "framework/domain/wash/model/wash_types.h"
#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/domain/wash/model/engine_model.h"
#include "framework/application/orchestrators/emergency_handler.h"
#include "framework/application/command_gateway.h"
#include "framework/application/op_mode_bridge.h"
#include "framework/application/recovery_service.h"
#include "framework/application/self_check_service.h"
#include "framework/application/mode_projection.h"
#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"
#include "framework/adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/bindings/m8_signal_sim.h"
#include "projects/m8/bindings/m8_motor_domains_setup.h"
#include "projects/m8/bindings/m8_motor_exec.h"
#include "projects/m8/bindings/m8_water_setup.h"
#include "framework/ports/outbound/hal/hal_vfd_port.h"
#include "framework/common/event_types.h"
#include "framework/common/time_util.h"
#include "framework/runtime/config/thread_config.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <stdbool.h>

#define SCENARIO_AUX_THREAD_STACK   (16U * 1024U)

extern void hal_io_sim_register(void);
extern void hal_vfd_sim_register(void);
extern void hal_do_group_mapper_register(void);
extern void engine_io_m8_register(void);
extern void engine_program_json_register_loader(void);
extern engine_direction_t wash_orchestrator_current_direction(void);

static void *scenario_dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

static void *scenario_limit_inject_fn(void *arg)
{
    (void)arg;

    while (true)
    {
        engine_direction_t dir = wash_orchestrator_current_direction();

        switch (dir)
        {
        case ENGINE_DIR_FORWARD:
            m8_signal_sim_set_fwd_limit(true);
            break;
        case ENGINE_DIR_BACKWARD:
            m8_signal_sim_set_rev_limit(true);
            break;
        default:
            break;
        }

        usleep(10U * 1000U);
    }

    return NULL;
}

static bool wait_for_mode(operational_mode_t target, unsigned max_ms)
{
    unsigned elapsed = 0U;

    while (elapsed < max_ms)
    {
        if (dev_ctx_get_operational_mode() == target)
        {
            return true;
        }
        usleep(50U * 1000U);
        elapsed += 50U;
    }
    return (dev_ctx_get_operational_mode() == target);
}

static sw_err_t inject_cmd(cmd_type_t type, wash_mode_t mode)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t cmd;

    if ((cp == NULL) || (cp->inject == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    cmd.type = type;
    if (type == CMD_START_WASH)
    {
        cmd.payload.start_wash.mode = mode;
    }
    return cp->inject(&cmd);
}

static void scenario_setup(void)
{
    hal_io_sim_register();
    hal_vfd_sim_register();
    hal_sensor_filter_register();
    hal_do_group_mapper_register();
    engine_io_m8_register();
    engine_program_json_register_loader();

    {
        const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
        if ((vfd != NULL) && (vfd->init != NULL))
        {
            (void)vfd->init();
        }
    }

    time_util_init();
    (void)event_bus_init();
    (void)dev_ctx_init();
    (void)m8_sensor_setup();
    m8_signal_sim_reset_all();

    m8_signal_sim_set_lift_bottom(true);
    m8_signal_sim_set_rev_limit(true);
    m8_signal_sim_set_lift_top(true);
    m8_signal_sim_set_rear_lock_home(true);

    (void)m8_motor_exec_init();
    (void)m8_motor_domains_setup_mask(M8_DOMAIN_BRUSH | M8_DOMAIN_GANTRY);
    (void)m8_water_setup();
    (void)m8_motor_exec_start();
    (void)hal_sensor_poll_register_task();
    (void)hal_vfd_manager_poll_register_task();
    (void)emergency_handler_init();
    (void)operational_mode_init();
    (void)command_gateway_init();
    (void)recovery_service_init();
    (void)self_check_service_init();
    (void)op_mode_bridge_init();
    (void)mode_projection_init();
    (void)wash_orchestrator_init();

    (void)thread_register("event_dispatch", scenario_dispatch_fn,
                          SCHED_OTHER, 0, THD_EVENT_DISPATCH_STACK);
    (void)thread_register("limit_inject", scenario_limit_inject_fn,
                          SCHED_OTHER, 0, SCENARIO_AUX_THREAD_STACK);
    (void)scheduler_start_all();

    usleep(200U * 1000U);
}

static void tc1_normal_complete(void)
{
    printf("TC-1: standard wash completes normally\n");

    assert(dev_ctx_get_operational_mode() == OP_MODE_IDLE);

    assert(inject_cmd(CMD_START_WASH, WASH_MODE_STANDARD) == SW_OK);
    assert(wait_for_mode(OP_MODE_WASHING, 500U));
    printf("  → WASHING\n");

    assert(wait_for_mode(OP_MODE_IDLE, 5000U));
    printf("  → IDLE (wash done)\n");
    printf("  PASS\n");
}

static void tc2_manual_stop(void)
{
    printf("TC-2: manual stop during wash\n");

    assert(dev_ctx_get_operational_mode() == OP_MODE_IDLE);

    assert(inject_cmd(CMD_START_WASH, WASH_MODE_STANDARD) == SW_OK);
    assert(wait_for_mode(OP_MODE_WASHING, 500U));
    printf("  → WASHING\n");

    usleep(80U * 1000U);
    assert(inject_cmd(CMD_STOP_WASH, WASH_MODE_STANDARD) == SW_OK);

    assert(wait_for_mode(OP_MODE_IDLE, 2000U));
    assert(dev_ctx_get_operational_mode() != OP_MODE_EXCEPTION);
    printf("  → IDLE (manual stop)\n");
    printf("  PASS\n");
}

static void tc3_stop_resume_operation(void)
{
    device_context_t ctx;

    printf("TC-3: stop/resume operation\n");

    assert(dev_ctx_get_operational_mode() == OP_MODE_IDLE);

    assert(inject_cmd(CMD_STOP_OPERATION, WASH_MODE_STANDARD) == SW_OK);
    ctx = dev_ctx_snapshot();
    assert((ctx.operational_mode == OP_MODE_IDLE) && !ctx.service_enabled);
    printf("  → IDLE + service_disabled\n");

    assert(inject_cmd(CMD_START_WASH, WASH_MODE_STANDARD) != SW_OK);

    assert(inject_cmd(CMD_RESUME_OPERATION, WASH_MODE_STANDARD) == SW_OK);
    ctx = dev_ctx_snapshot();
    assert(ctx.service_enabled);
    printf("  → service_enabled\n");
    printf("  PASS\n");
}

int main(void)
{
    scenario_setup();
    tc1_normal_complete();
    tc2_manual_stop();
    tc3_stop_resume_operation();
    printf("ALL PASS\n");
    return 0;
}
