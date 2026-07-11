/**
 * @file    interlocked_slots.h
 * @brief   多槽位互锁连续旋转运动模式
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_DEVICE_CONTROL_PATTERNS_INTERLOCKED_SLOTS_H
#define DOMAIN_DEVICE_CONTROL_PATTERNS_INTERLOCKED_SLOTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/device_control/patterns/motion_lifecycle.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

#define INTERLOCKED_SLOTS_MAX        4
#define INTERLOCKED_SLOT_PAIRS_MAX   4

typedef int interlocked_slot_id_t;

typedef struct
{
    interlocked_slot_id_t a;
    interlocked_slot_id_t b;
} interlocked_slot_pair_t;

typedef enum
{
    INTERLOCKED_SLOT_STATE_IDLE = 0,
    INTERLOCKED_SLOT_STATE_RUNNING,
    INTERLOCKED_SLOT_STATE_STOPPING,
    INTERLOCKED_SLOT_STATE_FAULT,
} interlocked_slot_state_t;

typedef struct
{
    hal_motor_exec_t          *exec;
    int                        motor[INTERLOCKED_SLOTS_MAX];
    int                        count;
    interlocked_slot_pair_t    pairs[INTERLOCKED_SLOT_PAIRS_MAX];
    int                        pair_count;
    motion_lifecycle_opts_t    opts;
    bool                       inited;
} interlocked_slots_t;

sw_err_t interlocked_slots_init(interlocked_slots_t *self,
                                hal_motor_exec_t *exec,
                                const int *motor_index,
                                int count,
                                const interlocked_slot_pair_t *pairs,
                                int pair_count,
                                const motion_lifecycle_opts_t *opts);

sw_err_t interlocked_slots_start(interlocked_slots_t *self, interlocked_slot_id_t id, int speed_gear);
sw_err_t interlocked_slots_stop(interlocked_slots_t *self, interlocked_slot_id_t id);
sw_err_t interlocked_slots_stop_all(interlocked_slots_t *self);
interlocked_slot_state_t interlocked_slots_state(const interlocked_slots_t *self, interlocked_slot_id_t id);
hal_motor_fault_code_t interlocked_slots_fault_code(const interlocked_slots_t *self, interlocked_slot_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_INTERLOCKED_SLOTS_H */
