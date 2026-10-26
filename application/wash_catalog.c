/**
 * @file    wash_catalog.c
 * @brief   套餐号到方案路径与超时的注册表
 */

#include "application/wash_catalog.h"

#include <stddef.h>

static const wash_catalog_entry_t *s_rows;
static unsigned                    s_count;

/**
 * @brief  校验一套待登记行
 */
static sw_err_t validate_rows(const wash_catalog_entry_t *rows, unsigned count)
{
    unsigned i;
    unsigned j;

    if ((rows == NULL) || (count == 0U)) {
        return SW_ERR_PARAM;
    }
    if (count > WASH_CATALOG_MAX) {
        return SW_ERR_OVERFLOW;
    }
    for (i = 0U; i < count; i++) {
        if ((rows[i].program_path == NULL) || (rows[i].program_path[0] == '\0')
            || (rows[i].timeout_ms == 0U) || (rows[i].mode == WASH_MODE_INVALID)) {
            return SW_ERR_PARAM;
        }
        for (j = 0U; j < i; j++) {
            if (rows[j].mode == rows[i].mode) {
                return SW_ERR_PARAM;
            }
        }
    }
    return SW_OK;
}

sw_err_t wash_catalog_bind(const wash_catalog_entry_t *rows, unsigned count)
{
    sw_err_t ret = validate_rows(rows, count);

    if (ret != SW_OK) {
        return ret;
    }
    if ((s_rows == rows) && (s_count == count)) {
        return SW_OK;
    }
    s_rows  = rows;
    s_count = count;
    return SW_OK;
}

bool wash_catalog_find(wash_mode_t mode, wash_catalog_entry_t *out)
{
    unsigned i;

    for (i = 0U; i < s_count; i++) {
        if (s_rows[i].mode == mode) {
            if (out != NULL) {
                *out = s_rows[i];
            }
            return true;
        }
    }
    return false;
}

void wash_catalog_reset_for_test(void)
{
    s_rows  = NULL;
    s_count = 0U;
}
