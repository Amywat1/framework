/**
 * @file    hal_motor_sim.c
 * @brief   通用电机 HAL 端口仿真实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "adapters/hal/sim_hw/hal_motor_sim.h"
#include "ports/hal/hal_motor_port.h"
#include "ports/hal/hal_io_port.h"
#include "ports/hal/hal_vfd_port.h"
#include "adapters/hal/sim_hw/sim_encoder_counter.h"
#include "common/log.h"

#include <string.h>

typedef struct
{
    bool                 bound;
    hal_motor_bind_cfg_t cfg;
    int                  speed_ref;
} motor_sim_slot_t;

static motor_sim_slot_t s_slot[HAL_MOTOR_BIND_SLOT_MAX];

static bool is_di_valid(io_di_t pin)
{
    return io_di_raw(pin) != IO_HANDLE_NULL;
}

static motor_sim_slot_t *slot_by_id(int id)
{
    if ((id < 0) || (id >= HAL_MOTOR_BIND_SLOT_MAX) || !s_slot[id].bound)
    {
        return NULL;
    }
    return &s_slot[id];
}

static bool sim_di_read(io_di_t pin)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->di_read == NULL))
    {
        return false;
    }
    return ops->di_read(pin);
}

static sw_err_t set_vfd_output(const hal_motor_bind_cfg_t *cfg, int speed_ref)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    hal_vfd_id_t         vfd_id;
    sw_err_t             ret;

    if ((cfg == NULL) || (cfg->vfd_backend_id < 0))
    {
        return SW_ERR_PARAM;
    }
    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    vfd_id = (hal_vfd_id_t)cfg->vfd_backend_id;

    if (speed_ref > 0)
    {
        if ((vfd->set_freq == NULL) || (vfd->run == NULL))
        {
            return SW_ERR_NOT_INIT;
        }
        ret = vfd->set_freq(vfd_id, (uint16_t)speed_ref);
        if (ret != SW_OK)
        {
            return ret;
        }
        return vfd->run(vfd_id, (hal_vfd_gear_t)1);
    }
    if (speed_ref < 0)
    {
        if ((vfd->set_freq == NULL) || (vfd->run == NULL))
        {
            return SW_ERR_NOT_INIT;
        }
        ret = vfd->set_freq(vfd_id, (uint16_t)(-speed_ref));
        if (ret != SW_OK)
        {
            return ret;
        }
        return vfd->run(vfd_id, (hal_vfd_gear_t)-1);
    }
    if (vfd->stop == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return vfd->stop(vfd_id);
}

static sw_err_t sim_motor_set_output(int id, int speed_ref)
{
    motor_sim_slot_t *slot = slot_by_id(id);
    sw_err_t          ret;

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    if (slot->cfg.drv_type == HAL_MOTOR_DRV_VFD)
    {
        ret = set_vfd_output(&slot->cfg, speed_ref);
    }
    else
    {
        ret = SW_OK;
    }

    if (ret == SW_OK)
    {
        slot->speed_ref = speed_ref;
        LOG_INFO("hal_motor_sim: id=%d speed_ref=%d", id, speed_ref);
    }
    return ret;
}

static bool sim_motor_at_fwd_limit(int id)
{
    motor_sim_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || !is_di_valid(slot->cfg.limit_io_cw))
    {
        return false;
    }
    return sim_di_read(slot->cfg.limit_io_cw);
}

static bool sim_motor_at_rev_limit(int id)
{
    motor_sim_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || !is_di_valid(slot->cfg.limit_io_ccw))
    {
        return false;
    }
    return sim_di_read(slot->cfg.limit_io_ccw);
}

static bool sim_motor_encoder_counter_online(int id)
{
    motor_sim_slot_t *slot = slot_by_id(id);

    return (slot != NULL) && slot->cfg.has_encoder;
}

static sw_err_t sim_motor_read_hw_pulse(int id, uint32_t *p_value)
{
    motor_sim_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || (p_value == NULL) || !slot->cfg.has_encoder)
    {
        return SW_ERR_PARAM;
    }

    return sim_encoder_counter_read(id, p_value);
}

static sw_err_t sim_motor_clear_hw_pulse(int id)
{
    motor_sim_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || !slot->cfg.has_encoder)
    {
        return SW_ERR_PARAM;
    }

    return sim_encoder_counter_clear(id);
}

static sw_err_t sim_motor_read_current(int id, uint16_t *p_current)
{
    motor_sim_slot_t    *slot = slot_by_id(id);
    const hal_vfd_ops_t *vfd  = hal_vfd_get_ops();

    if ((slot == NULL) || (p_current == NULL) ||
        (slot->cfg.drv_type != HAL_MOTOR_DRV_VFD) ||
        (slot->cfg.vfd_backend_id < 0))
    {
        return SW_ERR_PARAM;
    }
    if ((vfd == NULL) || (vfd->read_current == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    return vfd->read_current((hal_vfd_id_t)slot->cfg.vfd_backend_id, p_current);
}

static sw_err_t sim_motor_read_status(int id, uint16_t *p_status)
{
    motor_sim_slot_t    *slot = slot_by_id(id);
    const hal_vfd_ops_t *vfd  = hal_vfd_get_ops();

    if ((slot == NULL) || (p_status == NULL) ||
        (slot->cfg.drv_type != HAL_MOTOR_DRV_VFD) ||
        (slot->cfg.vfd_backend_id < 0))
    {
        return SW_ERR_PARAM;
    }
    if ((vfd == NULL) || (vfd->read_status == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    return vfd->read_status((hal_vfd_id_t)slot->cfg.vfd_backend_id, p_status);
}

static sw_err_t sim_motor_fault_reset(int id)
{
    motor_sim_slot_t    *slot = slot_by_id(id);
    const hal_vfd_ops_t *vfd  = hal_vfd_get_ops();

    if ((slot == NULL) || (slot->cfg.drv_type != HAL_MOTOR_DRV_VFD) ||
        (slot->cfg.vfd_backend_id < 0))
    {
        return SW_ERR_PARAM;
    }
    if ((vfd == NULL) || (vfd->fault_reset == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    return vfd->fault_reset((hal_vfd_id_t)slot->cfg.vfd_backend_id);
}

sw_err_t hal_motor_sim_bind(int motor_id, const hal_motor_bind_cfg_t *cfg)
{
    motor_sim_slot_t *slot;

    if ((cfg == NULL) || (motor_id < 0) || (motor_id >= HAL_MOTOR_BIND_SLOT_MAX))
    {
        return SW_ERR_PARAM;
    }
    if ((cfg->drv_type == HAL_MOTOR_DRV_VFD) &&
        (cfg->vfd_backend_id < 0))
    {
        return SW_ERR_PARAM;
    }

    slot            = &s_slot[motor_id];
    slot->cfg       = *cfg;
    slot->speed_ref = 0;
    slot->bound     = true;
    return SW_OK;
}

static const hal_motor_ops_t s_ops = {
    .set_output             = sim_motor_set_output,
    .at_fwd_limit           = sim_motor_at_fwd_limit,
    .at_rev_limit           = sim_motor_at_rev_limit,
    .encoder_counter_online = sim_motor_encoder_counter_online,
    .read_hw_pulse          = sim_motor_read_hw_pulse,
    .clear_hw_pulse         = sim_motor_clear_hw_pulse,
    .read_current           = sim_motor_read_current,
    .read_status            = sim_motor_read_status,
    .fault_reset            = sim_motor_fault_reset,
};

void hal_motor_sim_register(void)
{
    memset(s_slot, 0, sizeof(s_slot));
    sim_encoder_counter_reset_all();
    hal_motor_register(&s_ops);
    LOG_INFO("hal_motor_sim: registered");
}
