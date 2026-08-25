/**
 * @file    param_store.h
 * @brief   运行期参数存储端口接口（键值读写 + 持久化）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    `param_kv` 在内部调用此接口，屏蔽 JSON/SQLite 等存储细节。
 *          具体实现：adapters/outbound/storage/json/json_param_store.c
 *
 *          两层同在 storage 出站旁：
 *            param_store  — 原始键值存取（本文件）
 *            param_kv     — 整型/字符串便利访问（param_kv.h）
 *          具名键访问优先走 param_kv，避免在多处散落键名。
 */

#ifndef DOMAIN_PORTS_OUTBOUND_STORAGE_PARAM_STORE_H
#define DOMAIN_PORTS_OUTBOUND_STORAGE_PARAM_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stddef.h>

/* -------------------------------------------------------------------------
 * 参数存储操作表
 * ------------------------------------------------------------------------- */
typedef struct {
    /**
     * @brief  从存储加载参数（启动时调用一次）
     * @retval SW_OK（文件不存在时使用默认值，也返回 SW_OK）/ SW_ERR_STORAGE
     */
    sw_err_t (*load)(void);

    /**
     * @brief  将内存中的参数持久化到存储
     * @retval SW_OK / SW_ERR_STORAGE
     */
    sw_err_t (*save)(void);

    /**
     * @brief  按键名读取字符串值
     * @param  key       参数键名
     * @param  buf       输出缓冲区
     * @param  buf_size  缓冲区大小
     * @retval SW_OK / SW_ERR_PARAM（键不存在）
     */
    sw_err_t (*get)(const char *key, char *buf, size_t buf_size);

    /**
     * @brief  按键名写入字符串值（不自动持久化，需调用 save）
     * @param  key  参数键名
     * @param  val  参数值字符串
     * @retval SW_OK / SW_ERR_PARAM
     */
    sw_err_t (*set)(const char *key, const char *val);
} param_store_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取（由 bootstrap/wiring.c 调用）
 * ------------------------------------------------------------------------- */
sw_err_t                 param_store_register(const param_store_ops_t *ops);
const param_store_ops_t *param_store_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_STORAGE_PARAM_STORE_H */
