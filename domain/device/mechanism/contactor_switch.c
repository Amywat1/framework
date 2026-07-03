/**
 * @file    contactor_switch.c
 * @brief   接触器切换时序模块实现。
 *
 * 纯状态推进，不依赖任何时钟源——当前时刻由调用方每次传入，
 * 保证在 MCC prepare() 的非阻塞轮询语义下不引入任何阻塞等待。
 */

#include "domain/device/mechanism/contactor_switch.h"
#include <stddef.h>

sw_err_t contactor_switch_init(contactor_switch_t *cs, const contactor_ops_t *ops,
                               contactor_timing_t timing, int initial_output_id)
{
    if ((cs == NULL) || (ops == NULL)) {
        return SW_ERR_PARAM;
    }
    if ((ops->engage == NULL) || (ops->release == NULL)) {
        return SW_ERR_PARAM;
    }

    cs->ops         = *ops;
    cs->timing      = timing;
    cs->current_id  = initial_output_id;
    cs->phase       = CONTACTOR_PHASE_IDLE;
    cs->until_ms    = 0U;
    cs->initialized = true;

    return SW_OK;
}

bool contactor_switch_prepare(contactor_switch_t *cs, int target_id, uint64_t now_ms)
{
    if ((cs == NULL) || !cs->initialized) {
        return false;
    }

    switch (cs->phase) {
    case CONTACTOR_PHASE_IDLE:
        if (cs->current_id == target_id) {
            return true;
        }
        cs->ops.release(cs->ops.ctx, cs->current_id);
        cs->phase    = CONTACTOR_PHASE_RELEASING;
        cs->until_ms = now_ms + cs->timing.release_ms;
        return false;

    case CONTACTOR_PHASE_RELEASING:
        if (now_ms < cs->until_ms) {
            return false;
        }
        cs->ops.engage(cs->ops.ctx, target_id);
        cs->current_id = target_id;
        cs->phase      = CONTACTOR_PHASE_ENGAGING;
        cs->until_ms   = now_ms + cs->timing.close_ms;
        return false;

    case CONTACTOR_PHASE_ENGAGING:
        if (target_id != cs->current_id) {
            /* 吸合等待期间目标又变更，需重新切换 */
            cs->ops.release(cs->ops.ctx, cs->current_id);
            cs->phase    = CONTACTOR_PHASE_RELEASING;
            cs->until_ms = now_ms + cs->timing.release_ms;
            return false;
        }
        if (now_ms < cs->until_ms) {
            return false;
        }
        cs->phase = CONTACTOR_PHASE_IDLE;
        return true;

    default:
        return false;
    }
}

int contactor_switch_current(const contactor_switch_t *cs)
{
    if ((cs == NULL) || !cs->initialized) {
        return -1;
    }
    return cs->current_id;
}
