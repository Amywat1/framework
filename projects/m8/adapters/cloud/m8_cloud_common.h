/**
 * @file    m8_cloud_common.h
 * @brief   M8 云端点位 handler 共用辅助
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_COMMON_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_COMMON_H

#include "framework/common/point_table/point_table.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  命令类点位统一回显：恒为 0 */
sw_err_t m8_cloud_get_cmd_pulse(point_value_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_COMMON_H */
