/**
 * @file    report_port.c
 * @brief   云端上报端口注册表
 */

#include "ports/outbound/cloud/report/report_port.h"

static const cloud_report_ops_t *s_ops;

void cloud_report_register(const cloud_report_ops_t *ops)
{
    s_ops = ops;
}

const cloud_report_ops_t *cloud_report_get_ops(void)
{
    return s_ops;
}
