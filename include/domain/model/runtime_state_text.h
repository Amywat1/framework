#ifndef DOMAIN_MODEL_RUNTIME_STATE_TEXT_H
#define DOMAIN_MODEL_RUNTIME_STATE_TEXT_H

#include "domain/model/domain_enums.h"

/**
 * @file runtime_state_text.h
 * @brief 声明运行时状态枚举的统一文本映射。
 */

/**
 * @brief 将设备状态转换为稳定文本。
 * @param device_state 设备状态。
 * @return 对应文本；未知值返回 `"stopped"`。
 */
const char *runtime_state_text_device_state(device_state_t device_state);

/**
 * @brief 将会话状态转换为稳定文本。
 * @param session_state 会话状态。
 * @return 对应文本；未知值返回 `"none"`。
 */
const char *runtime_state_text_session_state(session_state_t session_state);

/**
 * @brief 将执行状态转换为稳定文本。
 * @param execution_state 执行状态。
 * @return 对应文本；未知值返回 `"none"`。
 */
const char *runtime_state_text_execution_state(execution_state_t execution_state);

/**
 * @brief 将工步生命周期状态转换为稳定文本。
 * @param lifecycle_state 工步生命周期状态。
 * @return 对应文本；未知值返回 `"pending"`。
 */
const char *runtime_state_text_segment_lifecycle_state(segment_lifecycle_state_t lifecycle_state);

#endif
