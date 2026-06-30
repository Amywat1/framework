/**
 * @file    m8_motor_setup.c
 * @brief   M8 机型电机 HAL 实例绑定
 * @author  HUWANGWEI
 * @date    2026-06-15
 */

#include "machines/m8/adapters/setup/m8_motor_setup.h"
#include "machines/m8/config/m8_motor_table.h"
#include "ports/hal/hal_motor_bind.h"
#include "ports/hal/hal_vfd_port.h"
#include "common/vfd_types.h"
#include "common/log.h"

#include <stdint.h>

#ifdef BUILD_SIM
#  include "adapters/hal/sim_hw/hal_motor_sim.h"
#else
#  include "adapters/hal/generic/hal_motor.h"
#endif

/* -------------------------------------------------------------------------
 * VFD 速度控制与诊断回调（仅 M8 机型使用；drv_ctx 携带 vfd_id）
 * ------------------------------------------------------------------------- */

static sw_err_t m8_vfd_set_speed(int speed_ref, void *ctx)
{
    const hal_vfd_ops_t *vfd    = hal_vfd_get_ops();
    hal_vfd_id_t         vfd_id = (hal_vfd_id_t)(intptr_t)ctx;
    sw_err_t             ret;

    if (vfd == NULL) { return SW_ERR_NOT_INIT; }

    if (speed_ref > 0)
    {
        if ((vfd->set_freq == NULL) || (vfd->run == NULL)) { return SW_ERR_NOT_INIT; }
        ret = vfd->set_freq(vfd_id, (uint16_t)speed_ref);
        if (ret != SW_OK) { return ret; }
        return vfd->run(vfd_id, (hal_vfd_gear_t)1);
    }
    if (speed_ref < 0)
    {
        if ((vfd->set_freq == NULL) || (vfd->run == NULL)) { return SW_ERR_NOT_INIT; }
        ret = vfd->set_freq(vfd_id, (uint16_t)(-speed_ref));
        if (ret != SW_OK) { return ret; }
        return vfd->run(vfd_id, (hal_vfd_gear_t)-1);
    }
    if (vfd->stop == NULL) { return SW_ERR_NOT_INIT; }
    return vfd->stop(vfd_id);
}

static sw_err_t m8_vfd_read_current(uint16_t *p_val, void *ctx)
{
    const hal_vfd_ops_t *vfd    = hal_vfd_get_ops();
    hal_vfd_id_t         vfd_id = (hal_vfd_id_t)(intptr_t)ctx;

    if ((vfd == NULL) || (vfd->get_cached == NULL)) { return SW_ERR_NOT_INIT; }
    return vfd->get_cached(vfd_id, HAL_VFD_REG_CURRENT, p_val);
}

static sw_err_t m8_vfd_read_status(uint16_t *p_val, void *ctx)
{
    const hal_vfd_ops_t *vfd    = hal_vfd_get_ops();
    hal_vfd_id_t         vfd_id = (hal_vfd_id_t)(intptr_t)ctx;

    if ((vfd == NULL) || (vfd->read == NULL)) { return SW_ERR_NOT_INIT; }
    return vfd->read(vfd_id, HAL_VFD_REG_STATE, p_val);
}

static sw_err_t m8_vfd_fault_reset(void *ctx)
{
    const hal_vfd_ops_t *vfd    = hal_vfd_get_ops();
    hal_vfd_id_t         vfd_id = (hal_vfd_id_t)(intptr_t)ctx;

    if ((vfd == NULL) || (vfd->fault_reset == NULL)) { return SW_ERR_NOT_INIT; }
    return vfd->fault_reset(vfd_id);
}

/* -------------------------------------------------------------------------
 * 绑定入口
 * ------------------------------------------------------------------------- */

static sw_err_t bind_motor_entry(const motor_cfg_t *mcfg)
{
    hal_motor_bind_cfg_t bind;

    if (mcfg == NULL) { return SW_ERR_PARAM; }

    bind = (hal_motor_bind_cfg_t){
        .set_speed    = m8_vfd_set_speed,
        .read_current = m8_vfd_read_current,
        .read_status  = m8_vfd_read_status,
        .fault_reset  = m8_vfd_fault_reset,
        .drv_ctx      = (void *)(intptr_t)mcfg->vfd_id,
        .io_cw        = mcfg->io_cw,
        .io_ccw       = mcfg->io_ccw,
        .io_stop      = mcfg->io_stop,
        .io_vel0      = mcfg->io_vel0,
        .io_vel1      = mcfg->io_vel1,
        .limit_io_cw  = mcfg->limit_io_cw,
        .limit_io_ccw = mcfg->limit_io_ccw,
        .has_encoder  = mcfg->has_encoder,
        .encoder_io   = mcfg->encoder_io,
    };

#ifdef BUILD_SIM
    return hal_motor_sim_bind(mcfg->id, &bind);
#else
    return hal_motor_bind(mcfg->id, &bind);
#endif
}

sw_err_t m8_motor_setup(void)
{
    sw_err_t ret;

    for (int i = 0; i < M8_MOTOR_TABLE_SIZE; i++)
    {
        const motor_cfg_t *mcfg = &m8_motor_table[i];

        if (mcfg->drv_type != MOTOR_DRV_VFD)
        {
            continue;
        }

        ret = bind_motor_entry(mcfg);
        if (ret != SW_OK)
        {
            LOG_ERROR("m8_motor_setup: bind id=%d failed ret=%d", mcfg->id, (int)ret);
            return ret;
        }
    }

    LOG_INFO("m8_motor_setup ok");
    return SW_OK;
}
