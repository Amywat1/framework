/**
 * @file    hal_motor.c
 * @brief   通用电机 HAL 通用适配层实现（依赖 hal_io_port，无平台 SDK，无 VFD 直接依赖）
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "framework/adapters/outbound/hal/generic/hal_motor.h"

#include "framework/common/log.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/ports/outbound/hal/hal_motor_port.h"

#include <string.h>

typedef struct {
    bool                 bound;
    hal_motor_bind_cfg_t cfg;
} motor_bind_slot_t;

static motor_bind_slot_t s_slot[HAL_MOTOR_BIND_SLOT_MAX];

static sw_err_t motor_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->do_set == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return io->do_set(pin, val);
}

static bool motor_di_read(io_di_t pin)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->di_read == NULL)) {
        return false;
    }
    return io->di_read(pin);
}


static bool is_di_valid(io_di_t pin)
{
    return io_di_raw(pin) != IO_HANDLE_NULL;
}

static bool is_do_valid(io_do_t pin)
{
    return io_do_raw(pin) != IO_HANDLE_NULL;
}

static motor_bind_slot_t *slot_by_id(int id)
{
    if ((id < 0) || (id >= HAL_MOTOR_BIND_SLOT_MAX) || !s_slot[id].bound) {
        return NULL;
    }
    return &s_slot[id];
}


static sw_err_t set_do_output(const hal_motor_bind_cfg_t *cfg, int speed_ref)
{
    sw_err_t ret = SW_OK;

    if (speed_ref > 0) {
        if (is_do_valid(cfg->io_stop)) {
            ret = motor_do_set(cfg->io_stop, false);
        }
        if ((ret == SW_OK) && is_do_valid(cfg->io_ccw)) {
            ret = motor_do_set(cfg->io_ccw, false);
        }
        if ((ret == SW_OK) && is_do_valid(cfg->io_cw)) {
            ret = motor_do_set(cfg->io_cw, true);
        }
        return ret;
    }
    if (speed_ref < 0) {
        if (is_do_valid(cfg->io_stop)) {
            ret = motor_do_set(cfg->io_stop, false);
        }
        if ((ret == SW_OK) && is_do_valid(cfg->io_cw)) {
            ret = motor_do_set(cfg->io_cw, false);
        }
        if ((ret == SW_OK) && is_do_valid(cfg->io_ccw)) {
            ret = motor_do_set(cfg->io_ccw, true);
        }
        return ret;
    }

    if (is_do_valid(cfg->io_cw)) {
        ret = motor_do_set(cfg->io_cw, false);
    }
    if ((ret == SW_OK) && is_do_valid(cfg->io_ccw)) {
        ret = motor_do_set(cfg->io_ccw, false);
    }
    if ((ret == SW_OK) && is_do_valid(cfg->io_stop)) {
        ret = motor_do_set(cfg->io_stop, true);
    }
    return ret;
}

static sw_err_t hal_motor_set_output(int id, int speed_ref)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if (slot == NULL) {
        return SW_ERR_NOT_INIT;
    }

    if (slot->cfg.set_speed != NULL) {
        return slot->cfg.set_speed(speed_ref, slot->cfg.drv_ctx);
    }

    return set_do_output(&slot->cfg, speed_ref);
}

static bool hal_motor_at_fwd_limit(int id)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || !is_di_valid(slot->cfg.limit_io_cw)) {
        return false;
    }
    return motor_di_read(slot->cfg.limit_io_cw);
}

static bool hal_motor_at_rev_limit(int id)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || !is_di_valid(slot->cfg.limit_io_ccw)) {
        return false;
    }
    return motor_di_read(slot->cfg.limit_io_ccw);
}

static sw_err_t hal_motor_read_hw_pulse(int id, uint32_t *p_value)
{
    motor_bind_slot_t  *slot = slot_by_id(id);
    const hal_io_ops_t *io;
    int                 raw;

    if ((slot == NULL) || (p_value == NULL) ||
        !slot->cfg.has_encoder || !is_di_valid(slot->cfg.encoder_io))
    {
        return SW_ERR_PARAM;
    }

    io = hal_io_get_ops();
    if ((io == NULL) || (io->pulse_read == NULL)) { return SW_ERR_NOT_INIT; }

    raw = io->pulse_read(slot->cfg.encoder_io);
    if ((raw < 0) || (raw == (int)0x0FFFFFFF)) { return SW_ERR_COMM; }

    *p_value = (uint32_t)raw;
    return SW_OK;
}

static sw_err_t hal_motor_clear_hw_pulse(int id)
{
    motor_bind_slot_t  *slot = slot_by_id(id);
    const hal_io_ops_t *io;

    if ((slot == NULL) || !slot->cfg.has_encoder || !is_di_valid(slot->cfg.encoder_io))
    {
        return SW_ERR_PARAM;
    }

    io = hal_io_get_ops();
    if ((io == NULL) || (io->pulse_clear == NULL)) { return SW_ERR_NOT_INIT; }

    return io->pulse_clear(slot->cfg.encoder_io);
}

static sw_err_t hal_motor_read_current(int id, uint16_t *p_current)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || (p_current == NULL)) {
        return SW_ERR_PARAM;
    }
    if (slot->cfg.read_current == NULL) {
        return SW_ERR_NOT_SUPPORT;
    }

    return slot->cfg.read_current(p_current, slot->cfg.drv_ctx);
}

#define VFD_STATUS_RUNNING_BIT 0x0001U

static sw_err_t hal_motor_read_running(int id, bool *p_is_running)
{
    motor_bind_slot_t *slot = slot_by_id(id);
    uint16_t           status;
    sw_err_t           ret;

    if ((slot == NULL) || (p_is_running == NULL)) {
        return SW_ERR_PARAM;
    }
    if (slot->cfg.read_status == NULL) {
        return SW_ERR_NOT_SUPPORT;
    }

    ret = slot->cfg.read_status(&status, slot->cfg.drv_ctx);
    if (ret != SW_OK) {
        return ret;
    }
    *p_is_running = ((status & VFD_STATUS_RUNNING_BIT) != 0U);
    return SW_OK;
}

static sw_err_t hal_motor_set_gear(int id, int8_t gear)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (slot->cfg.set_gear == NULL)
    {
        return SW_ERR_NOT_SUPPORT;
    }
    return slot->cfg.set_gear((hal_vfd_gear_t)gear, slot->cfg.drv_ctx);
}

static sw_err_t hal_motor_fault_reset(int id)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if (slot == NULL) {
        return SW_ERR_PARAM;
    }
    if (slot->cfg.fault_reset == NULL) {
        return SW_ERR_NOT_SUPPORT;
    }

    return slot->cfg.fault_reset(slot->cfg.drv_ctx);
}

sw_err_t hal_motor_bind(int motor_id, const hal_motor_bind_cfg_t *cfg)
{
    motor_bind_slot_t *slot;

    if ((cfg == NULL) || (motor_id < 0) || (motor_id >= HAL_MOTOR_BIND_SLOT_MAX)) {
        return SW_ERR_PARAM;
    }

    slot        = &s_slot[motor_id];
    slot->cfg   = *cfg;
    slot->bound = true;
    return SW_OK;
}

static const hal_motor_ops_t s_ops = {
    .set_output             = hal_motor_set_output,
    .set_gear               = hal_motor_set_gear,
    .at_fwd_limit           = hal_motor_at_fwd_limit,
    .at_rev_limit           = hal_motor_at_rev_limit,
    .read_hw_pulse          = hal_motor_read_hw_pulse,
    .clear_hw_pulse         = hal_motor_clear_hw_pulse,
    .read_current           = hal_motor_read_current,
    .read_running           = hal_motor_read_running,
    .fault_reset            = hal_motor_fault_reset,
};

void hal_motor_generic_register(void)
{
    hal_motor_register(&s_ops);
}
