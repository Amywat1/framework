/**
 * @file    param_kv.h
 * @brief   运行参数具名访问（param_store 上的整型/字符串便利层）
 *
 * @note    不是独立分层：读写仍经已注册的 param_store 端口。
 *          持久化路径由项目通过 PARAM_STORE_JSON_FILE_PATH 注入适配器，
 *          本头不感知文件路径。
 */

#ifndef DOMAIN_PORTS_OUTBOUND_STORAGE_PARAM_KV_H
#define DOMAIN_PORTS_OUTBOUND_STORAGE_PARAM_KV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 参数键名（统一在此定义）
 * 参数存储路径由项目构建通过 PARAM_STORE_JSON_FILE_PATH 编译宏注入
 * （见 adapters/outbound/storage/json/json_param_store.c），
 * 本便利层不感知具体路径。
 * ------------------------------------------------------------------------- */
#define PARAM_KEY_WASH_MODE "washMode" /* 洗车模式（wash_mode_t）*/
/* 待规划：洗车单量（washCount）*/

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化参数管理（从文件加载；文件不存在时使用编译期默认值）
 * @retval SW_OK / SW_ERR_STORAGE（降级运行，不影响启动）
 */
sw_err_t param_kv_init(void);

/**
 * @brief  读取整型参数
 * @param  key          参数键名
 * @param  default_val  文件中不存在时的默认值
 */
int param_kv_get_int(const char *key, int default_val);

/**
 * @brief  读取字符串参数
 */
sw_err_t param_kv_get_str(const char *key, char *buf, int buf_size, const char *default_val);

/**
 * @brief  写入整型参数（内存中立即生效，持久化需调 param_kv_save）
 */
sw_err_t param_kv_set_int(const char *key, int val);

/**
 * @brief  写入字符串参数
 */
sw_err_t param_kv_set_str(const char *key, const char *val);

/**
 * @brief  将当前所有参数持久化到文件
 */
sw_err_t param_kv_save(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_STORAGE_PARAM_KV_H */
