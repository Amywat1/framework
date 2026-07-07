/**
 * @file    brush.c
 * @brief   刷子机构领域层实现。
 *
 * 每个刷子槽位映射到 hal_motor_exec_t 的一个电机索引；互锁对由项目层在
 * brush_init() 时注入，本层只负责按配置在启动前主动停止互锁伙伴，真正的
 * 互斥保护由 hal_motor_exec 的 MOTOR_INTERLOCK_MUTEX 提供。
 */

#include "framework/domain/device_control/mechanism/brush.h"
#include <stddef.h>

/* -------------------- 静态模块状态 -------------------- */

static hal_motor_exec_t       *s_exec;
static int                     s_motor[BRUSH_MAX_COUNT];
static int                     s_count;
static brush_interlock_pair_t  s_interlocks[BRUSH_MAX_INTERLOCKS];
static int                     s_interlock_count;

/* -------------------- 内部工具 -------------------- */

static brush_state_t phase_to_state(hal_motor_phase_t ph)
{
    switch (ph) {
    case HAL_MOTOR_PHASE_RUNNING:
        return BRUSH_STATE_RUNNING;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return BRUSH_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return BRUSH_STATE_FAULT;

    default:
        return BRUSH_STATE_IDLE;
    }
}

static bool id_valid(brush_id_t id)
{
    return (id >= 0) && (id < s_count);
}

/* -------------------- 公共 API -------------------- */

sw_err_t brush_init(hal_motor_exec_t *exec,
                    const int *motor_index, int count,
                    const brush_interlock_pair_t *interlocks, int interlock_count)
{
    int i;

    if ((exec == NULL) || (motor_index == NULL)
        || (count <= 0) || (count > BRUSH_MAX_COUNT)
        || (interlock_count < 0) || (interlock_count > BRUSH_MAX_INTERLOCKS)) {
        return SW_ERR_PARAM;
    }
    if ((interlock_count > 0) && (interlocks == NULL)) {
        return SW_ERR_PARAM;
    }
    for (i = 0; i < interlock_count; i++) {
        if ((interlocks[i].a < 0) || (interlocks[i].a >= count)
            || (interlocks[i].b < 0) || (interlocks[i].b >= count)) {
            return SW_ERR_PARAM;
        }
    }

    s_exec  = exec;
    s_count = count;
    for (i = 0; i < count; i++) {
        s_motor[i] = motor_index[i];
    }
    s_interlock_count = interlock_count;
    for (i = 0; i < interlock_count; i++) {
        s_interlocks[i] = interlocks[i];
    }
    return SW_OK;
}

sw_err_t brush_start(brush_id_t id, int speed_gear)
{
    int target;
    int i;
    hal_motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (!id_valid(id)) {
        return SW_ERR_PARAM;
    }
    if (brush_state(id) == BRUSH_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    target = s_motor[id];

    if (hal_motor_phase(s_exec, target) == HAL_MOTOR_PHASE_RUNNING) {
        /* 目标刷子已在运行：仅调速，无需触碰互锁伙伴 */
        r = hal_motor_set_speed(s_exec, target, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_FORWARD);
        return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    }

    /* 先停止所有与 id 互锁的伙伴（若在运行），MCC 自动处理冷却排队与接触器切换 */
    for (i = 0; i < s_interlock_count; i++) {
        if (s_interlocks[i].a == id) {
            (void)hal_motor_stop(s_exec, s_motor[s_interlocks[i].b]);
        } else if (s_interlocks[i].b == id) {
            (void)hal_motor_stop(s_exec, s_motor[s_interlocks[i].a]);
        }
    }

    r = hal_motor_run_continuous(s_exec, target, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_FORWARD);
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t brush_stop(brush_id_t id)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (!id_valid(id)) {
        return SW_ERR_PARAM;
    }
    (void)hal_motor_stop(s_exec, s_motor[id]);
    return SW_OK;
}

sw_err_t brush_stop_all(void)
{
    int i;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    for (i = 0; i < s_count; i++) {
        (void)hal_motor_stop(s_exec, s_motor[i]);
    }
    return SW_OK;
}

brush_state_t brush_state(brush_id_t id)
{
    if ((s_exec == NULL) || !id_valid(id)) {
        return BRUSH_STATE_IDLE;
    }
    return phase_to_state(hal_motor_phase(s_exec, s_motor[id]));
}

hal_motor_fault_code_t brush_fault_code(brush_id_t id)
{
    if ((s_exec == NULL) || !id_valid(id)) {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(s_exec, s_motor[id]);
}
