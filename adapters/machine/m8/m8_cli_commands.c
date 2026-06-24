/**
 * @file    cli_commands.c
 * @brief   CLI 鍛戒护澶勭悊瀹炵幇
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    鎻愪緵缁熶竴鐨?CLI 鍛戒护鍏ュ彛锛? *          - device锛氳澶囩姸鎬佹煡璇笌鍛戒护涓嬪彂
 *          - safety锛氭姤璀︿笌鎬ュ仠鐘舵€佹煡璇€佸浣? *          - param锛氬弬鏁拌鍐欎笌鎸佷箙鍖? *          - diag锛氬簳灞傜‖浠惰瘖鏂? */

#include "adapters/machine/m8/m8_cli_commands.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common/log.h"
#include "domain/model/command.h"
#include "domain/model/wash_types.h"
#include "ports/cloud/command_port.h"
#include "ports/hal/hal_io_port.h"
#include "common/io_handle.h"
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

/**
 * @brief  缁?command_port 娉ㄥ叆璁惧鍛戒护锛堜笌浜戠/浠跨湡鎺у埗鍙板悓涓€璺緞锛? */
static sw_err_t inject_device_cmd(cmd_type_t type, wash_mode_t mode)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t                       cmd;

    if ((cp == NULL) || (cp->inject == NULL))
    {
        LOG_WARN("cli: command_port not registered");
        return SW_ERR_NOT_INIT;
    }

    cmd.type = type;
    if (type == CMD_START_WASH)
    {
        cmd.payload.start_wash.mode = mode;
    }

    return cp->inject(&cmd);
}

