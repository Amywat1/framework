/**
 * @file    interlocked_group.c
 * @brief   多槽位互锁连续运动模式实现
 * @author  HUWANGWEI
 * @date    2026-07-17
 */

#include "domain/device_control/patterns/interlocked_group.h"

#include <stddef.h>

static bool slot_valid(const interlocked_group_t *self, interlocked_group_slot_id_t id)
{
    return (self != NULL) && self->inited && (id >= 0) && (id < self->count);
}

static interlocked_group_state_t slot_phase_to_state(hal_motor_phase_t ph)
{
    switch (ph) {
    case HAL_MOTOR_PHASE_RUNNING:
        return INTERLOCKED_GROUP_STATE_RUNNING;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return INTERLOCKED_GROUP_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return INTERLOCKED_GROUP_STATE_FAULT;

    default:
        return INTERLOCKED_GROUP_STATE_IDLE;
    }
}

sw_err_t interlocked_group_init(interlocked_group_t            *self,
                                hal_motor_exec_t               *exec,
                                const int                      *motor_index,
                                int                             count,
                                const interlocked_group_pair_t *pairs,
                                int                             pair_count,
                                const motion_lifecycle_opts_t  *opts)
{
    int i;

    if ((self == NULL) || (exec == NULL) || (motor_index == NULL) || (count <= 0)
        || (count > INTERLOCKED_GROUP_SLOTS_MAX) || (pair_count < 0)
        || (pair_count > INTERLOCKED_GROUP_PAIRS_MAX)) {
        return SW_ERR_PARAM;
    }
    if ((pair_count > 0) && (pairs == NULL)) {
        return SW_ERR_PARAM;
    }
    for (i = 0; i < pair_count; i++) {
        if ((pairs[i].a < 0) || (pairs[i].a >= count) || (pairs[i].b < 0) || (pairs[i].b >= count)) {
            return SW_ERR_PARAM;
        }
    }

    self->exec  = exec;
    self->count = count;
    for (i = 0; i < count; i++) {
        self->motor[i] = motor_index[i];
    }
    self->pair_count = pair_count;
    for (i = 0; i < pair_count; i++) {
        self->pairs[i] = pairs[i];
    }
    self->opts   = (opts != NULL) ? *opts : (motion_lifecycle_opts_t){0};
    self->inited = true;
    return SW_OK;
}

sw_err_t interlocked_group_start(interlocked_group_t        *self,
                                 interlocked_group_slot_id_t id,
                                 hal_motor_dir_t             dir,
                                 int                         speed_gear)
{
    int                    target;
    int                    i;
    hal_motor_cmd_result_t r;
    sw_err_t               ret;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }
    if (!slot_valid(self, id)) {
        return SW_ERR_PARAM;
    }
    if (speed_gear <= 0) {
        return interlocked_group_stop(self, id);
    }
    if (interlocked_group_state(self, id) == INTERLOCKED_GROUP_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    target = self->motor[id];
    if (hal_motor_phase(self->exec, target) == HAL_MOTOR_PHASE_RUNNING) {
        r = hal_motor_set_speed(self->exec, target, hal_motor_speed_gear(speed_gear), dir);
        ret = hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
        if (ret != SW_OK) {
            motion_lifecycle_report_fault(&self->opts, dir == HAL_MOTOR_DIR_FORWARD,
                                          interlocked_group_fault_code(self, id));
        }
        return ret;
    }

    for (i = 0; i < self->pair_count; i++) {
        if (self->pairs[i].a == id) {
            (void)hal_motor_stop(self->exec, self->motor[self->pairs[i].b]);
        } else if (self->pairs[i].b == id) {
            (void)hal_motor_stop(self->exec, self->motor[self->pairs[i].a]);
        }
    }

    r   = hal_motor_run_continuous(self->exec, target, hal_motor_speed_gear(speed_gear), dir);
    ret = hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    if (ret != SW_OK) {
        motion_lifecycle_report_fault(&self->opts, dir == HAL_MOTOR_DIR_FORWARD,
                                      interlocked_group_fault_code(self, id));
    }
    return ret;
}

sw_err_t interlocked_group_stop(interlocked_group_t *self, interlocked_group_slot_id_t id)
{
    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }
    if (!slot_valid(self, id)) {
        return SW_ERR_PARAM;
    }

    (void)hal_motor_stop(self->exec, self->motor[id]);
    return SW_OK;
}

sw_err_t interlocked_group_stop_all(interlocked_group_t *self)
{
    int i;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    for (i = 0; i < self->count; i++) {
        (void)hal_motor_stop(self->exec, self->motor[i]);
    }
    motion_lifecycle_publish_completed(&self->opts);
    return SW_OK;
}

interlocked_group_state_t interlocked_group_state(const interlocked_group_t *self, interlocked_group_slot_id_t id)
{
    if (!slot_valid(self, id)) {
        return INTERLOCKED_GROUP_STATE_IDLE;
    }
    return slot_phase_to_state(hal_motor_phase(self->exec, self->motor[id]));
}

hal_motor_dir_t interlocked_group_direction(const interlocked_group_t *self, interlocked_group_slot_id_t id)
{
    if (!slot_valid(self, id)) {
        return HAL_MOTOR_DIR_FORWARD;
    }
    return hal_motor_direction(self->exec, self->motor[id]);
}

hal_motor_fault_code_t interlocked_group_fault_code(const interlocked_group_t *self, interlocked_group_slot_id_t id)
{
    if (!slot_valid(self, id)) {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(self->exec, self->motor[id]);
}

sw_err_t interlocked_group_recover(interlocked_group_t         *self,
                                   interlocked_group_slot_id_t  id,
                                   hal_motor_recovery_step_t    step)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }
    if (!slot_valid(self, id)) {
        return SW_ERR_PARAM;
    }

    r = hal_motor_recover(self->exec, self->motor[id], step);
    /* 单槽恢复属于维护操作，不发布组级 motion_completed；
     * 仅 stop_all 代表整组运动结束时才发布。 */
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}
