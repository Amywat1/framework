/**
 * @file    hal_vfd.c
 * @brief   VFD HAL 通用组合层实现
 * @author  HUWANGWEI
 * @date    2026-07-04
 *
 * @note    不含正反向切换等待；run/stop 为即时 backend 调用。
 *          tick() 推进 RST 脉冲释方与慢速通信监测，须在固定调度上下文调用。
 */

#include "adapters/hal/generic/hal_vfd.h"

#include "adapters/hal/generic/pulse_out.h"
#include "common/log.h"
#include "common/time_util.h"
#include "common/vfd_types.h"
#include "ports/hal/hal_vfd_port.h"

#include <string.h>

/** @brief 连续 Modbus 失败 N 次后上报 COMM_LOST */
#define VFD_COMM_FAIL_NOTIFY  3U

typedef struct
{
    bool                 bound;
    hal_vfd_bind_cfg_t   cfg;
    void                 (*event_cb)(int event_code);

    uint16_t             cached_fault_code;
    uint16_t             cached_current;
    bool                 fault_active;
    bool                 comm_ok;
    uint8_t              comm_fail_count;
    uint32_t             last_monitor_ms;

    pulse_out_slot_t     rst_pulse;
} hal_vfd_slot_t;

static hal_vfd_slot_t s_slot[HAL_VFD_BIND_SLOT_MAX];

static bool slot_id_valid(hal_vfd_id_t id)
{
    return ((id >= 0) && ((unsigned)id < HAL_VFD_BIND_SLOT_MAX));
}

static hal_vfd_slot_t *slot_by_id(hal_vfd_id_t id)
{
    if (!slot_id_valid(id) || !s_slot[(unsigned)id].bound)
    {
        return NULL;
    }
    return &s_slot[(unsigned)id];
}

static bool bind_cfg_valid(const hal_vfd_bind_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->drv_ctx == NULL))
    {
        return false;
    }
    if ((cfg->apply_gear == NULL) || (cfg->stop_outputs == NULL) ||
        (cfg->set_rst == NULL) || (cfg->read == NULL) ||
        (cfg->write == NULL) || (cfg->get_state == NULL))
    {
        return false;
    }
    if ((cfg->rst_pulse_ms == 0U) || (cfg->monitor_period_ms == 0U))
    {
        return false;
    }
    return true;
}

static sw_err_t pulse_set_rst_level(void *ctx, bool level)
{
    hal_vfd_slot_t *slot = (hal_vfd_slot_t *)ctx;

    if (slot == NULL)
    {
        return SW_ERR_PARAM;
    }
    return slot->cfg.set_rst(slot->cfg.drv_ctx, level);
}

static void emit_event(hal_vfd_slot_t *slot, int event_code)
{
    if ((slot != NULL) && (slot->event_cb != NULL))
    {
        slot->event_cb(event_code);
    }
}

static void on_comm_success(hal_vfd_slot_t *slot)
{
    if (!slot->comm_ok)
    {
        slot->comm_ok = true;
        emit_event(slot, HAL_VFD_EVT_COMM_RESTORED);
    }
    slot->comm_fail_count = 0U;
}

static void on_comm_failure(hal_vfd_slot_t *slot)
{
    if (slot->comm_fail_count < 0xFFU)
    {
        slot->comm_fail_count++;
    }

    if ((slot->comm_fail_count >= VFD_COMM_FAIL_NOTIFY) && slot->comm_ok)
    {
        slot->comm_ok = false;
        emit_event(slot, HAL_VFD_EVT_COMM_LOST);
    }
}

static void update_fault_state(hal_vfd_slot_t *slot, uint16_t code)
{
    bool now_fault;

    slot->cached_fault_code = code;
    now_fault               = (code != 0U);

    if (now_fault && !slot->fault_active)
    {
        slot->fault_active = true;
        emit_event(slot, HAL_VFD_EVT_FAULT_DETECTED);
    }
    else if (!now_fault && slot->fault_active)
    {
        slot->fault_active = false;
        emit_event(slot, HAL_VFD_EVT_FAULT_CLEARED);
    }
}

