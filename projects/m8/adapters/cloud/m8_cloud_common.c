/**
 * @file    m8_cloud_common.c
 * @brief   M8 云端点位 handler 共用辅助实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_common.h"

sw_err_t m8_cloud_get_cmd_pulse(point_value_t *out)
{
    out->b = false;
    return SW_OK;
}
