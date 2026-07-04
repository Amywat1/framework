/**
 * @file    m8_tsl_table.c
 * @brief   M8 云端物模型点位表实现
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    本文件是新增/删除/修改物模型点位的唯一改动点：每个点位一个 get/set
 *          静态函数 + 数组中一行登记。通用序列化/分发引擎见
 *          framework/adapters/outbound/cloud/tsl/tsl_point.h，不在本文件重复实现。当前落地阿里云
 *          控制台物模型（M8物模型点位.xlsx）中数据源/执行入口已明确的点位；
 *          其余点位（需要新执行器能力、新传感器绑定、计数持久化等）见开发
 *          计划，暂不在此表登记。
 */

#include "projects/m8/adapters/cloud/m8_tsl_table.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/config/m8_signal_table.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/application/orchestrators/report_aggregator.h"
#include "framework/common/sw_version.h"
#include "framework/common/log.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * 洗车进程（只读，派生自 dev_ctx）
 * ------------------------------------------------------------------------- */
static sw_err_t get_sts_stopping(tsl_value_t *out)
{
    dev_state_t st = dev_ctx_get_device_state();

    out->b = (st == DEV_STATE_STOP) || (st == DEV_STATE_INIT) || (st == DEV_STATE_FAULT);
    return SW_OK;
}

static sw_err_t get_sts_standby(tsl_value_t *out)
{
    out->b = (dev_ctx_get_device_state() == DEV_STATE_IDLE);
    return SW_OK;
}

static sw_err_t get_sts_dev_warning(tsl_value_t *out)
{
    device_context_t ctx = dev_ctx_snapshot();

    out->b = ctx.has_alarm;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 手动控制（命令点位，复用 command_port / report_aggregator）
 * ------------------------------------------------------------------------- */
static sw_err_t inject_cmd(cmd_type_t type)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t                     cmd;

    if (cp == NULL)
    {
        LOG_WARN("m8_tsl: command_port not registered");
        return SW_ERR_NOT_INIT;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    return cp->inject(&cmd);
}

/** 命令类点位统一回显：恒为 0（下发成功后清零） */
static sw_err_t get_cmd_pulse(tsl_value_t *out)
{
    out->b = false;
    return SW_OK;
}

static sw_err_t set_cmd_home(const tsl_value_t *in)
{
    return in->b ? inject_cmd(CMD_HOME_DEVICE) : SW_OK;
}

static sw_err_t set_cmd_custom_stop(const tsl_value_t *in)
{
    return in->b ? inject_cmd(CMD_STOP_WASH) : SW_OK;
}

static sw_err_t set_cmd_sync(const tsl_value_t *in)
{
    if (in->b)
    {
        report_aggregator_request_resync();
    }
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 传感器检测（只读，复用 m8_signal_is_active）
 * ------------------------------------------------------------------------- */
static sw_err_t get_sts_emergency(tsl_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_ESTOP);
    return SW_OK;
}

static sw_err_t get_sts_gantry_back_limit(tsl_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_GANTRY_REV_LIM);
    return SW_OK;
}

static sw_err_t get_sts_gantry_front_limit(tsl_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM);
    return SW_OK;
}

static sw_err_t get_sts_lifter_up_limit(tsl_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_LIFT_UP_LIM);
    return SW_OK;
}

static sw_err_t get_sts_lifter_down_limit(tsl_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM);
    return SW_OK;
}

static sw_err_t get_sts_wheel_lock_zero(tsl_value_t *out)
{
    out->b = m8_signal_is_active(M8_SIG_REAR_LOCK_HOME);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 监测数据（只读）
 * ------------------------------------------------------------------------- */
static sw_err_t get_sts_gantry_position(tsl_value_t *out)
{
    device_context_t ctx = dev_ctx_snapshot();

    out->i = ctx.gantry_pos;
    return SW_OK;
}

static sw_err_t get_sts_firmware_version(tsl_value_t *out)
{
    strncpy(out->s, SW_VERSION_STR, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

static sw_err_t get_sts_device_model(tsl_value_t *out)
{
    strncpy(out->s, SW_PRODUCT_NAME, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 测试组（通讯回环自检）
 * ------------------------------------------------------------------------- */
static bool s_communication_test_val = false;

static sw_err_t set_cmd_communication_test(const tsl_value_t *in)
{
    s_communication_test_val = in->b;
    return SW_OK;
}

static sw_err_t get_sts_communication_test(tsl_value_t *out)
{
    out->b = s_communication_test_val;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 点位表
 * ------------------------------------------------------------------------- */
static const tsl_point_t s_m8_tsl_points[] = {
    /* 洗车进程 */
    { "sts_stopping",           TSL_BOOL,   get_sts_stopping,           NULL },
    { "sts_standby",            TSL_BOOL,   get_sts_standby,            NULL },
    { "sts_dev_warning",        TSL_BOOL,   get_sts_dev_warning,        NULL },

    /* 手动控制 */
    { "cmd_home",               TSL_BOOL,   get_cmd_pulse,              set_cmd_home },
    { "cmd_custom_stop",        TSL_BOOL,   get_cmd_pulse,              set_cmd_custom_stop },
    { "cmd_sync",               TSL_BOOL,   get_cmd_pulse,              set_cmd_sync },

    /* 传感器检测 */
    { "sts_emergency",          TSL_BOOL,   get_sts_emergency,          NULL },
    { "sts_gantry_back_limit",  TSL_BOOL,   get_sts_gantry_back_limit,  NULL },
    { "sts_gantry_front_limit", TSL_BOOL,   get_sts_gantry_front_limit, NULL },
    { "sts_lifter_up_limit",    TSL_BOOL,   get_sts_lifter_up_limit,    NULL },
    { "sts_lifter_down_limit",  TSL_BOOL,   get_sts_lifter_down_limit,  NULL },
    { "sts_wheel_lock_zero",    TSL_BOOL,   get_sts_wheel_lock_zero,    NULL },

    /* 监测数据 */
    { "sts_gantry_position",    TSL_INT,    get_sts_gantry_position,    NULL },
    { "sts_firmware_version",   TSL_STRING, get_sts_firmware_version,   NULL },
    { "sts_device_model",       TSL_STRING, get_sts_device_model,       NULL },

    /* 测试组 */
    { "cmd_communication_test", TSL_BOOL,   NULL,                       set_cmd_communication_test },
    { "sts_communication_test", TSL_BOOL,   get_sts_communication_test, NULL },
};

const tsl_point_t *m8_tsl_table(size_t *out_count)
{
    if (out_count != NULL)
    {
        *out_count = sizeof(s_m8_tsl_points) / sizeof(s_m8_tsl_points[0]);
    }
    return s_m8_tsl_points;
}

/* -------------------------------------------------------------------------
 * snack_cloud_adapter.h 固定函数指针形状的适配（供 wiring.c / project_hooks.c 注入）
 * ------------------------------------------------------------------------- */
sw_err_t m8_build_report_json(const cloud_report_payload_t *p,
                               char *buf, size_t buf_size)
{
    size_t              count;
    const tsl_point_t  *points = m8_tsl_table(&count);

    (void)p; /* 各点位 get() 自行读取 dev_ctx/传感器，不依赖聚合后的定长结构体 */

    return tsl_build_report_json(points, count, buf, buf_size);
}

void m8_tsl_command_dispatch(const char *json_str)
{
    size_t              count;
    const tsl_point_t  *points = m8_tsl_table(&count);

    tsl_command_dispatch(points, count, json_str);
}
