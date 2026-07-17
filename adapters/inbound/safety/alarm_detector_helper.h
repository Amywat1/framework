/**
 * @file    alarm_detector_helper.h
 * @brief   通用报警 detector 辅助器（normalized signal -> alarm_binding_port）
 * @author  HUWANGWEI
 * @date    2026-07-12
 */

#ifndef PORTS_INBOUND_SAFETY_ALARM_DETECTOR_HELPER_H
#define PORTS_INBOUND_SAFETY_ALARM_DETECTOR_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t alarm_code;
    bool     clear_when_inactive;
} alarm_detector_cfg_t;

typedef struct {
    alarm_detector_cfg_t cfg;
    bool                 initialized;
    bool                 active;
} alarm_detector_t;

sw_err_t alarm_detector_init(alarm_detector_t *detector, const alarm_detector_cfg_t *cfg);

/**
 * @brief  输入已归一化的故障状态；true 表示故障 active
 * @note   首次 active 会触发 trigger；active -> inactive 且允许自动清除时调用 clear。
 */
sw_err_t alarm_detector_update(alarm_detector_t *detector, bool fault_active);

bool alarm_detector_is_active(const alarm_detector_t *detector);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_INBOUND_SAFETY_ALARM_DETECTOR_HELPER_H */
