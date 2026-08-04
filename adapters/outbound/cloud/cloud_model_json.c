/**
 * @file    cloud_model_json.c
 * @brief   物模型的属性 JSON 门面与 property_port 安装
 * @author  HUWANGWEI
 * @date    2026-08-04
 *
 * @note    为何在适配层：`cloud_property_port.on_property_set` 的契约参数是
 *          JSON 载荷字符串，实现它必须解析 JSON。把它留在 `cloud/` 会让该
 *          目录依赖编解码，也让物模型的语义规则与一种传输格式绑定。
 *
 * @note    点位表由 `cloud_model_entries()` 取，本文件不持有状态——避免与
 *          `cloud_model.c` 出现两份可能不一致的点位表引用。
 */

#include "adapters/outbound/cloud/cloud_model_json.h"

#include "adapters/outbound/cloud/cloud_point_json.h"
#include "domain/cloud/cloud_model.h"
#include "ports/inbound/cloud/property/property_port.h"

#include <stddef.h>

static cloud_property_ops_t s_property_ops;

static sw_err_t model_on_property_set(const char *json_payload, point_apply_result_t *result)
{
    size_t                     count   = 0U;
    const cloud_point_entry_t *entries = cloud_model_entries(&count);

    if ((entries == NULL) || (count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_apply_json(entries, count, json_payload, result);
}

sw_err_t cloud_model_json_install(cloud_property_reply_fn_t reply)
{
    s_property_ops.on_property_set    = model_on_property_set;
    s_property_ops.reply_property_set = reply;
    return cloud_property_register(&s_property_ops);
}

sw_err_t cloud_model_build_properties(char *buf, size_t buf_size)
{
    size_t                     count   = 0U;
    const cloud_point_entry_t *entries = cloud_model_entries(&count);

    if ((entries == NULL) || (count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_to_json(entries, count, buf, buf_size);
}

sw_err_t cloud_model_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size)
{
    size_t                     entry_count = 0U;
    const cloud_point_entry_t *entries     = cloud_model_entries(&entry_count);

    if ((entries == NULL) || (entry_count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_to_json_filtered(entries, entry_count, ids, count, buf, buf_size);
}

sw_err_t cloud_model_apply_property_set(const char *json_str, point_apply_result_t *result)
{
    return model_on_property_set(json_str, result);
}
