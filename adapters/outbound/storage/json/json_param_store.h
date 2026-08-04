/**
 * @file    json_param_store.h
 * @brief   JSON 文件参数存储适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    实现 param_store_ops_t，底层使用 cJSON + fopen 读写 JSON 文件。
 *          由 wiring.c 调用 json_param_store_register() 注册到 param_store 端口。
 */

#ifndef ADAPTERS_OUTBOUND_STORAGE_JSON_JSON_PARAM_STORE_H
#define ADAPTERS_OUTBOUND_STORAGE_JSON_JSON_PARAM_STORE_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 JSON 文件参数存储实现到 param_store 端口。
 *
 * @note   本函数只注册 ops，不保存项目路径，也不执行文件读写。
 */
void json_param_store_register(void);

/**
 * @brief  配置参数 JSON 文件路径。
 *
 * @param  path JSON 文件路径，必须为非空字符串。
 * @retval SW_OK 配置成功。
 * @retval SW_ERR_PARAM 路径为空或长度超过内部缓冲区。
 * @note   未调用本函数时，适配器使用 PARAM_STORE_JSON_FILE_PATH 编译宏作为默认路径。
 */
sw_err_t json_param_store_configure(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_STORAGE_JSON_JSON_PARAM_STORE_H */
