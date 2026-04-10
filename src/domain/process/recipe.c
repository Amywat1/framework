/**
 * @file    recipe.c
 * @brief   洗车配方实现（装配编译期步骤表）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/process/recipe.h"
#include "common/sw_error.h"

/* 配方数据通过 #include 装配，编译期静态只读 */
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
