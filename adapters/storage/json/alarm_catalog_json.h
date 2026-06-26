/**
 * @file    alarm_catalog_json.h
 * @brief   报警目录 JSON 配置加载器（cJSON）→ alarm_def_t[]
 * @author  huwangwei
 * @date    2026-06-26
 *
 * @note    报警目录（code/level/clear/desc）以 JSON 为唯一来源，设备运行期由本
 *          加载器（基于工程已有的 cJSON）解析为报警定义数组，再经 alarm_core_load()
 *          注入报警核心。本加载器只负责「报警定义」，不涉及「信号→报警码」的检测
 *          绑定（那属机型检测层）。解析失败返回错误码并给出中文描述，调用方应据此
 *          回退到 alarm_core 的内置兜底目录。
 */

#ifndef ADAPTERS_STORAGE_JSON_ALARM_CATALOG_JSON_H
#define ADAPTERS_STORAGE_JSON_ALARM_CATALOG_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/model/alarm_code.h"

/**
 * @brief  从 JSON 文件解析报警目录到 alarm_def_t 数组
 * @param  path       JSON 文件路径
 * @param  out        输出数组（调用方提供，容量 max）
 * @param  max        out 容量（条目数）
 * @param  out_count  实际解析出的条目数（成功时有效）
 * @param  err        错误描述输出缓冲（可为空）
 * @param  errsz      err 缓冲大小
 * @retval SW_OK 成功；其余为失败（文件/解析/字段/超容量等），err 含中文描述
 */
sw_err_t alarm_catalog_load_json_file(const char *path,
                                      alarm_def_t *out, unsigned max,
                                      unsigned *out_count,
                                      char *err, unsigned errsz);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_STORAGE_JSON_ALARM_CATALOG_JSON_H */
