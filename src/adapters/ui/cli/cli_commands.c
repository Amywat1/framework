/**
 * @file    cli_commands.c
 * @brief   CLI 命令处理实现（device / safety / param / diag 四个域）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    替代旧架构 app_debug_ctl / alarm_debug_ctl / bsp_debug_ctl / param_debug_ctl。
 *          各域功能：
 *            device  — 设备状态查询与命令下发（对应旧 app 域）
 *            safety  — 报警/急停状态查询与复位（对应旧 alarm 域）
 *            param   — 参数读写持久化（保留原有接口）
 *            diag    — 硬件直控调试（对应旧 bsp 域）
 */

#include "adapters/ui/cli/cli_commands.h"
#include "application/usecases/start_wash.h"
#include "application/usecases/stop_wash.h"
#include "application/usecases/reset_fault.h"
#include "application/usecases/home_device.h"
#include "service/dev_ctx/dev_ctx.h"
#include "service/svc_param/svc_param.h"
#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "ports/hal/hal_io_port.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * device 命令域
 * ------------------------------------------------------------------------- */
int device_cmd_handler(char *subcmd, char *p1, char *p2)
{
    (void)p2;
    if (subcmd == NULL) { return 0; }

    if (strcmp(subcmd, "status") == 0)
    {
        device_context_t ctx = dev_ctx_snapshot();
        LOG_INFO("device: state=%d safety=%d step=%d mode=%d alarm=%d cloud=%d",
                 (int)ctx.device_state, (int)ctx.safety_state,
                 (int)ctx.wash_step, (int)ctx.wash_mode,
                 (int)ctx.has_error_alarm, (int)ctx.cloud_connected);
        return 1;
    }
    if (strcmp(subcmd, "order") == 0)
    {
        wash_mode_t mode = WASH_MODE_STANDARD;
        if (p1 != NULL) { mode = (wash_mode_t)atoi(p1); }
        sw_err_t ret = start_wash(mode);
        LOG_INFO("device order mode=%d ret=%d", (int)mode, (int)ret);
        return 1;
    }
    if (strcmp(subcmd, "stop") == 0)
    {
        (void)stop_wash();
        LOG_INFO("device stop wash");
        return 1;
    }
    if (strcmp(subcmd, "stop-op") == 0)
    {
        (void)stop_operation();
        LOG_INFO("device stop operation");
        return 1;
    }
    if (strcmp(subcmd, "resume") == 0)
    {
        (void)event_publish(EVT_CMD_RESUME_OPERATION, 0U);
        LOG_INFO("device resume");
        return 1;
    }
    if (strcmp(subcmd, "reset") == 0)
    {
        sw_err_t ret = reset_fault();
        LOG_INFO("device reset ret=%d", (int)ret);
        return 1;
    }
    if (strcmp(subcmd, "home") == 0)
    {
        (void)home_device();
        LOG_INFO("device home");
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * safety 命令域
 * ------------------------------------------------------------------------- */
int safety_cmd_handler(char *subcmd, char *p1, char *p2)
{
    (void)p1;
    (void)p2;
    if (subcmd == NULL) { return 0; }

    if (strcmp(subcmd, "status") == 0)
    {
        LOG_INFO("safety: has_error=%d has_warning=%d",
                 (int)alarm_core_has_error(), (int)alarm_core_has_warning());
        /* 显示已知报警码状态 */
        static const uint16_t codes[] = {
            ALARM_CODE_ESTOP,
            ALARM_CODE_GANTRY_FWD_LIM, ALARM_CODE_GANTRY_REV_LIM,
            ALARM_CODE_VFD_GANTRY, ALARM_CODE_VFD_BRUSH,
            ALARM_CODE_MODBUS_GANTRY, ALARM_CODE_MODBUS_BRUSH,
            ALARM_CODE_MQTT_OFFLINE,
        };
        for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        {
            if (alarm_core_is_active(codes[i]))
            {
                LOG_INFO("  ACTIVE: code=%u", (unsigned)codes[i]);
            }
        }
        return 1;
    }
    if (strcmp(subcmd, "reset") == 0)
    {
        alarm_core_manual_reset();
        LOG_INFO("safety: manual reset done");
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * param 命令域
 * ------------------------------------------------------------------------- */
int param_cmd_handler(char *subcmd, char *p1, char *p2)
{
    if (subcmd == NULL) { return 0; }

    if (strcmp(subcmd, "get") == 0)
    {
        if (p1 == NULL) { return 0; }
        char buf[64] = {0};
        int  ival    = svc_param_get_int(p1, INT32_MIN);
        if (ival != INT32_MIN)
        {
            LOG_INFO("param get: %s = %d", p1, ival);
        }
        else
        {
            (void)svc_param_get_str(p1, buf, sizeof(buf), "(not found)");
            LOG_INFO("param get: %s = %s", p1, buf);
        }
        return 1;
    }
    if ((strcmp(subcmd, "set") == 0) && (p1 != NULL) && (p2 != NULL))
    {
        (void)svc_param_set_int(p1, atoi(p2));
        LOG_INFO("param set: %s = %s", p1, p2);
        return 1;
    }
    if (strcmp(subcmd, "save") == 0)
    {
        sw_err_t ret = svc_param_save();
        LOG_INFO("param save: %s", (ret == SW_OK) ? "ok" : "failed");
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * diag 命令域（硬件直控，替代旧 bsp 域）
 * ------------------------------------------------------------------------- */
int diag_cmd_handler(char *subcmd, char *p1, char *p2)
{
    if (subcmd == NULL) { return 0; }

    if ((strcmp(subcmd, "do") == 0) && (p1 != NULL) && (p2 != NULL))
    {
        const hal_io_ops_t *ops = hal_io_get_ops();
        if (ops != NULL)
        {
            int pin = atoi(p1);
            int val = atoi(p2);
            sw_err_t ret = ops->do_set(pin, val != 0);
            LOG_INFO("diag do pin=%d val=%d ret=%d", pin, val, (int)ret);
        }
        return 1;
    }
    if ((strcmp(subcmd, "di") == 0) && (p1 != NULL))
    {
        const hal_io_ops_t *ops = hal_io_get_ops();
        if (ops != NULL)
        {
            int  pin = atoi(p1);
            bool val = ops->di_read(pin);
            LOG_INFO("diag di pin=%d = %d", pin, (int)val);
        }
        return 1;
    }
    if (strcmp(subcmd, "state") == 0)
    {
        device_context_t ctx = dev_ctx_snapshot();
        LOG_INFO("diag state: dev=%d safety=%d step=%d alarm=%d",
                 (int)ctx.device_state, (int)ctx.safety_state,
                 (int)ctx.wash_step, (int)ctx.has_error_alarm);
        return 1;
    }
    return 0;
}
