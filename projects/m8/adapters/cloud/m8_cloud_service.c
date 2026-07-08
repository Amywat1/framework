/**
 * @file    m8_cloud_service.c
 * @brief   M8 云端服务类点位 handler 实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_service.h"
#include "framework/application/orchestrators/report_aggregator.h"

static bool s_communication_test_val = false;

sw_err_t m8_cloud_set_cmd_sync(const point_value_t *in)
{
    if (in->b)
    {
        report_aggregator_request_resync();
    }
    return SW_OK;
}

sw_err_t m8_cloud_set_cmd_communication_test(const point_value_t *in)
{
    s_communication_test_val = in->b;
    return SW_OK;
}

sw_err_t m8_cloud_get_sts_communication_test(point_value_t *out)
{
    out->b = s_communication_test_val;
    return SW_OK;
}
