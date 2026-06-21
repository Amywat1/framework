/**
 * @file    hal_motor.c
 * @brief   通用电机 HAL 端口实现（依赖 hal_io_port / hal_vfd_port，无平台 SDK）
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "adapters/hal/generic/hal_motor.h"
#include "ports/hal/hal_motor_port.h"
#include "ports/hal/hal_vfd_port.h"
#include "ports/hal/hal_io_port.h"
#include "common/log.h"

#include <string.h>

/* io-exp SDK 头文件不在仓库内，这里按实际用法声明脉冲计数接口。 */
extern int io_pluse_read(int board_id, int pin_id);
extern int io_SDO_write(int board_id, int index, int sub_index, int *data);

typedef struct
{
    bool                 bound;
    hal_motor_bind_cfg_t cfg;
} motor_bind_slot_t;

static motor_bind_slot_t s_slot[HAL_MOTOR_BIND_SLOT_MAX];

static sw_err_t motor_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return io->do_set(pin, val);
}

static bool motor_di_read(io_di_t pin)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->di_read == NULL))
    {
        return false;
    }
    return io->di_read(pin);
}

static bool motor_board_is_online(int board_id)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->board_is_online == NULL))
    {
        return false;
    }
    return io->board_is_online(board_id);
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
    if ((id < 0) || (id >= HAL_MOTOR_BIND_SLOT_MAX) || !s_slot[id].bound)
    {
        return NULL;
    }
    return &s_slot[id];
}

static sw_err_t get_encoder_counter_pin(const hal_motor_bind_cfg_t *cfg,
                                        int *p_board_id,
                                        int *p_pin_id)
{
    uint16_t raw;
    int      board_id;

    if ((cfg == NULL) || (p_board_id == NULL) || (p_pin_id == NULL) ||
        !cfg->has_encoder || !is_di_valid(cfg->encoder_io))
    {
        return SW_ERR_PARAM;
    }

    raw      = io_di_raw(cfg->encoder_io);
    board_id = (int)io_handle_board(raw);
    if (board_id <= 0)
    {
        return SW_ERR_PARAM;
    }

    *p_board_id = board_id;
    *p_pin_id   = (int)io_handle_pin(raw);
    return SW_OK;
}

static sw_err_t set_vfd_output(const hal_motor_bind_cfg_t *cfg, int speed_ref)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    hal_vfd_id_t         vfd_id;

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
        if (vfd->run_fwd == NULL)
        {
            return SW_ERR_NOT_INIT;
        }
        return vfd->run_fwd(vfd_id, (uint16_t)speed_ref);
    }
    if (speed_ref < 0)
    {
        if (vfd->run_rev == NULL)
        {
            return SW_ERR_NOT_INIT;
        }
        return vfd->run_rev(vfd_id, (uint16_t)(-speed_ref));
    }
    if (vfd->stop == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return vfd->stop(vfd_id);
}

static sw_err_t set_do_output(const hal_motor_bind_cfg_t *cfg, int speed_ref)
{
    sw_err_t ret = SW_OK;

    if (speed_ref > 0)
    {
        if (is_do_valid(cfg->io_cw))
        {
            ret = motor_do_set(cfg->io_cw, true);
        }
        if ((ret == SW_OK) && is_do_valid(cfg->io_ccw))
        {
            ret = motor_do_set(cfg->io_ccw, false);
        }
        return ret;
    }
    if (speed_ref < 0)
    {
        if (is_do_valid(cfg->io_ccw))
        {
            ret = motor_do_set(cfg->io_ccw, true);
        }
        if ((ret == SW_OK) && is_do_valid(cfg->io_cw))
        {
            ret = motor_do_set(cfg->io_cw, false);
        }
        return ret;
    }

    if (is_do_valid(cfg->io_cw))
    {
        ret = motor_do_set(cfg->io_cw, false);
    }
    if ((ret == SW_OK) && is_do_valid(cfg->io_ccw))
    {
        ret = motor_do_set(cfg->io_ccw, false);
    }
    if ((ret == SW_OK) && is_do_valid(cfg->io_stop))
    {
        ret = motor_do_set(cfg->io_stop, true);
    }
    return ret;
}

static sw_err_t hal_motor_set_output(int id, int speed_ref)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if (slot == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    if (slot->cfg.drv_type == HAL_MOTOR_DRV_VFD)
    {
        return set_vfd_output(&slot->cfg, speed_ref);
    }
    if (slot->cfg.drv_type == HAL_MOTOR_DRV_DO)
    {
        return set_do_output(&slot->cfg, speed_ref);
    }

    return SW_ERR_NOT_SUPPORT;
}

