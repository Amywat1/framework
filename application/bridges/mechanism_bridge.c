/**
 * @file    mechanism_bridge.c
 * @brief   机构控制应用桥接实现
 */

#include "application/bridges/mechanism_bridge.h"

#include "common/log.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/mechanism/motor/motor_executor.h"
#include "domain/mechanism/patterns/fluid_path.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/periodic_task.h"

#include <sched.h>
#include <stddef.h>
#include <string.h>

#define MECHANISM_BRIDGE_AXIS_MAX MOTOR_MAX_MOTORS

static motor_exec_t *s_exec;
static motor_axis_t  s_axes[MECHANISM_BRIDGE_AXIS_MAX];
static int           s_axis_count;
static bool          s_motor_task_registered;
static bool          s_fluid_task_registered;

static void motor_tick_task(void *ctx)
{
    int i;

    (void)ctx;
    if (s_exec != NULL) {
        motor_executor_tick(s_exec);
    }
    for (i = 0; i < s_axis_count; ++i) {
        motor_axis_poll(&s_axes[i]);
    }
}

static void fluid_tick_task(void *ctx)
{
    (void)ctx;
    fluid_path_poll(time_util_get_ms());
}

static int axis_index_of(int motor)
{
    int i;

    for (i = 0; i < s_axis_count; ++i) {
        if (s_axes[i].motor == motor) {
            return i;
        }
    }
    return -1;
}

static motor_init_result_t bridge_init_err(const char *msg)
{
    motor_init_result_t r;

    r.ok    = false;
    r.error = msg;
    return r;
}

motor_init_result_t mechanism_bridge_bind(unsigned slot_id, const motor_config_t *cfg, const motor_ports_t *ports)
{
    motor_exec_t       *exec = NULL;
    motor_init_result_t result;

    if (s_exec != NULL) {
        return bridge_init_err("bridge already bound");
    }
    result = motor_executor_bind(slot_id, cfg, ports, &exec);
    if (!result.ok) {
        return result;
    }
    if (mechanism_bridge_bind_motor(exec) != SW_OK) {
        return bridge_init_err("bridge attach failed");
    }
    return result;
}

sw_err_t mechanism_bridge_bind_motor(motor_exec_t *exec)
{
    if (exec == NULL) {
        return SW_ERR_PARAM;
    }
    if (s_exec != NULL) {
        return SW_ERR_STATE;
    }
    s_exec = exec;
    return SW_OK;
}

motor_init_result_t mechanism_bridge_reinit(void)
{
    if (s_exec == NULL) {
        return bridge_init_err("bridge not bound");
    }
    return motor_executor_reinit(s_exec);
}

void mechanism_bridge_reset_watchdog(void)
{
    if (s_exec != NULL) {
        motor_executor_reset_watchdog(s_exec);
    }
}

bool mechanism_bridge_in_safe_state(void)
{
    return (s_exec != NULL) && motor_executor_in_safe_state(s_exec);
}

sw_err_t mechanism_bridge_add_axis(int motor, const motion_lifecycle_opts_t *opts, motor_axis_t **out_axis)
{
    sw_err_t ret;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (motor < 0) {
        return SW_ERR_PARAM;
    }
    if (axis_index_of(motor) >= 0) {
        return SW_ERR_STATE;
    }
    if (s_axis_count >= MECHANISM_BRIDGE_AXIS_MAX) {
        return SW_ERR_OVERFLOW;
    }
    ret = motor_axis_init(&s_axes[s_axis_count], s_exec, motor, opts);
    if (ret != SW_OK) {
        return ret;
    }
    if (out_axis != NULL) {
        *out_axis = &s_axes[s_axis_count];
    }
    s_axis_count++;
    return SW_OK;
}

motor_axis_t *mechanism_bridge_axis(int motor)
{
    int idx = axis_index_of(motor);

    if (idx < 0) {
        return NULL;
    }
    return &s_axes[idx];
}

sw_err_t mechanism_bridge_register_tasks(void)
{
    sw_err_t ret;

    if (!s_motor_task_registered) {
        ret = periodic_task_register(
            "motor_tick", THD_MOTOR_TICK_PERIOD_MS, motor_tick_task, NULL, SCHED_OTHER, 0, THD_MOTOR_TICK_STACK);
        if (ret != SW_OK) {
            return ret;
        }
        s_motor_task_registered = true;
    }

    if (!s_fluid_task_registered) {
        ret = periodic_task_register("fluid_path_poll",
                                     THD_FLUID_PATH_POLL_PERIOD_MS,
                                     fluid_tick_task,
                                     NULL,
                                     SCHED_OTHER,
                                     0,
                                     THD_FLUID_PATH_POLL_STACK);
        if (ret != SW_OK) {
            return ret;
        }
        s_fluid_task_registered = true;
    }

    LOG_INFO("mechanism_bridge: tasks registered");
    return SW_OK;
}

void mechanism_bridge_halt_all(void)
{
    int i;

    for (i = 0; i < s_axis_count; ++i) {
        (void)motor_axis_stop(&s_axes[i]);
    }
}

#ifdef MECHANISM_BRIDGE_UNIT_TEST
void mechanism_bridge_reset_for_test(void)
{
    s_exec                  = NULL;
    s_axis_count            = 0;
    s_motor_task_registered = false;
    s_fluid_task_registered = false;
    (void)memset(s_axes, 0, sizeof(s_axes));
}
#endif
