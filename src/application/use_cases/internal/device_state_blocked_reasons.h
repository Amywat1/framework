#ifndef SRC_APPLICATION_USE_CASES_INTERNAL_DEVICE_STATE_BLOCKED_REASONS_H
#define SRC_APPLICATION_USE_CASES_INTERNAL_DEVICE_STATE_BLOCKED_REASONS_H

#include "domain/model/domain_enums.h"

/**
 * @file device_state_blocked_reasons.h
 * @brief 设备状态导致命令不可受理时的稳定 reason_code。
 *
 * @details 语义见《设备状态与命令契约》第 4 节；行命令、JSON、二进制等格式共用。
 */

/**
 * @brief 返回 start 在当前设备状态下不可受理的原因码。
 * @param device_state 当前设备状态。
 * @return 对应的稳定 reason_code。
 */
static inline const char *device_state_start_blocked_reason(device_state_t device_state)
{
    switch (device_state)
    {
    case DEVICE_STATE_INIT:
        return "device_state_init";
    case DEVICE_STATE_STOPPED:
        return "device_state_stopped";
    case DEVICE_STATE_RECOVERING:
        return "device_state_recovering";
    case DEVICE_STATE_RUNNING:
        return "device_state_running";
    case DEVICE_STATE_EXCEPTION:
        return "device_state_exception";
    case DEVICE_STATE_IDLE:
    default:
        return "device_not_idle";
    }
}

/**
 * @brief 返回 homing 在当前设备状态下不可受理的原因码。
 * @return 非 STOPPED 态发起 homing 时的稳定 reason_code。
 */
static inline const char *device_state_homing_blocked_reason(void)
{
    return "homing_requires_stopped";
}

/**
 * @brief 返回 stop 在当前设备状态下不可受理的原因码。
 * @return 非 RUNNING 态发起 stop 时的稳定 reason_code。
 */
static inline const char *device_state_stop_blocked_reason(void)
{
    return "stop_requires_running";
}

/**
 * @brief 返回 fault clear 在当前设备状态下不可受理的原因码。
 * @return 非 EXCEPTION 态发起 fault clear 时的稳定 reason_code。
 */
static inline const char *device_state_fault_clear_blocked_reason(void)
{
    return "fault_clear_requires_exception";
}

/**
 * @brief 返回存在运行中会话时 start 不可受理的原因码。
 * @return 运行中会话已存在时的稳定 reason_code。
 */
static inline const char *device_state_running_session_blocked_reason(void)
{
    return "running_session_exists";
}

#endif
