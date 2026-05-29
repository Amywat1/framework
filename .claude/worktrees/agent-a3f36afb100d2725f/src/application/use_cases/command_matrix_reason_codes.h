#ifndef SRC_APPLICATION_USE_CASES_COMMAND_MATRIX_REASON_CODES_H
#define SRC_APPLICATION_USE_CASES_COMMAND_MATRIX_REASON_CODES_H

#include "domain/model/domain_enums.h"

/**
 * @brief 返回 start 的状态拒绝原因码。
 * @param device_state 当前设备状态。
 * @return 对应的拒绝原因码。
 */
static inline const char *command_matrix_start_rejection_reason(device_state_t device_state)
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
 * @brief 返回 homing 需要 STOPPED 的原因码。
 * @return homing 的拒绝原因码。
 */
static inline const char *command_matrix_homing_requires_stopped_reason(void)
{
    return "homing_requires_stopped";
}

/**
 * @brief 返回 stop 需要 RUNNING 的原因码。
 * @return stop 的拒绝原因码。
 */
static inline const char *command_matrix_stop_rejection_reason(void)
{
    return "stop_requires_running";
}

/**
 * @brief 返回 fault clear 需要 EXCEPTION 的原因码。
 * @return fault clear 的拒绝原因码。
 */
static inline const char *command_matrix_fault_clear_rejection_reason(void)
{
    return "fault_clear_requires_exception";
}

/**
 * @brief 返回存在运行中会话的原因码。
 * @return 运行中会话已存在的拒绝原因码。
 */
static inline const char *command_matrix_running_session_exists_reason(void)
{
    return "running_session_exists";
}

#endif
