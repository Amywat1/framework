/**
 * @file    snack_vfd_backend.c
 * @brief   Linux 真机 VFD HAL：drv_vfd backend + components/vfd_manager 组合层
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#include "adapters/outbound/hal/providers/snack/modbus/snack_vfd_backend.h"

#include "adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.h"
#include "adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"
#include "adapters/outbound/hal/providers/snack/modbus/drv_vfd.h"

#include <string.h>

#define SNACK_VFD_BACKEND_SLOT_COUNT 8U

typedef struct {
    drv_vfd_t                        drv;
    snack_vfd_backend_instance_cfg_t cfg;
    bool                             bound;
    bool                             inited;
} snack_vfd_slot_t;

static snack_vfd_slot_t s_slot[SNACK_VFD_BACKEND_SLOT_COUNT];

static sw_err_t vfd_do_set(io_do_t pin, bool val)
{
    return drv_io_do_set(pin, val);
}

static bool vfd_id_valid(hal_vfd_id_t id)
{
    return ((unsigned)id < SNACK_VFD_BACKEND_SLOT_COUNT);
}

static bool instance_cfg_valid(const snack_vfd_backend_instance_cfg_t *cfg)
{
    unsigned i;

    if ((cfg == NULL) || (cfg->serial_port == NULL) || (cfg->modbus_addr <= 0) || (cfg->modbus_addr > 247)) {
        return false;
    }
    if ((cfg->gear_count == 0U) || (cfg->gear_count > SNACK_VFD_BACKEND_SPEED_GEAR_COUNT)) {
        return false;
    }
    for (i = 0U; i < cfg->gear_count; i++) {
        if (((cfg->speed_io[i] & 0x01U) != 0U && cfg->pin_spd1.raw == IO_HANDLE_NULL)
            || ((cfg->speed_io[i] & 0x02U) != 0U && cfg->pin_spd2.raw == IO_HANDLE_NULL)
            || ((cfg->speed_io[i] & 0xFCU) != 0U)) {
            return false;
        }
    }
    return true;
}

static drv_vfd_t *drv_from_ctx(void *ctx)
{
    snack_vfd_slot_t *slot = (snack_vfd_slot_t *)ctx;

    if ((slot == NULL) || !slot->inited) {
        return NULL;
    }
    return &slot->drv;
}

static sw_err_t backend_init(void *ctx)
{
    snack_vfd_slot_t *slot = (snack_vfd_slot_t *)ctx;
    sw_err_t          ret;

    if ((slot == NULL) || !slot->bound) {
        return SW_ERR_NOT_INIT;
    }

    memset(&slot->drv, 0, sizeof(slot->drv));
    ret = drv_vfd_init(&slot->drv,
                       slot->cfg.serial_port,
                       slot->cfg.baud,
                       slot->cfg.modbus_addr,
                       slot->cfg.pin_fwd,
                       slot->cfg.pin_rev,
                       slot->cfg.pin_rst,
                       vfd_do_set);
    if (ret != SW_OK) {
        slot->inited = false;
        return ret;
    }

    ret = drv_vfd_config_speed_io(
        &slot->drv, slot->cfg.pin_spd1, slot->cfg.pin_spd2, slot->cfg.gear_count, slot->cfg.speed_io);
    if (ret != SW_OK) {
        slot->inited = false;
        return ret;
    }

    slot->inited = true;
    return SW_OK;
}

static sw_err_t backend_apply_gear(void *ctx, hal_vfd_gear_t gear)
{
    drv_vfd_t *drv = drv_from_ctx(ctx);

    if (drv == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_apply_gear(drv, gear);
}

static sw_err_t backend_apply_frequency(void *ctx, hal_vfd_frequency_t frequency_centi_hz)
{
    drv_vfd_t *drv = drv_from_ctx(ctx);

    if (drv == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_apply_frequency(drv, frequency_centi_hz);
}

static sw_err_t backend_stop_outputs(void *ctx)
{
    drv_vfd_t *drv = drv_from_ctx(ctx);

    if (drv == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_stop_outputs(drv);
}

static sw_err_t backend_set_rst(void *ctx, bool level)
{
    drv_vfd_t *drv = drv_from_ctx(ctx);

    if (drv == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_set_rst(drv, level);
}

static sw_err_t backend_read(void *ctx, hal_vfd_reg_t reg, uint16_t *p_val)
{
    drv_vfd_t *drv = drv_from_ctx(ctx);

    if (drv == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_read(drv, reg, p_val);
}

static sw_err_t backend_write(void *ctx, hal_vfd_reg_t reg, uint16_t val)
{
    drv_vfd_t *drv = drv_from_ctx(ctx);

    if (drv == NULL) {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_write(drv, reg, val);
}

static hal_vfd_state_t backend_get_state(void *ctx)
{
    return drv_vfd_get_state(drv_from_ctx(ctx));
}

static bool backend_has_rst_pin(void *ctx)
{
    drv_vfd_t *vfd = drv_from_ctx(ctx);

    if (vfd == NULL) {
        return false;
    }
    return vfd->pin_rst.raw != IO_HANDLE_NULL;
}

/** @brief 本 provider 的 backend 契约单例，所有 snack VFD 实例共用 */
static const hal_vfd_backend_ops_t s_snack_vfd_backend_ops = {
    .init            = backend_init,
    .apply_gear      = backend_apply_gear,
    .apply_frequency = backend_apply_frequency,
    .stop_outputs    = backend_stop_outputs,
    .set_rst         = backend_set_rst,
    .read            = backend_read,
    .write           = backend_write,
    .get_state       = backend_get_state,
    .has_rst_pin     = backend_has_rst_pin,
};

