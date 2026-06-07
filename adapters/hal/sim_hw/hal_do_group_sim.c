/**
 * @file    hal_do_group_sim.c
 * @brief   DO 组×槽位 HAL 仿真实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "adapters/hal/sim_hw/hal_do_group_sim.h"
#include "ports/hal/hal_do_group_port.h"
#include "common/io_handle.h"
#include "common/log.h"

static bool s_slot_on[HAL_DO_GROUP_MAX][HAL_DO_SLOT_MAX];
static bool s_bound[HAL_DO_GROUP_MAX][HAL_DO_SLOT_MAX];

static bool group_valid(hal_do_group_t group)
{
    return (group < HAL_DO_GROUP_MAX);
}

static bool slot_valid(hal_do_slot_t slot)
{
    return (slot < HAL_DO_SLOT_MAX);
}

sw_err_t hal_do_group_sim_bind(hal_do_group_t group,
                               hal_do_slot_t  slot,
                               io_do_t        pin)
{
    (void)pin;

    if (!group_valid(group) || !slot_valid(slot))
    {
        return SW_ERR_PARAM;
    }

    s_bound[group][slot] = true;  /* 仿真忽略 pin，统一视为已绑定 */
    return SW_OK;
}

static sw_err_t sim_init(void)
{
    return SW_OK;
}

static sw_err_t sim_slot_set(hal_do_group_t group, hal_do_slot_t slot, bool on)
{
    if (!group_valid(group) || !slot_valid(slot) || !s_bound[group][slot])
    {
        return SW_ERR_NOT_INIT;
    }

    s_slot_on[group][slot] = on;
    LOG_INFO("sim_do_group: group %u slot %u %s",
             (unsigned)group, (unsigned)slot, on ? "ON" : "OFF");
    return SW_OK;
}

static sw_err_t sim_all_off(void)
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
            ret = sim_slot_set(g, s, false);
            if ((err == SW_OK) && (ret != SW_OK))
            {
                err = ret;
            }
        }
    }
    return err;
}

static const hal_do_group_ops_t s_ops = {
    .init     = sim_init,
    .slot_set = sim_slot_set,
    .all_off  = sim_all_off,
};

void hal_do_group_sim_register(void)
{
    hal_do_group_register(&s_ops);
    LOG_INFO("hal_do_group_sim: registered");
}
