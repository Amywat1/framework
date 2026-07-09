/**
 * @file    m8_cloud_service.c
 * @brief   M8 云端服务类点位 handler 实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "projects/m8/adapters/cloud/m8_cloud_service.h"
#include "projects/m8/adapters/cloud/m8_cloud_runtime.h"
#include "framework/application/orchestrators/report_scheduler.h"
#include "framework/domain/wash/model/wash_types.h"
#include "framework/ports/inbound/command/command_port.h"

static bool s_communication_test_val = false;

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

sw_err_t m8_cloud_service_sync(const point_value_t *in)
{
    if ((in != NULL) && in->b)
    {
        report_scheduler_request_resync();
    }
    return SW_OK;
}

sw_err_t m8_cloud_service_comm_test(const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    s_communication_test_val = in->b;
    return SW_OK;
}

sw_err_t m8_cloud_service_open_data_monitor(const point_value_t *in)
{
    if (in == NULL)
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_set_bool("cmd_open_data_minitor", in->b);
    return SW_OK;
}

sw_err_t m8_cloud_get_cmd_open_data_monitor(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->b = m8_cloud_runtime_get_bool("cmd_open_data_minitor");
    return SW_OK;
}

sw_err_t m8_cloud_service_custom_stop(const point_value_t *in)
{
    if ((in == NULL) || !in->b)
    {
        return SW_OK;
    }

    m8_cloud_runtime_set_custom_stopping(true);
    return inject_cmd(CMD_STOP_WASH);
}

sw_err_t m8_cloud_service_start_wash(const point_value_t *in)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t                     cmd;

    if ((in == NULL) || !in->b)
    {
        return SW_OK;
    }

    if ((cp == NULL) || (cp->inject == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    m8_cloud_runtime_on_wash_started();
    cmd.type                    = CMD_START_WASH;
    cmd.payload.start_wash.mode = WASH_MODE_STANDARD;
    return cp->inject(&cmd);
}

sw_err_t m8_cloud_get_sts_communication_test(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->b = s_communication_test_val;
    return SW_OK;
}
