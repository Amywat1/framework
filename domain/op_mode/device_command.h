/**
 * @file    device_command.h
 * @brief   外部设备命令领域词汇（权威定义）
 * @author  HUWANGWEI
 * @date    2026-07-11
 *
 * @note    命令类型与载荷属于 domain 层；port 层仅引用本头文件。
 */

#ifndef DOMAIN_OP_MODE_DEVICE_COMMAND_H
#define DOMAIN_OP_MODE_DEVICE_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/op_mode/op_mode_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief  设备命令种类
 */
typedef enum {
    DEV_CMD_NONE = 0,
    DEV_CMD_SUBMIT_ORDER, /**< 入订单簿；不立即开洗 */
    DEV_CMD_CLEAR_MACHINE, /**< 停放行、中止在洗、整机清障 */
    DEV_CMD_SET_SERVICE, /**< 设置运营总开关；关闭时若在 STANDBY/HOLD 则落到 STOPPED */
    DEV_CMD_MANUAL_ACTUATOR,
    DEV_CMD_START_SELF_CHECK,
    DEV_CMD_RECOVER,
    DEV_CMD_STOP_ALL_OUTPUTS,
    DEV_CMD_CANCEL_ORDER, /**< 取消未放行订单 */
    DEV_CMD_MAX
} dev_cmd_kind_t;

/**
 * @brief  命令来源
 */
typedef enum {
    DEV_CMD_SOURCE_CLOUD = 0,
    DEV_CMD_SOURCE_CLI,
    DEV_CMD_SOURCE_SIM,
    DEV_CMD_SOURCE_TEST,
} dev_cmd_source_t;

/**
 * @brief  命令元数据
 */
typedef struct {
    uint64_t         request_id;
    uint64_t         wash_session_id;
    uint64_t         correlation_id;
    uint64_t         causation_id;
    dev_cmd_source_t source;
} dev_cmd_meta_t;

/**
 * @brief  命令载荷（按 kind 选 union 成员）
 */
typedef struct {
    dev_cmd_kind_t kind;
    union {
        struct {
            wash_mode_t mode;
            uint64_t    order_id; /**< 0 表示由订单簿分配；非 0 按号去重 */
        } submit_order;
        struct {
            uint64_t order_id;
        } cancel_order;
        struct {
            uint32_t act_id;
            int32_t  param;
        } manual_actuator;
        struct {
            bool enabled; /**< true 开启总开关；false 关闭 */
        } service;
    } payload;
} dev_cmd_body_t;

/**
 * @brief  完整入站命令（元数据 + 载荷）
 */
typedef struct {
    dev_cmd_meta_t meta;
    dev_cmd_body_t body;
} dev_cmd_t;

/**
 * @brief  构造无载荷命令
 */
static inline dev_cmd_t dev_cmd_make_simple(dev_cmd_kind_t kind)
{
    dev_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.meta.source = DEV_CMD_SOURCE_TEST;
    cmd.body.kind   = kind;
    return cmd;
}

/**
 * @brief  构造提交订单命令
 */
static inline dev_cmd_t dev_cmd_make_submit_order(wash_mode_t mode)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_SUBMIT_ORDER);

    cmd.body.payload.submit_order.mode     = mode;
    cmd.body.payload.submit_order.order_id = 0U;
    return cmd;
}

/**
 * @brief  构造带云端订单号的提交命令（重试用同一 order_id 去重）
 */
static inline dev_cmd_t dev_cmd_make_submit_order_id(wash_mode_t mode, uint64_t order_id)
{
    dev_cmd_t cmd = dev_cmd_make_submit_order(mode);

    cmd.body.payload.submit_order.order_id = order_id;
    return cmd;
}

/**
 * @brief  构造取消未放行订单命令
 */
static inline dev_cmd_t dev_cmd_make_cancel_order(uint64_t order_id)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_CANCEL_ORDER);

    cmd.body.payload.cancel_order.order_id = order_id;
    return cmd;
}

/**
 * @brief  构造手动点动命令
 */
static inline dev_cmd_t dev_cmd_make_manual(uint32_t act_id, int32_t param)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_MANUAL_ACTUATOR);

    cmd.body.payload.manual_actuator.act_id = act_id;
    cmd.body.payload.manual_actuator.param  = param;
    return cmd;
}

/**
 * @brief  构造运营总开关设置命令
 * @param  enabled  true 开启；false 关闭
 */
static inline dev_cmd_t dev_cmd_make_set_service(bool enabled)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_SET_SERVICE);

    cmd.body.payload.service.enabled = enabled;
    return cmd;
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_DEVICE_COMMAND_H */
