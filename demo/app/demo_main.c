/**
 * @file    demo_main.c
 * @brief   Demo 入口：bootstrap 启动与集成验证
 */

#include "adapters/outbound/safety/sim/hw_estop_sim.h"
#include "application/bridges/alarm_event_bridge.h"
#include "common/event_types.h"
#include "common/log.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "ports/inbound/command/command_port.h"
#include "ports/inbound/safety/alarm_binding_port.h"
#include "runtime/bootstrap/bootstrap.h"
#include "runtime/event_bus/event_bus.h"
#include "sw_version.h"

#include <stdio.h>
#include <unistd.h>

#define DEMO_ALARM_MAJOR 201101U

static volatile int s_estop_on_seen;

static void on_estop_on(const event_t *evt)
{
    (void)evt;
    s_estop_on_seen = 1;
}

static int check_command_stop_operation(void)
{
    dev_cmd_t                        cmd     = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);
    dev_cmd_receipt_t                receipt = {0};
    const device_command_port_ops_t *ops;

    ops = device_command_port_get_ops();
    if (ops == NULL) {
        fprintf(stderr, "[Demo] command port not registered\n");
        return 1;
    }

    if (ops->submit(&cmd, &receipt, 2000U) != SW_OK) {
        fprintf(stderr, "[Demo] command submit failed\n");
        return 1;
    }

    if (receipt.status != DEV_CMD_STATUS_ACCEPTED) {
        fprintf(stderr, "[Demo] command receipt status=%d\n", (int)receipt.status);
        return 1;
    }

    if (op_mode_is_service_enabled()) {
        fprintf(stderr, "[Demo] service should be disabled after STOP_OPERATION\n");
        return 1;
    }

    if (op_mode_get_current() != OP_MODE_STOPPED) {
        fprintf(stderr, "[Demo] mode should be STOPPED after STOP_OPERATION\n");
        return 1;
    }

    return 0;
}

static int check_alarm_trigger_chain(void)
{
    const alarm_binding_ops_t *binding = alarm_binding_get_ops();

    if (binding == NULL) {
        fprintf(stderr, "[Demo] alarm binding not registered\n");
        return 1;
    }

    if (binding->trigger(DEMO_ALARM_MAJOR) != SW_OK) {
        fprintf(stderr, "[Demo] alarm trigger failed\n");
        return 1;
    }

    alarm_event_bridge_drain();
    usleep(50000U);

    if (!alarm_registry_has_blocking_active()) {
        fprintf(stderr, "[Demo] blocking alarm not active after trigger\n");
        return 1;
    }

    return 0;
}

int main(void)
{
    sw_err_t ret;

    printf("========================================\n");
    printf("  Demo - Framework Integration\n");
    printf("  Framework: %s\n", SW_PROJECT_NAME);
    printf("  Version: %s (%s)\n", SW_VERSION_STR, SW_GIT_HASH);
    printf("========================================\n");

    ret = bootstrap_run();
    if (ret != SW_OK) {
        fprintf(stderr, "[Demo] bootstrap_run failed ret=%d\n", (int)ret);
        return 1;
    }

    hw_estop_sim_set_active(false);

    if (check_command_stop_operation() != 0) {
        return 1;
    }

    ret = event_subscribe(EVT_HW_ESTOP_ON, on_estop_on);
    if (ret != SW_OK) {
        fprintf(stderr, "[Demo] event_subscribe failed ret=%d\n", (int)ret);
        return 1;
    }

    usleep(20000U);

    hw_estop_sim_set_active(true);
    usleep(80000U);

    if (!s_estop_on_seen) {
        fprintf(stderr, "[Demo] ESTOP ON event not received\n");
        return 1;
    }

    if (!op_mode_is_estop_active()) {
        fprintf(stderr, "[Demo] op_mode estop flag not set\n");
        return 1;
    }

    if (check_alarm_trigger_chain() != 0) {
        return 1;
    }

    LOG_INFO("Demo: bootstrap integration OK");
    printf("[Demo] All checks passed.\n");
    return 0;
}