static sw_err_t snack_bind_instance(hal_vfd_id_t id, snack_vfd_slot_t *slot)
{
    hal_vfd_manager_bind_cfg_t cfg;

    cfg.ops             = &s_snack_vfd_backend_ops;
    cfg.drv_ctx         = slot;
    cfg.rst_pulse_ms    = HAL_VFD_DEFAULT_RST_PULSE_MS;
    cfg.fault_period_ms = HAL_VFD_DEFAULT_MONITOR_PERIOD_MS;
    cfg.current_period_ms
        = (slot->cfg.current_period_ms != 0U) ? slot->cfg.current_period_ms : HAL_VFD_DEFAULT_MONITOR_PERIOD_MS;
    cfg.monitor_mask = slot->cfg.monitor_mask;
    return hal_vfd_manager_bind(id, &cfg);
}

sw_err_t snack_vfd_backend_instance_setup(hal_vfd_id_t id, const snack_vfd_backend_instance_cfg_t *cfg)
{
    sw_err_t ret;

    if (!vfd_id_valid(id) || !instance_cfg_valid(cfg)) {
        return SW_ERR_PARAM;
    }
    if (s_slot[(unsigned)id].bound) {
        return SW_ERR_BUSY;
    }

    s_slot[(unsigned)id].cfg    = *cfg;
    s_slot[(unsigned)id].inited = false;

    ret = snack_bind_instance(id, &s_slot[(unsigned)id]);
    if (ret != SW_OK) {
        return ret;
    }

    s_slot[(unsigned)id].bound = true;
    return SW_OK;
}

sw_err_t snack_vfd_backend_instance_set_monitor_mask(hal_vfd_id_t id, hal_vfd_monitor_mask_t mask)
{
    if (!vfd_id_valid(id)) {
        return SW_ERR_PARAM;
    }
    if (!s_slot[(unsigned)id].bound) {
        return SW_ERR_NOT_INIT;
    }
    return hal_vfd_manager_set_monitor_mask(id, mask);
}

void snack_vfd_backend_register(void)
{
    hal_vfd_manager_register();
}

#ifdef SNACK_VFD_BACKEND_UNIT_TEST
void snack_vfd_backend_test_reset(void)
{
    memset(s_slot, 0, sizeof(s_slot));
}
#endif
