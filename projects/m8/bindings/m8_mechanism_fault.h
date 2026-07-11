/**
 * @file    m8_mechanism_fault.h
 * @brief   M8 机构 PROCESS 故障回调
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef M8_BINDINGS_M8_MECHANISM_FAULT_H
#define M8_BINDINGS_M8_MECHANISM_FAULT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include <stdbool.h>

void m8_gantry_on_process_fault(bool is_fwd, hal_motor_fault_code_t fault);
void m8_top_brush_lift_on_process_fault(bool is_up, hal_motor_fault_code_t fault);
void m8_rear_lock_on_process_fault(bool is_lock, hal_motor_fault_code_t fault);

#ifdef __cplusplus
}
#endif

#endif /* M8_BINDINGS_M8_MECHANISM_FAULT_H */
