/**
 * @file    fan.c
 * @brief   风机领域层实现。
 *
 * 以 DO/DI 回调驱动变频器开关与复位，通过 FAN_ALARM DI
 * 检测故障并在 tick 中自动触发停机。复位脉冲时序由内部计时管理。
 */

#include "domain/device/mechanism/fan.h"
#include "common/time_util.h"
#include <stddef.h>

/* -------------------- 静态模块状态 -------------------- */

static fan_io_ops_t s_ops;
static fan_cfg_t    s_cfg;
static fan_state_t  s_state       = FAN_STATE_IDLE;
static uint32_t     s_reset_start = 0U;

/* -------------------- 公共 API -------------------- */

sw_err_t fan_init(const fan_io_ops_t *ops, const fan_cfg_t *cfg)
{
    if ((ops == NULL) || (cfg == NULL)) {
        return SW_ERR_PARAM;
    }
    if ((ops->set_start == NULL) || (ops->set_reset == NULL) ||
        (ops->read_alarm == NULL)) {
        return SW_ERR_PARAM;
    }
    if (cfg->reset_pulse_ms == 0U) {
        return SW_ERR_PARAM;
    }

    s_ops   = *ops;
    s_cfg   = *cfg;
    s_state = FAN_STATE_IDLE;
    return SW_OK;
}

sw_err_t fan_start(void)
{
    if (s_ops.set_start == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if ((s_state == FAN_STATE_FAULT) || (s_state == FAN_STATE_RESETTING)) {
        return SW_ERR_STATE;
    }

    (void)s_ops.set_start(s_ops.ctx, true);
    s_state = FAN_STATE_RUNNING;
    return SW_OK;
}

sw_err_t fan_stop(void)
{
    if (s_ops.set_start == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if ((s_state == FAN_STATE_FAULT) || (s_state == FAN_STATE_RESETTING)) {
        return SW_ERR_STATE;
    }

    (void)s_ops.set_start(s_ops.ctx, false);
    s_state = FAN_STATE_IDLE;
    return SW_OK;
}

sw_err_t fan_reset(void)
{
    if (s_ops.set_start == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (s_state != FAN_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    (void)s_ops.set_reset(s_ops.ctx, true);
    s_reset_start = time_util_get_ms();
    s_state       = FAN_STATE_RESETTING;
    return SW_OK;
}

void fan_tick(void)
{
    if (s_ops.set_start == NULL) {
        return;
    }

    switch (s_state) {
    case FAN_STATE_IDLE:
    case FAN_STATE_RUNNING:
        if (s_ops.read_alarm(s_ops.ctx)) {
            (void)s_ops.set_start(s_ops.ctx, false);
            s_state = FAN_STATE_FAULT;
        }
        break;

    case FAN_STATE_RESETTING:
        if (time_elapsed_ms(s_reset_start, time_util_get_ms()) >= s_cfg.reset_pulse_ms) {
            (void)s_ops.set_reset(s_ops.ctx, false);
            s_state = s_ops.read_alarm(s_ops.ctx) ? FAN_STATE_FAULT : FAN_STATE_IDLE;
        }
        break;

    default:
        break;
    }
}

fan_state_t fan_state(void)
{
    return s_state;
}
