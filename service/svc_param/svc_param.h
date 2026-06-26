/**
 * @file    svc_param.h
 * @brief   运行时参数服务接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    对外提供统一的参数访问接口。
 *          持久化能力由存储端口实现提供。
 */

#ifndef SERVICE_SVC_PARAM_H
#define SERVICE_SVC_PARAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 参数键名（统一在此定义）
 * 参数存储路径由存储适配器（adapters/storage/json/json_param_store_cfg.h）管理，
 * service 层不感知具体路径。
 * ------------------------------------------------------------------------- */
#define PARAM_KEY_WASH_MODE         "washMode"          /* 洗车模式（wash_mode_t）*/
/* 待规划：洗车单量（washCount）*/

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化参数管理（从文件加载；文件不存在时使用编译期默认值）
 * @retval SW_OK / SW_ERR_STORAGE（降级运行，不影响启动）
 */
sw_err_t svc_param_init(void);

/**
 * @brief  读取整型参数
 * @param  key          参数键名
 * @param  default_val  文件中不存在时的默认值
 */
int svc_param_get_int(const char *key, int default_val);

/**
 * @brief  读取字符串参数
 */
sw_err_t svc_param_get_str(const char *key, char *buf, int buf_size,
                            const char *default_val);

/**
 * @brief  写入整型参数（内存中立即生效，持久化需调 svc_param_save）
 */
sw_err_t svc_param_set_int(const char *key, int val);

/**
 * @brief  写入字符串参数
 */
sw_err_t svc_param_set_str(const char *key, const char *val);

/**
 * @brief  将当前所有参数持久化到文件
 */
sw_err_t svc_param_save(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_SVC_PARAM_H */
