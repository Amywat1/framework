/**
 * @file    sim_console.c
 * @brief   仿真交互控制台（stdin 命令 → 注入传感器状态 / 命令事件）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/ports/inbound/command/command_port.h"
#include "projects/m8/bindings/m8_signal_sim.h"
#include "framework/adapters/outbound/hal/sim/sim_encoder_counter.h"
#include "framework/common/log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* -------------------------------------------------------------------------
 * 内部：注入命令（通过 command_port 走标准路径）
 * ------------------------------------------------------------------------- */
static void inject_cmd(cmd_type_t type, wash_mode_t mode)
{
    const command_port_ops_t *cp = command_port_get_ops();
    if (cp == NULL)
    {
        printf("[console] command_port not registered\n");
        return;
    }
    cmd_t cmd;
    cmd.type = type;
    if (type == CMD_START_WASH)
    {
        cmd.payload.start_wash.mode = mode;
    }
    (void)cp->inject(&cmd);
}

/* -------------------------------------------------------------------------
 * 内部：打印设备状态快照
 * ------------------------------------------------------------------------- */
static void print_state(void)
{
    device_context_t ctx = dev_ctx_snapshot();
    printf("[state] op_mode=%d service=%d estop=%d wash_mode=%d cloud=%d safety=%d alarm=%d code=%06u\n",
           (int)ctx.operational_mode,
           (int)ctx.service_enabled,
           (int)ctx.estop_active,
           (int)ctx.wash_mode,
           (int)ctx.cloud_connected,
           (int)ctx.safety_state, (int)ctx.has_alarm,
           (unsigned)ctx.alarm_code);
}

/* -------------------------------------------------------------------------
 * 内部：解析并执行一行命令
 * ------------------------------------------------------------------------- */
static void handle_line(char *line)
{
    char  *tok[4] = { NULL, NULL, NULL, NULL };
    int    n      = 0;
    char  *p      = strtok(line, " \t\r\n");

    while ((p != NULL) && (n < 4))
    {
        tok[n++] = p;
        p = strtok(NULL, " \t\r\n");
    }
    if (n == 0) { return; }

    /* ---- sensor 命令 ---- */
    if (strcmp(tok[0], "sensor") == 0)
    {
        if ((n < 3) || (tok[1] == NULL) || (tok[2] == NULL))
        {
            printf("usage: sensor <fwd|rev|estop|lift_top|lift_bottom|overload|fan> <0|1>\n");
            return;
        }
        bool val = (atoi(tok[2]) != 0);
        if      (strcmp(tok[1], "fwd")         == 0) { m8_signal_sim_set_fwd_limit(val); }
        else if (strcmp(tok[1], "rev")         == 0) { m8_signal_sim_set_rev_limit(val); }
        else if (strcmp(tok[1], "estop")       == 0) { m8_signal_sim_set_estop(val); }
        else if (strcmp(tok[1], "lift_top")    == 0) { m8_signal_sim_set_lift_top(val); }
        else if (strcmp(tok[1], "lift_bottom") == 0) { m8_signal_sim_set_lift_bottom(val); }
        else if (strcmp(tok[1], "overload")    == 0) { m8_signal_sim_set_active(M8_SIG_SIDE_BRUSH_OVERLOAD, val); }
        else if (strcmp(tok[1], "fan")         == 0) { m8_signal_sim_set_active(M8_SIG_FAN_ALARM, val); }
        else { printf("unknown sensor: %s\n", tok[1]); }
        printf("[sim] sensor %s = %d\n", tok[1], (int)val);
        return;
    }

    /* ---- encoder 命令 ---- */
    if (strcmp(tok[0], "encoder") == 0)
    {
        int delta = (n >= 2) ? atoi(tok[1]) : 1;
        sim_encoder_counter_add_pulse(0, delta);
        printf("[sim] encoder tick delta=%d\n", delta);
        return;
    }

    /* ---- cmd 命令 ---- */
    if (strcmp(tok[0], "cmd") == 0)
    {
        if (n < 2)
        {
            printf("usage: cmd <order [0|1]|stop|stop-op|resume|reset|home|enter-manual|recover>\n");
            return;
        }
        if (strcmp(tok[1], "order") == 0)
        {
            wash_mode_t mode = (n >= 3) ? (wash_mode_t)atoi(tok[2]) : WASH_MODE_STANDARD;
            inject_cmd(CMD_START_WASH, mode);
            printf("[sim] inject CMD_START_WASH mode=%d\n", (int)mode);
        }
        else if (strcmp(tok[1], "stop")    == 0) { inject_cmd(CMD_STOP_WASH,         WASH_MODE_STANDARD); printf("[sim] inject CMD_STOP_WASH\n"); }
        else if (strcmp(tok[1], "stop-op") == 0) { inject_cmd(CMD_STOP_OPERATION,    WASH_MODE_STANDARD); printf("[sim] inject CMD_STOP_OPERATION\n"); }
        else if (strcmp(tok[1], "resume")  == 0) { inject_cmd(CMD_RESUME_OPERATION,  WASH_MODE_STANDARD); printf("[sim] inject CMD_RESUME_OPERATION\n"); }
        else if (strcmp(tok[1], "reset")   == 0) { inject_cmd(CMD_RESET_FAULT,       WASH_MODE_STANDARD); printf("[sim] inject CMD_RESET_FAULT\n"); }
        else if (strcmp(tok[1], "home")    == 0) { inject_cmd(CMD_HOME_DEVICE,       WASH_MODE_STANDARD); printf("[sim] inject CMD_HOME_DEVICE\n"); }
        else if (strcmp(tok[1], "enter-manual") == 0) { inject_cmd(CMD_ENTER_MANUAL, WASH_MODE_STANDARD); printf("[sim] inject CMD_ENTER_MANUAL\n"); }
        else if (strcmp(tok[1], "recover") == 0) { inject_cmd(CMD_RECOVER, WASH_MODE_STANDARD); printf("[sim] inject CMD_RECOVER\n"); }
        else { printf("unknown cmd: %s\n", tok[1]); }
        return;
    }

    /* ---- state 命令 ---- */
    if (strcmp(tok[0], "state") == 0)
    {
        print_state();
        return;
    }

    /* ---- quit 命令 ---- */
    if (strcmp(tok[0], "quit") == 0)
    {
        printf("[sim] exit\n");
        exit(0);
    }

    /* ---- help ---- */
    if (strcmp(tok[0], "help") == 0)
    {
        printf("Commands:\n");
        printf("  sensor <fwd|rev|estop|lift_top|lift_bottom|overload|fan> <0|1>\n");
        printf("  encoder [+N|-N]\n");
        printf("  cmd <order [0|1]|stop|stop-op|resume|reset|home>\n");
        printf("  state\n");
        printf("  quit\n");
        return;
    }

    printf("unknown command: %s (type 'help')\n", tok[0]);
}

/* -------------------------------------------------------------------------
 * 控制台线程
 * ------------------------------------------------------------------------- */
static void *console_thread_fn(void *arg)
{
    (void)arg;
    char buf[256];

    printf("[console] M8 sim console ready. Type 'help' for commands.\n> ");
    fflush(stdout);

    while (fgets(buf, sizeof(buf), stdin) != NULL)
    {
        handle_line(buf);
        printf("> ");
        fflush(stdout);
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * 对外接口
 * ------------------------------------------------------------------------- */
void sim_console_start(void)
{
    pthread_t tid;
    if (pthread_create(&tid, NULL, console_thread_fn, NULL) == 0)
    {
        pthread_detach(tid);
    }
}
