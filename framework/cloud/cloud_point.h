/**
 * @file    cloud_point.h
 * @brief   云端物模型点位元模型与 dispatch 接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef FRAMEWORK_CLOUD_CLOUD_POINT_H
#define FRAMEWORK_CLOUD_CLOUD_POINT_H

#include "framework/common/point_table/point_table.h"
#include "framework/ports/inbound/command/command_port.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOUD_REPORT_JSON_MAX   4096U
#define CLOUD_POINT_TABLE_MAX   96U

typedef enum
{
    CLOUD_POINT_ACCESS_RO = 0,
    CLOUD_POINT_ACCESS_RW,
    CLOUD_POINT_ACCESS_WO,
} cloud_point_access_t;

typedef enum
{
    CLOUD_POINT_SEM_TELEMETRY = 0,
    CLOUD_POINT_SEM_DEVICE_CMD,
    CLOUD_POINT_SEM_CLOUD_SERVICE,
    CLOUD_POINT_SEM_MANUAL_ACT,
} cloud_point_semantic_t;

typedef enum
{
    CLOUD_REPORT_PERIODIC = 0,
    CLOUD_REPORT_ON_CHANGE,
    CLOUD_REPORT_RESYNC_ONLY,
    CLOUD_REPORT_NEVER,
} cloud_report_policy_t;

typedef sw_err_t (*cloud_service_fn_t)(const point_value_t *in);
typedef point_get_fail_policy_t cloud_point_get_fail_policy_t;

#define CLOUD_POINT_GET_FAIL_OMIT   POINT_GET_FAIL_OMIT
#define CLOUD_POINT_GET_FAIL_ABORT  POINT_GET_FAIL_ABORT
#define CLOUD_POINT_GET_FAIL_NULL   POINT_GET_FAIL_NULL

/** 云端物模型点位条目（编解码复用 base） */
typedef struct
{
    point_table_entry_t      base;
    cloud_point_access_t     access;
    cloud_point_semantic_t   semantic;
    cloud_report_policy_t    report_policy;
    cmd_type_t               cmd_type;
    cloud_service_fn_t       service;
} cloud_point_entry_t;

/** @brief 脉冲命令回显空闲态（恒定 false） */
sw_err_t cloud_point_get_echo_idle(point_value_t *out);
void cloud_point_set_get_fail_policy(cloud_point_get_fail_policy_t policy);
sw_err_t cloud_point_validate(const cloud_point_entry_t *entries, size_t count);
sw_err_t cloud_point_to_json(const cloud_point_entry_t *entries, size_t count,
                              char *buf, size_t buf_size);
sw_err_t cloud_point_to_json_filtered(const cloud_point_entry_t *entries, size_t count,
                                       const char *const *ids, size_t id_count,
                                       char *buf, size_t buf_size);
sw_err_t cloud_point_apply_json(const cloud_point_entry_t *entries, size_t count,
                                 const char *json_str,
                                 point_apply_result_t *result_opt);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_CLOUD_CLOUD_POINT_H */
