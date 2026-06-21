/**
 * @file    recipe.h
 * @brief   洗车配方接口（步骤表 + 查询）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    WashStepConfig_t 定义在本文件；配方数据存储在 config/recipes/ 中，
 *          由 recipe.c 通过 #include 装配，属于编译期静态只读数据。
 */

#ifndef DOMAIN_PROCESS_RECIPE_H
#define DOMAIN_PROCESS_RECIPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/wash_types.h"
#include "common/sw_error.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 步骤配置结构（编译期静态只读）
 * ------------------------------------------------------------------------- */
typedef struct
{
    wash_step_t  step;               /* 步骤 ID */
    const char  *name;               /* 步骤名称（日志用）*/
    uint16_t     gantry_freq;        /* 龙门频率（0=停止，单位 0.01Hz）*/
    bool         gantry_fwd;         /* true=前进，false=后退 */
    bool         brush_top_on;       /* 顶刷启动 */
    bool         brush_side_on;      /* 侧刷启动 */
    bool         water_prewash;      /* 预洗水路（泡沫+水帘）*/
    bool         water_brush;        /* 刷子冲水 */
    bool         water_highpres;     /* 高压冲洗 */
    int32_t      exit_pos_pulse;     /* 退出条件：龙门脉冲位置（-1=不使用）*/
    bool         exit_at_fwd_limit;  /* 退出条件：到达前限位 */
    bool         exit_at_rev_limit;  /* 退出条件：到达后限位 */
} wash_step_config_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  根据洗车模式获取步骤配置表
 * @param  mode    洗车模式
 * @param  steps   输出步骤表指针
 * @param  count   输出步骤数量
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t recipe_get(wash_mode_t mode, const wash_step_config_t **steps, int *count);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROCESS_RECIPE_H */
