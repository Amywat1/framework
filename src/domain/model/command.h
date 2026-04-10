/**
 * @file    command.h
 * @brief   统一命令类型定义（CLI / MQTT / 按钮 → 同一套命令结构）
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef DOMAIN_COMMAND_H
#define DOMAIN_COMMAND_H

#include "domain/model/wash_types.h"

/* -------------------------------------------------------------------------
 * 命令类型
 * ------------------------------------------------------------------------- */
typedef enum
{
    CMD_NONE            = 0,
    CMD_START_WASH,         /* 启动洗车，payload: wash_mode */
    CMD_STOP_WASH,          /* 中止当前洗车 */
    CMD_STOP_OPERATION,     /* 关闭运营（不再接单）*/
    CMD_RESUME_OPERATION,   /* 恢复运营 */
    CMD_RESET_FAULT,        /* 故障复位（清除 MANUAL 类报警）*/
    CMD_HOME_DEVICE,        /* 手动归位 */
    CMD_MAX
} cmd_type_t;

/* -------------------------------------------------------------------------
 * 命令结构体
 * ------------------------------------------------------------------------- */
typedef struct
{
    cmd_type_t type;
    union
    {
        struct
        {
            wash_mode_t mode; /* CMD_START_WASH 时使用 */
        } start_wash;
    } payload;
} cmd_t;

#endif /* DOMAIN_COMMAND_H */
