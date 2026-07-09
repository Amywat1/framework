/**
 * @file    m8_cloud_cmd.c
 * @brief   M8 云端命令类点位 handler 实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "projects/m8/adapters/cloud/m8_cloud_cmd.h"
#include "projects/m8/adapters/cloud/m8_cloud_runtime.h"
#include "projects/m8/adapters/manual/m8_manual_action.h"
#include "projects/m8/config/m8_brush_ids.h"
#include "projects/m8/config/m8_water_table.h"
#include "projects/m8/config/m8_io_pins.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/domain/device_control/mechanism/fan.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/lift.h"
#include "framework/domain/device_control/mechanism/rear_lock.h"
#include "framework/domain/device_control/mechanism/water.h"
#include "framework/domain/wash/model/wash_types.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/log.h"
#include <string.h>

#define M8_CLOUD_FORCE_MS   1000U

static sw_err_t gantry_fwd_wrap(void)
{
    return m8_manual_gantry_fwd(M8_MANUAL_DEFAULT_SPEED_GEAR);
}

static sw_err_t gantry_rev_wrap(void)
{
    return m8_manual_gantry_rev(M8_MANUAL_DEFAULT_SPEED_GEAR);
}

static sw_err_t lift_up_wrap(void)
{
    sw_err_t ret = require_manual_actuator();

    if (ret != SW_OK)
    {
        return ret;
    }

    return lift_up(M8_MANUAL_DEFAULT_SPEED_GEAR, NULL);
}

static sw_err_t lift_down_wrap(void)
{
    sw_err_t ret = require_manual_actuator();

    if (ret != SW_OK)
    {
        return ret;
    }

    return lift_down(M8_MANUAL_DEFAULT_SPEED_GEAR, NULL);
}

static sw_err_t inject_cmd(cmd_type_t type)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t                     cmd;

    if ((cp == NULL) || (cp->inject == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    cmd.type = type;
    return cp->inject(&cmd);
}

/**
 * @brief  运动类云端点位先经命令矩阵校验手动点动权限
 */
static sw_err_t require_manual_actuator(void)
{
    return inject_cmd(CMD_MANUAL_ACTUATOR);
}

static sw_err_t set_hold_bool(const char *id, const point_value_t *in,
                               sw_err_t (*on_fn)(void), sw_err_t (*off_fn)(void))
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_hold(id, in->b);

    if (in->b)
    {
        return (on_fn != NULL) ? on_fn() : SW_OK;
    }

    return (off_fn != NULL) ? off_fn() : SW_OK;
}

static sw_err_t get_hold_bool(const char *id, point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->b = m8_cloud_runtime_get_hold(id);
    return SW_OK;
}

static sw_err_t set_cfg_bool(const char *id, const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_bool(id, in->b);

    if (strcmp(id, "cmd_station_stop_func") == 0)
    {
        return in->b ? inject_cmd(CMD_STOP_OPERATION) : inject_cmd(CMD_RESUME_OPERATION);
    }

    return SW_OK;
}

static sw_err_t get_cfg_bool(const char *id, point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->b = m8_cloud_runtime_get_bool(id);
    return SW_OK;
}

static sw_err_t water_on(water_path_mask_t mask)
{
    sw_err_t ret = inject_cmd(CMD_MANUAL_ACTUATOR);

    if (ret != SW_OK)
    {
        return ret;
    }

    return water_path_set(mask);
}

static sw_err_t water_off(void)
{
    return water_all_off();
}

static sw_err_t water_pre_on(void)
{
    return water_on(M8_WATER_PATH_MASK(M8_WATER_PATH_CURTAIN));
}

static sw_err_t apply_force_time(sw_err_t (*move_fn)(int, const hal_motor_move_spec_t *))
{
    hal_motor_move_spec_t spec;
    sw_err_t              ret;

    if (move_fn == NULL)
    {
        return SW_ERR_PARAM;
    }

    ret = require_manual_actuator();
    if (ret != SW_OK)
    {
        return ret;
    }

    memset(&spec, 0, sizeof(spec));
    spec.use_time       = true;
    spec.duration_ms    = M8_CLOUD_FORCE_MS;
    spec.use_soft_limit = false;
    spec.use_limit      = false;

    return move_fn(M8_MANUAL_DEFAULT_SPEED_GEAR, &spec);
}

static sw_err_t set_tri_cmd(const char *id, const point_value_t *in,
                             sw_err_t (*on_pos)(void), sw_err_t (*on_neg)(void),
                             sw_err_t (*on_stop)(void))
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_tri(id, in->i);

    if (in->i == M8_CLOUD_TRI_POS)
    {
        return (on_pos != NULL) ? on_pos() : SW_OK;
    }

    if (in->i == M8_CLOUD_TRI_NEG)
    {
        return (on_neg != NULL) ? on_neg() : SW_OK;
    }

    return (on_stop != NULL) ? on_stop() : SW_OK;
}

