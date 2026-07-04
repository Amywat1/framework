/**
 * @file    m8_water_setup.c
 * @brief   M8 机型水路绑定表应用与 domain 执行器注�?
 * @author  HUWANGWEI
 * @date    2026-06-07
 */

#include "projects/m8/bindings/m8_water_setup.h"
#include "framework/domain/device_control/mechanism/water.h"
#include "framework/ports/outbound/hal/hal_do_group_port.h"
#include "projects/m8/config/m8_water_table.h"
#include "projects/m8/config/m8_machine_config.h"
#include "framework/common/log.h"
#include <assert.h>
#include <stddef.h>

/* 编译期断言：确保水路枚举不超出 HAL 二维表上�?*/
_Static_assert((unsigned)WATER_CH_COUNT    <= HAL_DO_GROUP_MAX,
               "WATER_CH_COUNT 超过 HAL_DO_GROUP_MAX，需相应扩大 hal_do_group 上限");
_Static_assert((unsigned)WATER_SLOT_COUNT <= HAL_DO_SLOT_MAX,
               "WATER_SLOT_COUNT 超过 HAL_DO_SLOT_MAX，需相应扩大 hal_do_group 上限");

#include "framework/adapters/outbound/hal/components/do_group_mapper/hal_do_group_mapper.h"

static sw_err_t m8_bind_group_slot(hal_do_group_t group,
                                   hal_do_slot_t  slot,
                                   io_do_t        pin)
{
    return hal_do_group_bind(group, slot, pin);
}

static sw_err_t m8_apply_bind_table(void)
{
    for (unsigned i = 0U; i < M8_WATER_BIND_TABLE_COUNT; i++)
    {
        const m8_water_bind_row_t *row = &m8_water_bind_table[i];
        sw_err_t ret;

        if (((unsigned)row->channel >= WATER_CH_COUNT)
            || ((unsigned)row->slot >= WATER_SLOT_COUNT))
        {
            return SW_ERR_PARAM;
        }

        ret = m8_bind_group_slot((hal_do_group_t)row->channel,
                                 (hal_do_slot_t)row->slot,
                                 row->pin);
        if (ret != SW_OK)
        {
            return ret;
        }
    }

    return SW_OK;
}

static sw_err_t m8_water_slot_set(water_channel_t ch, water_slot_t slot, bool on)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    if ((ops == NULL) || (ops->slot_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    if (((unsigned)ch >= WATER_CH_COUNT) || ((unsigned)slot >= WATER_SLOT_COUNT))
    {
        return SW_ERR_PARAM;
    }

    return ops->slot_set((hal_do_group_t)ch, (hal_do_slot_t)slot, on);
}

static sw_err_t m8_water_all_off(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    if ((ops == NULL) || (ops->all_off == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->all_off();
}

sw_err_t m8_water_setup(void)
{
    sw_err_t ret;

    if (hal_do_group_get_ops() == NULL)
    {
        LOG_ERROR("m8_water_setup: hal_do_group not registered");
        return SW_ERR_NOT_INIT;
    }

    ret = m8_apply_bind_table();
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_water_setup: bind table apply failed ret=%d", (int)ret);
        return ret;
    }

    ret = water_init(
        &(water_cfg_t){
            .valve_open_delay_ms = CFG_WATER_VALVE_OPEN_DELAY_MS,
            .pump_stop_delay_ms  = CFG_WATER_PUMP_STOP_DELAY_MS,
        },
        &(water_actuator_ops_t){
            .slot_set = m8_water_slot_set,
            .all_off  = m8_water_all_off,
        });
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_water_setup: water_init failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_water_setup ok");
    return SW_OK;
}
