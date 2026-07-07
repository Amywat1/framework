/**
 * @file    m8_point_table.c
 * @brief   M8 云端点位表实现
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    本文件是新增/删除/修改云端点位的唯一改动点：每个点位一个 get/set
 *          静态函数 + 数组中一行登记。通用 JSON 引擎见
 *          framework/common/point_table/point_table.h。当前落地阿里云控制台物模型
 *          （M8物模型点位.xlsx）中数据源/执行入口已明确的点位。
 */

#include "projects/m8/adapters/cloud/m8_point_table.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/config/m8_signal_table.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/application/orchestrators/report_aggregator.h"
#include "framework/common/sw_version.h"
#include "framework/common/log.h"
#include <string.h>

static sw_err_t get_sts_stopping(point_value_t *out)
{
    dev_state_t st = dev_ctx_get_device_state();

    out->b = (st == DEV_STATE_STOP) || (st == DEV_STATE_INIT) || (st == DEV_STATE_FAULT);
    return SW_OK;
}

static sw_err_t get_sts_standby(point_value_t *out)
{
    out->b = (dev_ctx_get_device_state() == DEV_STATE_IDLE);
    return SW_OK;
}

static sw_err_t get_sts_dev_warning(point_value_t *out)
{
    device_context_t ctx = dev_ctx_snapshot();

    out->b = ctx.has_alarm;
    return SW_OK;
}

static sw_err_t inject_cmd(cmd_type_t type)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t                     cmd;

    if (cp == NULL)
    {
        LOG_WARN("m8_point_table: command_port not registered");
        return SW_ERR_NOT_INIT;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    return cp->inject(&cmd);
}

static sw_err_t get_cmd_pulse(point_value_t *out)
{
    out->b = false;
    return SW_OK;
}

static sw_err_t set_cmd_home(const point_value_t *in)
{
    return in->b ? inject_cmd(CMD_HOME_DEVICE) : SW_OK;
}

static sw_err_t set_cmd_custom_stop(const point_value_t *in)
{
    return in->b ? inject_cmd(CMD_STOP_WASH) : SW_OK;
}

static sw_err_t set_cmd_sync(const point_value_t *in)
{
    if (in->b)
    {
        report_aggregator_request_resync();
    }
    return SW_OK;
}

static sw_err_t get_sts_emergency(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_ESTOP);
    return SW_OK;
}

static sw_err_t get_sts_gantry_back_limit(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_GANTRY_REV_LIM);
    return SW_OK;
}

static sw_err_t get_sts_gantry_front_limit(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM);
    return SW_OK;
}

static sw_err_t get_sts_lifter_up_limit(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_LIFT_UP_LIM);
    return SW_OK;
}

static sw_err_t get_sts_lifter_down_limit(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM);
    return SW_OK;
}

static sw_err_t get_sts_wheel_lock_zero(point_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_REAR_LOCK_HOME);
    return SW_OK;
}

static sw_err_t get_sts_gantry_position(point_value_t *out)
{
    device_context_t ctx = dev_ctx_snapshot();

    out->i = ctx.gantry_pos;
    return SW_OK;
}

static sw_err_t get_sts_firmware_version(point_value_t *out)
{
    strncpy(out->s, SW_VERSION_STR, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

static sw_err_t get_sts_device_model(point_value_t *out)
{
    strncpy(out->s, SW_PRODUCT_NAME, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

static bool s_communication_test_val = false;

static sw_err_t set_cmd_communication_test(const point_value_t *in)
{
    s_communication_test_val = in->b;
    return SW_OK;
}

static sw_err_t get_sts_communication_test(point_value_t *out)
{
    out->b = s_communication_test_val;
    return SW_OK;
}

static const point_table_entry_t s_m8_points[] = {
    { "sts_stopping",           POINT_TYPE_BOOL,   get_sts_stopping,           NULL },
    { "sts_standby",            POINT_TYPE_BOOL,   get_sts_standby,            NULL },
    { "sts_dev_warning",        POINT_TYPE_BOOL,   get_sts_dev_warning,        NULL },
    { "cmd_home",               POINT_TYPE_BOOL,   get_cmd_pulse,              set_cmd_home },
    { "cmd_custom_stop",        POINT_TYPE_BOOL,   get_cmd_pulse,              set_cmd_custom_stop },
    { "cmd_sync",               POINT_TYPE_BOOL,   get_cmd_pulse,              set_cmd_sync },
    { "sts_emergency",          POINT_TYPE_BOOL,   get_sts_emergency,          NULL },
    { "sts_gantry_back_limit",  POINT_TYPE_BOOL,   get_sts_gantry_back_limit,  NULL },
    { "sts_gantry_front_limit", POINT_TYPE_BOOL,   get_sts_gantry_front_limit, NULL },
    { "sts_lifter_up_limit",    POINT_TYPE_BOOL,   get_sts_lifter_up_limit,    NULL },
    { "sts_lifter_down_limit",  POINT_TYPE_BOOL,   get_sts_lifter_down_limit,  NULL },
    { "sts_wheel_lock_zero",    POINT_TYPE_BOOL,   get_sts_wheel_lock_zero,    NULL },
    { "sts_gantry_position",    POINT_TYPE_INT,    get_sts_gantry_position,    NULL },
    { "sts_firmware_version",   POINT_TYPE_STRING, get_sts_firmware_version,   NULL },
    { "sts_device_model",       POINT_TYPE_STRING, get_sts_device_model,       NULL },
    { "cmd_communication_test", POINT_TYPE_BOOL,   NULL,                       set_cmd_communication_test },
    { "sts_communication_test", POINT_TYPE_BOOL,   get_sts_communication_test, NULL },
};

const point_table_entry_t *m8_point_table(size_t *out_count)
{
    if (out_count != NULL)
    {
        *out_count = sizeof(s_m8_points) / sizeof(s_m8_points[0]);
    }
    return s_m8_points;
}

sw_err_t m8_cloud_report_json(const cloud_report_payload_t *p,
                               char *buf, size_t buf_size)
{
    size_t                     count;
    const point_table_entry_t *entries = m8_point_table(&count);

    (void)p;

    return point_table_to_json(entries, count, buf, buf_size);
}

void m8_cloud_command_dispatch(const char *json_str)
{
    size_t                     count;
    const point_table_entry_t *entries = m8_point_table(&count);

    point_table_from_json(entries, count, json_str);
}