static bool hal_motor_at_fwd_limit(int id)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || !is_di_valid(slot->cfg.limit_io_cw))
    {
        return false;
    }
    return motor_di_read(slot->cfg.limit_io_cw);
}

static bool hal_motor_at_rev_limit(int id)
{
    motor_bind_slot_t *slot = slot_by_id(id);

    if ((slot == NULL) || !is_di_valid(slot->cfg.limit_io_ccw))
    {
        return false;
    }
    return motor_di_read(slot->cfg.limit_io_ccw);
}

static bool hal_motor_encoder_counter_online(int id)
{
    motor_bind_slot_t *slot = slot_by_id(id);
    int                board_id;
    int                pin_id;

    if ((slot == NULL) || !slot->cfg.has_encoder)
    {
        return false;
    }

    if (get_encoder_counter_pin(&slot->cfg, &board_id, &pin_id) != SW_OK)
    {
        return false;
    }

    (void)pin_id;
    return motor_board_is_online(board_id);
}

static sw_err_t hal_motor_read_hw_pulse(int id, uint32_t *p_value)
{
    motor_bind_slot_t *slot = slot_by_id(id);
    int                raw;
    int                board_id;
    int                pin_id;
    sw_err_t           ret;

    if ((slot == NULL) || (p_value == NULL) || !slot->cfg.has_encoder)
    {
        return SW_ERR_PARAM;
    }

    ret = get_encoder_counter_pin(&slot->cfg, &board_id, &pin_id);
    if (ret != SW_OK)
    {
        return ret;
    }
    if (!motor_board_is_online(board_id))
    {
        return SW_ERR_COMM;
    }

    raw = io_pluse_read(board_id, pin_id);
    if ((raw < 0) || (raw == (int)0x0FFFFFFF))
    {
        return SW_ERR_COMM;
    }

    *p_value = (uint32_t)raw;
    return SW_OK;
}

static sw_err_t hal_motor_clear_hw_pulse(int id)
{
    motor_bind_slot_t *slot = slot_by_id(id);
    int                data = 0;
    int                board_id;
    int                pin_id;
    int                ret;
    sw_err_t           err;

    if ((slot == NULL) || !slot->cfg.has_encoder)
    {
        return SW_ERR_PARAM;
    }

    err = get_encoder_counter_pin(&slot->cfg, &board_id, &pin_id);
    if (err != SW_OK)
    {
        return err;
    }
    if (!motor_board_is_online(board_id))
    {
        return SW_ERR_COMM;
    }

    ret = io_SDO_write(board_id, 0x2005, pin_id, &data);
    return (ret >= 0) ? SW_OK : SW_ERR_COMM;
}

static sw_err_t hal_motor_read_current(int id, uint16_t *p_current)
{
    motor_bind_slot_t   *slot = slot_by_id(id);
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

static sw_err_t hal_motor_read_status(int id, uint16_t *p_status)
{
    motor_bind_slot_t   *slot = slot_by_id(id);
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

static sw_err_t hal_motor_fault_reset(int id)
{
    motor_bind_slot_t   *slot = slot_by_id(id);
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

sw_err_t hal_motor_bind(int motor_id, const hal_motor_bind_cfg_t *cfg)
{
    motor_bind_slot_t *slot;

    if ((cfg == NULL) || (motor_id < 0) || (motor_id >= HAL_MOTOR_BIND_SLOT_MAX))
    {
        return SW_ERR_PARAM;
    }
    if ((cfg->drv_type == HAL_MOTOR_DRV_VFD) &&
        (cfg->vfd_backend_id < 0))
    {
        return SW_ERR_PARAM;
    }

    slot        = &s_slot[motor_id];
    slot->cfg   = *cfg;
    slot->bound = true;
    return SW_OK;
}

static const hal_motor_ops_t s_ops = {
    .set_output             = hal_motor_set_output,
    .at_fwd_limit           = hal_motor_at_fwd_limit,
    .at_rev_limit           = hal_motor_at_rev_limit,
    .encoder_counter_online = hal_motor_encoder_counter_online,
    .read_hw_pulse          = hal_motor_read_hw_pulse,
    .clear_hw_pulse         = hal_motor_clear_hw_pulse,
    .read_current           = hal_motor_read_current,
    .read_status            = hal_motor_read_status,
    .fault_reset            = hal_motor_fault_reset,
};

void hal_motor_generic_register(void)
{
    hal_motor_register(&s_ops);
}
