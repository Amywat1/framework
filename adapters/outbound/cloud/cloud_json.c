/**
 * @file    cloud_json.c
 * @brief   物模型的属性 JSON 门面与下行安装
 * @author  HUWANGWEI
 * @date    2026-08-04
 *
 * @note    点位表由 `cloud_model_entries()` 取。下行回显 JSON 放静态区，避免 8KB 进栈。
 *          脉冲空闲 0 推迟 CLOUD_PULSE_ECHO_HOLD_MS 再发，由 cloud_json_poll() 到期上报。
 */

#include "adapters/outbound/cloud/cloud_json.h"

#include "adapters/outbound/cloud/cloud_point_json.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/log.h"
#include "common/time_util.h"
#include "domain/cloud/cloud_model.h"
#include "domain/cloud/cloud_point_watcher.h"
#include "third_party/cJSON/cJSON.h"

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static cloud_property_reply_fn_t s_reply = NULL;
static cloud_report_allow_fn_t   s_allow = NULL;
static char                      s_echo_json[CLOUD_REPORT_JSON_MAX];
static char                      s_idle_json[CLOUD_REPORT_JSON_MAX];
static char                      s_pending_idle[CLOUD_REPORT_JSON_MAX];
static char                      s_idle_send[CLOUD_REPORT_JSON_MAX];
static bool                      s_idle_pending = false;
static uint64_t                  s_idle_since_ms = 0U;
static pthread_mutex_t           s_idle_mu = PTHREAD_MUTEX_INITIALIZER;

