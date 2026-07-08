/**
 * @file    m8_cloud_register.h
 * @brief   M8 云端物模型一次注册接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_REGISTER_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_REGISTER_H

#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  wiring 阶段注册 M8 物模型 bundle（property_port + report_policy）
 */
sw_err_t m8_cloud_register(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_REGISTER_H */
