/**
 * @file    json_param_store_cfg.h
 * @brief   JSON 参数文件路径配置（adapter 级常量，不对外暴露）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    仅供 json_param_store.c 使用。
 *          文件路径属于存储适配器的部署参数，不属于 service 层语义。
 *          若需支持多机型不同路径，可通过 CMake DEFINE 覆盖此宏。
 */

#ifndef ADAPTERS_STORAGE_JSON_PARAM_STORE_CFG_H
#define ADAPTERS_STORAGE_JSON_PARAM_STORE_CFG_H

#ifndef PARAM_STORE_JSON_FILE_PATH
#define PARAM_STORE_JSON_FILE_PATH   "/home/neardi/m8/params.json"
#endif

#endif /* ADAPTERS_STORAGE_JSON_PARAM_STORE_CFG_H */
