/**
 * @file    svc_param.h
 * @brief   运行时参数管理接口（JSON 文件持久化）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    轻量改造自原 service/svc_param.h，接口保持不变。
 *          Phase 6 TODO: 底层持久化改为调用 param_store_ops 接口。
 */

#ifndef SERVICE_SVC_PARAM_H
#define SERVICE_SVC_PARAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 参数文件路径
 * ------------------------------------------------------------------------- */
#define SVC_PARAM_FILE_PATH     "/home/neardi/m8/params.json"

/* -------------------------------------------------------------------------
 * 参数键名（统一在此定义）
 * ------------------------------------------------------------------------- */
#define PARAM_KEY_WASH_MODE         "washMode"          /* 洗车模式（wash_mode_t）*/
#define PARAM_KEY_BRUSH_FREQ_TOP    "brushFreqTop"      /* 顶刷频率（0.01Hz，默认 4500）*/
#define PARAM_KEY_BRUSH_FREQ_SIDE   "brushFreqSide"     /* 侧刷频率（0.01Hz，默认 4500）*/
#define PARAM_KEY_GANTRY_FREQ_SLOW  "gantryFreqSlow"    /* 龙门慢速（0.01Hz）*/
#define PARAM_KEY_GANTRY_FREQ_FAST  "gantryFreqFast"    /* 龙门快速（0.01Hz）*/
#define PARAM_KEY_TOP_LIFT_DOWN_PUL "topLiftDownPulses" /* 顶刷下降脉冲数（默认 500）*/
#define PARAM_KEY_DEVICE_SN         "deviceSn"          /* 设备序列号 */

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
