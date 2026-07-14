/**
 * @file    json_deploy_store.h
 * @brief   JSON 文件部署配置存储适配器接口
 */

#ifndef ADAPTERS_STORAGE_JSON_DEPLOY_STORE_H
#define ADAPTERS_STORAGE_JSON_DEPLOY_STORE_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 JSON 文件部署配置存储实现到 deploy_store 端口。
 *
 * @note   本函数只注册 ops，不保存项目路径，也不执行文件读写。
 */
void json_deploy_store_register(void);

/**
 * @brief  配置部署 JSON 文件路径。
 *
 * @param  path JSON 文件路径，必须为非空字符串。
 * @retval SW_OK 配置成功。
 * @retval SW_ERR_PARAM 路径为空或长度超过内部缓冲区。
 * @note   未调用本函数时，适配器使用 DEPLOY_STORE_JSON_FILE_PATH 编译宏作为默认路径。
 */
sw_err_t json_deploy_store_configure(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_STORAGE_JSON_DEPLOY_STORE_H */
