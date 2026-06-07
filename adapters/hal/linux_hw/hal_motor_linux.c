/**
 * @file    hal_motor_linux.c
 * @brief   通用电机 HAL 端口 Linux 真机实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "ports/hal/hal_motor_port.h"
#include "config/machine/m8_motor_table.h"
#include "ports/hal/hal_vfd_port.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "ports/hal/hal_io_port.h"
#include "common/log.h"

/* io-exp SDK 头文件不在仓库内，这里按实际用法声明脉冲计数接口。 */
extern int io_pluse_read(int board_id, int pin_id);
extern int io_SDO_write(int board_id, int index, int sub_index, int *data);

static bool hal_io_board_online(int board_id)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->board_is_online == NULL))
    {
        return false;
    }
    return ops->board_is_online(board_id);
}

static bool hal_io_di_read_pin(io_di_t pin)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->di_read == NULL))
    {
        return false;
    }
    return ops->di_read(pin);
}

static bool is_do_valid(io_do_t pin)
{
    return io_do_raw(pin) != IO_HANDLE_NULL;
}

static bool is_di_valid(io_di_t pin)
{
    return io_di_raw(pin) != IO_HANDLE_NULL;
}

static const motor_cfg_t *find_cfg(int id)
{
    for (int i = 0; i < M8_MOTOR_TABLE_SIZE; i++)
    {
        if (m8_motor_table[i].id == id)
        {
            return &m8_motor_table[i];
        }
    }
    return NULL;
}

static sw_err_t get_encoder_counter_pin(const motor_cfg_t *cfg, int *p_board_id, int *p_pin_id)
{
    uint16_t raw;
    int      board_id;

    if ((cfg == NULL) || (p_board_id == NULL) || (p_pin_id == NULL))
    {
        return SW_ERR_PARAM;
    }

    raw = io_di_raw(cfg->encoder_io);
    if (raw == IO_HANDLE_NULL)
    {
        return SW_ERR_PARAM;
    }

    board_id = (int)io_handle_board(raw);
    if (board_id <= 0)
    {
        return SW_ERR_PARAM;
    }

    *p_board_id = board_id;
    *p_pin_id   = (int)io_handle_pin(raw);
    return SW_OK;
}

static bool m8_motor_encoder_counter_online(int id)
{
    const motor_cfg_t *cfg = find_cfg(id);
    int                board_id;
    int                pin_id;

    if ((cfg == NULL) || !cfg->has_encoder ||
        (cfg->encoder_backend != MOTOR_ENCODER_COUNTER))
    {
        return false;
    }

    if (get_encoder_counter_pin(cfg, &board_id, &pin_id) != SW_OK)
    {
        return false;
    }

    (void)pin_id;
    return hal_io_board_online(board_id);
}

static sw_err_t set_vfd_output(int id, int speed_ref)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (id == MOTOR_GANTRY)
    {
        if (speed_ref > 0)
        {
            if (vfd->run_fwd == NULL)
            {
                return SW_ERR_NOT_INIT;
            }
            return vfd->run_fwd(HAL_VFD_GANTRY, (uint16_t)speed_ref);
        }
        if (speed_ref < 0)
        {
            if (vfd->run_rev == NULL)
            {
                return SW_ERR_NOT_INIT;
            }
            return vfd->run_rev(HAL_VFD_GANTRY, (uint16_t)(-speed_ref));
        }
        if (vfd->stop == NULL)
        {
            return SW_ERR_NOT_INIT;
        }
        return vfd->stop(HAL_VFD_GANTRY);
    }

    return SW_ERR_PARAM;
}

static sw_err_t m8_motor_set_output(int id, int speed_ref)
{
    const motor_cfg_t *cfg = find_cfg(id);

    if (cfg == NULL)
    {
        return SW_ERR_PARAM;
    }

    if (cfg->drv_type != MOTOR_DRV_VFD)
    {
        LOG_ERROR("hal_motor_linux: unsupported drv_type=%d", (int)cfg->drv_type);
        return SW_ERR_NOT_SUPPORT;
    }

    return set_vfd_output(id, speed_ref);
}

static bool m8_motor_at_fwd_limit(int id)
{
    const motor_cfg_t *cfg = find_cfg(id);

    if (cfg == NULL)
    {
        return false;
    }

    if (id == MOTOR_GANTRY)
    {
        return m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM);
    }

    if (!is_di_valid(cfg->limit_io_cw))
    {
        return false;
    }
    /*
     * 当前仅龙门限位已接入 m8_signal_filter。
     * 若后续新增其它 MOVE 类电机并带限位，必须先将对应 DI 接入
     * config/machine/m8_signal_table.h，再改为读取滤波后的稳定态，
     * 不能长期停留在这里直接读原始 IO。
     */
    return hal_io_di_read_pin(cfg->limit_io_cw);
}

