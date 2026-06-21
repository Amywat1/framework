/**
 * @file    recipe.c
 * @brief   洗车配方实现（装配编译期步骤表）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "domain/process/recipe.h"
#include "common/sw_error.h"

#include <stddef.h>

/*
 * 配方数据通过 #include 装配，编译期静态只读。
 * include 路径约定：CMakeLists 须将 项目根目录加入 include_directories，
 * 使 "config/recipes/..." 解析到 config/recipes/（已在 CMake 骨架中配置）。
 */
#include "config/recipes/standard_wash_recipe.h"
#include "config/recipes/quick_wash_recipe.h"

sw_err_t recipe_get(wash_mode_t mode, const wash_step_config_t **steps, int *count)
{
    if ((steps == NULL) || (count == NULL))
    {
        return SW_ERR_PARAM;
    }

    switch (mode)
    {
        case WASH_MODE_QUICK:
            *steps = s_quick_steps;
            *count = s_quick_count;
            return SW_OK;

        case WASH_MODE_STANDARD:
            *steps = s_standard_steps;
            *count = s_standard_count;
            return SW_OK;

        default:
            return SW_ERR_PARAM;
    }
}
