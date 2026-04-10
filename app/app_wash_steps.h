/**
 * @file    app_wash_steps.h
 * @brief   洗车步骤表（M8 龙门式）
 * @author  胡望伟
 * @date    2026-04-08
 *
 * @note    M8 无输送带，洗车流程由龙门前进/后退 + 各机构动作组合。
 *          配置表描述每个步骤的执行参数和退出条件。
 */

#ifndef APP_WASH_STEPS_H
#define APP_WASH_STEPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_types.h"

/* -------------------------------------------------------------------------
 * 洗车模式
 * ------------------------------------------------------------------------- */
typedef enum
{
    WASH_MODE_STANDARD = 0, /* 标准洗（预洗 + 泡沫 + 刷洗 + 高压 + 清洗）*/
    WASH_MODE_QUICK,        /* 快洗（刷洗 + 高压）*/
    WASH_MODE_MAX
} WashMode_t;

/* -------------------------------------------------------------------------
 * 洗车步骤 ID
 * ------------------------------------------------------------------------- */
typedef enum
{
    WASH_STEP_IDLE          = 0,
    WASH_STEP_ENTRY,            /* 车辆进入，挡杆关闭 */
    WASH_STEP_PREWASH,          /* 预洗：前进喷泡沫 */
    WASH_STEP_BRUSH_TOP_FWD,    /* 顶刷洗：前进刷洗 */
    WASH_STEP_BRUSH_SIDE_FWD,   /* 侧刷洗：前进刷洗 */
    WASH_STEP_HIGHPRES_REV,     /* 高压冲洗：后退冲洗 */
    WASH_STEP_RINSE_FWD,        /* 清水漂洗：前进 */
    WASH_STEP_HOME,             /* 龙门归位 */
    WASH_STEP_COMPLETE,         /* 洗车完成 */
    WASH_STEP_MAX
} WashStep_t;

/* -------------------------------------------------------------------------
 * 步骤配置结构
 * ------------------------------------------------------------------------- */
typedef struct
{
    WashStep_t  step;               /* 步骤 ID */
    const char *name;               /* 步骤名称（日志用）*/
    uint16_t    gantry_freq;        /* 龙门频率（0=停止，单位 0.01Hz）*/
    bool        gantry_fwd;         /* true=前进，false=后退 */
    bool        brush_top_on;       /* 顶刷启动 */
    bool        brush_side_on;      /* 侧刷启动 */
    bool        water_prewash;      /* 预洗水路（泡沫+水帘）*/
    bool        water_brush;        /* 刷子冲水 */
    bool        water_highpres;     /* 高压冲洗 */
    bool        top_lift_down;      /* 顶刷下降到洗车位置 */
    int         exit_pos_pulse;     /* 退出条件：龙门到达此脉冲位置（-1=不使用）*/
    bool        exit_at_fwd_limit;  /* 退出条件：到达前限位 */
    bool        exit_at_rev_limit;  /* 退出条件：到达后限位 */
} WashStepConfig_t;

/* -------------------------------------------------------------------------
 * 对外导出
 * ------------------------------------------------------------------------- */
extern const WashStepConfig_t g_wash_steps_standard[];
extern const int              g_wash_steps_standard_count;

extern const WashStepConfig_t g_wash_steps_quick[];
extern const int              g_wash_steps_quick_count;

/**
 * @brief  根据模式获取步骤配置表
 * @param  mode     洗车模式
 * @param  p_count  返回步骤数量
 * @retval 步骤配置表指针
 */
const WashStepConfig_t *wash_steps_get(WashMode_t mode, int *p_count);

#ifdef __cplusplus
}
#endif

#endif /* APP_WASH_STEPS_H */
