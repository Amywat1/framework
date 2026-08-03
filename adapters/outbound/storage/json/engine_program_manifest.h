/**
 * @file    engine_program_manifest.h
 * @brief   控制方案文件完整性校验（SHA256 + manifest）
 * @author  huwangwei
 * @date    2026-07-08
 *
 * @note    manifest 由构建期 tools/gen_program_manifest.py 生成，运行期比对 JSON 文件哈希。
 */

#ifndef ADAPTERS_STORAGE_JSON_ENGINE_PROGRAM_MANIFEST_H
#define ADAPTERS_STORAGE_JSON_ENGINE_PROGRAM_MANIFEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>

/**
 * @brief manifest 路径缓冲建议长度
 *
 * 由方案路径推导 manifest 路径时后缀会增长 10 字节（.json → .manifest.json），
 * 调用方按此长度声明缓冲即可容纳常规部署路径。
 */
#define ENGINE_PROGRAM_MANIFEST_PATH_MAX 256U

/**
 * @brief  校验方案 JSON 文件与 manifest 记录一致
 * @param  json_path      方案 JSON 路径
 * @param  manifest_path  manifest 路径
 * @param  err            错误描述输出缓冲，可为空
 * @param  errsz          缓冲大小
 * @retval SW_OK         校验通过
 * @retval SW_ERR_CRC    哈希或字段不匹配
 * @retval SW_ERR_PARAM  参数或文件格式错误
 */
sw_err_t engine_program_manifest_verify(const char *json_path, const char *manifest_path, char *err, unsigned errsz);

/**
 * @brief  由 JSON 路径推导 manifest 路径（*.json → *.manifest.json）
 * @param  json_path   输入 JSON 路径
 * @param  out         输出缓冲
 * @param  outsz       缓冲大小
 * @return true=成功；false=缓冲不足或参数非法
 */
bool engine_program_manifest_path_from_json(const char *json_path, char *out, unsigned outsz);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_STORAGE_JSON_ENGINE_PROGRAM_MANIFEST_H */
