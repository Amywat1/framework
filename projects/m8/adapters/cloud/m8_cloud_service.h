/**
 * @file    m8_cloud_service.h
 * @brief   M8 云端服务类点位 handler
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_SERVICE_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_SERVICE_H

#include "framework/common/point_table/point_table.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

sw_err_t m8_cloud_service_sync(const point_value_t *in);
sw_err_t m8_cloud_service_comm_test(const point_value_t *in);
sw_err_t m8_cloud_service_open_data_monitor(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_open_data_monitor(point_value_t *out);
sw_err_t m8_cloud_service_custom_stop(const point_value_t *in);
sw_err_t m8_cloud_service_start_wash(const point_value_t *in);
sw_err_t m8_cloud_get_sts_communication_test(point_value_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_SERVICE_H */
