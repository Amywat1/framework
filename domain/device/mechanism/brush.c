/**
 * @file    brush.c
 * @brief   刷子机构领域层实现。
 *
 * 侧刷/顶刷的接触器切换时序完全由 MCC（motor_executor_t 的 prepare + 互锁）
 * 管理，本层仅负责把 brush_id_t 映射到对应的电机索引并转发命令。
 */

#include "domain/device/mechanism/brush.h"
#include <stddef.h>

/* -------------------- 静态模块状态 -------------------- */

static motor_executor_t *s_exec;
static int                s_motor[BRUSH_ID_MAX];
static brush_id_t         s_last_id = BRUSH_SIDE;

/* -------------------- 内部工具 -------------------- */

static brush_state_t phase_to_state(motor_phase_t ph)
{
    switch (ph) {
    case MOTOR_PHASE_RUNNING:
        return BRUSH_STATE_RUNNING;

    case MOTOR_PHASE_DECELERATING:
    case MOTOR_PHASE_REVERSAL_WAIT:
        return BRUSH_STATE_STOPPING;

    case MOTOR_PHASE_FAULT:
    case MOTOR_PHASE_ESTOP:
        return BRUSH_STATE_FAULT;

    default:
        return BRUSH_STATE_IDLE;
    }
}

/* -------------------- 公共 API -------------------- */

sw_err_t brush_init(motor_executor_t *exec, int motor_side, int motor_top)
{
    if (exec == NULL) {
        return SW_ERR_PARAM;
    }
    s_exec               = exec;
    s_motor[BRUSH_SIDE]  = motor_side;
    s_motor[BRUSH_TOP]   = motor_top;
    s_last_id            = BRUSH_SIDE;
    return SW_OK;
}

sw_err_t brush_start(brush_id_t id, int speed_gear)
{
    int target;
    int other;
    motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if ((unsigned)id >= (unsigned)BRUSH_ID_MAX) {
        return SW_ERR_PARAM;
    }
    if (brush_state() == BRUSH_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    s_last_id = id;
    target    = s_motor[id];
    other     = s_motor[(id == BRUSH_SIDE) ? BRUSH_TOP : BRUSH_SIDE];

    if (motor_phase(s_exec, target) == MOTOR_PHASE_RUNNING) {
        /* 目标刷子已在运行：仅调速，无需触碰另一路或接触器 */
        r = motor_set_speed(s_exec, target, motor_speed_gear(speed_gear), MOTOR_DIR_FORWARD);
        return motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    }

    /* 先停止另一路（若在运行），MCC 自动处理冷却排队与接触器切换 */
    (void)motor_stop(s_exec, other);
    r = motor_run_continuous(s_exec, target, motor_speed_gear(speed_gear), MOTOR_DIR_FORWARD);
    return motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t brush_stop(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (brush_state() == BRUSH_STATE_FAULT) {
        return SW_ERR_STATE;
    }
    (void)motor_stop(s_exec, s_motor[BRUSH_SIDE]);
    (void)motor_stop(s_exec, s_motor[BRUSH_TOP]);
    return SW_OK;
}

brush_state_t brush_state(void)
{
    brush_state_t side_state;
    brush_state_t top_state;

    if (s_exec == NULL) {
        return BRUSH_STATE_IDLE;
    }

    side_state = phase_to_state(motor_phase(s_exec, s_motor[BRUSH_SIDE]));
    top_state  = phase_to_state(motor_phase(s_exec, s_motor[BRUSH_TOP]));

    if ((side_state == BRUSH_STATE_FAULT) || (top_state == BRUSH_STATE_FAULT)) {
        return BRUSH_STATE_FAULT;
    }
    if ((side_state == BRUSH_STATE_RUNNING) || (top_state == BRUSH_STATE_RUNNING)) {
        return BRUSH_STATE_RUNNING;
    }
    if ((side_state == BRUSH_STATE_STOPPING) || (top_state == BRUSH_STATE_STOPPING)) {
        return BRUSH_STATE_STOPPING;
    }
    return BRUSH_STATE_IDLE;
}

brush_id_t brush_selected(void)
{
    return s_last_id;
}

motor_fault_code_t brush_fault_code(void)
{
    motor_fault_code_t fc;

    if (s_exec == NULL) {
        return MOTOR_FAULT_NONE;
    }

    fc = motor_fault_code(s_exec, s_motor[BRUSH_SIDE]);
    if (fc != MOTOR_FAULT_NONE) {
        return fc;
    }
    return motor_fault_code(s_exec, s_motor[BRUSH_TOP]);
}
