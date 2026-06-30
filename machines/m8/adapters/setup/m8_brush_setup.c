/**
 * @file    m8_brush_setup.c
 * @brief   M8 机型刷子接触器 DO 绑定与 domain 执行器注入
 * @author  HUWANGWEI
 * @date    2026-06-30
 */

#include "machines/m8/adapters/setup/m8_brush_setup.h"
#include "domain/device/unit/brush.h"
#include "ports/hal/hal_io_port.h"
#include "machines/m8/config/m8_io_pins.h"
#include "machines/m8/config/m8_machine_config.h"
#include "common/log.h"

static sw_err_t m8_brush_set_contactor(brush_id_t id, bool on)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    switch (id)
    {
        case BRUSH_ID_SIDE:
            return io->do_set(M8_IO_DO_SIDE_BRUSH_ACT, on);
        case BRUSH_ID_TOP:
            return io->do_set(M8_IO_DO_TOP_BRUSH_ACT, on);
        default:
            return SW_ERR_PARAM;
    }
}

sw_err_t m8_brush_setup(void)
{
    sw_err_t ret;

    if (hal_io_get_ops() == NULL)
    {
        LOG_ERROR("m8_brush_setup: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    ret = brush_init(
        &(brush_cfg_t){
            .contactor_wait_ms = CFG_BRUSH_CONTACTOR_WAIT_MS,
        },
        &(brush_actuator_ops_t){
            .set_contactor = m8_brush_set_contactor,
        });
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_brush_setup: brush_init failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_brush_setup ok");
    return SW_OK;
}
