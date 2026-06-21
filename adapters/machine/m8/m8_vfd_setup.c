/**
 * @file    m8_vfd_setup.c
 * @brief   M8 机型 VFD 实例绑定与事件接线
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#include "adapters/machine/m8/m8_vfd_setup.h"
#include "adapters/hal/linux_hw/hal_vfd_linux.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "config/machine/m8_vfd_table.h"
#include "ports/hal/hal_io_port.h"
#include "common/vfd_types.h"
#include "common/log.h"

static sw_err_t io_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

static void brush_vfd_event_cb(int event_code)
{
    (void)event_code;
}

static void gantry_vfd_event_cb(int event_code)
{
    (void)event_code;
}

sw_err_t m8_vfd_setup(void)
{
    sw_err_t ret;

    ret = hal_vfd_linux_instance_init(HAL_VFD_BRUSH,
                                      M8_VFD_BRUSH_SERIAL_PORT,
                                      M8_VFD_BRUSH_BAUD,
                                      M8_VFD_BRUSH_ADDR,
                                      M8_VFD_BRUSH_PIN_FWD,
                                      M8_VFD_BRUSH_HAS_REV,
                                      M8_VFD_BRUSH_PIN_REV,
                                      M8_VFD_BRUSH_PIN_RST);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_vfd_setup: brush init failed");
        return ret;
    }
    hal_vfd_linux_instance_register_event_cb(HAL_VFD_BRUSH, brush_vfd_event_cb);
    (void)io_do_set(M8_DO_TOP_BRUSH_ACT, false);
    (void)io_do_set(M8_DO_SIDE_BRUSH_ACT, false);

    ret = hal_vfd_linux_instance_init(HAL_VFD_GANTRY,
                                      M8_VFD_GANTRY_SERIAL_PORT,
                                      M8_VFD_GANTRY_BAUD,
                                      M8_VFD_GANTRY_ADDR,
                                      M8_VFD_GANTRY_PIN_FWD,
                                      M8_VFD_GANTRY_HAS_REV,
                                      M8_VFD_GANTRY_PIN_REV,
                                      M8_VFD_GANTRY_PIN_RST);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_vfd_setup: gantry init failed");
        return ret;
    }
    hal_vfd_linux_instance_register_event_cb(HAL_VFD_GANTRY, gantry_vfd_event_cb);

    LOG_INFO("m8_vfd_setup ok");
    return SW_OK;
}
