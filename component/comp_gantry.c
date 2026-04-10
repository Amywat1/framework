/**
 * @file    comp_gantry.c
 * @brief   龙门行走组件实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "comp_gantry.h"
#include "bsp/bsp_hal.h"
#include "common/log.h"
#include <unistd.h>
#include <stdatomic.h>

/* 码盘脉冲计数（原子操作，IO 回调线程与业务线程共享）*/
static atomic_int s_pos = 0;

sw_err_t comp_gantry_init(void)
{
    atomic_store(&s_pos, 0);
    LOG_INFO("comp_gantry init ok");
    return SW_OK;
}

sw_err_t comp_gantry_fwd(uint16_t freq_hz)
{
    if (comp_gantry_at_fwd_limit()) {
        LOG_WARN("comp_gantry_fwd: already at fwd limit, rejected");
        return SW_ERR_STATE;
    }
    return hal_gantry_fwd(freq_hz);
}

sw_err_t comp_gantry_rev(uint16_t freq_hz)
{
    if (comp_gantry_at_rev_limit()) {
        LOG_WARN("comp_gantry_rev: already at rev limit, rejected");
        return SW_ERR_STATE;
    }
    return hal_gantry_rev(freq_hz);
}

sw_err_t comp_gantry_stop(void)
{
    return hal_gantry_stop();
}

sw_err_t comp_gantry_home(uint16_t slow_freq, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0U;
    sw_err_t ret;

    LOG_INFO("comp_gantry_home start, freq=%u", (unsigned)slow_freq);

    /* 若已在后限位，无需移动 */
    if (comp_gantry_at_rev_limit()) {
        comp_gantry_reset_pos();
        LOG_INFO("comp_gantry_home: already at home");
        return SW_OK;
    }

    ret = hal_gantry_rev(slow_freq);
    if (ret != SW_OK) {
        return ret;
    }

    /* 等待后限位触发（10ms 轮询）*/
    while (!comp_gantry_at_rev_limit()) {
        usleep(10U * 1000U);
        elapsed_ms += 10U;
        if (elapsed_ms >= timeout_ms) {
            (void)hal_gantry_stop();
            LOG_ERROR("comp_gantry_home: timeout after %u ms", (unsigned)timeout_ms);
            return SW_ERR_TIMEOUT;
        }
    }

    (void)hal_gantry_stop();
    comp_gantry_reset_pos();
    LOG_INFO("comp_gantry_home: done in %u ms", (unsigned)elapsed_ms);
    return SW_OK;
}

bool comp_gantry_at_fwd_limit(void) { return hal_gantry_at_fwd_limit(); }
bool comp_gantry_at_rev_limit(void) { return hal_gantry_at_rev_limit(); }

int32_t comp_gantry_get_pos(void)
{
    return (int32_t)atomic_load(&s_pos);
}

void comp_gantry_reset_pos(void)
{
    atomic_store(&s_pos, 0);
}

void comp_gantry_encoder_tick(bool direction)
{
    /* 由 bsp_hal 的 IO 输入回调在 DI9 变化时调用 */
    if (direction) {
        atomic_fetch_add(&s_pos, 1);
    } else {
        atomic_fetch_sub(&s_pos, 1);
    }
}
