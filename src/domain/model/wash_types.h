/**
 * @file    wash_types.h
 * @brief   洗车流程相关领域类型定义
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef DOMAIN_WASH_TYPES_H
#define DOMAIN_WASH_TYPES_H

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
 * 洗车步骤 ID
 * ------------------------------------------------------------------------- */
typedef enum
{
    WASH_STEP_IDLE         = 0,
    WASH_STEP_ENTRY,            /* 车辆进入，挡杆关闭，等待就位 */
    WASH_STEP_PREWASH,          /* 预洗：龙门前进，喷泡沫+水帘 */
    WASH_STEP_BRUSH_TOP_FWD,    /* 顶刷洗：龙门前进，顶刷启动 */
    WASH_STEP_BRUSH_SIDE_REV,   /* 侧刷洗：龙门后退，侧刷启动，冲水 */
    WASH_STEP_HIGHPRES_FWD,     /* 高压冲洗：龙门前进，高压水 */
    WASH_STEP_RINSE_REV,        /* 清水漂洗：龙门后退 */
    WASH_STEP_HOME,             /* 龙门快速归位至后限位 */
    WASH_STEP_COMPLETE,         /* 流程结束 */
    WASH_STEP_MAX
} wash_step_t;

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

#endif /* DOMAIN_WASH_TYPES_H */
