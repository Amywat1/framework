/**
 * @file    m8_brush_rotation.c
 * @brief   M8 刷子旋转机构（组合 interlocked_slots 模式）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "projects/m8/domain/mechanism/m8_brush_rotation.h"

static interlocked_slots_t s_slots;

static brush_state_t map_state(interlocked_slot_state_t st)
{
    switch (st)
    {
    case INTERLOCKED_SLOT_STATE_RUNNING:
        return BRUSH_STATE_RUNNING;

    case INTERLOCKED_SLOT_STATE_STOPPING:
        return BRUSH_STATE_STOPPING;

    case INTERLOCKED_SLOT_STATE_FAULT:
        return BRUSH_STATE_FAULT;

    default:
        return BRUSH_STATE_IDLE;
    }
}

sw_err_t brush_init(hal_motor_exec_t *exec,
                    const int *motor_index,
                    int count,
                    const brush_interlock_pair_t *interlocks,
                    int interlock_count,
                    const motion_lifecycle_opts_t *opts)
{
    return interlocked_slots_init(&s_slots, exec, motor_index, count,
                                  interlocks, interlock_count, opts);
}

sw_err_t brush_start(brush_id_t id, int speed_gear)
{
    return interlocked_slots_start(&s_slots, id, speed_gear);
}

sw_err_t brush_stop(brush_id_t id)
{
    return interlocked_slots_stop(&s_slots, id);
}

sw_err_t brush_stop_all(void)
{
    return interlocked_slots_stop_all(&s_slots);
}

brush_state_t brush_state(brush_id_t id)
{
    return map_state(interlocked_slots_state(&s_slots, id));
}

hal_motor_fault_code_t brush_fault_code(brush_id_t id)
{
    return interlocked_slots_fault_code(&s_slots, id);
}
