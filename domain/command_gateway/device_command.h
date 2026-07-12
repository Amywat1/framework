/**
 * @file    device_command.h
 * @brief   外部设备命令领域词汇（权威定义）
 * @author  HUWANGWEI
 * @date    2026-07-11
 *
 * @note    命令类型与载荷属于 domain 层；port 层仅引用本头文件。
 */

#ifndef DOMAIN_COMMAND_GATEWAY_DEVICE_COMMAND_H
#define DOMAIN_COMMAND_GATEWAY_DEVICE_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/wash/model/wash_types.h"

#include <stdint.h>

/**
 * @brief  设备命令种类（闭集枚举）
 */
typedef enum {
    DEV_CMD_NONE = 0,
    DEV_CMD_START_WASH,
    DEV_CMD_STOP_WASH,
    DEV_CMD_STOP_OPERATION,
    DEV_CMD_RESUME_OPERATION,
    DEV_CMD_RESET_FAULT,
    DEV_CMD_HOME_DEVICE,
    DEV_CMD_ENTER_MANUAL,
    DEV_CMD_MANUAL_ACTUATOR,
    DEV_CMD_START_SELF_CHECK,
    DEV_CMD_RECOVER,
    DEV_CMD_STOP_ALL_OUTPUTS,
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
        } start_wash;
        struct {
            uint32_t act_id;
            int32_t  param;
        } manual_actuator;
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

    cmd.meta.request_id = 0U;
    cmd.meta.source     = DEV_CMD_SOURCE_TEST;
    cmd.body.kind       = kind;
    return cmd;
}

/**
 * @brief  构造启动洗车命令
 */
static inline dev_cmd_t dev_cmd_make_start_wash(wash_mode_t mode)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_WASH);

    cmd.body.payload.start_wash.mode = mode;
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

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_COMMAND_GATEWAY_DEVICE_COMMAND_H */
