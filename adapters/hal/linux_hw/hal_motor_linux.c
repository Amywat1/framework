/**
 * @file    hal_motor_linux.c
 * @brief   通用电机 HAL 端口 Linux 真机实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "ports/hal/hal_motor_port.h"
#include "config/machine/m8_motor_table.h"
#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "driver/drv_io.h"
#include "driver/drv_vfd.h"
#include "common/log.h"

/* io-exp SDK 头文件不在仓库内，这里按实际用法声明脉冲计数接口。 */
extern int io_pluse_read(int board_id, int pin_id);
extern int io_SDO_write(int board_id, int index, int sub_index, int *data);

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

static sw_err_t set_vfd_output(int id, int speed_ref)
{
    uint16_t   freq_hz = (uint16_t)((speed_ref >= 0) ? speed_ref : -speed_ref);
    drv_vfd_t *vfd;

    if (id == MOTOR_GANTRY)
    {
        vfd = m8_ctx_vfd_gantry();
        if (speed_ref > 0)
        {
            m8_ctx_set_gantry_fwd(true);
            return drv_vfd_run_fwd(vfd, freq_hz);
        }
        if (speed_ref < 0)
        {
            m8_ctx_set_gantry_fwd(false);
            return drv_vfd_run_rev(vfd, freq_hz);
        }
        return drv_vfd_stop(vfd);
    }

    vfd = m8_ctx_vfd_brush();
    if (speed_ref > 0)
    {
        return drv_vfd_run_fwd(vfd, freq_hz);
    }
    if (speed_ref < 0)
    {
        return drv_vfd_run_rev(vfd, freq_hz);
    }
    return drv_vfd_stop(vfd);
}

static sw_err_t set_km_output(const motor_cfg_t *cfg, int speed_ref)
{
    if (is_do_valid(cfg->io_cw))
    {
        (void)drv_io_do_set((drv_io_do_t)cfg->io_cw, speed_ref > 0);
    }
    if (is_do_valid(cfg->io_ccw))
    {
        (void)drv_io_do_set((drv_io_do_t)cfg->io_ccw, speed_ref < 0);
    }
    if ((speed_ref == 0) && is_do_valid(cfg->io_stop))
    {
        (void)drv_io_do_set((drv_io_do_t)cfg->io_stop, true);
    }
    return SW_OK;
}

static sw_err_t set_pulse_output(const motor_cfg_t *cfg, int speed_ref)
{
    if (speed_ref > 0)
    {
        if (is_do_valid(cfg->io_cw))
        {
            (void)drv_io_do_set((drv_io_do_t)cfg->io_cw, true);
        }
    }
    else if (speed_ref < 0)
    {
        if (is_do_valid(cfg->io_ccw))
        {
            (void)drv_io_do_set((drv_io_do_t)cfg->io_ccw, true);
        }
    }
    else if (is_do_valid(cfg->io_stop))
    {
        (void)drv_io_do_set((drv_io_do_t)cfg->io_stop, true);
    }
    return SW_OK;
}

static sw_err_t m8_motor_set_output(int id, int speed_ref)
{
    const motor_cfg_t *cfg = find_cfg(id);

    if (cfg == NULL)
    {
        return SW_ERR_PARAM;
    }

    switch (cfg->drv_type)
    {
        case MOTOR_DRV_VFD:
            return set_vfd_output(id, speed_ref);

        case MOTOR_DRV_KM:
            return set_km_output(cfg, speed_ref);

        case MOTOR_DRV_PULSE:
            return set_pulse_output(cfg, speed_ref);

        default:
            LOG_ERROR("hal_motor_linux: unsupported drv_type=%d", (int)cfg->drv_type);
            return SW_ERR_NOT_SUPPORT;
    }
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
    return drv_io_di_read((drv_io_di_t)cfg->limit_io_cw);
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
    return drv_io_di_read((drv_io_di_t)cfg->limit_io_ccw);
}

