/**
 * @file    cloud_model.c
 * @brief   项目物模型一次注册入口实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "framework/cloud/cloud_model.h"
#include "framework/cloud/cloud_point_watcher.h"
#include "framework/ports/inbound/cloud/property/property_port.h"
#include "framework/common/log.h"

static const cloud_point_entry_t   *s_entries       = NULL;
static size_t                       s_entry_count   = 0U;
static const report_policy_entry_t *s_policies        = NULL;
static size_t                       s_policy_count  = 0U;

static sw_err_t model_on_property_set(const char *json_payload,
                                       point_apply_result_t *result)
{
    if ((s_entries == NULL) || (s_entry_count == 0U))
    {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_apply_json(s_entries, s_entry_count, json_payload, result);
}

static const char *model_point_id_by_index(uint32_t index)
{
    if ((s_entries == NULL) || (index >= s_entry_count))
    {
        return NULL;
    }
    return s_entries[index].base.id;
}

sw_err_t cloud_model_register(const cloud_model_bundle_t *bundle)
{
    cloud_property_ops_t ops;

    if ((bundle == NULL) || (bundle->entries == NULL) || (bundle->count == 0U))
    {
        return SW_ERR_PARAM;
    }

    s_entries        = bundle->entries;
    s_entry_count    = bundle->count;
    s_policies       = bundle->report_policies;
    s_policy_count   = bundle->policy_count;

    ops.on_property_set    = model_on_property_set;
    ops.reply_property_set = bundle->property_reply;
    cloud_property_register(&ops);

    LOG_INFO("cloud_model: registered entries=%u", (unsigned)s_entry_count);
    return SW_OK;
}

sw_err_t cloud_model_validate_and_watch(void)
{
    sw_err_t ret;

    if ((s_entries == NULL) || (s_entry_count == 0U))
    {
        return SW_ERR_NOT_INIT;
    }

    ret = cloud_point_validate(s_entries, s_entry_count);
    if (ret != SW_OK)
    {
        LOG_ERROR("cloud_model: validate failed");
        return ret;
    }

    ret = cloud_point_watcher_init(s_entries, s_entry_count);
    if (ret != SW_OK)
    {
        LOG_ERROR("cloud_model: watcher init failed");
    }
    return ret;
}

sw_err_t cloud_model_start_scheduler(void)
{
    if ((s_policies == NULL) || (s_policy_count == 0U))
    {
        return SW_ERR_NOT_INIT;
    }

    report_scheduler_register_point_resolver(model_point_id_by_index);
    return report_scheduler_init(s_policies, s_policy_count);
}

sw_err_t cloud_model_build_properties(char *buf, size_t buf_size)
{
    if ((s_entries == NULL) || (s_entry_count == 0U))
    {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_to_json(s_entries, s_entry_count, buf, buf_size);
}

sw_err_t cloud_model_build_properties_delta(const char *const *ids,
                                             size_t count,
                                             char *buf,
                                             size_t buf_size)
{
    if ((s_entries == NULL) || (s_entry_count == 0U))
    {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_to_json_filtered(s_entries, s_entry_count, ids, count, buf, buf_size);
}

sw_err_t cloud_model_apply_property_set(const char *json_str,
                                         point_apply_result_t *result)
{
    return model_on_property_set(json_str, result);
}
