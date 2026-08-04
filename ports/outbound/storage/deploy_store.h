/**
 * @file    deploy_store.h
 * @brief   部署期配置存储端口接口（只读）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    存储设备 SN、站点 ID、服务器地址等出厂写入的只读配置。
 *          运行期不允许修改（没有 set / save 接口）。
 *          通用实现：framework/adapters/outbound/storage/json/json_deploy_store.c
 *          （配置文件路径由项目构建通过 DEPLOY_STORE_JSON_FILE_PATH 编译宏注入，
 *          样例见 projects/<project>/config/deployment/）
 */

#ifndef PORTS_OUTBOUND_STORAGE_DEPLOY_STORE_H
#define PORTS_OUTBOUND_STORAGE_DEPLOY_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stddef.h>

/* -------------------------------------------------------------------------
 * 部署配置存储操作表（只读）
 * ------------------------------------------------------------------------- */
typedef struct {
    /**
     * @brief  加载部署配置（启动时调用一次）
     * @retval SW_OK / SW_ERR_STORAGE
     */
    sw_err_t (*load)(void);

    /**
     * @brief  按键名读取配置值
     * @param  key       配置键名（如 "deviceName"、"topicPropertyUp"）
     * @param  buf       输出缓冲区
     * @param  buf_size  缓冲区大小
     * @retval SW_OK / SW_ERR_PARAM（键不存在）
     */
    sw_err_t (*get)(const char *key, char *buf, size_t buf_size);
} deploy_store_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取（由 bootstrap/wiring.c 调用）
 * ------------------------------------------------------------------------- */
sw_err_t                  deploy_store_register(const deploy_store_ops_t *ops);
const deploy_store_ops_t *deploy_store_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_OUTBOUND_STORAGE_DEPLOY_STORE_H */
