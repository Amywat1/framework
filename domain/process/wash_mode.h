/**
 * @file    wash_mode.h
 * @brief   洗车模式管理接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    负责当前洗车模式的读取与校验，解耦 svc_param 直接调用。
 */

#ifndef DOMAIN_PROCESS_WASH_MODE_H
#define DOMAIN_PROCESS_WASH_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/wash_types.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化洗车模式模块（从参数表加载默认模式）
 */
sw_err_t wash_mode_init(void);

/**
 * @brief  获取当前洗车模式
 */
wash_mode_t wash_mode_get(void);

/**
 * @brief  设置洗车模式
 * @retval SW_OK / SW_ERR_PARAM（mode 非法）
 */
sw_err_t wash_mode_set(wash_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PROCESS_WASH_MODE_H */
