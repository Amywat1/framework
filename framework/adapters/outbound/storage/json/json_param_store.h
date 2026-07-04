/**
 * @file    json_param_store.h
 * @brief   JSON 文件参数存储适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    实现 param_store_ops_t，底层使用 cJSON + fopen 读写 JSON 文件。
 *          由 wiring.c 调用 json_param_store_register() 注册到 param_store 端口。
 */

#ifndef ADAPTERS_STORAGE_JSON_PARAM_STORE_H
#define ADAPTERS_STORAGE_JSON_PARAM_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 JSON 文件参数存储实现到 param_store 端口
 */
void json_param_store_register(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_STORAGE_JSON_PARAM_STORE_H */
