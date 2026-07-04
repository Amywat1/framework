/**
 * @file    sim_encoder_counter.h
 * @brief   仿真编码器累计计数源
 * @author  HUWANGWEI
 * @date    2026-04-14
 */

#ifndef ADAPTERS_HAL_SIM_HW_SIM_ENCODER_COUNTER_H
#define ADAPTERS_HAL_SIM_HW_SIM_ENCODER_COUNTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include <stdint.h>

void     sim_encoder_counter_reset_all(void);
void     sim_encoder_counter_add_pulse(int id, int delta);
sw_err_t sim_encoder_counter_read(int id, uint32_t *p_value);
sw_err_t sim_encoder_counter_clear(int id);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_SIM_HW_SIM_ENCODER_COUNTER_H */
