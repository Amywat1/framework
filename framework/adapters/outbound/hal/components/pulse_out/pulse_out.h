/**
 * @file    pulse_out.h
 * @brief   通用 DO 脉冲时序原语（非阻塞，由 tick 驱动释方�?
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#ifndef ADAPTERS_HAL_COMPONENTS_PULSE_OUT_PULSE_OUT_H
#define ADAPTERS_HAL_COMPONENTS_PULSE_OUT_PULSE_OUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

typedef sw_err_t (*pulse_out_set_level_fn)(void *ctx, bool level);

/**
 * @brief  单路脉冲输出�?
 */
typedef struct
{
    void                   *ctx;
    pulse_out_set_level_fn  set_level;
    bool                    active;
    uint32_t                start_ms;
    uint32_t                pulse_ms;
} pulse_out_slot_t;

/**
 * @brief  启动一次脉冲（立即拉高，pulse_ms 后由 tick 拉低�?
 * @param  slot      脉冲槽，不可�?NULL
 * @param  pulse_ms  脉宽（ms），须大�?0
 * @param  now_ms    当前毫秒时间�?
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t pulse_out_start(pulse_out_slot_t *slot, uint32_t pulse_ms, uint32_t now_ms);

/**
 * @brief  推进脉冲计时，到期自动拉�?
 * @param  slot    脉冲�?
 * @param  now_ms  当前毫秒时间�?
 */
void pulse_out_tick(pulse_out_slot_t *slot, uint32_t now_ms);

/**
 * @brief  查询脉冲是否仍在进行
 * @param  slot  脉冲�?
 * @retval true  脉冲高电平尚未到�?
 */
bool pulse_out_is_active(const pulse_out_slot_t *slot);

/**
 * @brief  取消进行中的脉冲并拉低输�?
 * @param  slot  脉冲�?
 */
void pulse_out_cancel(pulse_out_slot_t *slot);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_PULSE_OUT_PULSE_OUT_H */