static bool m8_motor_at_rev_limit(int id)
{
    const motor_cfg_t *cfg = find_cfg(id);

    if (cfg == NULL)
    {
        return false;
    }

    if (id == MOTOR_GANTRY)
    {
        return m8_signal_is_active(M8_SIG_GANTRY_REV_LIM);
    }

    if (!is_di_valid(cfg->limit_io_ccw))
    {
        return false;
    }
    /*
     * 当前仅龙门限位已接入 m8_signal_filter。
     * 若后续新增其它 MOVE 类电机并带限位，必须先将对应 DI 接入
     * config/machine/m8_signal_table.h，再改为读取滤波后的稳定态。
     */
    return hal_io_di_read_pin(cfg->limit_io_ccw);
}

static sw_err_t m8_motor_read_hw_pulse(int id, uint32_t *p_value)
{
    const motor_cfg_t *cfg = find_cfg(id);
    int                raw;
    int                board_id;
    int                pin_id;
    sw_err_t           ret;

    if ((cfg == NULL) || (p_value == NULL) || !cfg->has_encoder ||
        (cfg->encoder_backend != MOTOR_ENCODER_COUNTER))
    {
        return SW_ERR_PARAM;
    }

    ret = get_encoder_counter_pin(cfg, &board_id, &pin_id);
    if (ret != SW_OK)
    {
        return ret;
    }
    if (!hal_io_board_online(board_id))
    {
        return SW_ERR_COMM;
    }

    raw      = io_pluse_read(board_id, pin_id);
    if ((raw < 0) || (raw == (int)0x0FFFFFFF))
    {
        return SW_ERR_COMM;
    }

    *p_value = (uint32_t)raw;
    return SW_OK;
}

static sw_err_t m8_motor_clear_hw_pulse(int id)
{
    const motor_cfg_t *cfg = find_cfg(id);
    int                data     = 0;
    int                board_id;
    int                pin_id;
    int                ret;
    sw_err_t           err;

    if ((cfg == NULL) || !cfg->has_encoder ||
        (cfg->encoder_backend != MOTOR_ENCODER_COUNTER))
    {
        return SW_ERR_PARAM;
    }

    err = get_encoder_counter_pin(cfg, &board_id, &pin_id);
    if (err != SW_OK)
    {
        return err;
    }
    if (!hal_io_board_online(board_id))
    {
        return SW_ERR_COMM;
    }

    ret      = io_SDO_write(board_id, 0x2005, pin_id, &data);
    return (ret >= 0) ? SW_OK : SW_ERR_COMM;
}

static sw_err_t m8_motor_read_current(int id, uint16_t *p_current)
{
    const motor_cfg_t   *cfg = find_cfg(id);
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if ((cfg == NULL) || (p_current == NULL) || (cfg->drv_type != MOTOR_DRV_VFD))
    {
        return SW_ERR_PARAM;
    }
    if (id != MOTOR_GANTRY)
    {
        return SW_ERR_PARAM;
    }
    if ((vfd == NULL) || (vfd->read_current == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return vfd->read_current(HAL_VFD_GANTRY, p_current);
}

static sw_err_t m8_motor_read_status(int id, uint16_t *p_status)
{
    const motor_cfg_t   *cfg = find_cfg(id);
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if ((cfg == NULL) || (p_status == NULL) || (cfg->drv_type != MOTOR_DRV_VFD))
    {
        return SW_ERR_PARAM;
    }
    if (id != MOTOR_GANTRY)
    {
        return SW_ERR_PARAM;
    }
    if ((vfd == NULL) || (vfd->read_status == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return vfd->read_status(HAL_VFD_GANTRY, p_status);
}

static const hal_motor_ops_t s_ops = {
    .set_output             = m8_motor_set_output,
    .at_fwd_limit           = m8_motor_at_fwd_limit,
    .at_rev_limit           = m8_motor_at_rev_limit,
    .encoder_counter_online = m8_motor_encoder_counter_online,
    .read_hw_pulse          = m8_motor_read_hw_pulse,
    .clear_hw_pulse         = m8_motor_clear_hw_pulse,
    .read_current           = m8_motor_read_current,
    .read_status            = m8_motor_read_status,
};

void hal_motor_linux_register(void)
{
    hal_motor_register(&s_ops);
}