static int32_t m8_motor_get_pos(int id)
{
    const motor_cfg_t *cfg = find_cfg(id);

    if ((cfg == NULL) || !cfg->has_encoder)
    {
        return -1;
    }

    /* 事件驱动模式：位置由 linux_hw 适配层维护并在此读取。
     * 硬件计数器模式下 motor.c 直接维护内部位置，不会再走本接口。 */
    if (id == MOTOR_GANTRY)
    {
        return m8_ctx_get_gantry_pos();
    }
    return -1;
}

static sw_err_t m8_motor_clear_pos(int id)
{
    const motor_cfg_t *cfg = find_cfg(id);

    if ((cfg == NULL) || !cfg->has_encoder)
    {
        return SW_ERR_PARAM;
    }

    if (id == MOTOR_GANTRY)
    {
        m8_ctx_reset_gantry_pos();
        return SW_OK;
    }
    return SW_ERR_PARAM;
}

static sw_err_t m8_motor_read_hw_pulse(int id, uint32_t *p_value)
{
    const motor_cfg_t *cfg = find_cfg(id);
    int                raw;
    int                board_id;
    int                pin_id;

    if ((cfg == NULL) || !cfg->has_encoder || !cfg->encoder_use_hw_counter || (p_value == NULL))
    {
        return SW_ERR_PARAM;
    }

    board_id = (int)io_handle_board(io_di_raw(cfg->encoder_io));
    pin_id   = (int)io_handle_pin(io_di_raw(cfg->encoder_io));
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

    if ((cfg == NULL) || !cfg->has_encoder || !cfg->encoder_use_hw_counter)
    {
        return SW_ERR_PARAM;
    }

    board_id = (int)io_handle_board(io_di_raw(cfg->encoder_io));
    pin_id   = (int)io_handle_pin(io_di_raw(cfg->encoder_io));
    ret      = io_SDO_write(board_id, 0x2005, pin_id, &data);
    return (ret >= 0) ? SW_OK : SW_ERR_COMM;
}

static sw_err_t m8_motor_read_current(int id, uint16_t *p_current)
{
    const motor_cfg_t *cfg = find_cfg(id);
    drv_vfd_t         *vfd = NULL;

    if ((cfg == NULL) || (p_current == NULL) || (cfg->drv_type != MOTOR_DRV_VFD))
    {
        return SW_ERR_PARAM;
    }

    if (id == MOTOR_GANTRY)
    {
        vfd = m8_ctx_vfd_gantry();
    }
    else if ((id == MOTOR_BRUSH_TOP) || (id == MOTOR_BRUSH_SIDE))
    {
        vfd = m8_ctx_vfd_brush();
    }

    if (vfd == NULL)
    {
        return SW_ERR_PARAM;
    }
    return drv_vfd_read_current(vfd, p_current);
}

static sw_err_t m8_motor_read_status(int id, uint16_t *p_status)
{
    const motor_cfg_t *cfg = find_cfg(id);
    drv_vfd_t         *vfd = NULL;

    if ((cfg == NULL) || (p_status == NULL) || (cfg->drv_type != MOTOR_DRV_VFD))
    {
        return SW_ERR_PARAM;
    }

    if (id == MOTOR_GANTRY)
    {
        vfd = m8_ctx_vfd_gantry();
    }
    else if ((id == MOTOR_BRUSH_TOP) || (id == MOTOR_BRUSH_SIDE))
    {
        vfd = m8_ctx_vfd_brush();
    }

    if (vfd == NULL)
    {
        return SW_ERR_PARAM;
    }
    return drv_vfd_read_status(vfd, p_status);
}

static const hal_motor_ops_t s_ops = {
    .set_output     = m8_motor_set_output,
    .at_fwd_limit   = m8_motor_at_fwd_limit,
    .at_rev_limit   = m8_motor_at_rev_limit,
    .get_pos        = m8_motor_get_pos,
    .clear_pos      = m8_motor_clear_pos,
    .read_hw_pulse  = m8_motor_read_hw_pulse,
    .clear_hw_pulse = m8_motor_clear_hw_pulse,
    .read_current   = m8_motor_read_current,
    .read_status    = m8_motor_read_status,
};

void hal_motor_linux_register(void)
{
    hal_motor_register(&s_ops);
}