static int32_t read_gantry_tri(void)
{
    gantry_state_t st = gantry_state();

    if (st == GANTRY_STATE_MOVING_FWD)
    {
        return M8_CLOUD_TRI_POS;
    }

    if (st == GANTRY_STATE_MOVING_REV)
    {
        return M8_CLOUD_TRI_NEG;
    }

    return M8_CLOUD_TRI_STOP;
}

static int32_t read_lifter_tri(void)
{
    lift_state_t st = lift_state();

    if (st == LIFT_STATE_LIFTING)
    {
        return M8_CLOUD_TRI_NEG;
    }

    if (st == LIFT_STATE_LOWERING)
    {
        return M8_CLOUD_TRI_POS;
    }

    return M8_CLOUD_TRI_STOP;
}

static int32_t read_brush_tri(brush_id_t id)
{
    return (brush_state(id) == BRUSH_STATE_RUNNING) ? M8_CLOUD_TRI_POS : M8_CLOUD_TRI_STOP;
}

static int32_t read_wheel_lock_tri(void)
{
    rear_lock_state_t st = rear_lock_state();

    if (st == REAR_LOCK_STATE_LOCKING)
    {
        return M8_CLOUD_TRI_POS;
    }

    if (st == REAR_LOCK_STATE_RELEASING)
    {
        return M8_CLOUD_TRI_NEG;
    }

    return M8_CLOUD_TRI_STOP;
}

sw_err_t m8_cloud_set_cmd_safe_home(const point_value_t *in)
{
    if ((in == NULL) || !in->b)
    {
        return SW_OK;
    }

    return inject_cmd(CMD_HOME_DEVICE);
}


