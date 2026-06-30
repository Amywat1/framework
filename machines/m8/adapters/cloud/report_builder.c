/**
 * @file    report_builder.c
 * @brief   M8 云端上报 JSON 构建器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "machines/m8/adapters/cloud/report_builder.h"
#include <stdio.h>

sw_err_t m8_build_report_json(const cloud_report_payload_t *p,
                               char *buf, size_t buf_size)
{
    int n = snprintf(buf, buf_size,
                     "{\"%s\":%d,\"%s\":%d,\"%s\":%d,"
                     "\"%s\":%s,\"%s\":%u,\"%s\":%s}",
                     M8_REPORT_FIELD_DEV_STATE,  (int)p->dev_state,
                     M8_REPORT_FIELD_WASH_MODE,  (int)p->wash_mode,
                     M8_REPORT_FIELD_GANTRY_POS, (int)p->gantry_pos,
                     M8_REPORT_FIELD_HAS_ALARM,  p->has_alarm ? "true" : "false",
                     M8_REPORT_FIELD_ALARM_CODE,  (unsigned)p->alarm_code,
                     M8_REPORT_FIELD_CLOUD_CONN,  p->cloud_connected ? "true" : "false");

    return ((n > 0) && ((size_t)n < buf_size)) ? SW_OK : SW_ERR_PARAM;
}
