/**
 * @file    gantry.c
 * @brief   龙门行走设备实现（机构级逻辑，无硬件依赖）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    硬件驱动通过 gantry_actuator_ops_t 注入，gantry.c 不引用任何 HAL 或 motor 符号。
 */

#include "domain/device/mechanism/gantry.h"
#include "infrastructure/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <stdatomic.h>

static gantry_actuator_ops_t s_ops;
static bool                  s_initialized = false;
static atomic_bool           s_homing      = false;

/* -------------------------------------------------------------------------
 * 内部：运动完成回调（由 ops.set_done_cb 注册给执行机构）
 * ------------------------------------------------------------------------- */
static void on_home_move_done(sw_err_t result)
{
    sw_err_t clear_ret;

    if (!atomic_load(&s_homing))
    {
        return; /* 非归位状态，忽略 */
    }

    atomic_store(&s_homing, false);

    if (result == SW_OK)
    {
        clear_ret = s_ops.clear_pos();
        if (clear_ret == SW_OK)
        {
            LOG_INFO("gantry: home done");
        }
        else
        {
            result = clear_ret;
            LOG_WARN("gantry: home clear pos failed ret=%d", (int)clear_ret);
        }
    }
    else
    {
        LOG_WARN("gantry: home aborted ret=%d", (int)result);
    }

    (void)event_publish(EVT_COMP_HOME_DONE, (uint32_t)result);
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t gantry_init(const gantry_actuator_ops_t *ops)
{
    sw_err_t ret;

    if ((ops == NULL)                 ||
        (ops->move_freq   == NULL)    ||
        (ops->stop        == NULL)    ||
        (ops->set_done_cb == NULL)    ||
        (ops->get_pos     == NULL)    ||
        (ops->clear_pos   == NULL)    ||
        (ops->at_fwd_limit == NULL)   ||
        (ops->at_rev_limit == NULL))
    {
        return SW_ERR_PARAM;
    }

    s_ops = *ops;
    atomic_store(&s_homing, false);

    ret = s_ops.set_done_cb(on_home_move_done);
    if (ret != SW_OK)
    {
        LOG_ERROR("gantry_init: set_done_cb failed ret=%d", (int)ret);
        return ret;
    }

    s_initialized = true;
    LOG_INFO("gantry: init ok");
    return SW_OK;
}

sw_err_t gantry_fwd(uint16_t freq_hz)
{
    if (!s_initialized) { return SW_ERR_NOT_INIT; }
    atomic_store(&s_homing, false);
    if (freq_hz == 0U) { return s_ops.stop(); }
    LOG_INFO("gantry: fwd freq=%u", (unsigned)freq_hz);
    return s_ops.move_freq((int)freq_hz);
}

sw_err_t gantry_rev(uint16_t freq_hz)
{
    if (!s_initialized) { return SW_ERR_NOT_INIT; }
    atomic_store(&s_homing, false);
    if (freq_hz == 0U) { return s_ops.stop(); }
    LOG_INFO("gantry: rev freq=%u", (unsigned)freq_hz);
    return s_ops.move_freq(-(int)freq_hz);
}

sw_err_t gantry_fwd_gear(uint8_t gear)
{
    if (!s_initialized) { return SW_ERR_NOT_INIT; }
    if (s_ops.move_gear == NULL) { return SW_ERR_NOT_SUPPORT; }
    atomic_store(&s_homing, false);
    if (gear == 0U) { return s_ops.stop(); }
    LOG_INFO("gantry: fwd gear=%u", (unsigned)gear);
    return s_ops.move_gear((int8_t)gear);
}

sw_err_t gantry_rev_gear(uint8_t gear)
{
    if (!s_initialized) { return SW_ERR_NOT_INIT; }
    if (s_ops.move_gear == NULL) { return SW_ERR_NOT_SUPPORT; }
    atomic_store(&s_homing, false);
    if (gear == 0U) { return s_ops.stop(); }
    LOG_INFO("gantry: rev gear=%u", (unsigned)gear);
    return s_ops.move_gear(-(int8_t)gear);
}

sw_err_t gantry_stop(void)
{
    if (!s_initialized) { return SW_ERR_NOT_INIT; }
    atomic_store(&s_homing, false);
    return s_ops.stop();
}

sw_err_t gantry_home_start(uint16_t freq_hz)
{
    sw_err_t ret;

    if (!s_initialized) { return SW_ERR_NOT_INIT; }

    if (atomic_load(&s_homing))
    {
        LOG_WARN("gantry_home_start: already homing");
        return SW_ERR_BUSY;
    }

    if (s_ops.at_rev_limit())
    {
        ret = s_ops.clear_pos();
        if (ret != SW_OK)
        {
            LOG_WARN("gantry_home_start: clear pos failed ret=%d", (int)ret);
            return ret;
        }
        (void)event_publish(EVT_COMP_HOME_DONE, 0U);
        LOG_INFO("gantry_home_start: already at home");
        return SW_OK;
    }

    atomic_store(&s_homing, true);
    ret = s_ops.move_freq(-(int)freq_hz);
    if (ret != SW_OK)
    {
        atomic_store(&s_homing, false);
        LOG_ERROR("gantry_home_start: move_freq failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("gantry_home_start: homing started freq=%u", (unsigned)freq_hz);
    return SW_OK;
}

bool gantry_at_fwd_limit(void)
{
    if (!s_initialized) { return false; }
    return s_ops.at_fwd_limit();
}

bool gantry_at_rev_limit(void)
{
    if (!s_initialized) { return false; }
    return s_ops.at_rev_limit();
}

int32_t gantry_get_pos(void)
{
    if (!s_initialized) { return -1; }
    return s_ops.get_pos();
}

sw_err_t gantry_clear_pos(void)
{
    if (!s_initialized) { return SW_ERR_NOT_INIT; }
    return s_ops.clear_pos();
}

bool gantry_is_running(void)
{
    if (!s_initialized || (s_ops.is_running == NULL)) { return false; }
    return s_ops.is_running();
}

bool gantry_is_fault(void)
{
    if (!s_initialized || (s_ops.is_fault == NULL)) { return false; }
    return s_ops.is_fault();
}

uint16_t gantry_get_current(void)
{
    if (!s_initialized || (s_ops.get_current == NULL)) { return 0U; }
    return s_ops.get_current();
}
