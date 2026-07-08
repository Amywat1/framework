/**
 * @file    m8_cloud_manual_cmd.c
 * @brief   M8 云端手动机构动作点位 handler 实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_manual_cmd.h"
#include "projects/m8/adapters/manual/m8_manual_action.h"
#include "projects/m8/config/m8_brush_ids.h"

static sw_err_t apply_gantry_pulse(const point_value_t *in,
                                    sw_err_t (*move_fn)(int speed_gear))
{
    if (in->b)
    {
        return move_fn(M8_MANUAL_DEFAULT_SPEED_GEAR);
    }

    return m8_manual_gantry_stop();
}

static sw_err_t apply_brush_pulse(brush_id_t id, const point_value_t *in)
{
    if (in->b)
    {
        return m8_manual_brush_start(id, M8_MANUAL_DEFAULT_SPEED_GEAR);
    }

    return m8_manual_brush_stop(id);
}

sw_err_t m8_cloud_set_cmd_gantry_fwd(const point_value_t *in)
{
    return apply_gantry_pulse(in, m8_manual_gantry_fwd);
}

sw_err_t m8_cloud_set_cmd_gantry_rev(const point_value_t *in)
{
    return apply_gantry_pulse(in, m8_manual_gantry_rev);
}

sw_err_t m8_cloud_set_cmd_gantry_stop(const point_value_t *in)
{
    if (in->b)
    {
        return m8_manual_gantry_stop();
    }
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_brush_side(const point_value_t *in)
{
    return apply_brush_pulse(M8_BRUSH_SIDE, in);
}

sw_err_t m8_cloud_set_cmd_brush_top(const point_value_t *in)
{
    return apply_brush_pulse(M8_BRUSH_TOP, in);
}
