/**
 * @file    hal_do_group_mapper.c
 * @brief   DO 组×槽�?HAL 端口实现（依�?hal_io_port，无平台 SDK�?
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/adapters/outbound/hal/components/do_group_mapper/hal_do_group_mapper.h"
#include "framework/ports/outbound/hal/hal_do_group_port.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/common/io_handle.h"
#include <stddef.h>

static io_do_t s_pin[HAL_DO_GROUP_MAX][HAL_DO_SLOT_MAX];
static bool    s_bound[HAL_DO_GROUP_MAX][HAL_DO_SLOT_MAX];

static sw_err_t io_do_set(io_do_t pin, bool on)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, on);
}

static bool group_valid(hal_do_group_t group)
{
    return (group < HAL_DO_GROUP_MAX);
}

static bool slot_valid(hal_do_slot_t slot)
{
    return (slot < HAL_DO_SLOT_MAX);
}

sw_err_t hal_do_group_bind(hal_do_group_t group,
                           hal_do_slot_t  slot,
                           io_do_t        pin)
{
    if (!group_valid(group) || !slot_valid(slot))
    {
        return SW_ERR_PARAM;
    }

    s_pin[group][slot]   = pin;
    s_bound[group][slot] = (io_do_raw(pin) != IO_HANDLE_NULL);
    return SW_OK;
}

static sw_err_t do_group_init(void)
{
    return SW_OK;
}

static sw_err_t do_group_slot_set(hal_do_group_t group, hal_do_slot_t slot, bool on)
{
    if (!group_valid(group) || !slot_valid(slot) || !s_bound[group][slot])
    {
        return SW_ERR_NOT_INIT;
    }
    return io_do_set(s_pin[group][slot], on);
}

static sw_err_t do_group_all_off(void)
{
    sw_err_t err = SW_OK;

    for (hal_do_group_t g = 0U; g < HAL_DO_GROUP_MAX; g++)
    {
        for (hal_do_slot_t s = 0U; s < HAL_DO_SLOT_MAX; s++)
        {
            sw_err_t ret;

            if (!s_bound[g][s])
            {
                continue;
            }

            ret = io_do_set(s_pin[g][s], false);
            if ((err == SW_OK) && (ret != SW_OK))
            {
                err = ret;
            }
        }
    }

    return err;
}

static const hal_do_group_ops_t s_ops = {
    .init     = do_group_init,
    .slot_set = do_group_slot_set,
    .all_off  = do_group_all_off,
};

void hal_do_group_mapper_register(void)
{
    hal_do_group_register(&s_ops);
}