static sw_err_t model_on_property_set(const char *json_payload, point_apply_result_t *result)
{
    size_t                     count   = 0U;
    const cloud_point_entry_t *entries = cloud_model_entries(&count);

    if ((entries == NULL) || (count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    return cloud_point_apply_json(entries, count, json_payload, result);
}

static bool json_object_empty(const char *json)
{
    return (json == NULL) || (json[0] == '\0') || (strcmp(json, "{}") == 0);
}

static sw_err_t write_empty_object(char *buf, size_t buf_size)
{
    if ((buf == NULL) || (buf_size < 3U)) {
        return SW_ERR_OVERFLOW;
    }
    buf[0] = '{';
    buf[1] = '}';
    buf[2] = '\0';
    return SW_OK;
}

static bool id_allowed(const char *id)
{
    return (s_allow == NULL) || ((id != NULL) && s_allow(id));
}

static sw_err_t build_snapshot(char *buf, size_t buf_size, bool apply_filter)
{
    size_t                     count   = 0U;
    const cloud_point_entry_t *entries = cloud_model_entries(&count);
    const char                *ids[CLOUD_POINT_TABLE_MAX];
    size_t                     n       = 0U;

    if ((entries == NULL) || (count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    if ((buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }
    if (!apply_filter || (s_allow == NULL)) {
        return cloud_point_to_json(entries, count, buf, buf_size);
    }

    for (size_t i = 0U; i < count; i++) {
        if (!cloud_point_snapshot_enabled(&entries[i])) {
            continue;
        }
        if (!id_allowed(entries[i].base.id)) {
            continue;
        }
        if (n >= CLOUD_POINT_TABLE_MAX) {
            return SW_ERR_OVERFLOW;
        }
        ids[n++] = entries[i].base.id;
    }
    if (n == 0U) {
        return write_empty_object(buf, buf_size);
    }
    return cloud_point_to_json_filtered(entries, count, ids, n, buf, buf_size);
}

static sw_err_t publish_json_if_present(const cloud_link_ops_t *ops, const char *json)
{
    if ((ops == NULL) || (ops->publish_properties_json == NULL) || json_object_empty(json)) {
        return SW_ERR_PARAM;
    }
    return ops->publish_properties_json(json);
}

static void copy_json(char *dst, size_t dst_size, const char *src)
{
    size_t n;

    if ((dst == NULL) || (dst_size == 0U) || (src == NULL)) {
        return;
    }
    n = strlen(src);
    if (n >= dst_size) {
        n = dst_size - 1U;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/**
 * @brief  把 add 中的键并入 dst；失败则用 add 覆盖 dst
 */
static void merge_idle_json(char *dst, size_t dst_size, const char *add)
{
    cJSON *dst_obj;
    cJSON *add_obj;
    cJSON *item;
    char  *printed;
    size_t n;

    if ((dst == NULL) || (dst_size == 0U) || (add == NULL) || json_object_empty(add)) {
        return;
    }
    if ((dst[0] == '\0') || json_object_empty(dst)) {
        copy_json(dst, dst_size, add);
        return;
    }

    dst_obj = cJSON_Parse(dst);
    add_obj = cJSON_Parse(add);
    if ((dst_obj == NULL) || (add_obj == NULL) || !cJSON_IsObject(dst_obj) || !cJSON_IsObject(add_obj)) {
        cJSON_Delete(dst_obj);
        cJSON_Delete(add_obj);
        copy_json(dst, dst_size, add);
        return;
    }

    cJSON_ArrayForEach(item, add_obj)
    {
        cJSON *dup;

        if ((item->string == NULL) || (item->string[0] == '\0')) {
            continue;
        }
        dup = cJSON_Duplicate(item, 1);
        if (dup == NULL) {
            continue;
        }
        cJSON_DeleteItemFromObjectCaseSensitive(dst_obj, item->string);
        cJSON_AddItemToObject(dst_obj, item->string, dup);
    }

    printed = cJSON_PrintUnformatted(dst_obj);
    cJSON_Delete(dst_obj);
    cJSON_Delete(add_obj);
    if (printed == NULL) {
        copy_json(dst, dst_size, add);
        return;
    }
    n = strlen(printed);
    if (n >= dst_size) {
        copy_json(dst, dst_size, add);
    } else {
        memcpy(dst, printed, n + 1U);
    }
    free(printed);
}

static void schedule_idle_json(const char *idle_json)
{
    if (json_object_empty(idle_json)) {
        return;
    }

    pthread_mutex_lock(&s_idle_mu);
    merge_idle_json(s_pending_idle, sizeof(s_pending_idle), idle_json);
    s_idle_since_ms = time_util_get_ms();
    s_idle_pending  = true;
    pthread_mutex_unlock(&s_idle_mu);
}

static void report_downlink_outcome(const char *request_json, const point_apply_result_t *result)
{
    const cloud_link_ops_t    *ops;
    const cloud_point_entry_t *entries;
    size_t                     count = 0U;
    sw_err_t                   ret;

    if ((request_json == NULL) || (result == NULL)) {
        return;
    }

    entries = cloud_model_entries(&count);
    if ((entries == NULL) || (count == 0U)) {
        return;
    }

    ret = cloud_point_to_json_downlink(entries, count, request_json, result->applied_ids, result->applied_id_count,
                                       s_echo_json, sizeof(s_echo_json), s_idle_json, sizeof(s_idle_json));
    if (ret != SW_OK) {
        return;
    }

    ops = cloud_link_get_ops();
    if (publish_json_if_present(ops, s_echo_json) != SW_OK) {
        return;
    }

    cloud_point_watcher_sync_ids(result->applied_ids, result->applied_id_count);
    schedule_idle_json(s_idle_json);
}

void cloud_json_poll(void)
{
    const cloud_link_ops_t *ops;
    uint64_t                now_ms;
    bool                    due = false;

    pthread_mutex_lock(&s_idle_mu);
    if (s_idle_pending) {
        now_ms = time_util_get_ms();
        if (time_elapsed_ms(s_idle_since_ms, now_ms) >= CLOUD_PULSE_ECHO_HOLD_MS) {
            copy_json(s_idle_send, sizeof(s_idle_send), s_pending_idle);
            s_pending_idle[0] = '\0';
            s_idle_pending    = false;
            due               = true;
        }
    }
    pthread_mutex_unlock(&s_idle_mu);

    if (!due) {
        return;
    }

    ops = cloud_link_get_ops();
    if (publish_json_if_present(ops, s_idle_send) == SW_OK) {
        return;
    }

    pthread_mutex_lock(&s_idle_mu);
    merge_idle_json(s_pending_idle, sizeof(s_pending_idle), s_idle_send);
    s_idle_pending = true;
    pthread_mutex_unlock(&s_idle_mu);
}

void cloud_json_reset_for_test(void)
{
    pthread_mutex_lock(&s_idle_mu);
    s_pending_idle[0] = '\0';
    s_idle_send[0]    = '\0';
    s_idle_pending    = false;
    s_idle_since_ms   = 0U;
    pthread_mutex_unlock(&s_idle_mu);
}

static void on_link_recv(const char *msg)
{
    point_apply_result_t result;
    sw_err_t             ret;

    point_apply_result_init(&result);
    ret = model_on_property_set(msg, &result);
    if (ret != SW_OK) {
        point_apply_result_record_error(&result, "", ret);
    } else {
        report_downlink_outcome(msg, &result);
    }

    if (s_reply != NULL) {
        (void)s_reply(msg, &result);
    }

    LOG_DEBUG("cloud_json: down applied=%u rejected=%u ret=%d",
             (unsigned)result.applied,
             (unsigned)result.rejected,
             (int)ret);
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

void cloud_json_set_report_allow(cloud_report_allow_fn_t fn)
{
    s_allow = fn;
}

sw_err_t cloud_json_build_properties(char *buf, size_t buf_size)
{
    return build_snapshot(buf, buf_size, true);
}

sw_err_t cloud_json_build_properties_all(char *buf, size_t buf_size)
{
    return build_snapshot(buf, buf_size, false);
}

sw_err_t cloud_json_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size)
{
    size_t                     entry_count = 0U;
    const cloud_point_entry_t *entries     = cloud_model_entries(&entry_count);
    const char                *kept[CLOUD_POINT_TABLE_MAX];
    size_t                     n           = 0U;

    if ((entries == NULL) || (entry_count == 0U)) {
        return SW_ERR_NOT_INIT;
    }
    if ((ids == NULL) || (count == 0U) || (buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    for (size_t i = 0U; i < count; i++) {
        if (!id_allowed(ids[i])) {
            continue;
        }
        if (n >= CLOUD_POINT_TABLE_MAX) {
            return SW_ERR_OVERFLOW;
        }
        kept[n++] = ids[i];
    }
    if (n == 0U) {
        return write_empty_object(buf, buf_size);
    }
    return cloud_point_to_json_filtered(entries, entry_count, kept, n, buf, buf_size);
}

sw_err_t cloud_json_apply_property_set(const char *json_str, point_apply_result_t *result)
{
    return model_on_property_set(json_str, result);
}