static void monitor_sample(hal_vfd_slot_t *slot, uint32_t now_ms)
{
    hal_vfd_state_t st;
    sw_err_t        ret;
    uint16_t        val;

    if ((slot->cfg.monitor_mask & HAL_VFD_MON_FAULT) != 0U)
    {
        ret = slot->cfg.read(slot->cfg.drv_ctx, HAL_VFD_REG_FAULT_CODE, &val);
        if (ret == SW_OK)
        {
            on_comm_success(slot);
            update_fault_state(slot, val);
        }
        else if (ret == SW_ERR_COMM)
        {
            on_comm_failure(slot);
        }
    }

    if ((slot->cfg.monitor_mask & HAL_VFD_MON_CURRENT) != 0U)
    {
        st = slot->cfg.get_state(slot->cfg.drv_ctx);
        if ((st == HAL_VFD_STATE_FWD) || (st == HAL_VFD_STATE_REV))
        {
            ret = slot->cfg.read(slot->cfg.drv_ctx, HAL_VFD_REG_CURRENT, &val);
            if (ret == SW_OK)
            {
                on_comm_success(slot);
                slot->cached_current = val;
                emit_event(slot, HAL_VFD_EVT_CURRENT_UPDATE);
            }
            else if (ret == SW_ERR_COMM)
            {
                on_comm_failure(slot);
            }
        }
    }

    slot->last_monitor_ms = now_ms;
}

sw_err_t hal_vfd_bind(hal_vfd_id_t id, const hal_vfd_bind_cfg_t *cfg)
{
    hal_vfd_slot_t *slot;

    if (!slot_id_valid(id) || !bind_cfg_valid(cfg))
    {
        return SW_ERR_PARAM;
    }

    slot = &s_slot[(unsigned)id];
    {
        void (*saved_cb)(int) = slot->event_cb;

        slot->cfg               = *cfg;
        slot->bound             = true;
        slot->event_cb          = saved_cb;
    }
    slot->cached_fault_code = 0U;
    slot->cached_current    = 0U;
    slot->fault_active      = false;
    slot->comm_ok           = true;
    slot->comm_fail_count   = 0U;
    slot->last_monitor_ms   = 0U;
    slot->rst_pulse.ctx     = slot;
    slot->rst_pulse.set_level = pulse_set_rst_level;
    slot->rst_pulse.active  = false;
    return SW_OK;
}

sw_err_t hal_vfd_set_monitor_mask(hal_vfd_id_t id, hal_vfd_monitor_mask_t mask)
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    slot->cfg.monitor_mask = mask;
    return SW_OK;
}

static sw_err_t vfd_init(void)
{
    unsigned i;

    for (i = 0U; i < HAL_VFD_BIND_SLOT_MAX; i++)
    {
        pulse_out_cancel(&s_slot[i].rst_pulse);
        s_slot[i].event_cb          = NULL;
        s_slot[i].cached_fault_code = 0U;
        s_slot[i].cached_current    = 0U;
        s_slot[i].fault_active      = false;
        s_slot[i].comm_ok           = true;
        s_slot[i].comm_fail_count   = 0U;
        s_slot[i].last_monitor_ms   = 0U;
    }
    return SW_OK;
}

static void vfd_tick(void)
{
    uint32_t now_ms = time_util_get_ms();
    unsigned i;

    for (i = 0U; i < HAL_VFD_BIND_SLOT_MAX; i++)
    {
        hal_vfd_slot_t *slot = &s_slot[i];

        if (!slot->bound)
        {
            continue;
        }

        pulse_out_tick(&slot->rst_pulse, now_ms);

        if (slot->cfg.monitor_mask == HAL_VFD_MON_NONE)
        {
            continue;
        }

        if (time_elapsed_ms(slot->last_monitor_ms, now_ms) >= slot->cfg.monitor_period_ms)
        {
            monitor_sample(slot, now_ms);
        }
    }
}

