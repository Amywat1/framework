/**
 * @file    svc_param.h
 * @brief   运行时参数管理接口（JSON 文件持久化）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef SVC_PARAM_H
#define SVC_PARAM_H

#include "common/sw_types.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 参数文件路径
 * ------------------------------------------------------------------------- */
#define SVC_PARAM_FILE_PATH     "/home/neardi/m8/params.json"

/* -------------------------------------------------------------------------
 * 参数键名（统一在此定义，避免散落在各模块）
 * ------------------------------------------------------------------------- */
#define PARAM_KEY_WASH_MODE         "washMode"          /* 洗车模式 */
#define PARAM_KEY_BRUSH_FREQ_TOP    "brushFreqTop"      /* 顶刷频率（0.01Hz）*/
#define PARAM_KEY_BRUSH_FREQ_SIDE   "brushFreqSide"     /* 侧刷频率（0.01Hz）*/
#define PARAM_KEY_GANTRY_FREQ_SLOW  "gantryFreqSlow"    /* 龙门慢速频率 */
#define PARAM_KEY_GANTRY_FREQ_FAST  "gantryFreqFast"    /* 龙门快速频率 */
#define PARAM_KEY_TOP_LIFT_DOWN_PUL "topLiftDownPulses" /* 顶刷下降脉冲数 */
#define PARAM_KEY_DEVICE_SN         "deviceSn"          /* 设备序列号 */

/* -------------------------------------------------------------------------
 * 接口声明
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化参数管理（从文件加载所有参数）
 * @retval SW_OK / SW_ERR_STORAGE（文件不存在时使用默认值）
 */
sw_err_t svc_param_init(void);

/**
 * @brief  读取整型参数
 * @param  key          参数键名
 * @param  default_val  文件中不存在时的默认值
 * @retval 参数值
 */
int svc_param_get_int(const char *key, int default_val);

/**
 * @brief  读取字符串参数
 * @param  key          参数键名
 * @param  buf          输出缓冲区
 * @param  buf_size     缓冲区大小
 * @param  default_val  默认值（buf 未找到时写入）
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t svc_param_get_str(const char *key, char *buf, int buf_size,
                            const char *default_val);

/**
 * @brief  写入整型参数（内存中立即生效，持久化需调 svc_param_save）
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t svc_param_set_int(const char *key, int val);

/**
 * @brief  写入字符串参数
 */
sw_err_t svc_param_set_str(const char *key, const char *val);

/**
 * @brief  将当前所有参数持久化到文件
 * @retval SW_OK / SW_ERR_STORAGE
 */
sw_err_t svc_param_save(void);

/**
 * @brief  CLI 调试接口（param 命令域）
 * 命令：get <key> / set <key> <val> / save
 */
int param_debug_ctl(char *cmd, char *p1, char *p2);

#endif /* SVC_PARAM_H */
