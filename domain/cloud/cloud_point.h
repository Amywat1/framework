/**
 * @file    cloud_point.h
 * @brief   云端物模型点位元模型与 dispatch 接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef DOMAIN_CLOUD_CLOUD_POINT_H
#define DOMAIN_CLOUD_CLOUD_POINT_H

#include "common/point_table/point_table.h"
#include "domain/op_mode/device_command.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOUD_REPORT_JSON_MAX 4096U
#define CLOUD_POINT_TABLE_MAX 96U

typedef enum {
    CLOUD_POINT_ACCESS_RO = 0,
    CLOUD_POINT_ACCESS_RW,
    CLOUD_POINT_ACCESS_WO,
} cloud_point_access_t;

typedef enum {
    CLOUD_POINT_SEM_TELEMETRY = 0,
    CLOUD_POINT_SEM_DEVICE_CMD,
    CLOUD_POINT_SEM_CLOUD_SERVICE,
    CLOUD_POINT_SEM_MANUAL_ACT,
} cloud_point_semantic_t;

typedef enum {
    CLOUD_REPORT_PERIODIC = 0,
    CLOUD_REPORT_ON_CHANGE,
    CLOUD_REPORT_RESYNC_ONLY,
    CLOUD_REPORT_NEVER,
} cloud_report_policy_t;

typedef sw_err_t (*cloud_service_fn_t)(const point_value_t *in);
typedef point_get_fail_policy_t cloud_point_get_fail_policy_t;

#define CLOUD_POINT_GET_FAIL_OMIT  POINT_GET_FAIL_OMIT
#define CLOUD_POINT_GET_FAIL_ABORT POINT_GET_FAIL_ABORT
#define CLOUD_POINT_GET_FAIL_NULL  POINT_GET_FAIL_NULL

/** 云端物模型点位条目（编解码复用 base） */
typedef struct {
    point_table_entry_t    base;
    cloud_point_access_t   access;
    cloud_point_semantic_t semantic;
    cloud_report_policy_t  report_policy;
    dev_cmd_kind_t         cmd_kind; /**< DEVICE_CMD 专用，映射 dev_cmd_kind_t */
    cloud_service_fn_t     service;
} cloud_point_entry_t;

/** @brief 脉冲命令回显空闲态（恒定 false） */
sw_err_t cloud_point_get_echo_idle(point_value_t *out);

/** @brief  DEVICE_CMD 语义提交回调（由项目 wiring 注册） */
typedef sw_err_t (*cloud_device_cmd_submit_fn_t)(dev_cmd_kind_t kind);

/** @brief  注册 DEVICE_CMD 语义提交回调（项目层宜转调 device_command_port.submit_async） */
void cloud_point_set_device_cmd_submit(cloud_device_cmd_submit_fn_t fn);

sw_err_t cloud_point_validate(const cloud_point_entry_t *entries, size_t count);

/**
 * @brief  按点位语义把已解析的值分派到实处
 * @param  entry   点位条目
 * @param  val     已解析的值（类型已与 entry->base.type 校验一致）
 * @param  result  逐 key 结果汇总，可为 NULL
 * @retval SW_OK           分派成功，或 DEVICE_CMD 置假的空操作
 * @retval SW_ERR_STATE    点位只读，拒绝写入
 * @retval SW_ERR_NOT_INIT 语义要求的回调未注册
 * @retval SW_ERR_PARAM    入参非法或语义未知
 *
 * @note   本函数只答"该值交给谁"，不涉及任何序列化格式。JSON 载荷的解析入口
 *         见 `adapters/outbound/cloud/cloud_point_json.h`。
 */
sw_err_t cloud_point_apply_value(const cloud_point_entry_t *entry,
                                 const point_value_t       *val,
                                 point_apply_result_t      *result);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_CLOUD_CLOUD_POINT_H */
