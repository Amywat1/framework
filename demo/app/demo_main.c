/**
 * @file    demo_main.c
 * @brief   Demo 入口：bootstrap 启动与集成验证
 */

#include "adapters/outbound/safety/sim/hw_estop_sim.h"
#include "application/ports/inbound/command/command_port.h"
#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/event_types.h"
#include "common/log.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "runtime/bootstrap/bootstrap.h"
#include "runtime/event_bus/event_bus.h"
#include "sw_version.h"

#include <stdio.h>
#include <unistd.h>

#define DEMO_ALARM_MAJOR 201101U

/** 轮询间隔：取急停采集周期的两倍，避免空转 */
#define DEMO_POLL_STEP_US 10000U

static volatile int s_estop_on_seen;

static void on_estop_on(const event_t *evt)
{
    (void)evt;
    s_estop_on_seen = 1;
}

/**
 * @brief  等待标志置位，带超时上限
 * @param  flag        被等待的标志（由事件回调置位）
 * @param  timeout_ms  最长等待时间
 * @retval 0  标志已置位
 * @retval 1  超时
 */
static int wait_flag(volatile int *flag, unsigned int timeout_ms)
{
    unsigned int waited_us = 0U;
    unsigned int limit_us  = timeout_ms * 1000U;

    while (!*flag) {
        if (waited_us >= limit_us) {
            return 1;
        }
        usleep(DEMO_POLL_STEP_US);
        waited_us += DEMO_POLL_STEP_US;
    }

    return 0;
}

/**
 * @brief  提交一条设备命令并要求被受理
 * @param  kind  命令种类
 * @retval 0     命令被受理
 * @retval 1     端口未注册、提交失败或命令被拒
 */
static int submit_expect_accepted(dev_cmd_kind_t kind)
{
    dev_cmd_t                        cmd     = dev_cmd_make_simple(kind);
    dev_cmd_receipt_t                receipt = {0};
    const device_command_port_ops_t *ops;

    ops = device_command_port_get_ops();
    if (ops == NULL) {
        fprintf(stderr, "[Demo] command port not registered\n");
        return 1;
    }

    if (ops->submit_sync(&cmd, &receipt, 2000U) != SW_OK) {
        fprintf(stderr, "[Demo] command %d submit failed\n", (int)kind);
        return 1;
    }

    if (receipt.status != DEV_CMD_STATUS_ACCEPTED) {
        fprintf(stderr, "[Demo] command %d receipt status=%d\n", (int)kind, (int)receipt.status);
        return 1;
    }

    return 0;
}

/*
 * 启动后为 OP_MODE_STOPPED。STOP_OPERATION 在 STOPPED/IDLE/WASH_DONE 均允许，
 * smoke 仍先经 RECOVER 进 IDLE 再停运，以覆盖完整归位链路。
 *
 * RECOVER@STOPPED 为 CONDITIONAL：先拒急停，再要求 service_enabled；
 * init 已开总开关且无急停，可走通。链路为 RECOVERY_REQUESTED → home_device →
 * HOME_COMPLETED → RECOVERY_COMPLETED。
 */
static int check_recover_to_idle(void)
{
    if (submit_expect_accepted(DEV_CMD_RECOVER) != 0) {
        return 1;
    }

    /* home_device 是异步语义：demo 实现立即发事件，仍需等 dispatch 线程处理完 */
    usleep(100000U);

    if (op_mode_get_current() != OP_MODE_IDLE) {
        fprintf(stderr, "[Demo] mode should be IDLE after RECOVER+home, got %d\n", (int)op_mode_get_current());
        return 1;
    }

    return 0;
}

static int check_command_stop_operation(void)
{
    if (submit_expect_accepted(DEV_CMD_STOP_OPERATION) != 0) {
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

    if (check_recover_to_idle() != 0) {
        return 1;
    }

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

    /* 轮询而非固定等待：急停采集线程以 SCHED_FIFO 注册，非特权环境下会被
     * scheduler 降级为 SCHED_OTHER（见其 fallback 日志），此时边沿检测延迟
     * 取决于系统负载。固定等 80ms 在负载机器上会偶发失败，而 smoke 的偶发
     * 失败比不跑更糟——它会让真实回归被当成抖动忽略。 */
    if (wait_flag(&s_estop_on_seen, 2000U) != 0) {
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
