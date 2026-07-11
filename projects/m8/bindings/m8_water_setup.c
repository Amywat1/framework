/**
 * @file    m8_water_setup.c
 * @brief   M8 水路初始化（直写 hal_io，不经过 do_group_mapper）
 * @author  HUWANGWEI
 * @date    2026-06-07
 */

#include "projects/m8/bindings/m8_water_setup.h"
#include "framework/domain/device_control/patterns/fluid_path.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "projects/m8/config/m8_water_table.h"
#include "projects/m8/config/m8_machine_config.h"
#include "framework/common/log.h"

static io_do_t lookup_pin(fluid_path_channel_idx_t ch, fluid_path_slot_t slot)
{
    unsigned i;

    for (i = 0U; i < M8_WATER_ACTUATOR_TABLE_COUNT; i++)
    {
        const m8_water_actuator_row_t *row = &m8_water_actuator_table[i];

        if ((row->channel == ch) && (row->slot == slot))
        {
            return row->pin;
        }
    }
    return (io_do_t){0};
}

static bool pin_valid(io_do_t pin)
{
    return (pin.raw != IO_HANDLE_NULL);
}

static sw_err_t m8_water_slot_set(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, bool on)
{
    const hal_io_ops_t *ops = hal_io_get_ops();
    io_do_t             pin = lookup_pin(ch, slot);

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    if (((unsigned)ch >= M8_WATER_CH_COUNT) || ((unsigned)slot >= FLUID_PATH_SLOT_COUNT))
    {
        return SW_ERR_PARAM;
    }
    if (!pin_valid(pin))
    {
        return SW_ERR_PARAM;
    }
    return ops->do_set(pin, on);
}

static sw_err_t m8_fluid_path_all_off(void)
{
    const hal_io_ops_t *ops = hal_io_get_ops();
    unsigned            i;

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    for (i = 0U; i < M8_WATER_ACTUATOR_TABLE_COUNT; i++)
    {
        sw_err_t ret = ops->do_set(m8_water_actuator_table[i].pin, false);

        if (ret != SW_OK)
        {
            return ret;
        }
    }
    return SW_OK;
}

sw_err_t m8_water_setup(void)
{
    sw_err_t ret;

    ret = fluid_path_init(
        &(fluid_path_cfg_t){
            .valve_open_delay_ms = CFG_WATER_VALVE_OPEN_DELAY_MS,
            .pump_stop_delay_ms  = CFG_WATER_PUMP_STOP_DELAY_MS,
            .channel_count       = (uint8_t)M8_WATER_CH_COUNT,
        },
        &(fluid_path_actuator_ops_t){
            .slot_set = m8_water_slot_set,
            .all_off  = m8_fluid_path_all_off,
        },
        m8_water_path_table,
        M8_WATER_PATH_TABLE_COUNT);
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_water_setup: fluid_path_init failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_water_setup ok");
    return SW_OK;
}
