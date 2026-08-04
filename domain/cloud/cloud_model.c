/**
 * @file    cloud_model.c
 * @brief   项目物模型一次注册入口实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    本文件只持有点位表并提供查询与校验，不涉及任何序列化格式。
 *          属性 JSON 的构建/解析与 property_port 的安装在
 *          `adapters/outbound/cloud/cloud_model_json.c`——该端口的契约本身
 *          就是 JSON 载荷，实现它必须解析 JSON，故归适配层。
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
}

const char *cloud_model_point_id_by_index(uint32_t index)
{
    if ((s_entries == NULL) || (index >= s_entry_count)) {
        return NULL;
    }
    return s_entries[index].base.id;
}

sw_err_t cloud_model_register(const cloud_model_bundle_t *bundle)
{
    if ((bundle == NULL) || (bundle->entries == NULL) || (bundle->count == 0U)) {
        return SW_ERR_PARAM;
    }

    s_entries     = bundle->entries;
    s_entry_count = bundle->count;

    LOG_INFO("cloud_model: registered entries=%u", (unsigned)s_entry_count);
    return SW_OK;
}

sw_err_t cloud_model_validate(void)
{
    sw_err_t ret;

    if ((s_entries == NULL) || (s_entry_count == 0U)) {
        return SW_ERR_NOT_INIT;
    }

    ret = cloud_point_validate(s_entries, s_entry_count);
    if (ret != SW_OK) {
        LOG_ERROR("cloud_model: validate failed");
    }
    return ret;
}

sw_err_t cloud_model_init(void)
{
    sw_err_t ret;

    if ((s_entries == NULL) || (s_entry_count == 0U)) {
        return SW_ERR_NOT_INIT;
    }

    ret = cloud_point_watcher_init(s_entries, s_entry_count);
    if (ret != SW_OK) {
        LOG_ERROR("cloud_model: watcher init failed");
    }
    return ret;
}
