/**
 * @file    hal_vfd_sim.c
 * @brief   变频器 HAL 仿真实现（components/vfd_manager + 仿真 backend）
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#include "framework/adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.h"
#include "projects/m8/config/m8_vfd_table.h"
#include "framework/common/log.h"

#include <stddef.h>

typedef struct
{
    hal_vfd_id_t    id;
    hal_vfd_state_t state;
    uint16_t        cached_fault_code;
    uint16_t        cached_current;
} sim_vfd_ctx_t;

static sim_vfd_ctx_t s_ctx[HAL_VFD_ID_MAX];

static sw_err_t sim_apply_gear(void *ctx, hal_vfd_gear_t gear)
{
    sim_vfd_ctx_t *s = (sim_vfd_ctx_t *)ctx;

    if (s == NULL)
    {
        return SW_ERR_PARAM;
    }
    if ((gear < 0) && (s->id != HAL_VFD_GANTRY))
    {
        return SW_ERR_PARAM;
    }

    if (gear > 0)
    {
        s->state = HAL_VFD_STATE_FWD;
    }
    else if (gear < 0)
    {
        s->state = HAL_VFD_STATE_REV;
    }
    else
    {
        s->state = HAL_VFD_STATE_STOPPED;
    }
    return SW_OK;
}

static sw_err_t sim_stop_outputs(void *ctx)
{
    sim_vfd_ctx_t *s = (sim_vfd_ctx_t *)ctx;

    if (s == NULL)
    {
        return SW_ERR_PARAM;
    }
    s->state = HAL_VFD_STATE_STOPPED;
    return SW_OK;
}

static sw_err_t sim_set_rst(void *ctx, bool level)
{
    (void)ctx;
    (void)level;
    return SW_OK;
}

static sw_err_t sim_read(void *ctx, hal_vfd_reg_t reg, uint16_t *p_val)
{
    sim_vfd_ctx_t *s = (sim_vfd_ctx_t *)ctx;

    if ((s == NULL) || (p_val == NULL))
    {
        return SW_ERR_PARAM;
    }

    switch (reg)
    {
        case HAL_VFD_REG_STATE:
            *p_val = (uint16_t)s->state;
            return SW_OK;
        case HAL_VFD_REG_FAULT_CODE:
            *p_val = s->cached_fault_code;
            return SW_OK;
        case HAL_VFD_REG_CURRENT:
            *p_val = s->cached_current;
            return SW_OK;
        default:
            return SW_ERR_PARAM;
    }
}

static sw_err_t sim_write(void *ctx, hal_vfd_reg_t reg, uint16_t val)
{
    (void)ctx;
    (void)val;
    if ((reg == HAL_VFD_REG_FREQ) || (reg == HAL_VFD_REG_CLEAR_FAULT))
    {
        return SW_OK;
    }
    return SW_ERR_PARAM;
}

static hal_vfd_state_t sim_get_state(void *ctx)
{
    sim_vfd_ctx_t *s = (sim_vfd_ctx_t *)ctx;

    if (s == NULL)
    {
        return HAL_VFD_STATE_STOPPED;
    }
    return s->state;
}

static bool sim_has_rst_pin(void *ctx)
{
    (void)ctx;
    return true;
}

/** @brief 本 provider 的 backend 契约单例，所有仿真 VFD 实例共用 */
static const hal_vfd_backend_ops_t s_sim_vfd_backend_ops = {
    .apply_gear    = sim_apply_gear,
    .stop_outputs  = sim_stop_outputs,
    .set_rst       = sim_set_rst,
    .read          = sim_read,
    .write         = sim_write,
    .get_state     = sim_get_state,
    .has_rst_pin   = sim_has_rst_pin,
};

static sw_err_t sim_bind_one(hal_vfd_id_t id)
{
    hal_vfd_manager_bind_cfg_t cfg;

    s_ctx[id].id                 = id;
    s_ctx[id].state              = HAL_VFD_STATE_STOPPED;
    s_ctx[id].cached_fault_code  = 0U;
    s_ctx[id].cached_current     = 0U;

    cfg.ops                = &s_sim_vfd_backend_ops;
    cfg.drv_ctx            = &s_ctx[id];
    cfg.rst_pulse_ms       = HAL_VFD_DEFAULT_RST_PULSE_MS;
    cfg.monitor_period_ms  = HAL_VFD_DEFAULT_MONITOR_PERIOD_MS;
    cfg.monitor_mask       = HAL_VFD_MON_NONE;
    return hal_vfd_manager_bind(id, &cfg);
}

void hal_vfd_sim_register(void)
{
    if ((sim_bind_one(HAL_VFD_GANTRY) != SW_OK) ||
        (sim_bind_one(HAL_VFD_BRUSH) != SW_OK) ||
        (sim_bind_one(HAL_VFD_FAN) != SW_OK))
    {
        LOG_ERROR("hal_vfd_sim: bind failed");
        return;
    }

    hal_vfd_manager_register();
    LOG_INFO("hal_vfd_sim: registered");
}