/* -------------------------------------------------------------------------
 * device 鍛戒护鍩? * ------------------------------------------------------------------------- */
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
        LOG_INFO("device: state=%d step=%d mode=%d cloud=%d",
                 (int)ctx.device_state,
                 (int)ctx.wash_step,
                 (int)ctx.wash_mode,
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

        sw_err_t ret = inject_device_cmd(CMD_START_WASH, mode);
        LOG_INFO("device order mode=%d ret=%d", (int)mode, (int)ret);
        return 1;
    }

    if (strcmp(subcmd, "stop") == 0)
    {
        sw_err_t ret = inject_device_cmd(CMD_STOP_WASH, WASH_MODE_STANDARD);
        LOG_INFO("device stop wash ret=%d", (int)ret);
        return 1;
    }

    if (strcmp(subcmd, "stop-op") == 0)
    {
        sw_err_t ret = inject_device_cmd(CMD_STOP_OPERATION, WASH_MODE_STANDARD);
        LOG_INFO("device stop operation ret=%d", (int)ret);
        return 1;
    }

    if (strcmp(subcmd, "resume") == 0)
    {
        sw_err_t ret = inject_device_cmd(CMD_RESUME_OPERATION, WASH_MODE_STANDARD);
        LOG_INFO("device resume ret=%d", (int)ret);
        return 1;
    }

    if (strcmp(subcmd, "reset") == 0)
    {
        sw_err_t ret = inject_device_cmd(CMD_RESET_FAULT, WASH_MODE_STANDARD);
        LOG_INFO("device reset ret=%d", (int)ret);
        return 1;
    }

    if (strcmp(subcmd, "home") == 0)
    {
        sw_err_t ret = inject_device_cmd(CMD_HOME_DEVICE, WASH_MODE_STANDARD);
        LOG_INFO("device home ret=%d", (int)ret);
        return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * safety 鍛戒护鍩燂紙鎶ヨ鍔熻兘宸茬鐢級
 * ------------------------------------------------------------------------- */
int safety_cmd_handler(char *subcmd, char *p1, char *p2)
{
    (void)subcmd;
    (void)p1;
    (void)p2;
    LOG_INFO("safety: alarm disabled");
    return 1;
}

/* -------------------------------------------------------------------------
 * param 鍛戒护鍩? * ------------------------------------------------------------------------- */
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
 * diag 鍛戒护鍩? * ------------------------------------------------------------------------- */
int diag_cmd_handler(char *subcmd, char *p1, char *p2)
{
    if (subcmd == NULL)
    {
        return 0;
    }

    if (strcmp(subcmd, "do") == 0)
    {
        const hal_io_ops_t *ops = hal_io_get_ops();
        io_do_t             pin = {0};
        int                 val = 0;

        if ((p1 == NULL) || (p2 == NULL))
        {
            log_diag_do_usage();
            return 1;
        }

        if ((ops == NULL) || (ops->do_set == NULL) || (ops->try_parse_do == NULL))
        {
            LOG_ERROR("diag do: hal_io ops not ready");
            return 1;
        }

        if (!ops->try_parse_do(p1, &pin))
        {
            LOG_ERROR("diag do: unknown DO name '%s'", p1);
            log_diag_do_usage();
            return 1;
        }

        val = atoi(p2);
        LOG_INFO("diag do: %s(board=%u,pin=%u) <= %d",
                 (ops->do_name != NULL) && (ops->do_name(pin) != NULL)
                     ? ops->do_name(pin) : "DO_UNKNOWN",
                 (unsigned)io_handle_board(io_do_raw(pin)),
                 (unsigned)io_handle_pin(io_do_raw(pin)),
                 val != 0);
        LOG_INFO("diag do: ret=%d", (int)ops->do_set(pin, val != 0));
        return 1;
    }

    if (strcmp(subcmd, "di") == 0)
    {
        const hal_io_ops_t *ops = hal_io_get_ops();
        io_di_t             pin = {0};
        bool                val = false;

        if (p1 == NULL)
        {
            log_diag_di_usage();
            return 1;
        }

        if ((ops == NULL) || (ops->di_read == NULL) || (ops->try_parse_di == NULL))
        {
            LOG_ERROR("diag di: hal_io ops not ready");
            return 1;
        }

        if (!ops->try_parse_di(p1, &pin))
        {
            LOG_ERROR("diag di: unknown DI name '%s'", p1);
            log_diag_di_usage();
            return 1;
        }

        val = ops->di_read(pin);
        LOG_INFO("diag di: %s(board=%u,pin=%u) = %d",
                 (ops->di_name != NULL) && (ops->di_name(pin) != NULL)
                     ? ops->di_name(pin) : "DI_UNKNOWN",
                 (unsigned)io_handle_board(io_di_raw(pin)),
                 (unsigned)io_handle_pin(io_di_raw(pin)),
                 (int)val);
        return 1;
    }

    if (strcmp(subcmd, "io") == 0)
    {
        const hal_io_ops_t *ops = hal_io_get_ops();
        int                 first_board = 1;
        int                 last_board  = 1;

        if ((ops == NULL) || (ops->board_count == NULL) || (ops->get_stats == NULL))
        {
            LOG_ERROR("diag io: hal_io ops not ready");
            return 1;
        }

        last_board = ops->board_count();

        if (p1 != NULL)
        {
            first_board = atoi(p1);
            last_board  = first_board;
        }

        if ((first_board <= 0) || (last_board > ops->board_count()))
        {
            LOG_ERROR("diag io: invalid board id %d", first_board);
            log_diag_io_usage();
            return 1;
        }

        for (int board_id = first_board; board_id <= last_board; ++board_id)
        {
            hal_io_stats_t stats = {0};
            sw_err_t       ret   = ops->get_stats(board_id, &stats);

            if (ret != SW_OK)
            {
                LOG_ERROR("diag io: board=%d get stats failed ret=%d", board_id, (int)ret);
                continue;
            }

            LOG_INFO("diag io: board=%d online=%d dirty=%d off=%u recover=%u in_refresh=%u out_req=%u out_flush=%u resend=%u",
                     board_id,
                     (int)stats.online,
                     (int)stats.dirty_pending,
                     (unsigned)stats.offline_count,
                     (unsigned)stats.online_recover_count,
                     (unsigned)stats.input_refresh_count,
                     (unsigned)stats.output_request_count,
                     (unsigned)stats.output_flush_count,
                     (unsigned)stats.output_resend_count);
            LOG_INFO("diag io: board=%d last_online=%u last_offline=%u last_in=%u last_req=%u last_flush=%u in=0x%08X out=0x%08X",
                     board_id,
                     (unsigned)stats.last_online_ms,
                     (unsigned)stats.last_offline_ms,
                     (unsigned)stats.last_input_refresh_ms,
                     (unsigned)stats.last_output_req_ms,
                     (unsigned)stats.last_output_flush_ms,
                     (unsigned)stats.last_input_snapshot,
                     (unsigned)stats.last_output_snapshot);
        }

        return 1;
    }

    if (strcmp(subcmd, "state") == 0)
    {
        device_context_t ctx = dev_ctx_snapshot();
        LOG_INFO("diag state: dev=%d step=%d",
                 (int)ctx.device_state,
                 (int)ctx.wash_step);
        return 1;
    }

    return 0;
}
