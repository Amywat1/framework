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

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 上报 JSON 缓冲上限（provider 适配器使用） */
#define CLOUD_REPORT_JSON_MAX 4096U
/** 点位表条目上限；同时是 watcher shadow / 脏集合长度 */
#define CLOUD_POINT_TABLE_MAX 96U

/**
 * @brief  物模型点位种类
 *
 * 三种互斥：遥测只读、脉冲命令走网关、写入走 set。不再另设 access / semantic。
 */
typedef enum {
    CLOUD_KIND_TELEMETRY = 0, /**< 只读遥测，必须有 get */
    CLOUD_KIND_COMMAND,       /**< bool 脉冲命令，cmd_kind 有效，走命令网关 */
    CLOUD_KIND_WRITE,         /**< 写入，必须有 set */
} cloud_point_kind_t;

/** 云端物模型点位条目（编解码复用 base） */
typedef struct {
    point_table_entry_t base;
    cloud_point_kind_t  kind;
    bool                on_change; /**< 为真时纳入 watcher 脏检测；必须有 get，禁止 FLOAT */
    dev_cmd_kind_t      cmd_kind;  /**< 仅 COMMAND 使用 */
} cloud_point_entry_t;

/** @brief 脉冲命令回显空闲态（恒定 false） */
sw_err_t cloud_point_get_echo_idle(point_value_t *out);

/** @brief  COMMAND 语义提交回调（由项目 wiring 注册） */
typedef sw_err_t (*cloud_device_cmd_submit_fn_t)(dev_cmd_kind_t kind);

/** @brief  注册 COMMAND 提交回调（项目层宜转调 device_command_port.submit_async） */
void cloud_point_set_device_cmd_submit(cloud_device_cmd_submit_fn_t fn);

/**
 * @brief  登记期校验物模型表
 * @retval SW_OK        表合法
 * @retval SW_ERR_PARAM 空表、超上限、id 冲突或 kind 约束不满足
 */
sw_err_t cloud_point_validate(const cloud_point_entry_t *entries, size_t count);

/**
 * @brief  按点位 kind 把已解析的值分派到实处
 * @param  entry   点位条目
 * @param  val     已解析的值（类型已与 entry->base.type 校验一致）
 * @param  result  逐 key 结果汇总，可为 NULL
 * @retval SW_OK           分派成功，或 COMMAND 置假的空操作
 * @retval SW_ERR_STATE    遥测点位拒绝写入
 * @retval SW_ERR_NOT_INIT 语义要求的回调未注册
 * @retval SW_ERR_PARAM    入参非法或 kind 未知
 *
 * @note   本函数只答"该值交给谁"，不涉及任何序列化格式。JSON 载荷的解析入口
 *         见 `adapters/outbound/cloud/cloud_json.h`。
 */
sw_err_t cloud_point_apply_value(const cloud_point_entry_t *entry,
                                 const point_value_t       *val,
                                 point_apply_result_t      *result);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_CLOUD_CLOUD_POINT_H */
