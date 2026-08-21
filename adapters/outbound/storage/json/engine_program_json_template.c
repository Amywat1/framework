/**
 * @file    engine_program_json_template.c
 * @brief   方案 JSON 步骤模板查找与展开
 * @author  huwangwei
 * @date    2026-08-21
 */

#include "adapters/outbound/storage/json/engine_program_json_internal.h"

#include <string.h>

static const cJSON *find_step_template(const cJSON *templates, const char *id)
{
    if ((templates == NULL) || (id == NULL)) {
        return NULL;
    }

    if (cJSON_IsObject(templates)) {
        return cJSON_GetObjectItemCaseSensitive(templates, id);
    }

    if (cJSON_IsArray(templates)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, templates)
        {
            const char *tid = jstr(item, "id");
            if ((tid != NULL) && (strcmp(tid, id) == 0)) {
                return item;
            }
        }
    }

    return NULL;
}

static bool object_replace_or_add(cJSON *dst, const cJSON *src_item)
{
    cJSON *dup = cJSON_Duplicate(src_item, true);
    if (dup == NULL) {
        return false;
    }

    if (cJSON_GetObjectItemCaseSensitive(dst, src_item->string) != NULL) {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(dst, src_item->string, dup)) {
            cJSON_Delete(dup);
            return false;
        }
        return true;
    }

    if (!cJSON_AddItemToObject(dst, src_item->string, dup)) {
        cJSON_Delete(dup);
        return false;
    }
    return true;
}

cJSON *engine_program_json_expand_step_template(const json_build_ctx_t *ctx,
                                                const cJSON            *node,
                                                char                   *err,
                                                unsigned                errsz)
{
    const char *use = jstr(node, "use");
    if (use == NULL) {
        return NULL;
    }

    const cJSON *templ = find_step_template((ctx != NULL) ? ctx->templates : NULL, use);
    if (templ == NULL) {
        jfail(err, errsz, "未知步骤模板: %s", use);
        return NULL;
    }
    if (!cJSON_IsObject(templ)) {
        jfail(err, errsz, "步骤模板不是对象: %s", use);
        return NULL;
    }

    cJSON *merged = cJSON_Duplicate(templ, true);
    if (merged == NULL) {
        jfail(err, errsz, "%s", "内存不足");
        return NULL;
    }

    const cJSON *field = NULL;
    cJSON_ArrayForEach(field, node)
    {
        if ((field->string == NULL) || (strcmp(field->string, "use") == 0)) {
            continue;
        }
        if (!object_replace_or_add(merged, field)) {
            cJSON_Delete(merged);
            jfail(err, errsz, "%s", "内存不足");
            return NULL;
        }
    }

    return merged;
}
