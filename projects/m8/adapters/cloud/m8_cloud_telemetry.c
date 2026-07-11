/**
 * @file    m8_cloud_telemetry.c
 * @brief   M8 云端只读遥测点位 handler 实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "projects/m8/adapters/cloud/m8_cloud_telemetry.h"
#include "projects/m8/adapters/cloud/m8_cloud_runtime.h"
#include "framework/domain/telemetry/snapshot/operational_snapshot.h"
#include "framework/domain/telemetry/snapshot/safety_snapshot.h"
#include "framework/domain/device_control/model/device_state.h"
#include "projects/m8/domain/mechanism/gantry.h"
#include "projects/m8/domain/mechanism/m8_top_brush_lift.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/config/m8_signal_table.h"
#include "projects/m8/config/m8_io_pins.h"
#include "framework/common/sw_version.h"
#include <string.h>

static sw_err_t get_bool(bool value, point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->b = value;
    return SW_OK;
}

static sw_err_t get_int(int32_t value, point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->i = value;
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_stopping(point_value_t *out)
{
    return get_bool(operational_snapshot_is_stopping(), out);
}

sw_err_t m8_cloud_get_sts_standby(point_value_t *out)
{
    return get_bool(operational_snapshot_is_standby(), out);
}

sw_err_t m8_cloud_get_sts_homing(point_value_t *out)
{
    return get_bool(false, out);
}

sw_err_t m8_cloud_get_sts_normal_homing(point_value_t *out)
{
    (void)out;
    return get_bool(false, out);
}

sw_err_t m8_cloud_get_sts_custom_stopping(point_value_t *out)
{
    return get_bool(m8_cloud_runtime_is_custom_stopping(), out);
}

sw_err_t m8_cloud_get_sts_warn_homing(point_value_t *out)
{
    operational_snapshot_t snap = operational_snapshot_get();

    return get_bool(snap.mode == OP_MODE_RECOVERING, out);
}

sw_err_t m8_cloud_get_sts_dev_warning(point_value_t *out)
{
    return get_bool(safety_snapshot_is_warning_active(), out);
}

sw_err_t m8_cloud_get_sts_park_state(point_value_t *out)
{
    bool parked = false;

    if (operational_snapshot_get().mode == OP_MODE_IDLE)
    {
        parked = m8_cloud_di_active(M8_IO_DI_FRONT_WHEEL_LIMIT, false);
    }

    return get_bool(parked, out);
}

sw_err_t m8_cloud_get_sts_gantry_position(point_value_t *out)
{
    return get_int((int32_t)gantry_position(), out);
}

sw_err_t m8_cloud_get_sts_lifter_position(point_value_t *out)
{
    return get_int((int32_t)lift_position(), out);
}

sw_err_t m8_cloud_get_sts_detect_height(point_value_t *out)
{
    return get_int(0, out);
}

sw_err_t m8_cloud_get_sts_firmware_version(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    strncpy(out->s, SW_VERSION_STR, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_device_model(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    strncpy(out->s, SW_PRODUCT_NAME, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_wash_today_counts(point_value_t *out)
{
    return get_int(m8_cloud_runtime_get_wash_today(), out);
}

sw_err_t m8_cloud_get_sts_wash_start_counts(point_value_t *out)
{
    return get_int(m8_cloud_runtime_get_wash_start(), out);
}

sw_err_t m8_cloud_get_sts_wash_complete_counts(point_value_t *out)
{
    return get_int(m8_cloud_runtime_get_wash_complete(), out);
}

sw_err_t m8_cloud_get_sts_wash_failed_counts(point_value_t *out)
{
    return get_int(m8_cloud_runtime_get_wash_failed(), out);
}

sw_err_t m8_cloud_get_sts_port_number(point_value_t *out)
{
    return get_int(m8_cloud_runtime_get_port_number(), out);
}

sw_err_t m8_cloud_get_sts_emergency(point_value_t *out)
{
    return get_bool(m8_signal_is_active(M8_SIG_ESTOP), out);
}

sw_err_t m8_cloud_get_sts_gantry_back_limit(point_value_t *out)
{
    return get_bool(m8_signal_is_active(M8_SIG_GANTRY_REV_LIM), out);
}

sw_err_t m8_cloud_get_sts_gantry_front_limit(point_value_t *out)
{
    return get_bool(m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM), out);
}

sw_err_t m8_cloud_get_sts_lifter_up_limit(point_value_t *out)
{
    return get_bool(m8_signal_is_active(M8_SIG_LIFT_UP_LIM), out);
}

sw_err_t m8_cloud_get_sts_lifter_down_limit(point_value_t *out)
{
    return get_bool(m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM), out);
}

sw_err_t m8_cloud_get_sts_signal_stop(point_value_t *out)
{
    return get_bool(m8_cloud_di_active(M8_IO_DI_FRONT_WHEEL_LIMIT, false), out);
}

sw_err_t m8_cloud_get_sts_wheel_lock_arrive(point_value_t *out)
{
    return get_bool(m8_cloud_di_active(M8_IO_DI_REAR_WHEEL_LOCK, false), out);
}

sw_err_t m8_cloud_get_sts_wheel_lock_zero(point_value_t *out)
{
    return get_bool(m8_signal_is_active(M8_SIG_REAR_LOCK_HOME), out);
}

sw_err_t m8_cloud_get_sts_water_press(point_value_t *out)
{
    return get_bool(false, out);
}

sw_err_t m8_cloud_get_sts_top_brush_crl_limit(point_value_t *out)
{
    return get_bool(m8_cloud_di_active(M8_IO_DI_SWITCH_TOP_BRUSH, false), out);
}

sw_err_t m8_cloud_get_sts_top_brush_collision(point_value_t *out)
{
    return get_bool(m8_cloud_di_active(M8_IO_DI_TOP_BRUSH_COLLISION, false), out);
}

sw_err_t m8_cloud_get_sts_signal_height(point_value_t *out)
{
    return get_bool(false, out);
}

sw_err_t m8_cloud_get_sts_left_person_collision_strip(point_value_t *out)
{
    return get_bool(m8_cloud_di_inverted(M8_IO_DI_BUMPER_LEFT, false), out);
}

sw_err_t m8_cloud_get_sts_right_person_collision_strip(point_value_t *out)
{
    return get_bool(m8_cloud_di_inverted(M8_IO_DI_BUMPER_RIGHT, false), out);
}

sw_err_t m8_cloud_get_sts_front_left_collision(point_value_t *out)
{
    return get_bool(m8_cloud_di_inverted(M8_IO_DI_BUMPER_ROD_LEFT, false), out);
}

sw_err_t m8_cloud_get_sts_front_right_collision(point_value_t *out)
{
    return get_bool(m8_cloud_di_inverted(M8_IO_DI_BUMPER_ROD_RIGHT, false), out);
}

sw_err_t m8_cloud_get_sts_I1_IO_change(point_value_t *out)
{
    return m8_cloud_runtime_get_io_str("sts_I1_IO_change", out);
}

sw_err_t m8_cloud_get_sts_I1_IO(point_value_t *out)
{
    return m8_cloud_runtime_get_io_str("sts_I1_IO", out);
}

sw_err_t m8_cloud_get_sts_O1_IO_change(point_value_t *out)
{
    return m8_cloud_runtime_get_io_str("sts_O1_IO_change", out);
}

sw_err_t m8_cloud_get_sts_O1_IO(point_value_t *out)
{
    return m8_cloud_runtime_get_io_str("sts_O1_IO", out);
}
