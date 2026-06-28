/**
 * @file    gantry.c
 * @brief   龙门行走设备实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "domain/device/unit/gantry.h"
#include "domain/device/actuator/motor/motor.h"
#include "machines/m8/config/m8_motor_table.h"
#include "infrastructure/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <stdatomic.h>

/*
 * 位置来源：统一委托给 motor 管理层。
 * motor_tick_loop 定时同步 HAL 外部位置；
 * 将来切到硬件脉冲计数器模式时，上层无需改动。
 */
static atomic_bool s_homing = false; /* 正在归位中 */

/* -------------------------------------------------------------------------
 * motor 完成回调（由 motor_tick 线程在释放锁后调用）
 * ------------------------------------------------------------------------- */
static void on_motor_done(int motor_id, sw_err_t result, void *ctx)
{
    sw_err_t clear_ret;
    (void)ctx;

    /* motor_set_done_cb 已按 motor_id 注册，此处无需过滤 */
    (void)motor_id;

    if (!atomic_load(&s_homing))
    {
        return; /* 非归位状态，忽略 */
    }

    atomic_store(&s_homing, false);

    if (result == SW_OK)
    {
        clear_ret = motor_clear_encoder(MOTOR_GANTRY);
        if (clear_ret == SW_OK)
        {
            LOG_INFO("gantry: home done");
        }
        else
        {
            result = clear_ret;
            LOG_WARN("gantry: home clear encoder failed ret=%d", (int)clear_ret);
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
sw_err_t gantry_init(void)
{
    sw_err_t ret;

    atomic_store(&s_homing, false);

    ret = motor_set_done_cb(MOTOR_GANTRY, on_motor_done, NULL);
    if (ret != SW_OK)
    {
        LOG_ERROR("gantry_init: motor_set_done_cb failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("gantry: init ok");
    return SW_OK;
}

sw_err_t gantry_fwd(uint16_t freq_hz)
{
    atomic_store(&s_homing, false);
    if (freq_hz == 0U)
    {
        return motor_stop(MOTOR_GANTRY);
    }
    LOG_INFO("gantry: fwd freq=%u", (unsigned)freq_hz);
    return motor_move(MOTOR_GANTRY, (int)freq_hz);
}

sw_err_t gantry_rev(uint16_t freq_hz)
{
    atomic_store(&s_homing, false);
    if (freq_hz == 0U)
    {
        return motor_stop(MOTOR_GANTRY);
    }
    LOG_INFO("gantry: rev freq=%u", (unsigned)freq_hz);
    return motor_move(MOTOR_GANTRY, -(int)freq_hz);
}

sw_err_t gantry_stop(void)
{
    atomic_store(&s_homing, false);
    return motor_stop(MOTOR_GANTRY);
}

sw_err_t gantry_home_start(uint16_t freq_hz)
{
    sw_err_t ret;

    if (atomic_load(&s_homing))
    {
        LOG_WARN("gantry_home_start: already homing");
        return SW_ERR_BUSY;
    }

    /* 若已在后限位，立即完成 */
    if (motor_at_rev_limit(MOTOR_GANTRY))
    {
        ret = motor_clear_encoder(MOTOR_GANTRY);
        if (ret != SW_OK)
        {
            LOG_WARN("gantry_home_start: clear encoder failed ret=%d", (int)ret);
            return ret;
        }
        (void)event_publish(EVT_COMP_HOME_DONE, 0U);
        LOG_INFO("gantry_home_start: already at home");
        return SW_OK;
    }

    atomic_store(&s_homing, true);

    ret = motor_move(MOTOR_GANTRY, -(int)freq_hz);
    if (ret != SW_OK)
    {
        atomic_store(&s_homing, false);
        LOG_ERROR("gantry_home_start: motor_move failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("gantry_home_start: homing started freq=%u", (unsigned)freq_hz);
    return SW_OK;
}

bool gantry_at_fwd_limit(void)
{
    return motor_at_fwd_limit(MOTOR_GANTRY);
}

bool gantry_at_rev_limit(void)
{
    return motor_at_rev_limit(MOTOR_GANTRY);
}

int32_t gantry_get_pos(void)
{
    return motor_get_pos(MOTOR_GANTRY);
}

void gantry_reset_pos(void)
{
    if (motor_clear_encoder(MOTOR_GANTRY) != SW_OK)
    {
        LOG_WARN("gantry_reset_pos: clear encoder failed");
    }
}
