/**
 * @file    point_table.c
 * @brief   点位表模型：结果汇总与查表实现
 * @author  HUWANGWEI
 * @date    2026-08-04
 *
 * @note    本文件只含与序列化格式无关的部分。JSON 编解码见
 *          `adapters/outbound/serialization/json/point_table_to_json.c`
 *          与 `point_table_from_json.c`。
 */

#include "common/point_table/point_table.h"

#include <string.h>

void point_apply_result_init(point_apply_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
}

void point_apply_result_record_error(point_apply_result_t *result, const char *id, sw_err_t err)
{
    if ((result == NULL) || (result->first_error != SW_OK)) {
        return;
    }

    result->first_error = err;
    if (id != NULL) {
        strncpy(result->first_error_id, id, sizeof(result->first_error_id) - 1U);
    }
}

const point_table_entry_t *point_table_find_entry_at(const void *entries,
                                                     size_t      count,
                                                     size_t      entry_stride,
                                                     const char *id)
{
    const char *row;

    if ((entries == NULL) || (id == NULL) || (entry_stride == 0U)) {
        return NULL;
    }

    for (size_t i = 0U; i < count; i++) {
        row = ((const char *)entries) + (i * entry_stride);
        if ((((const point_table_entry_t *)row)->id != NULL)
            && (strcmp(((const point_table_entry_t *)row)->id, id) == 0)) {
            return (const point_table_entry_t *)row;
        }
    }
    return NULL;
}

const point_table_entry_t *point_table_find_entry(const point_table_entry_t *entries, size_t count, const char *id)
{
    return point_table_find_entry_at(entries, count, sizeof(point_table_entry_t), id);
}
