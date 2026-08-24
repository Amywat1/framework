/**
 * @file    cloud_model.c
 * @brief   项目物模型一次注册入口实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    本文件只持有点位表并提供查询，不涉及任何序列化格式。
 */

#include "domain/cloud/cloud_model.h"

#include "common/log.h"
#include "domain/cloud/cloud_point_watcher.h"

static const cloud_point_entry_t *s_entries     = NULL;
static size_t                     s_entry_count = 0U;

const cloud_point_entry_t *cloud_model_entries(size_t *count_out)
{
    if (count_out != NULL) {
        *count_out = (s_entries == NULL) ? 0U : s_entry_count;
    }
    return s_entries;
}

size_t cloud_model_point_count(void)
{
    return (s_entries == NULL) ? 0U : s_entry_count;
}

void cloud_model_reset_for_test(void)
{
    s_entries     = NULL;
    s_entry_count = 0U;
    cloud_point_watcher_reset_for_test();
}

sw_err_t cloud_model_register(const cloud_point_entry_t *entries, size_t count)
{
    sw_err_t ret;

    ret = cloud_point_validate(entries, count);
    if (ret != SW_OK) {
        return ret;
    }

    s_entries     = entries;
    s_entry_count = count;

    LOG_INFO("cloud_model: registered entries=%u", (unsigned)s_entry_count);
    return SW_OK;
}
