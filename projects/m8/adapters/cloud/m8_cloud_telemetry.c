/**
 * @file    m8_cloud_telemetry.c
 * @brief   M8 云端只读遥测点位 handler 实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_telemetry.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/config/m8_signal_table.h"
#include "framework/common/sw_version.h"
#include <string.h>

sw_err_t m8_cloud_get_sts_stopping(point_value_t *out)
{
    dev_state_t st = dev_ctx_get_device_state();

    out->b = (st == DEV_STATE_STOP) || (st == DEV_STATE_INIT) || (st == DEV_STATE_FAULT);
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_standby(point_value_t *out)
{
    out->b = (dev_ctx_get_device_state() == DEV_STATE_IDLE);
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_dev_warning(point_value_t *out)
{
    device_context_t ctx = dev_ctx_snapshot();

    out->b = ctx.has_alarm;
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_emergency(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_ESTOP);
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_gantry_back_limit(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_GANTRY_REV_LIM);
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_gantry_front_limit(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM);
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_lifter_up_limit(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_LIFT_UP_LIM);
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_lifter_down_limit(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM);
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_wheel_lock_zero(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_REAR_LOCK_HOME);
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_gantry_position(point_value_t *out)
{
    device_context_t ctx = dev_ctx_snapshot();

    out->i = ctx.gantry_pos;
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_firmware_version(point_value_t *out)
{
    strncpy(out->s, SW_VERSION_STR, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_device_model(point_value_t *out)
{
    strncpy(out->s, SW_PRODUCT_NAME, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}
