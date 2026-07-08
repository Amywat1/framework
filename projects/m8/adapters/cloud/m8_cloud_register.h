/**
 * @file    m8_cloud_register.h
 * @brief   M8 云端物模型一次注册接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_REGISTER_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_REGISTER_H

#include "framework/common/sw_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  wiring 阶段注册 M8 物模型 bundle（property_port + report_policy）
 */
sw_err_t m8_cloud_register(void);

/** @brief  测试/CLI 直连入口 */
sw_err_t m8_cloud_build_properties(char *buf, size_t buf_size);
sw_err_t m8_cloud_on_property_set(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_REGISTER_H */