static sw_err_t vfd_run(hal_vfd_id_t id, hal_vfd_gear_t gear)
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    if (gear == 0)
    {
        return slot->cfg.stop_outputs(slot->cfg.drv_ctx);
    }
    return slot->cfg.apply_gear(slot->cfg.drv_ctx, gear);
}

static sw_err_t vfd_set_freq(hal_vfd_id_t id, uint16_t freq_hz)
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return slot->cfg.write(slot->cfg.drv_ctx, HAL_VFD_REG_FREQ, freq_hz);
}

static sw_err_t vfd_stop(hal_vfd_id_t id)
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return slot->cfg.stop_outputs(slot->cfg.drv_ctx);
}

static sw_err_t vfd_fault_reset(hal_vfd_id_t id)
{
    hal_vfd_slot_t *slot = slot_by_id(id);
    sw_err_t        ret;
    bool            use_io;
    uint32_t        now_ms;

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    pulse_out_cancel(&slot->rst_pulse);
    ret = slot->cfg.stop_outputs(slot->cfg.drv_ctx);
    if (ret != SW_OK)
    {
        return ret;
    }

    use_io = (slot->cfg.has_rst_pin != NULL) &&
             slot->cfg.has_rst_pin(slot->cfg.drv_ctx);
    if (use_io)
    {
        now_ms = time_util_get_ms();
        return pulse_out_start(&slot->rst_pulse, slot->cfg.rst_pulse_ms, now_ms);
    }

    ret = slot->cfg.write(slot->cfg.drv_ctx, HAL_VFD_REG_CLEAR_FAULT, 0U);
    if (ret == SW_ERR_PARAM)
    {
        LOG_ERROR("hal_vfd: fault_reset id=%d no rst pin and no modbus clear", id);
    }
    return ret;
}

static hal_vfd_state_t vfd_get_state(hal_vfd_id_t id)
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if (slot == NULL)
    {
        return HAL_VFD_STATE_STOPPED;
    }
    return slot->cfg.get_state(slot->cfg.drv_ctx);
}

static sw_err_t vfd_read(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val)
{
    hal_vfd_slot_t *slot = slot_by_id(id);
    sw_err_t        ret;

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (p_val == NULL)
    {
        return SW_ERR_PARAM;
    }

    ret = slot->cfg.read(slot->cfg.drv_ctx, reg, p_val);
    if (ret == SW_OK)
    {
        on_comm_success(slot);
        if (reg == HAL_VFD_REG_FAULT_CODE)
        {
            update_fault_state(slot, *p_val);
        }
    }
    else if (ret == SW_ERR_COMM)
    {
        on_comm_failure(slot);
    }
    return ret;
}

static sw_err_t vfd_get_cached(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val)
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (p_val == NULL)
    {
        return SW_ERR_PARAM;
    }

    switch (reg)
    {
        case HAL_VFD_REG_FAULT_CODE:
            *p_val = slot->cached_fault_code;
            return SW_OK;
        case HAL_VFD_REG_CURRENT:
            *p_val = slot->cached_current;
            return SW_OK;
        default:
            return SW_ERR_PARAM;
    }
}

static void vfd_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code))
{
    hal_vfd_slot_t *slot = slot_by_id(id);

    if (slot != NULL)
    {
        slot->event_cb = cb;
    }
}

static const hal_vfd_ops_t s_ops = {
    .init              = vfd_init,
    .tick              = vfd_tick,
    .run               = vfd_run,
    .set_freq          = vfd_set_freq,
    .stop              = vfd_stop,
    .fault_reset       = vfd_fault_reset,
    .get_state         = vfd_get_state,
    .read              = vfd_read,
    .get_cached        = vfd_get_cached,
    .register_event_cb = vfd_register_event_cb,
};

void hal_vfd_generic_register(void)
{
    hal_vfd_register(&s_ops);
}
