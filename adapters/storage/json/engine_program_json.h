/**
 * @file    engine_program_json.h
 * @brief   洗车方案 JSON 配置加载器（cJSON）→ engine_program_t
 * @author  huwangwei
 * @date    2026-06-26
 *
 * @note    方案以 YAML 为人维护源文件，构建期由 tools/yaml2json.py 转为 JSON，
 *          设备运行期用本加载器（基于工程已有的 cJSON）解析为引擎数据模型。
 *          表达式字段在加载期编译为 engine_expr_t。仅覆盖规格子集；遇到
 *          未支持字段或结构会返回错误并给出描述（schema 校验是引擎职责）。
 */

#ifndef ADAPTERS_STORAGE_JSON_ENGINE_PROGRAM_JSON_H
#define ADAPTERS_STORAGE_JSON_ENGINE_PROGRAM_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/engine/engine_model.h"

/**
 * @brief  解析 JSON 文本为方案模型
 * @param  json   JSON 文本（以 \0 结尾）
 * @param  err    错误描述输出缓冲（可为空）
 * @param  errsz  缓冲大小
 * @return 成功返回方案指针（调用方负责 engine_program_free）；失败返回 NULL
 */
engine_program_t *engine_program_load_json_string(const char *json, char *err, unsigned errsz);

/**
 * @brief  从 JSON 文件解析方案模型
 * @param  path   文件路径
 * @param  err    错误描述输出缓冲（可为空）
 * @param  errsz  缓冲大小
 * @return 成功返回方案指针（调用方负责 engine_program_free）；失败返回 NULL
 */
engine_program_t *engine_program_load_json_file(const char *path, char *err, unsigned errsz);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_STORAGE_JSON_ENGINE_PROGRAM_JSON_H */
