/**
 * @file    wash_types.h
 * @brief   洗车流程相关领域类型定义
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef DOMAIN_WASH_TYPES_H
#define DOMAIN_WASH_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 洗车模式
 * ------------------------------------------------------------------------- */
typedef enum
{
    WASH_MODE_STANDARD = 0, /* 标准洗（预洗 + 泡沫 + 刷洗 + 高压 + 清洗）*/
    WASH_MODE_QUICK,        /* 快洗（刷洗 + 高压）*/
    WASH_MODE_MAX
} wash_mode_t;

/* -------------------------------------------------------------------------
 * 步骤退出条件
 *
 * 支持组合：多个条件同时为 true 时取或逻辑（任一满足即退出）。
 * 超时由调用方统一设置，不在此结构中。
 * ------------------------------------------------------------------------- */
typedef struct
{
    bool    at_fwd_limit;   /* 到达龙门前限位 */
    bool    at_rev_limit;   /* 到达龙门后限位 */
    int32_t pos_pulse;      /* 龙门到达指定脉冲位置（-1 = 不使用）*/
} wash_exit_cond_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_WASH_TYPES_H */
