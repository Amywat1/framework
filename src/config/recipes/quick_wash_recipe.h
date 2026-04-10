/**
 * @file    quick_wash_recipe.h
 * @brief   M8 快洗步骤配置表（6 步，无预洗）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    本文件仅供 domain/process/recipe.c 通过 #include 装配。
 */

#ifndef CONFIG_RECIPES_QUICK_WASH_H
#define CONFIG_RECIPES_QUICK_WASH_H

/* clang-format off */
static const wash_step_config_t s_quick_steps[] = {
/*   step                        name           g_freq  g_fwd  top    side   prewash  brush  hipres  lift_dn  pos   fwd_lim  rev_lim */
    { WASH_STEP_ENTRY,           "入场",             0, false, false, false,  false,  false,  false,  false,   -1,   false,   false },
    { WASH_STEP_BRUSH_TOP_FWD,   "顶刷洗前进",     2000,  true,  true, false,  false,  false,  false,   true,   -1,   true,    false },
    { WASH_STEP_BRUSH_SIDE_REV,  "侧刷洗后退",     2000, false, false,  true,  false,   true,  false,  false,   -1,   false,   true  },
    { WASH_STEP_HIGHPRES_FWD,    "高压冲洗前进",   2500,  true, false, false,  false,  false,   true,  false,   -1,   true,    false },
    { WASH_STEP_HOME,            "归位",           4000, false, false, false,  false,  false,  false,  false,   -1,   false,   true  },
    { WASH_STEP_COMPLETE,        "完成",              0, false, false, false,  false,  false,  false,  false,   -1,   false,   false },
};
static const int s_quick_count = (int)(sizeof(s_quick_steps) / sizeof(s_quick_steps[0]));
/* clang-format on */

#endif /* CONFIG_RECIPES_QUICK_WASH_H */