sw_err_t m8_cloud_set_cmd_stop(const point_value_t *in)
{
    if ((in == NULL) || !in->b)
    {
        return SW_OK;
    }

    (void)gantry_stop();
    (void)brush_stop_all();
    (void)lift_stop();
    (void)rear_lock_stop();
    (void)fan_stop();
    (void)water_all_off();
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_water_pre_rinse(const point_value_t *in)
{
    return set_hold_bool("cmd_water_pre_rinse", in, water_pre_on, water_off);
}

sw_err_t m8_cloud_get_cmd_water_pre_rinse(point_value_t *out)
{
    return get_hold_bool("cmd_water_pre_rinse", out);
}

sw_err_t m8_cloud_set_cmd_water_shampoo(const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_hold("cmd_water_shampoo", in->b);
    return in->b ? water_on(M8_WATER_PATH_MASK(M8_WATER_PATH_FOAM)) : water_off();
}

sw_err_t m8_cloud_get_cmd_water_shampoo(point_value_t *out)
{
    return get_hold_bool("cmd_water_shampoo", out);
}

sw_err_t m8_cloud_set_cmd_water_side_brush(const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_hold("cmd_water_side_brush", in->b);
    return in->b ? water_on(M8_WATER_PATH_MASK(M8_WATER_PATH_BRUSH)) : water_off();
}

sw_err_t m8_cloud_get_cmd_water_side_brush(point_value_t *out)
{
    return get_hold_bool("cmd_water_side_brush", out);
}

sw_err_t m8_cloud_set_cmd_water_foam_rinse(const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_hold("cmd_water_foam_rinse", in->b);
    return in->b ? water_on(M8_WATER_PATH_MASK(M8_WATER_PATH_HIGHPRES)) : water_off();
}

sw_err_t m8_cloud_get_cmd_water_foam_rinse(point_value_t *out)
{
    return get_hold_bool("cmd_water_foam_rinse", out);
}

sw_err_t m8_cloud_set_cmd_dryer_A(const point_value_t *in)
{
    sw_err_t ret;

    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    if (in->b)
    {
        ret = require_manual_actuator();
        if (ret != SW_OK)
        {
            return ret;
        }
        return fan_start();
    }

    return fan_stop();
}

sw_err_t m8_cloud_get_cmd_dryer_A(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->b = (fan_state() == FAN_STATE_RUNNING);
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_floodlight(const point_value_t *in)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_hold("cmd_floodlight", in->b);

    if ((io == NULL) || (io->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    return io->do_set(M8_IO_DO_FLOODLIGHT, in->b);
}

sw_err_t m8_cloud_get_cmd_floodlight(point_value_t *out)
{
    return get_hold_bool("cmd_floodlight", out);
}

sw_err_t m8_cloud_set_cmd_top_brush_rotation(const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    if (in->i == M8_CLOUD_TRI_POS)
    {
        return m8_manual_brush_start(M8_BRUSH_TOP, M8_MANUAL_DEFAULT_SPEED_GEAR);
    }

    if (in->i == M8_CLOUD_TRI_NEG)
    {
        LOG_WARN("m8_cloud_cmd: top brush reverse not supported");
        return SW_OK;
    }

    return m8_manual_brush_stop(M8_BRUSH_TOP);
}

sw_err_t m8_cloud_get_cmd_top_brush_rotation(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->i = read_brush_tri(M8_BRUSH_TOP);
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_lifter_move(const point_value_t *in)
{
    return set_tri_cmd("cmd_lifter_move", in,
                       lift_down_wrap,
                       lift_up_wrap,
                       (sw_err_t (*)(void))lift_stop);
}

sw_err_t m8_cloud_get_cmd_lifter_move(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->i = read_lifter_tri();
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_side_brush_rotation(const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    if (in->i == M8_CLOUD_TRI_POS)
    {
        return m8_manual_brush_start(M8_BRUSH_SIDE, M8_MANUAL_DEFAULT_SPEED_GEAR);
    }

    if (in->i == M8_CLOUD_TRI_NEG)
    {
        LOG_WARN("m8_cloud_cmd: side brush reverse not supported");
        return SW_OK;
    }

    return m8_manual_brush_stop(M8_BRUSH_SIDE);
}

sw_err_t m8_cloud_get_cmd_side_brush_rotation(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->i = read_brush_tri(M8_BRUSH_SIDE);
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_gantry_move(const point_value_t *in)
{
    return set_tri_cmd("cmd_gantry_move", in,
                       gantry_fwd_wrap,
                       gantry_rev_wrap,
                       (sw_err_t (*)(void))m8_manual_gantry_stop);
}

sw_err_t m8_cloud_get_cmd_gantry_move(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->i = read_gantry_tri();
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_wheel_lock(const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_tri("cmd_wheel_lock", in->i);

    if (in->i == M8_CLOUD_TRI_POS)
    {
        sw_err_t ret = require_manual_actuator();

        if (ret != SW_OK)
        {
            return ret;
        }
        return rear_lock_lock(M8_MANUAL_DEFAULT_SPEED_GEAR, NULL);
    }

    if (in->i == M8_CLOUD_TRI_NEG)
    {
        sw_err_t ret = require_manual_actuator();

        if (ret != SW_OK)
        {
            return ret;
        }
        return rear_lock_release(M8_MANUAL_DEFAULT_SPEED_GEAR, NULL);
    }

    return rear_lock_stop();
}

sw_err_t m8_cloud_get_cmd_wheel_lock(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->i = read_wheel_lock_tri();
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_station_stop_func(const point_value_t *in)
{
    return set_cfg_bool("cmd_station_stop_func", in);
}

sw_err_t m8_cloud_get_cmd_station_stop_func(point_value_t *out)
{
    return get_cfg_bool("cmd_station_stop_func", out);
}

sw_err_t m8_cloud_set_cmd_no_air_drying(const point_value_t *in)
{
    return set_cfg_bool("cmd_no_air_drying", in);
}

sw_err_t m8_cloud_get_cmd_no_air_drying(point_value_t *out)
{
    return get_cfg_bool("cmd_no_air_drying", out);
}

sw_err_t m8_cloud_set_cmd_config_sensor_water(const point_value_t *in)
{
    return set_cfg_bool("cmd_config_sensor_water", in);
}

sw_err_t m8_cloud_get_cmd_config_sensor_water(point_value_t *out)
{
    return get_cfg_bool("cmd_config_sensor_water", out);
}

sw_err_t m8_cloud_set_cmd_config_sensor_fl_collision(const point_value_t *in)
{
    return set_cfg_bool("cmd_config_sensor_fl_collision", in);
}

sw_err_t m8_cloud_get_cmd_config_sensor_fl_collision(point_value_t *out)
{
    return get_cfg_bool("cmd_config_sensor_fl_collision", out);
}

sw_err_t m8_cloud_set_cmd_config_sensor_fr_collision(const point_value_t *in)
{
    return set_cfg_bool("cmd_config_sensor_fr_collision", in);
}

sw_err_t m8_cloud_get_cmd_config_sensor_fr_collision(point_value_t *out)
{
    return get_cfg_bool("cmd_config_sensor_fr_collision", out);
}

sw_err_t m8_cloud_set_cmd_config_sensor_high_limit(const point_value_t *in)
{
    return set_cfg_bool("cmd_config_sensor_high_limit", in);
}

sw_err_t m8_cloud_get_cmd_config_sensor_high_limit(point_value_t *out)
{
    return get_cfg_bool("cmd_config_sensor_high_limit", out);
}

sw_err_t m8_cloud_set_cmd_gantry_force_move_backward(const point_value_t *in)
{
    if ((in == NULL) || !in->b)
    {
        return SW_OK;
    }

    return apply_force_time(gantry_move_rev);
}

sw_err_t m8_cloud_set_cmd_gantry_force_move_forward(const point_value_t *in)
{
    if ((in == NULL) || !in->b)
    {
        return SW_OK;
    }

    return apply_force_time(gantry_move_fwd);
}

sw_err_t m8_cloud_set_cmd_lifter_force_down(const point_value_t *in)
{
    if ((in == NULL) || !in->b)
    {
        return SW_OK;
    }

    return apply_force_time(lift_down);
}

sw_err_t m8_cloud_set_cmd_lifter_force_up(const point_value_t *in)
{
    if ((in == NULL) || !in->b)
    {
        return SW_OK;
    }

    return apply_force_time(lift_up);
}
