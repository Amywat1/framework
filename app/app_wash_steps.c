/**
 * @file    app_wash_steps.c
 * @brief   洗车步骤表数据（M8 机型）
 * @author  胡望伟
 * @date    2026-04-08
 *
 * @note    频率单位：0.01Hz（士林 VFD 约定）
 *          例：2500 = 25.00Hz（龙门慢速），5000 = 50.00Hz（龙门快速）
 *          exit_pos_pulse = -1 表示不使用脉冲退出条件。
 */

#include "app_wash_steps.h"

/* clang-format off */

/* -------------------------------------------------------------------------
 * 标准洗步骤表（8 步）
 * ------------------------------------------------------------------------- */
const WashStepConfig_t g_wash_steps_standard[] = {
/*  step                      name               g_freq  g_fwd  top   side  prewash  brush  hipres  lift_dn  pos   fwd_lim  rev_lim */
  { WASH_STEP_ENTRY,          "入场",              0,     false, false,false, false,  false,  false,  false,   -1,   false,   false },
  { WASH_STEP_PREWASH,        "预洗前进",         2500,   true,  false,false, true,   false,  false,  false,   -1,   true,    false },
  { WASH_STEP_BRUSH_TOP_FWD,  "顶刷洗前进",       2000,   true,  true, false, false,  false,  false,  true,    -1,   true,    false },
  { WASH_STEP_BRUSH_SIDE_FWD, "侧刷洗后退",       2000,   false, false,true,  false,  true,   false,  false,   -1,   false,   true  },
  { WASH_STEP_HIGHPRES_REV,   "高压冲洗前进",     2500,   true,  false,false, false,  false,  true,   false,   -1,   true,    false },
  { WASH_STEP_RINSE_FWD,      "清水漂洗后退",     3000,   false, false,false, false,  false,  false,  false,   -1,   false,   true  },
  { WASH_STEP_HOME,           "归位",             4000,   false, false,false, false,  false,  false,  false,   -1,   false,   true  },
  { WASH_STEP_COMPLETE,       "完成",              0,     false, false,false, false,  false,  false,  false,   -1,   false,   false },
};
const int g_wash_steps_standard_count = (int)(sizeof(g_wash_steps_standard) /
                                               sizeof(g_wash_steps_standard[0]));

/* -------------------------------------------------------------------------
 * 快洗步骤表（6 步，无预洗）
 * ------------------------------------------------------------------------- */
const WashStepConfig_t g_wash_steps_quick[] = {
  { WASH_STEP_ENTRY,          "入场",             0,     false, false,false, false,  false,  false,  false,   -1,   false,   false },
  { WASH_STEP_BRUSH_TOP_FWD,  "顶刷洗前进",      2000,   true,  true, false, false,  false,  false,  true,    -1,   true,    false },
  { WASH_STEP_BRUSH_SIDE_FWD, "侧刷洗后退",      2000,   false, false,true,  false,  true,   false,  false,   -1,   false,   true  },
  { WASH_STEP_HIGHPRES_REV,   "高压冲洗前进",    2500,   true,  false,false, false,  false,  true,   false,   -1,   true,    false },
  { WASH_STEP_HOME,           "归位",            4000,   false, false,false, false,  false,  false,  false,   -1,   false,   true  },
  { WASH_STEP_COMPLETE,       "完成",             0,     false, false,false, false,  false,  false,  false,   -1,   false,   false },
};
const int g_wash_steps_quick_count = (int)(sizeof(g_wash_steps_quick) /
                                            sizeof(g_wash_steps_quick[0]));

/* clang-format on */

const WashStepConfig_t *wash_steps_get(WashMode_t mode, int *p_count)
{
    switch (mode) {
        case WASH_MODE_QUICK:
            *p_count = g_wash_steps_quick_count;
            return g_wash_steps_quick;
        case WASH_MODE_STANDARD:
        default:
            *p_count = g_wash_steps_standard_count;
            return g_wash_steps_standard;
    }
}
