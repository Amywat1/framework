/**
 * @file    motor_control_core_sim.c
 * @brief   MCC 仿真桩 —— 供 PC 模拟器（x86_64）构建使用，替代 aarch64 预编译库。
 *
 * 所有运动命令立即同步生效：
 *   - motor_run_continuous / motor_move_to → 立即置 RUNNING
 *   - motor_stop → 立即置 STOPPED
 *   - motor_home → 立即置 STOPPED 并建立可信基准
 *   - motor_tick → 空操作
 *
 * 不模拟加减速、限位检测或电流监测，仅用于编译验证与集成逻辑测试。
 */

#include "motor/motor_executor.h"
#include <string.h>

/* -------------------- 初始化 -------------------- */

motor_init_result_t motor_init(motor_executor_t        *exec,
                               const motor_config_t    *cfg,
                               const motor_ports_t     *ports)
{
    motor_init_result_t r;

    memset(exec, 0, sizeof(*exec));
    exec->cfg         = *cfg;
    exec->ports       = *ports;
    exec->motor_count = cfg->motor_count;
    exec->initialized = true;

    r.ok    = true;
    r.error = "";
    return r;
}

motor_init_result_t motor_reinit(motor_executor_t *exec)
{
    motor_init_result_t r;
    int i;

    for (i = 0; i < exec->motor_count; i++) {
        exec->m[i].phase      = MOTOR_PHASE_STOPPED;
        exec->m[i].fault      = false;
        exec->m[i].fault_code = MOTOR_FAULT_NONE;
    }
    r.ok    = true;
    r.error = "";
    return r;
}

/* -------------------- 周期处理 -------------------- */

void motor_tick(motor_executor_t *exec)
{
    (void)exec;
}

/* -------------------- 运动命令 -------------------- */

motor_cmd_result_t motor_run_continuous(motor_executor_t *exec, int motor,
                                        motor_speed_t spd, motor_direction_t dir)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (motor < 0 || motor >= exec->motor_count) {
        r.status = MOTOR_CMD_REJECTED;
        r.reason = "motor out of range";
        return r;
    }
    exec->m[motor].phase       = MOTOR_PHASE_RUNNING;
    exec->m[motor].dir         = dir;
    exec->m[motor].target_freq = spd.value;
    return r;
}

motor_cmd_result_t motor_move_to(motor_executor_t *exec, int motor,
                                 motor_speed_t spd, motor_direction_t dir,
                                 const motor_move_spec_t *spec)
{
    (void)spec;
    return motor_run_continuous(exec, motor, spd, dir);
}

motor_cmd_result_t motor_stop(motor_executor_t *exec, int motor)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (motor < 0 || motor >= exec->motor_count) {
        r.status = MOTOR_CMD_REJECTED;
        r.reason = "motor out of range";
        return r;
    }
    exec->m[motor].phase = MOTOR_PHASE_STOPPED;
    return r;
}

motor_cmd_result_t motor_pause(motor_executor_t *exec, int motor)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (motor >= 0 && motor < exec->motor_count) {
        exec->m[motor].phase = MOTOR_PHASE_PAUSED;
    }
    return r;
}

motor_cmd_result_t motor_resume(motor_executor_t *exec, int motor)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (motor >= 0 && motor < exec->motor_count) {
        exec->m[motor].phase = MOTOR_PHASE_RUNNING;
    }
    return r;
}

motor_cmd_result_t motor_set_speed(motor_executor_t *exec, int motor,
                                   motor_speed_t spd, motor_direction_t dir)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (motor >= 0 && motor < exec->motor_count) {
        exec->m[motor].target_freq = spd.value;
        exec->m[motor].dir         = dir;
    }
    return r;
}

motor_cmd_result_t motor_home(motor_executor_t *exec, int motor)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (motor >= 0 && motor < exec->motor_count) {
        /* 仿真中立即完成回原点，建立可信基准，位置清零 */
        exec->m[motor].phase            = MOTOR_PHASE_STOPPED;
        exec->m[motor].baseline_trusted = true;
        exec->m[motor].position         = 0;
    }
    return r;
}

motor_cmd_result_t motor_zero_encoder(motor_executor_t *exec, int motor)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (motor >= 0 && motor < exec->motor_count) {
        exec->m[motor].position = 0;
    }
    return r;
}

motor_cmd_result_t motor_confirm_baseline(motor_executor_t *exec, int motor)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (motor >= 0 && motor < exec->motor_count) {
        exec->m[motor].baseline_trusted = true;
    }
    return r;
}

motor_cmd_result_t motor_recover(motor_executor_t *exec, int motor,
                                 motor_recovery_step_t step)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_ACCEPTED;
    r.reason = "";
    if (step == MOTOR_RECOVERY_MODULE_STOP
            && motor >= 0 && motor < exec->motor_count) {
        exec->m[motor].phase      = MOTOR_PHASE_STOPPED;
        exec->m[motor].fault      = false;
        exec->m[motor].fault_code = MOTOR_FAULT_NONE;
    }
    return r;
}

/* -------------------- 控制 -------------------- */

void motor_reset_estop(motor_executor_t *exec)
{
    exec->estop_latched = false;
}

void motor_reset_watchdog(motor_executor_t *exec)
{
    exec->safe_latched = false;
}

/* -------------------- 查询 -------------------- */

motor_phase_t motor_phase(const motor_executor_t *exec, int motor)
{
    if (motor < 0 || motor >= exec->motor_count) {
        return MOTOR_PHASE_STOPPED;
    }
    return exec->m[motor].phase;
}

int64_t motor_position(const motor_executor_t *exec, int motor)
{
    if (motor < 0 || motor >= exec->motor_count) {
        return 0;
    }
    return exec->m[motor].position;
}

int motor_current_freq(const motor_executor_t *exec, int motor)
{
    if (motor < 0 || motor >= exec->motor_count) {
        return 0;
    }
    return exec->m[motor].cur_freq;
}

motor_direction_t motor_direction(const motor_executor_t *exec, int motor)
{
    if (motor < 0 || motor >= exec->motor_count) {
        return MOTOR_DIR_FORWARD;
    }
    return exec->m[motor].dir;
}

motor_fault_code_t motor_fault_code(const motor_executor_t *exec, int motor)
{
    if (motor < 0 || motor >= exec->motor_count) {
        return MOTOR_FAULT_NONE;
    }
    return exec->m[motor].fault_code;
}

bool motor_baseline_trusted(const motor_executor_t *exec, int motor)
{
    if (motor < 0 || motor >= exec->motor_count) {
        return false;
    }
    return exec->m[motor].baseline_trusted;
}

bool motor_in_safe_state(const motor_executor_t *exec)
{
    return exec->safe_latched;
}

/* -------------------- 事件 -------------------- */

void motor_set_event_callback(motor_executor_t *exec, motor_event_cb_t cb, void *ctx)
{
    exec->cb     = cb;
    exec->cb_ctx = ctx;
}

bool motor_pop_event(motor_executor_t *exec, motor_event_t *out)
{
    (void)exec;
    (void)out;
    return false;
}
