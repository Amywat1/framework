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
 * @brief 框架当前支持的部署配置 schema 版本
 *
 * @note  兼容规则见 common/asset_version.h：主版本不同或资产次版本更高即拒绝
 *        加载。新增可选字段时提升次版本，删改既有字段含义时提升主版本。
 * @note  部署 JSON 必须声明 schemaVersion 字段，缺失即拒绝加载——该文件承载
 *        设备身份与云端主题，静默接受一份不声明版本的配置，等于让字段含义的
 *        任何变更都无从被发现。
 * @note  load 的两种失败要分开看：版本串缺失或无法解析返回 SW_ERR_PARAM，
 *        版本可解析但不兼容返回 SW_ERR_STATE（资产本身合法、只是与本框架不配）。
 */
#define DEPLOY_CONFIG_SCHEMA_SUPPORTED "1.0"

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
