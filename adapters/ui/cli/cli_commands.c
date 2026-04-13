/**
 * @file    cli_commands.c
 * @brief   CLI 命令处理实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    提供统一的 CLI 命令入口：
 *          - device：设备状态查询与命令下发
 *          - safety：报警与急停状态查询、复位
 *          - param：参数读写与持久化
 *          - diag：底层硬件诊断
 */

#include "adapters/ui/cli/cli_commands.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "application/usecases/home_device.h"
#include "application/usecases/reset_fault.h"
#include "application/usecases/start_wash.h"
#include "application/usecases/stop_wash.h"
#include "common/event_types.h"
#include "common/log.h"
#include "core/event_bus/event_bus.h"
#include "domain/model/alarm_code.h"
#include "domain/safety/alarm_core.h"
#include "driver/drv_io.h"
#include "ports/hal/hal_io_port.h"
#include "service/dev_ctx/dev_ctx.h"
#include "service/svc_param/svc_param.h"

static void log_diag_do_usage(void)
{
    LOG_INFO("usage: diag do <DO_NAME> <0|1>");
    LOG_INFO("example: diag do DO_WATER_PUMP 1");
}

static void log_diag_di_usage(void)
{
    LOG_INFO("usage: diag di <DI_NAME>");
    LOG_INFO("example: diag di DI_ESTOP");
}

static void log_diag_io_usage(void)
{
    LOG_INFO("usage: diag io [BOARD_ID]");
    LOG_INFO("example: diag io 1");
}

/* -------------------------------------------------------------------------
 * device 命令域
 * ------------------------------------------------------------------------- */
int device_cmd_handler(char *subcmd, char *p1, char *p2)
{
    (void)p2;

    if (subcmd == NULL)
    {
        return 0;
    }

    if (strcmp(subcmd, "status") == 0)
    {
        device_context_t ctx = dev_ctx_snapshot();
        LOG_INFO("device: state=%d safety=%d step=%d mode=%d alarm=%d cloud=%d",
                 (int)ctx.device_state,
                 (int)ctx.safety_state,
                 (int)ctx.wash_step,
                 (int)ctx.wash_mode,
                 (int)ctx.has_error_alarm,
                 (int)ctx.cloud_connected);
        return 1;
    }

    if (strcmp(subcmd, "order") == 0)
    {
        wash_mode_t mode = WASH_MODE_STANDARD;
        if (p1 != NULL)
        {
            mode = (wash_mode_t)atoi(p1);
        }

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

    if (subcmd == NULL)
    {
        return 0;
    }

    if (strcmp(subcmd, "status") == 0)
    {
        LOG_INFO("safety: has_error=%d has_warning=%d",
                 (int)alarm_core_has_error(),
                 (int)alarm_core_has_warning());

        static const uint16_t codes[] = {
            ALARM_CODE_ESTOP,
            ALARM_CODE_GANTRY_FWD_LIM, ALARM_CODE_GANTRY_REV_LIM,
            ALARM_CODE_VFD_GANTRY, ALARM_CODE_VFD_BRUSH,
            ALARM_CODE_GANTRY_CURRENT, ALARM_CODE_BRUSH_CURRENT,
            ALARM_CODE_MODBUS_GANTRY, ALARM_CODE_MODBUS_BRUSH,
            ALARM_CODE_MQTT_OFFLINE,
        };

        for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
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
    if (subcmd == NULL)
    {
        return 0;
    }

    if (strcmp(subcmd, "get") == 0)
    {
        char buf[64] = {0};
        int  ival    = 0;

        if (p1 == NULL)
        {
            return 0;
        }

        ival = svc_param_get_int(p1, INT32_MIN);
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
 * diag 命令域
 * ------------------------------------------------------------------------- */
int diag_cmd_handler(char *subcmd, char *p1, char *p2)
{
    if (subcmd == NULL)
    {
        return 0;
    }

    if (strcmp(subcmd, "do") == 0)
    {
        const hal_io_ops_t *ops = hal_io_get_ops();
        drv_io_do_t         pin = {0};
        int                 val = 0;

        if ((p1 == NULL) || (p2 == NULL))
        {
            log_diag_do_usage();
            return 1;
        }

        if ((ops == NULL) || (ops->do_set == NULL))
        {
            LOG_ERROR("diag do: hal_io ops not ready");
            return 1;
        }

        if (!drv_io_try_parse_do(p1, &pin))
        {
            LOG_ERROR("diag do: unknown DO name '%s'", p1);
            log_diag_do_usage();
            return 1;
        }

        val = atoi(p2);
        LOG_INFO("diag do: %s(board=%u,pin=%u) <= %d",
                 drv_io_do_name(pin) != NULL ? drv_io_do_name(pin) : "DO_UNKNOWN",
                 (unsigned)drv_io_handle_board(drv_io_do_raw(pin)),
                 (unsigned)drv_io_handle_pin(drv_io_do_raw(pin)),
                 val != 0);
        LOG_INFO("diag do: ret=%d", (int)ops->do_set(pin, val != 0));
        return 1;
    }

    if (strcmp(subcmd, "di") == 0)
    {
        const hal_io_ops_t *ops = hal_io_get_ops();
        drv_io_di_t         pin = {0};
        bool                val = false;

        if (p1 == NULL)
        {
            log_diag_di_usage();
            return 1;
        }

        if ((ops == NULL) || (ops->di_read == NULL))
        {
            LOG_ERROR("diag di: hal_io ops not ready");
            return 1;
        }

        if (!drv_io_try_parse_di(p1, &pin))
        {
            LOG_ERROR("diag di: unknown DI name '%s'", p1);
            log_diag_di_usage();
            return 1;
        }

        val = ops->di_read(pin);
        LOG_INFO("diag di: %s(board=%u,pin=%u) = %d",
                 drv_io_di_name(pin) != NULL ? drv_io_di_name(pin) : "DI_UNKNOWN",
                 (unsigned)drv_io_handle_board(drv_io_di_raw(pin)),
                 (unsigned)drv_io_handle_pin(drv_io_di_raw(pin)),
                 (int)val);
        return 1;
    }

    if (strcmp(subcmd, "io") == 0)
    {
        int first_board = 1;
        int last_board  = drv_io_board_count();

        if (p1 != NULL)
        {
            first_board = atoi(p1);
            last_board  = first_board;
        }

        if ((first_board <= 0) || (last_board > drv_io_board_count()))
        {
            LOG_ERROR("diag io: invalid board id %d", first_board);
            log_diag_io_usage();
            return 1;
        }

        for (int board_id = first_board; board_id <= last_board; ++board_id)
        {
            drv_io_stats_t stats = {0};
            sw_err_t       ret   = drv_io_get_stats(board_id, &stats);

            if (ret != SW_OK)
            {
                LOG_ERROR("diag io: board=%d get stats failed ret=%d", board_id, (int)ret);
                continue;
            }

            LOG_INFO("diag io: board=%d online=%d poll=%u write=%u input_change=%u",
                     board_id,
                     (int)drv_io_board_is_online(board_id),
                     (unsigned)stats.poll_count,
                     (unsigned)stats.write_count,
                     (unsigned)stats.input_change_count);
        }

        return 1;
    }

    if (strcmp(subcmd, "state") == 0)
    {
        device_context_t ctx = dev_ctx_snapshot();
        LOG_INFO("diag state: dev=%d safety=%d step=%d alarm=%d",
                 (int)ctx.device_state,
                 (int)ctx.safety_state,
                 (int)ctx.wash_step,
                 (int)ctx.has_error_alarm);
        return 1;
    }

    return 0;
}
