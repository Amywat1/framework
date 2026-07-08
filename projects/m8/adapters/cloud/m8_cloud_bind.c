/**
 * @file    m8_cloud_bind.c
 * @brief   M8 云端传输绑定实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_bind.h"
#include "projects/m8/adapters/cloud/m8_cloud_model.h"
#include "framework/common/point_table/point_table.h"

sw_err_t m8_cloud_build_properties(char *buf, size_t buf_size)
{
    size_t                     count;
    const point_table_entry_t *entries = m8_cloud_model(&count);

    return point_table_to_json(entries, count, buf, buf_size);
}

sw_err_t m8_cloud_build_properties_delta(const char *const *ids,
                                          size_t count,
                                          char *buf,
                                          size_t buf_size)
{
    size_t                     model_count;
    const point_table_entry_t *entries = m8_cloud_model(&model_count);

    return point_table_to_json_filtered(entries, model_count, ids, count, buf, buf_size);
}

void m8_cloud_on_property_set(const char *json_str)
{
    size_t                     count;
    const point_table_entry_t *entries = m8_cloud_model(&count);

    point_table_from_json(entries, count, json_str);
}
