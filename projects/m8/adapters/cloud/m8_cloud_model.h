/**
 * @file    m8_cloud_model.h
 * @brief   M8 云端物模型点位表接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_MODEL_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_MODEL_H

#include "framework/cloud/cloud_point.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  获取 M8 云端物模型点位表
 */
const cloud_point_entry_t *m8_cloud_model(size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_MODEL_H */
