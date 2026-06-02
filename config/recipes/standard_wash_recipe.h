/**
 * @file    standard_wash_recipe.h
 * @brief   M8 标准洗步骤配置表（8 步）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    本文件仅供 domain/process/recipe.c 通过 #include 装配，
 *          不直接被其他模块包含。调用方须先定义 wash_step_config_t。
 *
 *          频率单位：0.01Hz（士林 VFD 约定），例：2500 = 25.00Hz。
 */

#ifndef CONFIG_RECIPES_STANDARD_WASH_H
#define CONFIG_RECIPES_STANDARD_WASH_H

/* clang-format off */
static const wash_step_config_t s_standard_steps[] = {
/*   step                        name           g_freq  g_fwd  top    side   prewash  brush  hipres  pos   fwd_lim  rev_lim */
    { WASH_STEP_ENTRY,           "入场",             0, false, false, false,  false,  false,  false,   -1,   false,   false },
    { WASH_STEP_PREWASH,         "预洗前进",       2500,  true, false, false,  true,   false,  false,   -1,   true,    false },
    { WASH_STEP_BRUSH_TOP_FWD,   "顶刷洗前进",     2000,  true,  true, false,  false,  false,  false,   -1,   true,    false },
    { WASH_STEP_BRUSH_SIDE_REV,  "侧刷洗后退",     2000, false, false,  true,  false,   true,  false,   -1,   false,   true  },
    { WASH_STEP_HIGHPRES_FWD,    "高压冲洗前进",   2500,  true, false, false,  false,  false,   true,   -1,   true,    false },
    { WASH_STEP_RINSE_REV,       "清水漂洗后退",   3000, false, false, false,  false,  false,  false,   -1,   false,   true  },
    { WASH_STEP_HOME,            "归位",           4000, false, false, false,  false,  false,  false,   -1,   false,   true  },
    { WASH_STEP_COMPLETE,        "完成",              0, false, false, false,  false,  false,  false,   -1,   false,   false },
};
static const int s_standard_count = (int)(sizeof(s_standard_steps) / sizeof(s_standard_steps[0]));
/* clang-format on */

#endif /* CONFIG_RECIPES_STANDARD_WASH_H */
