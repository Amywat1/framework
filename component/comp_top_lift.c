/**
 * @file    comp_top_lift.c
 * @brief   顶刷升降组件实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "comp_top_lift.h"
#include "bsp/bsp_hal.h"
#include "service/svc_param.h"
#include "common/log.h"
#include <unistd.h>

/* 单次发送脉冲批次大小（每批检查一次限位，防止过冲）*/
#define LIFT_PULSE_BATCH    50U

sw_err_t comp_top_lift_init(void)
{
    LOG_INFO("comp_top_lift init ok");
    return SW_OK;
}

sw_err_t comp_top_lift_up(uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0U;
    sw_err_t ret;

    if (hal_top_lift_at_up()) {
        LOG_INFO("comp_top_lift_up: already at top");
        return SW_OK;
    }

    LOG_INFO("comp_top_lift_up start");

    /* 分批发送脉冲，每批检查上限位 */
    while (!hal_top_lift_at_up()) {
        ret = hal_top_lift_up(LIFT_PULSE_BATCH);
        if (ret != SW_OK) {
            return ret;
        }

        /* 粗略超时估算（每批 LIFT_PULSE_BATCH 脉冲 × 单脉冲 2×pulse_us）*/
        elapsed_ms += (LIFT_PULSE_BATCH * 2U * 100U) / 1000U + 1U;
        if (elapsed_ms >= timeout_ms) {
            LOG_ERROR("comp_top_lift_up: timeout");
            return SW_ERR_TIMEOUT;
        }
    }

    LOG_INFO("comp_top_lift_up done in ~%u ms", (unsigned)elapsed_ms);
    return SW_OK;
}

sw_err_t comp_top_lift_down(uint32_t pulses)
{
    sw_err_t ret;

    if (hal_top_lift_at_down()) {
        LOG_WARN("comp_top_lift_down: already at bottom");
        return SW_ERR_STATE;
    }

    if (pulses == 0U) {
        /* 从参数表读取配置的下降脉冲数 */
        pulses = (uint32_t)svc_param_get_int(PARAM_KEY_TOP_LIFT_DOWN_PUL, 500);
    }

    LOG_INFO("comp_top_lift_down: %u pulses", (unsigned)pulses);

    /* 下降过程中按批检查下限位，防止过冲 */
    while ((pulses > 0U) && !hal_top_lift_at_down()) {
        uint32_t batch = (pulses > LIFT_PULSE_BATCH) ? LIFT_PULSE_BATCH : pulses;
        ret = hal_top_lift_down(batch);
        if (ret != SW_OK) {
            return ret;
        }
        pulses -= batch;
    }

    return SW_OK;
}

bool comp_top_lift_at_top(void)    { return hal_top_lift_at_up(); }
bool comp_top_lift_at_bottom(void) { return hal_top_lift_at_down(); }
