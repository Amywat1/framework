/**
 * @file    engine_program_json_internal.h
 * @brief   方案 JSON 加载器内部接口（schema / 模板 / 模型构建共用）
 *
 * @note    仅供 engine_program_json*.c 互调，不对外包含。
 */

#ifndef ADAPTERS_OUTBOUND_STORAGE_JSON_ENGINE_PROGRAM_JSON_INTERNAL_H
#define ADAPTERS_OUTBOUND_STORAGE_JSON_ENGINE_PROGRAM_JSON_INTERNAL_H

#include "adapters/outbound/storage/json/engine_program_json.h"
#include "third_party/cJSON/cJSON.h"

typedef struct {
    const cJSON *templates;
} json_build_ctx_t;

void        jfail(char *err, unsigned errsz, const char *fmt, const char *arg);
const char *jstr(const cJSON *o, const char *k);
bool        jint(const cJSON *o, const char *k, int *out);
bool        juint(const cJSON *o, const char *k, uint32_t *out);
bool        jdouble(const cJSON *o, const char *k, double *out);
void        copy_name(char *dst, unsigned cap, const char *src);

/**
 * @brief  按字段白名单校验方案 JSON
 * @return true 通过；false 失败并写入 err
 */
bool engine_program_json_validate_schema(const cJSON *root, char *err, unsigned errsz);

/**
 * @brief  按 use 字段展开步骤模板
 * @return 展开后的新对象（调用方 cJSON_Delete）；无 use 时返回 NULL 且不写 err；
 *         失败返回 NULL 且写入 err
 */
cJSON *engine_program_json_expand_step_template(const json_build_ctx_t *ctx,
                                                const cJSON            *node,
                                                char                   *err,
                                                unsigned                errsz);

#endif /* ADAPTERS_OUTBOUND_STORAGE_JSON_ENGINE_PROGRAM_JSON_INTERNAL_H */
