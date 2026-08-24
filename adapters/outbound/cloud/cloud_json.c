/**
 * @file    cloud_json.c
 * @brief   物模型的属性 JSON 门面与下行安装
 * @author  HUWANGWEI
 * @date    2026-08-04
 *
 * @note    点位表由 `cloud_model_entries()` 取，本文件不持有状态。
 */

#include "adapters/outbound/cloud/cloud_json.h"

#include "adapters/outbound/cloud/cloud_point_json.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "domain/cloud/cloud_model.h"

#include <stddef.h>

static cloud_property_reply_fn_t s_reply = NULL;

static sw_err_t model_on_property_set(const char *json_payload, point_apply_result_t *result)
{
    size_t                     count   = 0U;
    const cloud_point_entry_t *entries = cloud_model_entries(&count);

    if ((entries == NULL) || (count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_apply_json(entries, count, json_payload, result);
}

static void on_link_recv(const char *msg)
{
    point_apply_result_t result;
    sw_err_t             ret;

    point_apply_result_init(&result);
    ret = model_on_property_set(msg, &result);
    if (ret != SW_OK) {
        point_apply_result_record_error(&result, "", ret);
    }

    if (s_reply != NULL) {
        (void)s_reply(msg, &result);
    }
}

sw_err_t cloud_json_install(cloud_device_cmd_submit_fn_t submit, cloud_property_reply_fn_t reply)
{
    const cloud_link_ops_t *link;

    cloud_point_set_device_cmd_submit(submit);
    s_reply = reply;

    link = cloud_link_get_ops();
    if ((link != NULL) && (link->set_recv_handler != NULL)) {
        link->set_recv_handler(on_link_recv);
    }

    return SW_OK;
}

sw_err_t cloud_json_build_properties(char *buf, size_t buf_size)
{
    size_t                     count   = 0U;
    const cloud_point_entry_t *entries = cloud_model_entries(&count);

    if ((entries == NULL) || (count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_to_json(entries, count, buf, buf_size);
}

sw_err_t cloud_json_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size)
{
    size_t                     entry_count = 0U;
    const cloud_point_entry_t *entries     = cloud_model_entries(&entry_count);

    if ((entries == NULL) || (entry_count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_to_json_filtered(entries, entry_count, ids, count, buf, buf_size);
}

sw_err_t cloud_json_apply_property_set(const char *json_str, point_apply_result_t *result)
{
    return model_on_property_set(json_str, result);
}
