/**
 * @file    m8_motor_setup.c
 * @brief   M8 机型电机 HAL 实例绑定
 * @author  HUWANGWEI
 * @date    2026-06-15
 */

#include "adapters/machine/m8/m8_motor_setup.h"
#include "config/machine/m8_motor_table.h"
#include "ports/hal/hal_motor_bind.h"
#include "common/log.h"

#ifdef BUILD_SIM
#  include "adapters/hal/sim_hw/hal_motor_sim.h"
#else
#  include "adapters/hal/generic/hal_motor.h"
#endif

static sw_err_t bind_motor_entry(const motor_cfg_t *mcfg, int vfd_backend_id)
{
    hal_motor_bind_cfg_t bind;

    if (mcfg == NULL)
    {
        return SW_ERR_PARAM;
    }

    bind = (hal_motor_bind_cfg_t){
        .drv_type        = HAL_MOTOR_DRV_VFD,
        .vfd_backend_id  = vfd_backend_id,
        .io_cw           = mcfg->io_cw,
        .io_ccw          = mcfg->io_ccw,
        .io_stop         = mcfg->io_stop,
        .io_vel0         = mcfg->io_vel0,
        .io_vel1         = mcfg->io_vel1,
        .limit_io_cw     = mcfg->limit_io_cw,
        .limit_io_ccw    = mcfg->limit_io_ccw,
        .has_encoder     = mcfg->has_encoder,
        .encoder_io      = mcfg->encoder_io,
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

        ret = bind_motor_entry(mcfg, mcfg->vfd_id);
        if (ret != SW_OK)
        {
            LOG_ERROR("m8_motor_setup: bind id=%d failed ret=%d", mcfg->id, (int)ret);
            return ret;
        }
    }

    LOG_INFO("m8_motor_setup ok");
    return SW_OK;
}
