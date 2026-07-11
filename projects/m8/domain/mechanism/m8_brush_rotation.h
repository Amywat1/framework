/**
 * @file    m8_brush_rotation.h
 * @brief   M8 刷子旋转机构接口（侧刷/顶刷槽位）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef M8_DOMAIN_MECHANISM_BRUSH_ROTATION_H
#define M8_DOMAIN_MECHANISM_BRUSH_ROTATION_H

#include "framework/domain/device_control/patterns/motion_lifecycle.h"
#include "framework/domain/device_control/patterns/interlocked_slots.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRUSH_MAX_COUNT      INTERLOCKED_SLOTS_MAX
#define BRUSH_MAX_INTERLOCKS INTERLOCKED_SLOT_PAIRS_MAX

typedef interlocked_slot_id_t brush_id_t;
typedef interlocked_slot_pair_t brush_interlock_pair_t;

typedef enum
{
    BRUSH_STATE_IDLE = 0,
    BRUSH_STATE_RUNNING,
    BRUSH_STATE_STOPPING,
    BRUSH_STATE_FAULT,
} brush_state_t;

sw_err_t brush_init(hal_motor_exec_t *exec,
                    const int *motor_index,
                    int count,
                    const brush_interlock_pair_t *interlocks,
                    int interlock_count,
                    const motion_lifecycle_opts_t *opts);

sw_err_t brush_start(brush_id_t id, int speed_gear);
sw_err_t brush_stop(brush_id_t id);
sw_err_t brush_stop_all(void);
brush_state_t brush_state(brush_id_t id);
hal_motor_fault_code_t brush_fault_code(brush_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* M8_DOMAIN_MECHANISM_BRUSH_ROTATION_H */
